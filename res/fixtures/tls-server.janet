# TLS fixture server for the test suite.
#
# Takes a port and a mode. In "sample" mode it serves with Mongoose's
# published sample credentials, supplied through add-server rather than
# taken from the copy compiled into Loopy. In "bad" mode it serves with
# malformed PEM, which must fail the handshake: were the credentials
# ignored, the built-in pair would answer and the connection would succeed.
#
# The credentials below are not secret. They are published in Mongoose's
# source and are already compiled into every Loopy binary. The certificate
# is valid until January 2033.

(import ../../_build/release/loopy :as loopy)

(def sample-cert
  ```
  -----BEGIN CERTIFICATE-----
  MIIBCTCBsAIJAK9wbIDkHnAoMAoGCCqGSM49BAMCMA0xCzAJBgNVBAYTAklFMB4X
  DTIzMDEyOTIxMjEzOFoXDTMzMDEyNjIxMjEzOFowDTELMAkGA1UEBhMCSUUwWTAT
  BgcqhkjOPQIBBggqhkjOPQMBBwNCAARzSQS5OHd17lUeNI+6kp9WYu0cxuEIi/JT
  jphbCmdJD1cUvhmzM9/phvJT9ka10Z9toZhgnBq0o0xfTQ4jC1vwMAoGCCqGSM49
  BAMCA0gAMEUCIQCe0T2E0GOiVe9KwvIEPeX1J1J0T7TNacgR0Ya33HV9VgIgNvdn
  aEWiBp1xshs4iz6WbpxrS1IHucrqkZuJLfNZGZI=
  -----END CERTIFICATE-----
  ```)

(def sample-key
  ```
  -----BEGIN EC PRIVATE KEY-----
  MHcCAQEEICBz3HOkQLPBDtdknqC7k1PNsWj6HfhyNB5MenfjmqiooAoGCCqGSM49
  AwEHoUQDQgAEc0kEuTh3de5VHjSPupKfVmLtHMbhCIvyU46YWwpnSQ9XFL4ZszPf
  6YbyU/ZGtdGfbaGYYJwatKNMX00OIwtb8A==
  -----END EC PRIVATE KEY-----
  ```)

(def bad-cert
  ```
  -----BEGIN CERTIFICATE-----
  bm90IGEgY2VydGlmaWNhdGU=
  -----END CERTIFICATE-----
  ```)

(def bad-key
  ```
  -----BEGIN EC PRIVATE KEY-----
  bm90IGEga2V5
  -----END EC PRIVATE KEY-----
  ```)

(def port (get (dyn *args*) 1 "8902"))
(def mode (get (dyn *args*) 2 "sample"))

# Janet strips the indentation from a long string but leaves off the final
# newline, which PEM conventionally carries. Passed as buffers rather than
# strings because slurp returns a buffer, so this is what reading a PEM off
# disk actually produces.
(def cert (buffer (if (= "bad" mode) bad-cert sample-cert) "\n"))
(def key (buffer (if (= "bad" mode) bad-key sample-key) "\n"))

(def mgr (loopy/manager))
(loopy/add-server mgr
                  (fn [_req] {:status 200 :body "tls ok"})
                  (string "127.0.0.1:" port)
                  {:cert cert :key key})

(forever (loopy/poll mgr 2000))
