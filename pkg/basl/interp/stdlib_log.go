package interp

import (
	"fmt"
	"os"
	"strings"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

var logLevel int = 1 // 0=debug, 1=info, 2=warn, 3=error, 4=fatal

func (interp *Interpreter) makeLogModule() *Env {
	env := NewEnv(nil)
	logAt := func(name string, level int, fatal bool) value.Value {
		tag := strings.ToUpper(strings.TrimPrefix(name, "log."))
		return value.NewNativeFunc(name, func(args []value.Value) (value.Value, error) {
			if len(args) != 1 {
				return value.Void, fmt.Errorf("%s: expected 1 argument", name)
			}
			if level < logLevel {
				return value.Void, nil
			}
			msg := args[0].String()
			if interp.baslLogHandler != nil {
				interp.callFunc(*interp.baslLogHandler, []value.Value{value.NewString(tag), value.NewString(msg)})
			} else if interp.LogFn != nil {
				interp.LogFn(tag, msg)
			} else {
				fmt.Fprintf(os.Stderr, "[%s] %s\n", tag, msg)
			}
			if fatal {
				os.Exit(1)
			}
			return value.Void, nil
		})
	}
	env.Define("debug", logAt("log.debug", 0, false))
	env.Define("info", logAt("log.info", 1, false))
	env.Define("warn", logAt("log.warn", 2, false))
	env.Define("error", logAt("log.error", 3, false))
	env.Define("fatal", logAt("log.fatal", 4, true))
	env.Define("set_level", value.NewNativeFunc("log.set_level", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("log.set_level: expected string level")
		}
		switch args[0].AsString() {
		case "debug":
			logLevel = 0
		case "info":
			logLevel = 1
		case "warn":
			logLevel = 2
		case "error":
			logLevel = 3
		case "fatal":
			logLevel = 4
		default:
			return value.Void, fmt.Errorf("log.set_level: unknown level %q", args[0].AsString())
		}
		return value.Void, nil
	}))
	env.Define("set_handler", value.NewNativeFunc("log.set_handler", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || (args[0].T != value.TypeFunc && args[0].T != value.TypeNativeFunc) {
			return value.Void, fmt.Errorf("log.set_handler: expected fn(string level, string msg)")
		}
		h := args[0]
		interp.baslLogHandler = &h
		return value.Void, nil
	}))
	return env
}
