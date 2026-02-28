package interp

import (
	"fmt"
	"net"
	"net/http"
	"testing"
)

func TestHttpGet(t *testing.T) {
	// Start a Go HTTP server
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	mux := http.NewServeMux()
	mux.HandleFunc("/hello", func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(200)
		w.Write([]byte("world"))
	})
	srv := &http.Server{Handler: mux}
	go srv.Serve(ln)
	defer srv.Close()

	addr := ln.Addr().String()
	src := fmt.Sprintf(`import "fmt"; import "http";
fn main() -> i32 {
	HttpResponse resp, err e = http.get("http://%s/hello");
	fmt.print(string(e));
	fmt.print(string(resp.status));
	fmt.print(resp.body);
	return 0;
}`, addr)
	_, out, evalErr := evalBASL(src)
	if evalErr != nil {
		t.Fatal(evalErr)
	}
	if out[0] != "ok" || out[1] != "200" || out[2] != "world" {
		t.Fatalf("got %v", out)
	}
}

func TestHttpPost(t *testing.T) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	mux := http.NewServeMux()
	mux.HandleFunc("/echo", func(w http.ResponseWriter, r *http.Request) {
		buf := make([]byte, 1024)
		n, _ := r.Body.Read(buf)
		w.Write(buf[:n])
	})
	srv := &http.Server{Handler: mux}
	go srv.Serve(ln)
	defer srv.Close()

	addr := ln.Addr().String()
	src := fmt.Sprintf(`import "fmt"; import "http";
fn main() -> i32 {
	HttpResponse resp, err e = http.post("http://%s/echo", "payload");
	fmt.print(resp.body);
	return 0;
}`, addr)
	_, out, evalErr := evalBASL(src)
	if evalErr != nil {
		t.Fatal(evalErr)
	}
	if out[0] != "payload" {
		t.Fatalf("got %v", out)
	}
}

func TestHttpGetBadUrl(t *testing.T) {
	src := `import "fmt"; import "http";
fn main() -> i32 {
	HttpResponse resp, err e = http.get("http://127.0.0.1:1/nope");
	fmt.print(string(e));
	return 0;
}`
	_, out, err := evalBASL(src)
	if err != nil {
		t.Fatal(err)
	}
	if out[0] == "ok" {
		t.Fatal("expected error for bad URL")
	}
}
