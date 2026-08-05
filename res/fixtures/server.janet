# Fixture server for the test suite.
#
# The tests in test/loopy.janet assert against these exact routes and
# values, so change them only alongside that file.

(import ../../_build/release/loopy :as loopy)

(def routes
  {"/thing" {:status 200
             :headers {"Content-Type" "text/html; charset=utf-8"
                       # Asserted as five separate header lines
                       "Thang" [1 2 3 4 5]}
             :body "<!doctype html><html><body>
                      <h1>Is a thing.</h1>
                    </body></html>"}
   "/bork" (fn [req]
             (let [[fname] (peg/match '(* "firstname=" (<- (any 1)) -1) (req :query-string))]
               {:status 200 :body (string "<!doctype html><html><body>Your firstname is "
                                          fname "?</body></html>")}))
   # Contains a zero byte, so Content-Length must be 7
   "/blob" {:status 200
            :body @"123\0123"}
   "/redirect" {:status 302
                :headers {"Location" "/thing"}}
   "/readme" {:kind :file :file "README.md" :mime "text/plain"}
   "/websocket" (fn [req]
                  (defn handler [msg]
                    (def client (msg :connection))
                    (when (= :message (msg :event))
                      (loopy/message client "hello")))
                  (loopy/websocket handler req))
   :default {:kind :static
             :root "."}})

(def port (get (dyn *args*) 1 "8901"))

(def mgr (loopy/manager))
(loopy/add-server mgr (-> routes loopy/router loopy/logger) (string "0.0.0.0:" port))

(forever (loopy/poll mgr 2000))
