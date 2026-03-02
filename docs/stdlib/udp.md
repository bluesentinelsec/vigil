# udp

UDP networking: stateless message sending and receiving.

```
import "udp";
```

## Functions

### udp.send(string addr, string data) -> err

Sends a UDP datagram to the given address. Creates a new connection per call and closes it after sending.

- `addr` format: `"host:port"`.
- Returns `ok` on success.
- Returns `err(message, err.io)` on failure.

```c
err e = udp.send("127.0.0.1:5000", "hello");
```

### udp.listen(string addr) -> (UdpConn, err)

Binds a UDP socket to the given address for receiving datagrams.

- Returns `(conn, ok)` on success.
- Returns `(void, err(message, err.io))` on failure.

```c
UdpConn conn, err e = udp.listen("0.0.0.0:5000");
```

## UdpConn Methods

### conn.recv(i32 max_bytes) -> (string, err)

Blocks until a datagram is received. Returns up to `max_bytes` of data.

- Returns `(data, ok)` on success.
- Returns `("", err(message, err.io))` on failure.

### conn.close() -> err

Closes the UDP socket.

```c
UdpConn conn, err e = udp.listen("0.0.0.0:5000");
string msg, err re = conn.recv(1024);
fmt.println(msg);
conn.close();
```
