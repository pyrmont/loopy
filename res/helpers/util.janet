# Private values

(def- fixture "res/fixtures/server.janet")

(def- tls-fixture "res/fixtures/tls-server.janet")

# The tests require a build, both here and in the fixture. Check for it as
# this module loads, so the failure is reported before any test file tries
# to import the module and fails less legibly.
(def- module
  (string "_build/release/loopy" (if (= :windows (os/which)) ".dll" ".so")))

(unless (os/stat module :mode)
  (errorf "%s is missing: run `jeep build` before `jeep test`" module))

# Responses from the :static and :file kinds are served by Mongoose, which
# does not drain the connection, so the socket stays open under keep-alive.
# Reading to EOF would block forever; frame reads on Content-Length, which
# every route in the fixture supplies.
(def- content-length-peg
  (peg/compile ~(some (+ (* "Content-Length: " (<- :d+)) 1))))

# Public values

(def host "127.0.0.1")

(def port "8901")

(def tls-port "8902")

(def tls-bad-port "8903")

# The sample handshake from RFC 6455 s1.3. Using the published key lets a
# test assert the exact accept value the server must derive from it.
(def ws-key "dGhlIHNhbXBsZSBub25jZQ==")

(def ws-accept "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")

# Public helpers

(defn- spawn-fixture
  ```
  Spawns a fixture server and waits for it to accept connections

  Returns the process. Loopy's manager is driven by a blocking poll loop,
  so a server cannot share a thread with the test client.
  ```
  [args listen-port]
  # Send the fixture's output to a file rather than discarding it, so a
  # startup failure can be reported. A file is used in preference to a pipe
  # because nothing drains it while the tests run, and it is opened with
  # os/open rather than file/temp because Windows will not redirect a
  # spawned process to the latter. The name carries the port so that two
  # fixtures can be running at once.
  (def log-path (string "_build/fixture-" listen-port ".log"))
  (def log (os/open log-path :wct))
  (def proc (os/spawn args :p {:out log :err log}))
  # Poll the port rather than sleeping a fixed interval
  (var ready? false)
  (var tries 0)
  (while (and (not ready?) (< tries 100))
    (def [ok? conn] (protect (net/connect host listen-port)))
    (if ok?
      (do (:close conn) (set ready? true))
      (os/sleep 0.05))
    (++ tries))
  # The child keeps its own handle, so this only drops the parent's copy
  (:close log)
  (unless ready?
    (protect (os/proc-kill proc true))
    (errorf "fixture server did not start:\n%s" (string (or (slurp log-path) ""))))
  proc)

(defn start-server
  "Starts the HTTP fixture server"
  []
  (spawn-fixture ["janet" fixture port] port))

(defn start-tls-server
  ```
  Starts the TLS fixture server

  mode is "sample" to serve Mongoose's published credentials or "bad" to
  serve malformed PEM. Both are supplied through add-server rather than
  taken from the pair compiled into Loopy.
  ```
  [mode &opt listen-port]
  (default listen-port (if (= "bad" mode) tls-bad-port tls-port))
  (spawn-fixture ["janet" tls-fixture listen-port mode] listen-port))

(defn stop-server
  "Stops a server started by `start-server`"
  [proc]
  (os/proc-kill proc true))

(defn http
  "Makes a GET request for `path` and returns the raw response"
  [path]
  (with [conn (net/connect host port)]
    (net/write conn (string "GET " path " HTTP/1.1\r\n"
                            "Host: " host ":" port "\r\n"
                            "Connection: close\r\n\r\n"))
    (def buf @"")
    (var sep nil)
    (while (nil? (set sep (string/find "\r\n\r\n" buf)))
      (def chunk (net/read conn 4096))
      (unless chunk (break))
      (buffer/push buf chunk))
    (assert sep "response had no header terminator")
    (def matched (peg/match content-length-peg (string/slice buf 0 sep)))
    (def clen (if matched (scan-number (first matched)) 0))
    (while (< (- (length buf) sep 4) clen)
      (def chunk (net/read conn 4096))
      (unless chunk (break))
      (buffer/push buf chunk))
    (string buf)))

(defn status-of
  "Extracts the status code from a raw response"
  [res]
  (scan-number (get (string/split " " res) 1)))

(defn headers-of
  "Extracts the header block from a raw response"
  [res]
  (string/slice res 0 (string/find "\r\n\r\n" res)))

(defn body-of
  "Extracts the body from a raw response"
  [res]
  (string/slice res (+ 4 (string/find "\r\n\r\n" res))))

(defn https-get
  ```
  Fetches `path` over TLS, returning a tuple of exit code and body

  Janet has no TLS client, so this shells out to curl. An exit code of 0
  means the handshake succeeded; 35 is curl's code for an SSL connect
  error. Certificate validation is skipped: these are self-signed.
  ```
  [path &opt listen-port]
  (default listen-port tls-port)
  # As in spawn-fixture, a file opened with os/open rather than file/temp:
  # Windows will not redirect a spawned process to the latter.
  (def out-path (string "_build/curl-" listen-port ".out"))
  (def out (os/open out-path :wct))
  (def url (string "https://" host ":" listen-port path))
  (def [ok? proc] (protect (os/spawn ["curl" "-sk" "-m" "5" url] :p {:out out :err out})))
  (unless ok?
    (:close out)
    (error "curl is needed to test TLS but could not be run"))
  (def code (os/proc-wait proc))
  (:close out)
  [code (string (or (slurp out-path) ""))])

(defn ws-connect
  ```
  Opens a websocket connection

  Returns a tuple of the connection and the raw handshake response.
  ```
  []
  (def conn (net/connect host port))
  (net/write conn (string "GET /websocket HTTP/1.1\r\n"
                          "Host: " host ":" port "\r\n"
                          "Upgrade: websocket\r\n"
                          "Connection: Upgrade\r\n"
                          "Sec-WebSocket-Key: " ws-key "\r\n"
                          "Sec-WebSocket-Version: 13\r\n\r\n"))
  [conn (string (net/read conn 4096))])

(defn ws-text-frame
  "Builds a masked client text frame. Payloads must be under 126 bytes."
  [payload]
  (def mask [0x12 0x34 0x56 0x78])
  (def n (length payload))
  (assert (< n 126) "payload too long for a short frame")
  (def buf @"")
  (buffer/push-byte buf 0x81)          # FIN + text opcode
  (buffer/push-byte buf (bor 0x80 n))  # MASK + length
  (each b mask (buffer/push-byte buf b))
  (forv i 0 n
    (buffer/push-byte buf (bxor (get payload i) (get mask (% i 4)))))
  buf)
