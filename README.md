# Loopy

[![Test Status][icon]][status]

[icon]: https://github.com/pyrmont/loopy/workflows/test/badge.svg
[status]: https://github.com/pyrmont/loopy/actions?query=workflow%3Atest

Loopy is an HTTP and WebSocket server for the [Janet][] programming language.

[Janet]: https://janet-lang.org

Loopy is a fork of [Circlet][]. It wraps Mongoose 7, updated for Mongoose's
new connection API, and reworks WebSockets so that any route can upgrade a
request rather than needing a dedicated listener. It can also serve over TLS
without any external dependencies.

[Circlet]: https://github.com/janet-lang/circlet
[Mongoose]: https://mongoose.ws

## Library

### Installation

Add the dependency to your `info.jdn` file:

```janet
  :dependencies ["https://github.com/pyrmont/loopy"]
```

### Usage

The simplest server is a handler and a port:

```janet
(import loopy)

(defn hello [req]
  {:status 200
   :headers {"Content-Type" "text/html"}
   :body "<!doctype html><html><body><h1>Hello.</h1></body></html>"})

(loopy/server hello 8000)
```

`(loopy/server handler port &opt address)` binds, listens and polls forever.
The address defaults to `127.0.0.1`; use `0.0.0.0` to listen on every
interface.

For anything beyond a single listener, drive the event loop yourself. A
manager owns the connections and `poll` advances them, blocking for at most
the given number of milliseconds:

```janet
(def mgr (loopy/manager))
(loopy/add-server mgr hello "0.0.0.0:8000")
(forever (loopy/poll mgr 2000))
```

### Requests

A handler receives a table describing the request:

| Key             | Value                                     |
| --------------- | ----------------------------------------- |
| `:uri`          | the requested path                        |
| `:method`       | the HTTP method, as a string              |
| `:protocol`     | the HTTP version used                     |
| `:headers`      | a table of header names to values         |
| `:body`         | the request body                          |
| `:query-string` | the query string, without the leading `?` |
| `:connection`   | the underlying connection                 |

A header sent more than once appears as an array of its values.

### Responses

A handler returns a table or struct. Every key is optional and `:status`
defaults to 200:

```janet
{:status 200
 :headers {"Content-Type" "text/plain"}
 :body "Hello."}
```

A header whose value is an array is written once per element, which is how
you send several `Set-Cookie` headers.

Setting `:kind` serves from the filesystem instead:

- `{:kind :file :file "README.md" :mime "text/plain"}` sends a single file.
  The `:mime` key is optional.
- `{:kind :static :root "."}` serves a directory.

Both are handled by Mongoose directly. Unlike an ordinary response, they
leave the connection open for reuse, so a client should frame its read on
`Content-Length` rather than waiting for the connection to close.

### Middleware

Middleware is any function taking a request and returning a response, so
handlers and middleware are the same shape and compose freely.
`(loopy/middleware x)` coerces a value into that shape, wrapping a constant
so it can be used wherever a function is expected.

Three pieces are provided:

- `(loopy/router routes)` dispatches on `:uri`. Keys are path strings, and
  the value under `:default` handles anything unmatched. Without a
  `:default`, an unmatched path yields a 404.
- `(loopy/logger nextmw)` prints each request and how long it took.
- `(loopy/cookies nextmw)` parses the `Cookie` header into a table under
  `:cookies`.

They compose with `->`, applied left to right:

```janet
(loopy/add-server mgr (-> routes loopy/router loopy/logger) "0.0.0.0:8000")
```

### WebSockets

A route upgrades a request by calling `(loopy/websocket handler req)`. The
handler is then invoked for each event on that connection, and the connection
stays open between messages:

```janet
(defn echo [msg]
  (when (= :message (msg :event))
    (loopy/message (msg :connection) (msg :data))))

(def routes
  {"/websocket" (fn [req] (loopy/websocket echo req))
   :default {:status 404}})
```

An event is a table with `:event`, `:connection` and `:protocol`. A
`:message` event also carries `:data` and a `:data-type` of `:text` or
`:binary`. Other events include `:close`, `:ping` and `:pong`.

`(loopy/message conn data &opt data-type)` sends a message, defaulting to
`:text`. Pass `:binary` to send binary frames.

Because one manager serves both, HTTP routes and WebSocket routes live in the
same routing table and share a single `poll` loop.

### TLS

The fourth argument to `add-server` enables TLS. Pass `true` to use the
built-in certificate, which is Mongoose's sample pair and is suitable only
for local testing:

```janet
(loopy/add-server mgr handler "0.0.0.0:8443" true)
```

Pass a table or struct with `:cert` and `:key` to serve your own. Both hold
PEM data rather than paths, and either a string or a buffer will do:

```janet
(loopy/add-server mgr handler "0.0.0.0:8443"
                  {:cert (slurp "cert.pem") :key (slurp "key.pem")})
```

The TLS stack is built into Mongoose and needs no external library, but it
speaks only ECDSA over secp256r1. An RSA key will not load. Generate a
suitable keypair with:

```sh
openssl ecparam -name prime256v1 -genkey -noout -out key.pem
openssl req -x509 -new -key key.pem -days 365 -subj /CN=localhost \
  -addext subjectAltName=DNS:localhost -out cert.pem
```

### Random numbers

TLS needs a cryptographic source of randomness for key material. Mongoose's
own `mg_random` reads `/dev/urandom` on Unix, but leaves its Windows branch
empty and falls through to `rand()`, which is not such a source.

Loopy therefore supplies its own. Mongoose wraps its implementation in
`#if MG_ENABLE_CUSTOM_RANDOM`, so building with
`-DMG_ENABLE_CUSTOM_RANDOM=1` (one of Mongoose's [build options][mg-build])
compiles that version out and leaves the symbol for the embedding program to
define. Loopy defines `mg_random` in `src/loopy.c`, reading `/dev/urandom`
on Unix and calling [`rand_s`][rand-s] on Windows. `rand_s` wraps
`RtlGenRandom` and needs no additional library, unlike `BCryptGenRandom`
or `CryptGenRandom`.

There is no fallback. If the system cannot supply randomness, Loopy aborts
rather than emit a key derived from a weak source.

Note that the Windows branch has not yet been built or run on Windows. The
mechanism itself is covered by the test suite, which exercises the Unix
branch on every TLS handshake.

[mg-build]: https://mongoose.ws/documentation/#build-options
[rand-s]: https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/rand-s

## Development

### Building

Building requires Janet and [jeep][]:

```sh
jeep build
```

The compiled module is written to `_build/release`.

[jeep]: https://github.com/pyrmont/jeep

### Testing

The tests exercise a real server over a socket, so the module must be built
first:

```sh
jeep build
jeep test
```

`res/tools/test.html` is a browser client for exercising the WebSocket
handler by hand. Serve the test fixture on port 8000 and open the file:

```sh
janet res/fixtures/server.janet 8000
```

## Bugs

Found a bug? I'd love to know about it. The best way is to report your bug in
the [Issues][] section on GitHub.

[Issues]: https://github.com/pyrmont/loopy/issues

## Credits

Loopy is a fork of [Circlet][], written by Calvin Rose and contributors. It
embeds [Mongoose][], written by Sergey Lyubka and Cesanta Software.

## Licence

Loopy is licensed under the GPL-2.0-only Licence. See [LICENSE][] for more
details.

[LICENSE]: https://github.com/pyrmont/loopy/blob/master/LICENSE
