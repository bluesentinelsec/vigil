package interp

import (
	"fmt"
	"net"
	"testing"
)

func TestTcpConnectWriteRead(t *testing.T) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	addr := ln.Addr().String()

	go func() {
		conn, err := ln.Accept()
		if err != nil {
			return
		}
		defer conn.Close()
		buf := make([]byte, 1024)
		n, _ := conn.Read(buf)
		conn.Write(buf[:n])
	}()

	src := fmt.Sprintf(`import "fmt"; import "tcp";
fn main() -> i32 {
	TcpConn conn, err e1 = tcp.connect("%s");
	err e2 = conn.write("ping");
	string data, err e3 = conn.read(1024);
	conn.close();
	fmt.print(string(e1));
	fmt.print(string(e2));
	fmt.print(data);
	return 0;
}`, addr)
	_, out, evalErr := evalBASL(src)
	if evalErr != nil {
		t.Fatal(evalErr)
	}
	if out[0] != "ok" || out[1] != "ok" || out[2] != "ping" {
		t.Fatalf("got %v", out)
	}
}

func TestTcpConnectRefused(t *testing.T) {
	src := `import "fmt"; import "tcp";
fn main() -> i32 {
	TcpConn conn, err e = tcp.connect("127.0.0.1:1");
	fmt.print(string(e));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] == "ok" {
		t.Fatal("expected error for connection refused")
	}
}
