# This is embedded, so all loopy functions are available.
#
# It is evaluated after lib/middleware.janet and depends on `middleware`.

(defn add-server
  ```
  Adds a server to a manager

  tls is false for a plain server, true to serve TLS with the built-in
  certificate, or a table or struct with :cert and :key holding PEM data to
  serve TLS with your own. The built-in certificate is a sample and is not
  suitable for anything but local testing.
  ```
  [mgr handler url &opt tls]
  (default tls false)
  (def mw (middleware handler))
  (defn evloop []
    (print (string/format "loopy server listening on %s..." url))
    (var req (yield nil))
    (forever
      (set req (yield (mw req)))))
  (bind mgr url evloop tls))

(defn server
  ```
  Creates and runs a simple http server

  handler parameter is the function handling the requests. It could be
  middleware. port is the number of the port the server will listen on.
  address is an optional address the server will listen on.
  ```
  [handler port &opt address]
  (default address "127.0.0.1")
  (def mgr (manager))
  (add-server mgr handler (string/format "%s:%d" address port))
  (forever (poll mgr 2000)))

(defn message
  ```
  Sends a message to a websocket connection
  ```
  [conn data &opt data-type]
  (default data-type :text)
  (send-ws {:data data
            :data-type data-type
            :event :message
            :connection conn}))

(defn websocket
  ```
  Creates a websocket
  ```
  [handler req]
  (prin "WS Request: ")
  (pp req)
  (def conn (req :connection))
  (defn evloop []
    (print (string/format "loopy websocket established" conn))
    (var msg (yield nil))
    (forever
      (set msg (yield (handler msg)))))
  (try
    (upgrade-websocket conn req evloop)
    ([err]
     (print err))))
