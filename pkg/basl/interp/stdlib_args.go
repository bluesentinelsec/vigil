package interp

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// validateArgType validates a string value against a declared type
func validateArgType(val, typ, name string) error {
	switch typ {
	case "string":
		return nil
	case "bool":
		if val != "true" && val != "false" {
			return fmt.Errorf("flag --%s expects bool (true/false), got: %s", name, val)
		}
	case "i32":
		if _, err := strconv.ParseInt(val, 10, 32); err != nil {
			return fmt.Errorf("flag --%s expects i32, got: %s", name, val)
		}
	case "i64":
		if _, err := strconv.ParseInt(val, 10, 64); err != nil {
			return fmt.Errorf("flag --%s expects i64, got: %s", name, val)
		}
	case "u32":
		if _, err := strconv.ParseUint(val, 10, 32); err != nil {
			return fmt.Errorf("flag --%s expects u32, got: %s", name, val)
		}
	case "u64":
		if _, err := strconv.ParseUint(val, 10, 64); err != nil {
			return fmt.Errorf("flag --%s expects u64, got: %s", name, val)
		}
	case "f64":
		if _, err := strconv.ParseFloat(val, 64); err != nil {
			return fmt.Errorf("flag --%s expects f64, got: %s", name, val)
		}
	default:
		return fmt.Errorf("flag --%s: unsupported type '%s' (supported: bool, string, i32, i64, u32, u64, f64)", name, typ)
	}
	return nil
}

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
			// Validate default value against type
			name := args[0].AsString()
			typ := args[1].AsString()
			defVal := args[2].AsString()
			if err := validateArgType(defVal, typ, name); err != nil {
				return value.NewErr(err.Error()), nil
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
								val := cliArgs[i]
								// Validate type
								if err := validateArgType(val, typ, key); err != nil {
									return value.Void, &MultiReturnVal{Values: []value.Value{
										value.Void,
										value.NewErr(err.Error()),
									}}
								}
								flagValues[key] = value.NewString(val)
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
							val := cliArgs[i]
							// Validate type
							if err := validateArgType(val, typ, name); err != nil {
								return value.Void, &MultiReturnVal{Values: []value.Value{
									value.Void,
									value.NewErr(err.Error()),
								}}
							}
							flagValues[name] = value.NewString(val)
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
	case "get_i32":
		return value.NewNativeFunc("Result.get_i32", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("Result.get_i32: expected string key")
			}
			key := args[0].AsString()

			for i, k := range flagsMap.Keys {
				if k.AsString() == key {
					val := flagsMap.Values[i].AsString()
					n, err := strconv.ParseInt(val, 10, 32)
					if err != nil {
						return value.Void, fmt.Errorf("Result.get_i32: '%s' is not a valid i32: %s", key, val)
					}
					return value.NewI32(int32(n)), nil
				}
			}
			return value.NewI32(0), nil
		}), nil
	case "get_i64":
		return value.NewNativeFunc("Result.get_i64", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("Result.get_i64: expected string key")
			}
			key := args[0].AsString()

			for i, k := range flagsMap.Keys {
				if k.AsString() == key {
					val := flagsMap.Values[i].AsString()
					n, err := strconv.ParseInt(val, 10, 64)
					if err != nil {
						return value.Void, fmt.Errorf("Result.get_i64: '%s' is not a valid i64: %s", key, val)
					}
					return value.NewI64(n), nil
				}
			}
			return value.NewI64(0), nil
		}), nil
	case "get_f64":
		return value.NewNativeFunc("Result.get_f64", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("Result.get_f64: expected string key")
			}
			key := args[0].AsString()

			for i, k := range flagsMap.Keys {
				if k.AsString() == key {
					val := flagsMap.Values[i].AsString()
					n, err := strconv.ParseFloat(val, 64)
					if err != nil {
						return value.Void, fmt.Errorf("Result.get_f64: '%s' is not a valid f64: %s", key, val)
					}
					return value.NewF64(n), nil
				}
			}
			return value.NewF64(0.0), nil
		}), nil
	default:
		return value.Void, fmt.Errorf("line %d: Result has no method '%s'", line, method)
	}
}
