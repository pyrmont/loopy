#include <janet.h>
#include <stdio.h>
#include <string.h>
#include "mongoose.h"

/******************************************************************************
 * Miscellaneous
 ******************************************************************************/

/* TLS */

static const char *s_tls_cert =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBCTCBsAIJAK9wbIDkHnAoMAoGCCqGSM49BAMCMA0xCzAJBgNVBAYTAklFMB4X\n"
    "DTIzMDEyOTIxMjEzOFoXDTMzMDEyNjIxMjEzOFowDTELMAkGA1UEBhMCSUUwWTAT\n"
    "BgcqhkjOPQIBBggqhkjOPQMBBwNCAARzSQS5OHd17lUeNI+6kp9WYu0cxuEIi/JT\n"
    "jphbCmdJD1cUvhmzM9/phvJT9ka10Z9toZhgnBq0o0xfTQ4jC1vwMAoGCCqGSM49\n"
    "BAMCA0gAMEUCIQCe0T2E0GOiVe9KwvIEPeX1J1J0T7TNacgR0Ya33HV9VgIgNvdn\n"
    "aEWiBp1xshs4iz6WbpxrS1IHucrqkZuJLfNZGZI=\n"
    "-----END CERTIFICATE-----\n";

static const char *s_tls_key =
    "-----BEGIN EC PRIVATE KEY-----\n"
    "MHcCAQEEICBz3HOkQLPBDtdknqC7k1PNsWj6HfhyNB5MenfjmqiooAoGCCqGSM49\n"
    "AwEHoUQDQgAEc0kEuTh3de5VHjSPupKfVmLtHMbhCIvyU46YWwpnSQ9XFL4ZszPf\n"
    "6YbyU/ZGtdGfbaGYYJwatKNMX00OIwtb8A==\n"
    "-----END EC PRIVATE KEY-----\n";

/* Structs */

typedef struct mg_connection mg_connection_t;
typedef struct mg_http_message mg_http_message_t;
typedef struct mg_http_serve_opts mg_http_serve_opts_t;
typedef struct mg_mgr mg_mgr_t;
typedef struct mg_str mg_str_t;
typedef struct mg_ws_message mg_ws_message_t;

/******************************************************************************
 * Connection Data
 ******************************************************************************/

/* Held in a connection's fn_data. Mongoose copies fn_data from a listener
 * to each connection it accepts, so a listener's data is shared by every
 * connection it serves. Upgrading to a websocket replaces it with data of
 * its own, giving that connection a fiber that persists across messages.
 *
 * This is an abstract so that the garbage collector can reach the fiber
 * and the credentials: nothing else refers to them once they are handed
 * to Mongoose. */

typedef struct {
    JanetFiber *fiber;
    JanetString cert; /* NULL unless serving TLS with supplied credentials */
    JanetString key;
} loopy_data_t;

/* Marking */

static int data_mark(void *p, size_t size) {
    (void) size;
    loopy_data_t *d = (loopy_data_t *)p;
    if (d->fiber) janet_mark(janet_wrap_fiber(d->fiber));
    if (d->cert) janet_mark(janet_wrap_string(d->cert));
    if (d->key) janet_mark(janet_wrap_string(d->key));
    return 0;
}

/* Abstract Type */

static struct JanetAbstractType loopy_data_abstract = {
    "loopy.data",
    NULL,
    data_mark,
    JANET_ATEND_GCMARK
};

/******************************************************************************
 * Connection Object
 ******************************************************************************/

typedef struct {
    mg_connection_t *conn;
} loopy_connection_t;

/* Marking */

static int connection_mark(void *p, size_t size) {
    (void) size;
    loopy_connection_t *cw = (loopy_connection_t *)p;
    mg_connection_t *conn = cw->conn;
    loopy_data_t *d = (loopy_data_t *)(conn->fn_data);
    if (d) janet_mark(janet_wrap_abstract(d));
    return 0;
}

/* Abstract Type */

static struct JanetAbstractType loopy_connection_abstract = {
    "loopy.connection",
    NULL,
    connection_mark,
    JANET_ATEND_GCMARK
};

/******************************************************************************
 * Manager Object
 ******************************************************************************/

/* Deinitialising */

static int manager_gc(void *p, size_t size) {
    (void) size;
    mg_mgr_free((struct mg_mgr *)p);
    return 0;
}

/* Marking */

static int manager_mark(void *p, size_t size) {
    (void) size;
    mg_mgr_t *mgr = (mg_mgr_t *)p;
    /* Iterate and mark all connections */
    mg_connection_t *conn = mgr->conns;
    while (conn) {
        loopy_data_t *d = conn->fn_data;
        if (d) {
            janet_mark(janet_wrap_abstract(d));
        }
        conn = conn->next;
    }
    return 0;
}

/* Abstract Type */

static struct JanetAbstractType loopy_manager_abstract = {
    "loopy.manager",
    manager_gc,
    manager_mark,
    JANET_ATEND_GCMARK
};

/******************************************************************************
 * Utility Functions
 ******************************************************************************/

static const char *http_status(int status_code) {
    switch (status_code) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 102: return "Processing";
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 203: return "Non-authoritative Information";
        case 204: return "No Content";
        case 205: return "Reset Content";
        case 206: return "Partial Content";
        case 207: return "Multi-Status";
        case 208: return "Already Reported";
        case 226: return "IM Used";
        case 300: return "Multiple Choices";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 305: return "Use Proxy";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 402: return "Payment Required";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 406: return "Not Acceptable";
        case 407: return "Proxy Authentication Required";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 411: return "Length Required";
        case 412: return "Precondition Failed";
        case 413: return "result Too Large";
        case 414: return "Request-URI Too Long";
        case 415: return "Unsupported Media Type";
        case 416: return "Requested Range Not Satisfiable";
        case 417: return "Expectation Failed";
        case 418: return "I'm a teapot";
        case 421: return "Misdirected Request";
        case 422: return "Unprocessable Entity";
        case 423: return "Locked";
        case 424: return "Failed Dependency";
        case 426: return "Upgrade Required";
        case 428: return "Precondition Required";
        case 429: return "Too Many Requests";
        case 431: return "Request Header Fields Too Large";
        case 444: return "Connection Closed Without Response";
        case 451: return "Unavailable For Legal Reasons";
        case 499: return "Client Closed Request";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        case 506: return "Variant Also Negotiates";
        case 507: return "Insufficient Storage";
        case 508: return "Loop Detected";
        case 510: return "Not Extended";
        case 511: return "Network Authentication Required";
        case 599: return "Network Connect Timeout Error";
        default: return "";
  }
}

/* Turn a string value into c string */
static const char *getstring(Janet x, const char *dflt) {
    // printf("In getstring() the value is %s\n", janet_formatc("%s", janet_formatc("%t", x)));
    if (janet_checktype(x, JANET_STRING)) {
        const uint8_t *bytes = janet_unwrap_string(x);
        return (const char *)bytes;
    } else {
        return dflt;
    }
}

static int buffer_push_hm(JanetBuffer *b, JanetDictView d) {
    /* Initial line */
    janet_buffer_push_cstring(b, getstring(janet_dictionary_get(d.kvs, d.cap, janet_ckeywordv("method")), ""));
    janet_buffer_push_u8(b, ' ');
    janet_buffer_push_cstring(b, getstring(janet_dictionary_get(d.kvs, d.cap, janet_ckeywordv("uri")), ""));
    janet_buffer_push_cstring(b, getstring(janet_dictionary_get(d.kvs, d.cap, janet_ckeywordv("query-string")), ""));
    janet_buffer_push_u8(b, ' ');
    janet_buffer_push_cstring(b, getstring(janet_dictionary_get(d.kvs, d.cap, janet_ckeywordv("protocol")), ""));
    janet_buffer_push_cstring(b, "\r\n");

    /* Headers */
    JanetDictView view;
    if (!janet_dictionary_view(janet_dictionary_get(d.kvs, d.cap, janet_ckeywordv("headers")),
                               &view.kvs,
                               &view.len,
                               &view.cap)) {
        return 0;
    }
    for (int i = 0; i < view.cap; i++) {
        if (janet_checktype(view.kvs[i].key, JANET_NIL)) continue;
        const char *k = getstring(view.kvs[i].key, "");
        if (strcmp(k, "") == 0) continue; /* TODO Can we panic here? */
        janet_buffer_push_cstring(b, k);
        janet_buffer_push_cstring(b, ": ");
        const char *v = getstring(view.kvs[i].value, "");
        janet_buffer_push_cstring(b, v);
        janet_buffer_push_cstring(b, "\r\n");
    }
    janet_buffer_push_cstring(b, "\r\n");

    /* Body */
    janet_buffer_push_cstring(b, getstring(janet_dictionary_get(d.kvs, d.cap, janet_ckeywordv("body")), ""));
    janet_buffer_push_cstring(b, "\r\n");
    return 1;
}

/* Conversions */

static mg_str_t janet2mg_str(Janet x) {
    JanetString str = janet_unwrap_string(x);
    return mg_str_n((const char *)str, janet_string_length(str));
}

static Janet mg2janet_str(mg_str_t str) {
    return janet_stringv((const uint8_t *)(str.buf), str.len);
}

static Janet mg2janet_hm(mg_connection_t *c, mg_http_message_t *hm) {
    JanetTable *result = janet_table(10);

    janet_table_put(result, janet_ckeywordv("body"), mg2janet_str(hm->body));
    janet_table_put(result, janet_ckeywordv("uri"), mg2janet_str(hm->uri));
    janet_table_put(result, janet_ckeywordv("query-string"), mg2janet_str(hm->query));
    janet_table_put(result, janet_ckeywordv("method"), mg2janet_str(hm->method));
    janet_table_put(result, janet_ckeywordv("protocol"), mg2janet_str(hm->proto));

    /* Add headers */
    JanetTable *headers = janet_table(5);
    for (int i = 0; i < MG_MAX_HTTP_HEADERS; i++) {
        if (hm->headers[i].name.len == 0)
            break;
        Janet key = mg2janet_str(hm->headers[i].name);
        Janet value = mg2janet_str(hm->headers[i].value);
        Janet header = janet_table_get(headers, key);
        switch (janet_type(header)) {
            case JANET_NIL:
                janet_table_put(headers, key, value);
                break;

            case JANET_ARRAY:
                janet_array_push(janet_unwrap_array(header), value);
                break;

            default: {
                Janet newHeader[2] = { header, value };
                janet_table_put(headers, key, janet_wrap_array(janet_array_n(newHeader, 2)));
                break;
            }
        }
    }
    janet_table_put(result, janet_ckeywordv("headers"), janet_wrap_table(headers));

    loopy_connection_t *cw = (loopy_connection_t *)janet_abstract(&loopy_connection_abstract, sizeof(loopy_connection_t));
    cw->conn = c;
    janet_table_put(result, janet_ckeywordv("connection"), janet_wrap_abstract(cw));

    return janet_wrap_table(result);
}

static Janet mg2janet_wm(mg_connection_t *c, Janet event, mg_ws_message_t *wm) {
    JanetTable *result;
    if (wm) {
        result = janet_table(5);
        janet_table_put(result, janet_ckeywordv("data"), janet_stringv((const uint8_t *)(wm->data.buf), wm->data.len));
        if ((wm->flags & 0x0F) == WEBSOCKET_OP_TEXT) {
            janet_table_put(result, janet_ckeywordv("data-type"), janet_ckeywordv("text"));
        } else {
            janet_table_put(result, janet_ckeywordv("data-type"), janet_ckeywordv("binary"));
        }
    } else {
       result = janet_table(3);
    }

    janet_table_put(result, janet_ckeywordv("event"), event);
    janet_table_put(result, janet_ckeywordv("protocol"), janet_cstringv("websocket"));

    loopy_connection_t *cw = (loopy_connection_t *)janet_abstract(&loopy_connection_abstract, sizeof(loopy_connection_t));
    cw->conn = c;
    janet_table_put(result, janet_ckeywordv("connection"), janet_wrap_abstract(cw));

    return janet_wrap_table(result);
}

/* Send an HTTP reply. This should try not to panic, as at this point we
 * are outside of the janet interpreter. Instead, send a 500 response with
 * some formatted error message. */
static void send_http(mg_connection_t *c, Janet res, void *ev_data) {
    switch (janet_type(res)) {
        default:
            mg_http_reply(c, 500, NULL, "");
            break;

        case JANET_TABLE:
        case JANET_STRUCT: {
            const JanetKV *kvs;
            int32_t kvlen, kvcap;
            janet_dictionary_view(res, &kvs, &kvlen, &kvcap);

            /* Get response kind and check for special handling methods. */
            Janet kind = janet_dictionary_get(kvs, kvcap, janet_ckeywordv("kind"));
            if (janet_checktype(kind, JANET_KEYWORD)) {
                const uint8_t *kindstr = janet_unwrap_keyword(kind);
                /* Construct static serve options */
                mg_http_serve_opts_t opts;
                memset(&opts, 0, sizeof(opts));

                /* Check for serving static files */
                if (!janet_cstrcmp(kindstr, "static")) {
                    Janet root = janet_dictionary_get(kvs, kvcap, janet_ckeywordv("root"));
                    opts.root_dir = getstring(root, NULL);
                    mg_http_serve_dir(c, (mg_http_message_t *) ev_data, &opts);
                    return;
                }

                /* Check for serving single file */
                if (!janet_cstrcmp(kindstr, "file")) {
                    Janet filev = janet_dictionary_get(kvs, kvcap, janet_ckeywordv("file"));
                    const char *filepath;
                    if (!janet_checktype(filev, JANET_STRING)) {
                        mg_http_reply(c, 500, NULL, "expected string :file option to serve a file");
                        break;
                    }
                    filepath = getstring(filev, "");
                    const char *fileext = filepath;
                    for (int32_t i = janet_length(filev) - 1; i > 0 || filepath[i] == '.'; fileext = filepath + i, i--);
                    if (filepath != fileext) {
                        Janet mimev = janet_dictionary_get(kvs, kvcap, janet_ckeywordv("mime"));
                        const char *mime = getstring(mimev, "text/plain");
                        opts.mime_types = (const char *)janet_formatc("%s=%s", fileext, mime);
                    }
                    mg_http_serve_file(c, (mg_http_message_t *)ev_data, filepath, &opts);
                    return;
                }
            }

            /* Serve a generic HTTP response */

            Janet status = janet_dictionary_get(kvs, kvcap, janet_ckeywordv("status"));
            Janet headers = janet_dictionary_get(kvs, kvcap, janet_ckeywordv("headers"));
            Janet body = janet_dictionary_get(kvs, kvcap, janet_ckeywordv("body"));

            int code;
            if (janet_checktype(status, JANET_NIL))
                code = 200;
            else if (janet_checkint(status))
                code = janet_unwrap_integer(status);
            else
                code = 0;

            const JanetKV *headerkvs;
            int32_t headerlen, headercap;
            if (janet_checktype(headers, JANET_NIL)) {
                headerkvs = NULL;
                headerlen = 0;
                headercap = 0;
            } else if (!janet_dictionary_view(headers, &headerkvs, &headerlen, &headercap)) {
                break;
            }

            const uint8_t *bodybytes;
            int32_t bodylen;
            if (janet_checktype(body, JANET_NIL)) {
                bodybytes = NULL;
                bodylen = 0;
            } else if (!janet_bytes_view(body, &bodybytes, &bodylen)) {
                break;
            }

            mg_printf(c, "HTTP/1.1 %d %s\r\n", code, http_status(code));
            for (const JanetKV *kv = janet_dictionary_next(headerkvs, headercap, NULL);
                    kv;
                    kv = janet_dictionary_next(headerkvs, headercap, kv)) {
                const uint8_t *name = janet_to_string(kv->key);
                int32_t header_len;
                const Janet *header_items;
                if (janet_indexed_view(kv->value, &header_items, &header_len)) {
                    /* Array-like of headers */
                    for (int32_t i = 0; i < header_len; i++) {
                        const uint8_t *value = janet_to_string(header_items[i]);
                        mg_printf(c, "%s: %s\r\n", (const char *)name, (const char *)value);
                    }
                } else {
                    /* Single header */
                    const uint8_t *value = janet_to_string(kv->value);
                    mg_printf(c, "%s: %s\r\n", (const char *)name, (const char *)value);
                }
            }

            mg_printf(c, "Content-Length: %d\r\n", bodylen);
            mg_printf(c, "\r\n");
            if (bodylen) mg_send(c, bodybytes, bodylen);
        }
        break;
    }

    mg_printf(c, "\r\n");
    c->is_draining = 1;
}

/* The dispatching event handler. This handler is what is presented to
 * mongoose, but it dispatches to dynamically defined handlers. */
static void event_handler(mg_connection_t *c, int ev, void *p) {
    Janet evdata;

    switch (ev) {
        default:
            return;

        case MG_EV_HTTP_MSG: {
            mg_http_message_t *hm = (mg_http_message_t *)p;
            evdata = mg2janet_hm(c, hm);
            break;
        }

        case MG_EV_WS_OPEN: {
            return;
            /* TODO Do I want to handle this? */
            // evdata = mg2janet_wm(c, janet_ckeywordv("open"), NULL);
            // break;
        }

        case MG_EV_WS_MSG: {
            mg_ws_message_t *wm = (mg_ws_message_t *)p;
            evdata = mg2janet_wm(c, janet_ckeywordv("message"), wm);
            printf("Websocket message: %s\n", wm->data.buf);
            break;
        }

        case MG_EV_WS_CTL: {
            mg_ws_message_t *wm = (mg_ws_message_t *)p;
            Janet event;
            switch (wm->flags & 0x0F) {
                case WEBSOCKET_OP_CONTINUE:
                    event = janet_ckeywordv("continue");
                    break;
                case WEBSOCKET_OP_CLOSE:
                    event = janet_ckeywordv("close");
                    break;
                case WEBSOCKET_OP_PING:
                    event = janet_ckeywordv("ping");
                    break;
                case WEBSOCKET_OP_PONG:
                    event = janet_ckeywordv("pong");
                    break;
            }
            evdata = mg2janet_wm(c, event, NULL);
            printf("Websocket control: %d\n", (wm->flags & 0x0F));
            break;
        }

        case MG_EV_CLOSE: {
            if (c->is_closing || !c->is_websocket) return;
            evdata = mg2janet_wm(c, janet_ckeywordv("close"), NULL);
            break;
        }
    }

    loopy_data_t *d = (loopy_data_t *)(c->fn_data);
    if (NULL == d || NULL == d->fiber) return;
    JanetFiber *fiber = d->fiber;
    Janet out;
    JanetSignal status = janet_continue(fiber, evdata, &out);
    if (status != JANET_SIGNAL_OK && status != JANET_SIGNAL_YIELD) {
        printf("The code for this message is %d\n", ev);
        janet_stacktrace(fiber, out);
        return;
    }

    if (!c->is_websocket) send_http(c, out, p);
}

static void event_handler_s(mg_connection_t *c, int ev, void *p) {
    switch (ev) {
        case MG_EV_ACCEPT: {
            loopy_data_t *d = (loopy_data_t *)(c->fn_data);
            struct mg_tls_opts opts;
            memset(&opts, 0, sizeof(opts));
            if (d && d->cert && d->key) {
                /* Mongoose wants the PEM itself, not a path to it */
                opts.cert = mg_str_n((const char *)d->cert, janet_string_length(d->cert));
                opts.key = mg_str_n((const char *)d->key, janet_string_length(d->key));
            } else {
                opts.cert = mg_str(s_tls_cert);
                opts.key = mg_str(s_tls_key);
            }
            mg_tls_init(c, &opts);
            return;
        }

        default:
            event_handler(c, ev, p);
    }
}

static void log_char(char c, void *p) {
    JanetBuffer *buf = (JanetBuffer *)p;
    janet_buffer_push_u8(buf, (uint8_t)c);
}

/******************************************************************************
 * Exposed Functions
 ******************************************************************************/

static Janet cfun_bind(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 4);
    mg_mgr_t *mgr = janet_getabstract(argv, 0, &loopy_manager_abstract);
    JanetString url = janet_getstring(argv, 1);
    JanetFunction *http_handler = janet_getfunction(argv, 2);

    /* TLS is false to serve plain HTTP, true to serve TLS with the
     * built-in credentials, or a table or struct with :cert and :key
     * holding PEM data to serve TLS with credentials of your own. */
    Janet tls = argv[3];
    int is_tls = janet_truthy(tls);
    Janet certv = janet_wrap_nil();
    Janet keyv = janet_wrap_nil();
    const JanetKV *tlskvs;
    int32_t tlslen, tlscap;
    if (janet_dictionary_view(tls, &tlskvs, &tlslen, &tlscap)) {
        const uint8_t *pem;
        int32_t pemlen;
        certv = janet_dictionary_get(tlskvs, tlscap, janet_ckeywordv("cert"));
        keyv = janet_dictionary_get(tlskvs, tlscap, janet_ckeywordv("key"));
        /* A buffer is accepted as well as a string because slurp returns
         * one, and reading a PEM off disk is the common case. */
        if (!janet_bytes_view(certv, &pem, &pemlen) ||
            !janet_bytes_view(keyv, &pem, &pemlen)) {
            janet_panic("tls requires both :cert and :key, as PEM data");
        }
    }

    /* Set up buffer to capture possible error message */
    JanetBuffer *err = janet_buffer(128); /* TODO Do I need to deinitialise this? */
    mg_log_set_fn(log_char, err);

    mg_connection_t *conn = (is_tls) ? mg_http_listen(mgr, (const char *)url, event_handler_s, NULL) :
                                       mg_http_listen(mgr, (const char *)url, event_handler, NULL);
    if (NULL == conn) {
        janet_buffer_push_u8(err, 0);
        err->count--;
        janet_panicf("could not bind to %s, reason being: %s", url, err->data);
    }

    /* Reset logging function after no error */
    mg_log_set_fn(mg_pfn_stdout, NULL);

    /* Attach the data before allocating the fiber. Once fn_data is set the
     * manager's mark function can reach this, so anything stored here
     * afterwards survives a collection triggered by a later allocation. */
    loopy_data_t *d = (loopy_data_t *)janet_abstract(&loopy_data_abstract, sizeof(loopy_data_t));
    memset(d, 0, sizeof(loopy_data_t));
    conn->fn_data = (void *)d;
    if (!janet_checktype(certv, JANET_NIL)) {
        /* Copy the PEM into immutable strings: a buffer's contents can be
         * changed or moved after this call, but Mongoose reads them on
         * every accepted connection. Each is stored as soon as it is made,
         * so no later allocation can collect it. */
        const uint8_t *pem;
        int32_t pemlen;
        janet_bytes_view(certv, &pem, &pemlen);
        d->cert = janet_string(pem, pemlen);
        janet_bytes_view(keyv, &pem, &pemlen);
        d->key = janet_string(pem, pemlen);
    }
    d->fiber = janet_fiber(http_handler, 64, 0, NULL);

    Janet out;
    JanetSignal status = janet_continue(d->fiber, janet_wrap_nil(), &out);
    if (status != JANET_SIGNAL_YIELD) {
        janet_stacktrace(d->fiber, out);
    }

    return argv[0];
}

static Janet cfun_manager(int32_t argc, Janet *argv) {
    (void) argv;
    janet_fixarity(argc, 0);
    void *mgr = janet_abstract(&loopy_manager_abstract, sizeof(mg_mgr_t));
    mg_mgr_init(mgr);
    return janet_wrap_abstract(mgr);
}

static Janet cfun_poll(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 2);
    mg_mgr_t *mgr = janet_getabstract(argv, 0, &loopy_manager_abstract);
    int32_t wait = janet_getinteger(argv, 1);
    mg_mgr_poll(mgr, wait);
    return argv[0];
}

static Janet cfun_send_ws(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    JanetDictView d = janet_getdictionary(argv, 0);
    Janet x;
    x = janet_dictionary_get(d.kvs, d.cap, janet_ckeywordv("connection"));
    loopy_connection_t *cw = janet_getabstract(&x, 0, &loopy_connection_abstract);
    x = janet_dictionary_get(d.kvs, d.cap, janet_ckeywordv("event"));
    if (janet_keyeq(x, "message")) {
        x = janet_dictionary_get(d.kvs, d.cap, janet_ckeywordv("data-type"));
        int op = (janet_keyeq(x, "text")) ? WEBSOCKET_OP_TEXT : WEBSOCKET_OP_BINARY;
        x = janet_dictionary_get(d.kvs, d.cap, janet_ckeywordv("data"));
        mg_str_t msg = janet2mg_str(x);
        mg_ws_send(cw->conn, msg.buf, msg.len, op);
    // } else if (janet_keyeq(x, "continue")) {
    // } else if (janet_keyeq(x, "close")) {
    // } else if (janet_keyeq(x, "ping")) {
    // } else if (janet_keyeq(x, "pong")) {
    } else {
        janet_panic("unsupported :event");
    }
    return janet_wrap_nil();
}

static Janet cfun_upgrade_ws(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 3);
    loopy_connection_t *cw = janet_getabstract(argv, 0, &loopy_connection_abstract);
    JanetDictView d = janet_getdictionary(argv, 1);
    JanetFunction *ws_handler = janet_getfunction(argv, 2);

    JanetBuffer *b = janet_buffer(128);
    if (!buffer_push_hm(b, d)) {
        /* TODO Handle this parsing failure gracefully */
        janet_panic("invalid table");
    }
    mg_http_message_t hm;
    mg_http_parse((const char *)(b->data), b->count, &hm);
    mg_ws_upgrade(cw->conn, &hm, NULL);

    if (!cw->conn->is_websocket) {
        janet_panic("could not upgrade connection to websocket");
    }

    /* Replace the listener's data with this connection's own, so that the
     * websocket has a fiber persisting across messages. TLS is already
     * established by now, so no credentials are carried here. */
    loopy_data_t *wd = (loopy_data_t *)janet_abstract(&loopy_data_abstract, sizeof(loopy_data_t));
    memset(wd, 0, sizeof(loopy_data_t));
    cw->conn->fn_data = (void *)wd;
    wd->fiber = janet_fiber(ws_handler, 64, 0, NULL);

    Janet out;
    JanetSignal status = janet_continue(wd->fiber, janet_wrap_nil(), &out);
    if (status != JANET_SIGNAL_YIELD) {
        janet_stacktrace(wd->fiber, out);
    }

    return janet_wrap_abstract(cw);
}


/******************************************************************************
 * Environment Registration
 ******************************************************************************/

static const JanetReg cfuns[] = {
    {"bind", cfun_bind, NULL},
    {"manager", cfun_manager, NULL},
    {"poll", cfun_poll, NULL},
    {"send-ws", cfun_send_ws, NULL},
    {"upgrade-websocket", cfun_upgrade_ws, NULL},
    {NULL, NULL, NULL}
};

/* These symbols are generated from the files named in :embedded in
 * info.jdn. The build derives each name from its path, replacing every
 * path separator with three underscores, so lib/server.janet becomes
 * lib___server. Moving or renaming those files changes these names, and
 * the mismatch only surfaces as an undefined symbol at link time. */
extern const unsigned char *lib___middleware_embed;
extern size_t lib___middleware_embed_size;
extern const unsigned char *lib___server_embed;
extern size_t lib___server_embed_size;

JANET_MODULE_ENTRY(JanetTable *env) {
    /* Set the log level rather than inherit Mongoose's default, which is
     * not stable across releases: 7.13 defaulted to MG_LL_INFO and 7.22 to
     * MG_LL_DEBUG, which prints a line on every connection close. Errors
     * are kept because cfun_bind reads a failed listen out of this stream. */
    mg_log_set(MG_LL_ERROR);

    janet_cfuns(env, "loopy", cfuns);
    /* Both files are evaluated into the same environment, so order
     * matters: server.janet calls middleware.janet's `middleware`. */
    janet_dobytes(env,
                  lib___middleware_embed,
                  lib___middleware_embed_size,
                  "lib/middleware.janet",
                  NULL);
    janet_dobytes(env,
                  lib___server_embed,
                  lib___server_embed_size,
                  "lib/server.janet",
                  NULL);
}
