# loopy API

## loopy

[add-server](#add-server), [bind](#bind), [cookies](#cookies), [logger](#logger), [manager](#manager), [message](#message), [middleware](#middleware), [poll](#poll), [router](#router), [send-ws](#send-ws), [server](#server), [upgrade-websocket](#upgrade-websocket), [websocket](#websocket)

## add-server

**function**  | [source][1]

```janet
(add-server mgr handler url &opt tls)
```

Adds a server to a manager

tls is false for a plain server, true to serve TLS with the built-in
certificate, or a table or struct with :cert and :key holding PEM data to
serve TLS with your own. The built-in certificate is a sample and is not
suitable for anything but local testing.

[1]: lib/server.janet#L5


## bind

**cfunction**  | [source][2]

```janet
(bind manager url handler tls)
```

Bind manager to url and dispatch HTTP requests to handler. tls may be false for plain HTTP, true for TLS with built-in credentials, or a table or struct containing :cert and :key PEM data. Returns manager.

[2]: src/loopy.c#L564


## cookies

**function**  | [source][3]

```janet
(cookies nextmw)
```

Parses cookies into the table under :cookies key. nextmw parameter is  the handler function of the next middleware

[3]: lib/middleware.janet#L44


## logger

**function**  | [source][4]

```janet
(logger nextmw)
```

Creates a logging middleware. nextmw parameter is the handler function  of the next middleware

[4]: lib/middleware.janet#L24


## manager

**cfunction**  | [source][5]

```janet
(manager)
```

Create and initialise an HTTP server manager. Returns the manager.

[5]: src/loopy.c#L638


## message

**function**  | [source][6]

```janet
(message conn data &opt data-type)
```

Sends a message to a websocket connection

[6]: lib/server.janet#L38


## middleware

**function**  | [source][7]

```janet
(middleware x)
```

Coerce any type to http middleware

[7]: lib/middleware.janet#L5


## poll

**cfunction**  | [source][8]

```janet
(poll manager milliseconds)
```

Poll manager for events for up to milliseconds. Returns manager.

[8]: src/loopy.c#L648


## router

**function**  | [source][9]

```janet
(router routes)
```

Creates a router middleware. Route parameter must be table or struct  where keys are URI paths and values are handler functions for given URI

[9]: lib/middleware.janet#L12


## send-ws

**cfunction**  | [source][10]

```janet
(send-ws message)
```

Send a WebSocket message described by a table or struct. message must contain :connection, :event :message, :data-type :text or :binary, and string :data. Returns nil.

[10]: src/loopy.c#L660


## server

**function**  | [source][11]

```janet
(server handler port &opt address)
```

Creates and runs a simple http server

handler parameter is the function handling the requests. It could be
middleware. port is the number of the port the server will listen on.
address is an optional address the server will listen on.

[11]: lib/server.janet#L24


## upgrade-websocket

**cfunction**  | [source][12]

```janet
(upgrade-websocket connection request handler)
```

Upgrade an HTTP connection and request to a WebSocket handled by handler. Returns connection.

[12]: src/loopy.c#L686


## websocket

**function**  | [source][13]

```janet
(websocket handler req)
```

Creates a websocket

[13]: lib/server.janet#L49

