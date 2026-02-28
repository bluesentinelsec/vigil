package interp

import (
	"fmt"
	"net"
	"testing"
	"time"
)

func TestUdpSendRecv(t *testing.T) {
	// Start a Go UDP listener
	pc, err := net.ListenPacket("udp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer pc.Close()
	addr := pc.LocalAddr().String()

	// Read one packet and echo it back
	received := make(chan string, 1)
	go func() {
		buf := make([]byte, 1024)
		n, raddr, err := pc.ReadFrom(buf)
		if err != nil {
			return
		}
		received <- string(buf[:n])
		pc.WriteTo(buf[:n], raddr)
	}()

	src := fmt.Sprintf(`import "fmt"; import "udp";
fn main() -> i32 {
	err e = udp.send("%s", "hello udp");
	fmt.print(string(e));
	return 0;
}`, addr)
	_, out, evalErr := evalBASL(src)
	if evalErr != nil {
		t.Fatal(evalErr)
	}
	if out[0] != "ok" {
		t.Fatalf("got %v", out)
	}

	select {
	case msg := <-received:
		if msg != "hello udp" {
			t.Fatalf("received %q", msg)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("timeout waiting for UDP packet")
	}
}
