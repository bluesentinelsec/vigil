package interp

import (
	"fmt"
	"net"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// ── udp module ──

func (interp *Interpreter) makeUdpModule() *Env {
	env := NewEnv(nil)

	env.Define("listen", value.NewNativeFunc("udp.listen", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("udp.listen: expected string addr")
		}
		addr, err := net.ResolveUDPAddr("udp", args[0].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(err.Error())}}
		}
		conn, err := net.ListenUDP("udp", addr)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(err.Error())}}
		}
		obj := &value.ObjectVal{
			ClassName: "UdpConn",
			Fields:    map[string]value.Value{"__conn": {T: value.TypeString, Data: conn}},
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{{T: value.TypeObject, Data: obj}, value.Ok}}
	}))

	env.Define("send", value.NewNativeFunc("udp.send", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("udp.send: expected (string addr, string data)")
		}
		addr, err := net.ResolveUDPAddr("udp", args[0].AsString())
		if err != nil {
			return value.NewErr(err.Error()), nil
		}
		conn, err := net.DialUDP("udp", nil, addr)
		if err != nil {
			return value.NewErr(err.Error()), nil
		}
		defer conn.Close()
		_, err = conn.Write([]byte(args[1].AsString()))
		if err != nil {
			return value.NewErr(err.Error()), nil
		}
		return value.Ok, nil
	}))

	return env
}

func (interp *Interpreter) udpConnMethod(obj value.Value, method string, line int) (value.Value, error) {
	o := obj.AsObject()
	c, ok := o.Fields["__conn"].Data.(*net.UDPConn)
	if !ok {
		return value.Void, fmt.Errorf("line %d: UdpConn is invalid", line)
	}
	switch method {
	case "recv":
		return value.NewNativeFunc("UdpConn.recv", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeI32 {
				return value.Void, fmt.Errorf("UdpConn.recv: expected i32 count")
			}
			buf := make([]byte, args[0].AsI32())
			n, _, err := c.ReadFromUDP(buf)
			if err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error())}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(string(buf[:n])), value.Ok}}
		}), nil
	case "close":
		return value.NewNativeFunc("UdpConn.close", func(args []value.Value) (value.Value, error) {
			if err := c.Close(); err != nil {
				return value.NewErr(err.Error()), nil
			}
			return value.Ok, nil
		}), nil
	default:
		return value.Void, fmt.Errorf("line %d: UdpConn has no method '%s'", line, method)
	}
}
