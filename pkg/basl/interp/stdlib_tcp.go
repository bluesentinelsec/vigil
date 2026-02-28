package interp

import (
	"fmt"
	"io"
	"net"
	"time"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// ── tcp module ──

func (interp *Interpreter) makeTcpModule() *Env {
	env := NewEnv(nil)

	env.Define("listen", value.NewNativeFunc("tcp.listen", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("tcp.listen: expected string addr")
		}
		ln, err := net.Listen("tcp", args[0].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(err.Error())}}
		}
		obj := &value.ObjectVal{
			ClassName: "TcpListener",
			Fields:    map[string]value.Value{"__ln": {T: value.TypeString, Data: ln}},
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{{T: value.TypeObject, Data: obj}, value.Ok}}
	}))

	env.Define("connect", value.NewNativeFunc("tcp.connect", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("tcp.connect: expected string addr")
		}
		conn, err := net.DialTimeout("tcp", args[0].AsString(), 10*time.Second)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(err.Error())}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{makeTcpConn(conn), value.Ok}}
	}))

	return env
}

func makeTcpConn(c net.Conn) value.Value {
	obj := &value.ObjectVal{
		ClassName: "TcpConn",
		Fields:    map[string]value.Value{"__conn": {T: value.TypeString, Data: c}},
	}
	return value.Value{T: value.TypeObject, Data: obj}
}

func (interp *Interpreter) tcpListenerMethod(obj value.Value, method string, line int) (value.Value, error) {
	o := obj.AsObject()
	ln, ok := o.Fields["__ln"].Data.(net.Listener)
	if !ok {
		return value.Void, fmt.Errorf("line %d: TcpListener is invalid", line)
	}
	switch method {
	case "accept":
		return value.NewNativeFunc("TcpListener.accept", func(args []value.Value) (value.Value, error) {
			conn, err := ln.Accept()
			if err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(err.Error())}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{makeTcpConn(conn), value.Ok}}
		}), nil
	case "close":
		return value.NewNativeFunc("TcpListener.close", func(args []value.Value) (value.Value, error) {
			if err := ln.Close(); err != nil {
				return value.NewErr(err.Error()), nil
			}
			return value.Ok, nil
		}), nil
	default:
		return value.Void, fmt.Errorf("line %d: TcpListener has no method '%s'", line, method)
	}
}

func (interp *Interpreter) tcpConnMethod(obj value.Value, method string, line int) (value.Value, error) {
	o := obj.AsObject()
	c, ok := o.Fields["__conn"].Data.(net.Conn)
	if !ok {
		return value.Void, fmt.Errorf("line %d: TcpConn is invalid", line)
	}
	switch method {
	case "write":
		return value.NewNativeFunc("TcpConn.write", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("TcpConn.write: expected string")
			}
			_, err := c.Write([]byte(args[0].AsString()))
			if err != nil {
				return value.NewErr(err.Error()), nil
			}
			return value.Ok, nil
		}), nil
	case "read":
		return value.NewNativeFunc("TcpConn.read", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeI32 {
				return value.Void, fmt.Errorf("TcpConn.read: expected i32 count")
			}
			buf := make([]byte, args[0].AsI32())
			n, err := c.Read(buf)
			if err != nil && err != io.EOF {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error())}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(string(buf[:n])), value.Ok}}
		}), nil
	case "close":
		return value.NewNativeFunc("TcpConn.close", func(args []value.Value) (value.Value, error) {
			if err := c.Close(); err != nil {
				return value.NewErr(err.Error()), nil
			}
			return value.Ok, nil
		}), nil
	default:
		return value.Void, fmt.Errorf("line %d: TcpConn has no method '%s'", line, method)
	}
}
