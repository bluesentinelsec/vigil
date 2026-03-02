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
	case "parse_result":
		return value.NewNativeFunc("ArgParser.parse_result", func(args []value.Value) (value.Value, error) {
			cliArgs := interp.scriptArgs
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

			// Result storage
			flagValues := make(map[string]value.Value)
			argValues := make(map[string]value.Value)

			// Set defaults for flags
			for _, fd := range flagDefs.Elems {
				fobj := fd.AsObject()
				name := fobj.Fields["name"].AsString()
				flagValues[name] = fobj.Fields["default"]
			}

			// Parse CLI args
			positional := []string{}
			endOfOptions := false
			for i := 0; i < len(cliArgs); i++ {
				a := cliArgs[i]

				if a == "--" {
					endOfOptions = true
					continue
				}

				if !endOfOptions && strings.HasPrefix(a, "--") {
					key := a[2:]
					// Find flag def and set value
					found := false
					for _, fd := range flagDefs.Elems {
						fobj := fd.AsObject()
						if fobj.Fields["name"].AsString() == key {
							found = true
							typ := fobj.Fields["type"].AsString()
							if typ == "bool" {
								flagValues[key] = value.NewString("true")
							} else if i+1 < len(cliArgs) {
								i++
								flagValues[key] = value.NewString(cliArgs[i])
							} else {
								return value.Void, &MultiReturnVal{Values: []value.Value{
									value.Void,
									value.NewErr(fmt.Sprintf("flag --%s requires a value", key)),
								}}
							}
							break
						}
					}
					if !found {
						return value.Void, &MultiReturnVal{Values: []value.Value{
							value.Void,
							value.NewErr(fmt.Sprintf("unknown flag: %s", a)),
						}}
					}
				} else if !endOfOptions && strings.HasPrefix(a, "-") && len(a) == 2 {
					// Short flag
					key := a[1:2]
					if idx, ok := shortFlags[key]; ok {
						fobj := flagDefs.Elems[idx].AsObject()
						name := fobj.Fields["name"].AsString()
						typ := fobj.Fields["type"].AsString()
						if typ == "bool" {
							flagValues[name] = value.NewString("true")
						} else if i+1 < len(cliArgs) {
							i++
							flagValues[name] = value.NewString(cliArgs[i])
						} else {
							return value.Void, &MultiReturnVal{Values: []value.Value{
								value.Void,
								value.NewErr(fmt.Sprintf("flag -%s requires a value", key)),
							}}
						}
					} else {
						return value.Void, &MultiReturnVal{Values: []value.Value{
							value.Void,
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
					// Collect remaining args as array
					remaining := []value.Value{}
					for posIdx < len(positional) {
						remaining = append(remaining, value.NewString(positional[posIdx]))
						posIdx++
					}
					argValues[name] = value.NewArray(remaining)
				} else {
					val := ""
					if posIdx < len(positional) {
						val = positional[posIdx]
						posIdx++
					}
					argValues[name] = value.NewString(val)
				}
			}

			// Create Result object
			result := &value.ObjectVal{
				ClassName: "args.Result",
				Fields: map[string]value.Value{
					"__flags": {T: value.TypeMap, Data: &value.MapVal{
						Keys:   []value.Value{},
						Values: []value.Value{},
					}},
					"__args": {T: value.TypeMap, Data: &value.MapVal{
						Keys:   []value.Value{},
						Values: []value.Value{},
					}},
				},
			}

			// Populate flags map
			flagsMap := result.Fields["__flags"].Data.(*value.MapVal)
			for k, v := range flagValues {
				flagsMap.Keys = append(flagsMap.Keys, value.NewString(k))
				flagsMap.Values = append(flagsMap.Values, v)
			}

			// Populate args map
			argsMap := result.Fields["__args"].Data.(*value.MapVal)
			for k, v := range argValues {
				argsMap.Keys = append(argsMap.Keys, value.NewString(k))
				argsMap.Values = append(argsMap.Values, v)
			}

			return value.Void, &MultiReturnVal{Values: []value.Value{
				{T: value.TypeObject, Data: result},
				value.Ok,
			}}
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
					// Collect remaining args as newline-separated string
					// Newline is safe because command-line args cannot contain literal newlines
					remaining := []string{}
					for posIdx < len(positional) {
						remaining = append(remaining, positional[posIdx])
						posIdx++
					}
					result.Keys = append(result.Keys, value.NewString(name))
					result.Values = append(result.Values, value.NewString(strings.Join(remaining, "\n")))
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

// argsResultMethod handles methods on args.Result objects
func (interp *Interpreter) argsResultMethod(obj value.Value, method string, line int) (value.Value, error) {
	o := obj.AsObject()
	flagsMap := o.Fields["__flags"].Data.(*value.MapVal)
	argsMap := o.Fields["__args"].Data.(*value.MapVal)

	switch method {
	case "get_string":
		return value.NewNativeFunc("Result.get_string", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("Result.get_string: expected string key")
			}
			key := args[0].AsString()

			// Check flags first
			for i, k := range flagsMap.Keys {
				if k.AsString() == key {
					return flagsMap.Values[i], nil
				}
			}
			// Check args
			for i, k := range argsMap.Keys {
				if k.AsString() == key {
					v := argsMap.Values[i]
					if v.T == value.TypeString {
						return v, nil
					}
					return value.Void, fmt.Errorf("Result.get_string: '%s' is not a string", key)
				}
			}
			return value.NewString(""), nil
		}), nil
	case "get_bool":
		return value.NewNativeFunc("Result.get_bool", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("Result.get_bool: expected string key")
			}
			key := args[0].AsString()

			for i, k := range flagsMap.Keys {
				if k.AsString() == key {
					return value.NewBool(flagsMap.Values[i].AsString() == "true"), nil
				}
			}
			return value.False, nil
		}), nil
	case "get_list":
		return value.NewNativeFunc("Result.get_list", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("Result.get_list: expected string key")
			}
			key := args[0].AsString()

			for i, k := range argsMap.Keys {
				if k.AsString() == key {
					v := argsMap.Values[i]
					if v.T == value.TypeArray {
						return v, nil
					}
					return value.Void, fmt.Errorf("Result.get_list: '%s' is not a list", key)
				}
			}
			return value.NewArray(nil), nil
		}), nil
	default:
		return value.Void, fmt.Errorf("line %d: Result has no method '%s'", line, method)
	}
}
