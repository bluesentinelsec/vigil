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
			ClassName: "args.ArgParser",
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
			if len(args) != 4 && len(args) != 5 {
				return value.Void, fmt.Errorf("ArgParser.flag: expected (string name, string type, string default, string help[, string short])")
			}
			flagArr := o.Fields["__flags"].Data.(*value.ArrayVal)
			def := &value.ObjectVal{
				ClassName: "__argdef",
				Fields: map[string]value.Value{
					"name": args[0], "type": args[1], "default": args[2], "help": args[3],
				},
			}
			if len(args) == 5 {
				def.Fields["short"] = args[4]
			}
			flagArr.Elems = append(flagArr.Elems, value.Value{T: value.TypeObject, Data: def})
			return value.Ok, nil
		}), nil
	case "arg":
		return value.NewNativeFunc("ArgParser.arg", func(args []value.Value) (value.Value, error) {
			if len(args) != 3 && len(args) != 4 {
				return value.Void, fmt.Errorf("ArgParser.arg: expected (string name, string type, string help[, bool variadic])")
			}
			argArr := o.Fields["__args"].Data.(*value.ArrayVal)
			def := &value.ObjectVal{
				ClassName: "__argdef",
				Fields: map[string]value.Value{
					"name": args[0], "type": args[1], "help": args[2],
				},
			}
			if len(args) == 4 {
				def.Fields["variadic"] = args[3]
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

			// Build short flag map
			shortFlags := make(map[string]int)
			for i, fd := range flagDefs.Elems {
				fobj := fd.AsObject()
				if shortVal, ok := fobj.Fields["short"]; ok {
					shortFlags[shortVal.AsString()] = i
				}
			}

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
				} else if strings.HasPrefix(a, "-") && len(a) == 2 {
					// Short flag
					key := a[1:2]
					if idx, ok := shortFlags[key]; ok {
						fobj := flagDefs.Elems[idx].AsObject()
						typ := fobj.Fields["type"].AsString()
						if typ == "bool" {
							result.Values[idx] = value.NewString("true")
						} else if i+1 < len(cliArgs) {
							i++
							result.Values[idx] = value.NewString(cliArgs[i])
						}
					} else {
						return value.Void, &MultiReturnVal{Values: []value.Value{
							{T: value.TypeMap, Data: result},
							value.NewErr(fmt.Sprintf("unknown flag: %s", a)),
						}}
					}
				} else {
					positional = append(positional, a)
				}
			}

			// Map positional args
			posIdx := 0
			for _, ad := range argDefs.Elems {
				aobj := ad.AsObject()
				name := aobj.Fields["name"].AsString()

				// Check if variadic
				if variadicVal, ok := aobj.Fields["variadic"]; ok && variadicVal.AsBool() {
					// Collect remaining args as space-separated string
					// This keeps map homogeneous (all string values)
					remaining := []string{}
					for posIdx < len(positional) {
						remaining = append(remaining, positional[posIdx])
						posIdx++
					}
					result.Keys = append(result.Keys, value.NewString(name))
					result.Values = append(result.Values, value.NewString(strings.Join(remaining, " ")))
				} else {
					val := ""
					if posIdx < len(positional) {
						val = positional[posIdx]
						posIdx++
					}
					result.Keys = append(result.Keys, value.NewString(name))
					result.Values = append(result.Values, value.NewString(val))
				}
			}

			return value.Void, &MultiReturnVal{Values: []value.Value{{T: value.TypeMap, Data: result}, value.Ok}}
		}), nil
	default:
		return value.Void, fmt.Errorf("line %d: ArgParser has no method '%s'", line, method)
	}
}
