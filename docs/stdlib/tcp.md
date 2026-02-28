# tcp

TCP networking: client connections and server listeners.

```
import "tcp";
```

## Functions

### tcp.connect(string addr) -> (TcpConn, err)

Connects to a TCP server. Uses a 10-second timeout.

- `addr` format: `"host:port"` (e.g., `"127.0.0.1:8080"`).
- Returns `(conn, ok)` on success.
- Returns `(void, err(message))` on failure (connection refused, timeout, DNS failure).

```c
TcpConn conn, err e = tcp.connect("127.0.0.1:8080");
```

### tcp.listen(string addr) -> (TcpListener, err)

Starts a TCP listener on the given address.

- Returns `(listener, ok)` on success.
- Returns `(void, err(message))` on failure (port in use, permission denied).

```c
TcpListener ln, err e = tcp.listen("0.0.0.0:9000");
```

## TcpListener Methods

### ln.accept() -> (TcpConn, err)

Blocks until a client connects. Returns the new connection.

- Returns `(conn, ok)` on success.
- Returns `(void, err(message))` on failure.

### ln.close() -> err

Closes the listener.

## TcpConn Methods

### conn.write(string data) -> err

Writes data to the connection.

- Returns `ok` on success.
- Returns `err(message)` on failure.

### conn.read(i32 max_bytes) -> (string, err)

Reads up to `max_bytes` from the connection.

- Returns `(data, ok)` on success (may return fewer bytes than requested).
- Returns `("", err(message))` on failure or EOF.

### conn.close() -> err

Closes the connection.

```c
TcpConn conn, err e = tcp.connect("127.0.0.1:8080");
conn.write("GET / HTTP/1.0\r\n\r\n");
string response, err re = conn.read(4096);
conn.close();
```
