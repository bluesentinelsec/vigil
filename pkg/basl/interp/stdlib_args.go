package interp

import (
	"fmt"
	"strings"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

func (interp *Interpreter) makeArgsModule() *Env {
	env := NewEnv(nil)

	env.Define("parser", value.NewNativeFunc("args.parser", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("args.parser: expected (string name, string description)")
		}
		obj := &value.ObjectVal{
			ClassName: "ArgParser",
			Fields: map[string]value.Value{
				"__name":  args[0],
				"__desc":  args[1],
				"__flags": {T: value.TypeArray, Data: &value.ArrayVal{}},
				"__args":  {T: value.TypeArray, Data: &value.ArrayVal{}},
			},
		}
		return value.Value{T: value.TypeObject, Data: obj}, nil
	}))

	return env
}

type argDef struct {
	name   string
	typ    string
	defVal string
	help   string
	isFlag bool
}

func (interp *Interpreter) argParserMethod(obj value.Value, method string, line int) (value.Value, error) {
	o := obj.AsObject()
	switch method {
	case "flag":
		return value.NewNativeFunc("ArgParser.flag", func(args []value.Value) (value.Value, error) {
			if len(args) != 4 {
				return value.Void, fmt.Errorf("ArgParser.flag: expected (string name, string type, string default, string help)")
			}
			flagArr := o.Fields["__flags"].Data.(*value.ArrayVal)
			def := &value.ObjectVal{
				ClassName: "__argdef",
				Fields: map[string]value.Value{
					"name": args[0], "type": args[1], "default": args[2], "help": args[3],
				},
			}
			flagArr.Elems = append(flagArr.Elems, value.Value{T: value.TypeObject, Data: def})
			return value.Ok, nil
		}), nil
	case "arg":
		return value.NewNativeFunc("ArgParser.arg", func(args []value.Value) (value.Value, error) {
			if len(args) != 3 {
				return value.Void, fmt.Errorf("ArgParser.arg: expected (string name, string type, string help)")
			}
			argArr := o.Fields["__args"].Data.(*value.ArrayVal)
			def := &value.ObjectVal{
				ClassName: "__argdef",
				Fields: map[string]value.Value{
					"name": args[0], "type": args[1], "help": args[2],
				},
			}
			argArr.Elems = append(argArr.Elems, value.Value{T: value.TypeObject, Data: def})
			return value.Ok, nil
		}), nil
	case "parse":
		return value.NewNativeFunc("ArgParser.parse", func(args []value.Value) (value.Value, error) {
			cliArgs := interp.scriptArgs
			result := &value.MapVal{}
			flagDefs := o.Fields["__flags"].Data.(*value.ArrayVal)
			argDefs := o.Fields["__args"].Data.(*value.ArrayVal)

			// Set defaults for flags
			for _, fd := range flagDefs.Elems {
				fobj := fd.AsObject()
				name := fobj.Fields["name"].AsString()
				result.Keys = append(result.Keys, value.NewString(name))
				result.Values = append(result.Values, fobj.Fields["default"])
			}

			// Parse CLI args
			positional := []string{}
			for i := 0; i < len(cliArgs); i++ {
				a := cliArgs[i]
				if strings.HasPrefix(a, "--") {
					key := a[2:]
					// Find flag def and set value
					for j, fd := range flagDefs.Elems {
						fobj := fd.AsObject()
						if fobj.Fields["name"].AsString() == key {
							typ := fobj.Fields["type"].AsString()
							if typ == "bool" {
								result.Values[j] = value.NewString("true")
							} else if i+1 < len(cliArgs) {
								i++
								result.Values[j] = value.NewString(cliArgs[i])
							}
							break
						}
					}
				} else {
					positional = append(positional, a)
				}
			}

			// Map positional args
			for i, ad := range argDefs.Elems {
				aobj := ad.AsObject()
				name := aobj.Fields["name"].AsString()
				val := ""
				if i < len(positional) {
					val = positional[i]
				}
				result.Keys = append(result.Keys, value.NewString(name))
				result.Values = append(result.Values, value.NewString(val))
			}

			return value.Void, &MultiReturnVal{Values: []value.Value{{T: value.TypeMap, Data: result}, value.Ok}}
		}), nil
	default:
		return value.Void, fmt.Errorf("line %d: ArgParser has no method '%s'", line, method)
	}
}
