(use ../deps/testament)

(import ../res/helpers/util :as h)
(import ../_build/release/loopy :as loopy)


# HTTP

(deftest serves-a-static-route
  (def res (h/http "/thing"))
  (is (= 200 (h/status-of res)))
  (is (string/find "text/html" (h/headers-of res)))
  (is (string/find "Is a thing." (h/body-of res))))

(deftest parses-the-query-string
  (def res (h/http "/bork?firstname=mike"))
  (is (= 200 (h/status-of res)))
  (is (string/find "Your firstname is mike?" (h/body-of res))))

(deftest sends-a-redirect
  (def res (h/http "/redirect"))
  (is (= 302 (h/status-of res)))
  (is (string/find "Location: /thing" (h/headers-of res))))

(deftest repeats-headers-given-as-a-list
  # {"Thang" [1 2 3 4 5]} must become five separate header lines
  (def res (h/http "/thing"))
  (is (= 5 (length (string/find-all "Thang: " (h/headers-of res))))))

(deftest sends-bodies-containing-zero-bytes
  # The body is "123\0123", so Content-Length must count the null byte
  (def res (h/http "/blob"))
  (is (= 200 (h/status-of res)))
  (is (string/find "Content-Length: 7" (h/headers-of res))))

(deftest serves-a-single-file
  (def res (h/http "/readme"))
  (is (= 200 (h/status-of res)))
  (is (string/find "text/plain" (h/headers-of res))))

(deftest serves-a-directory-by-default
  (def res (h/http "/"))
  (is (= 200 (h/status-of res))))

(deftest returns-404-for-an-unrouted-path
  # The default route serves a directory, so a missing file is a 404
  (def res (h/http "/no/such/file"))
  (is (= 404 (h/status-of res))))


# Middleware
#
# These call the middleware directly, without a server, so they cover cases
# the fixture cannot reach.

(deftest router-falls-back-to-default
  (def r (loopy/router {"/known" {:status 200} :default {:status 302}}))
  (is (= 302 (get (r {:uri "/missing"}) :status))))

(deftest router-returns-a-404-response-without-a-default
  # Must be a response, not a bare 404: send_http only understands tables
  # and structs, so a number would go out as a 500
  (def r (loopy/router {"/known" {:status 200}}))
  (def res (r {:uri "/missing"}))
  (is (dictionary? res))
  (is (= 404 (get res :status))))


# TLS
#
# The handshake tests use Mongoose's published sample credentials, supplied
# through add-server. Because that pair is also the one compiled into Loopy,
# a success alone would not prove the supplied credentials were used, so the
# malformed case is what pins that down: if :cert were ignored, the built-in
# pair would answer and the handshake would succeed.

(deftest serves-tls-with-supplied-credentials
  (def server (h/start-tls-server "sample"))
  (defer (h/stop-server server)
    (def [code body] (h/https-get "/"))
    (is (= 0 code))
    (is (= "tls ok" body))))

(deftest fails-the-handshake-on-malformed-credentials
  (def server (h/start-tls-server "bad"))
  (defer (h/stop-server server)
    (def [code _] (h/https-get "/" h/tls-bad-port))
    # 35 is curl's SSL connect error; 0 would mean the built-in pair answered
    (is (not= 0 code))
    (is (= 35 code))))


# The remaining cases never reach a bind: credentials are validated before
# the socket is opened.

(deftest tls-requires-both-a-cert-and-a-key
  (def mgr (loopy/manager))
  (each bad [{:cert "pem"} {:key "pem"} {}]
    (is (thrown? (loopy/add-server mgr (fn [_req] {:status 200})
                                   "127.0.0.1:8555" bad)))))

(deftest tls-credentials-must-be-pem-data
  (def mgr (loopy/manager))
  (is (thrown? (loopy/add-server mgr (fn [_req] {:status 200})
                                 "127.0.0.1:8555" {:cert 42 :key 43}))))


# Websockets

(deftest completes-the-websocket-handshake
  (def [conn res] (h/ws-connect))
  (defer (:close conn)
    (is (= 101 (h/status-of res)))
    (is (string/find (string "Sec-WebSocket-Accept: " h/ws-accept) res))))

(deftest replies-to-a-websocket-message
  (def [conn res] (h/ws-connect))
  (defer (:close conn)
    (is (= 101 (h/status-of res)))
    (net/write conn (h/ws-text-frame "ping"))
    (def reply (net/read conn 1024))
    (is (not (nil? reply)))
    (when reply
      (def opcode (band (get reply 0) 0x0F))
      (def len (band (get reply 1) 0x7F))
      (is (= 1 opcode))
      (is (= "hello" (string (string/slice reply 2 (+ 2 len))))))))


# Run

(def- server (h/start-server))
# Collect reports rather than letting Testament exit, so the server is
# always reaped before this process ends.
(def- reports (run-tests! :no-exit? true))
(h/stop-server server)
(os/exit (if (some |(not (empty? (get $ :failures []))) reports) 1 0))
