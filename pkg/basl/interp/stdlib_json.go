package interp

import (
	"encoding/json"
	"fmt"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// --- JSON ---

func (interp *Interpreter) wrapJsonValue(data interface{}) value.Value {
	obj := &value.ObjectVal{
		ClassName: "json.Value",
		Fields:    map[string]value.Value{"__json": {T: value.TypeVoid, Data: data}},
		Methods:   make(map[string]*value.FuncVal),
	}
	return value.Value{T: value.TypeObject, Data: obj}
}

func extractJson(v value.Value) interface{} {
	if v.T != value.TypeObject {
		return nil
	}
	return v.AsObject().Fields["__json"].Data
}

func (interp *Interpreter) jsonMethod(obj value.Value, method string, line int) (value.Value, error) {
	data := extractJson(obj)
	switch method {
	case "get":
		return value.NewNativeFunc("json.Value.get", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("json.Value.get: expected string key")
			}
			m, ok := data.(map[string]interface{})
			if !ok {
				return value.Void, fmt.Errorf("json.Value.get: not an object")
			}
			v, ok := m[args[0].AsString()]
			if !ok {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr("key not found: "+args[0].AsString(), value.ErrKindNotFound)}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{interp.wrapJsonValue(v), value.Ok}}
		}), nil
	case "get_string":
		return value.NewNativeFunc("json.Value.get_string", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("json.Value.get_string: expected string key")
			}
			m, ok := data.(map[string]interface{})
			if !ok {
				return value.Void, fmt.Errorf("json.Value.get_string: not an object")
			}
			v, ok := m[args[0].AsString()]
			if !ok {
				return value.NewString(""), nil
			}
			return value.NewString(fmt.Sprintf("%v", v)), nil
		}), nil
	case "get_i32":
		return value.NewNativeFunc("json.Value.get_i32", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("json.Value.get_i32: expected string key")
			}
			m, ok := data.(map[string]interface{})
			if !ok {
				return value.Void, fmt.Errorf("json.Value.get_i32: not an object")
			}
			v, ok := m[args[0].AsString()]
			if !ok {
				return value.NewI32(0), nil
			}
			if f, ok := v.(float64); ok {
				return value.NewI32(int32(f)), nil
			}
			return value.NewI32(0), nil
		}), nil
	case "get_f64":
		return value.NewNativeFunc("json.Value.get_f64", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("json.Value.get_f64: expected string key")
			}
			m, ok := data.(map[string]interface{})
			if !ok {
				return value.Void, fmt.Errorf("json.Value.get_f64: not an object")
			}
			v, ok := m[args[0].AsString()]
			if !ok {
				return value.NewF64(0), nil
			}
			if f, ok := v.(float64); ok {
				return value.NewF64(f), nil
			}
			return value.NewF64(0), nil
		}), nil
	case "get_bool":
		return value.NewNativeFunc("json.Value.get_bool", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("json.Value.get_bool: expected string key")
			}
			m, ok := data.(map[string]interface{})
			if !ok {
				return value.Void, fmt.Errorf("json.Value.get_bool: not an object")
			}
			v, ok := m[args[0].AsString()]
			if !ok {
				return value.False, nil
			}
			if b, ok := v.(bool); ok {
				return value.NewBool(b), nil
			}
			return value.False, nil
		}), nil
	case "at":
		return value.NewNativeFunc("json.Value.at", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeI32 {
				return value.Void, fmt.Errorf("json.Value.at: expected i32 index")
			}
			arr, ok := data.([]interface{})
			if !ok {
				return value.Void, fmt.Errorf("json.Value.at: not an array")
			}
			idx := int(args[0].AsI32())
			if idx < 0 || idx >= len(arr) {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr("index out of bounds", value.ErrKindBounds)}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{interp.wrapJsonValue(arr[idx]), value.Ok}}
		}), nil
	case "at_i32":
		return value.NewNativeFunc("json.Value.at_i32", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeI32 {
				return value.Void, fmt.Errorf("json.Value.at_i32: expected i32 index")
			}
			arr, ok := data.([]interface{})
			if !ok {
				// Not an array - return 0 as documented
				return value.NewI32(0), nil
			}
			idx := int(args[0].AsI32())
			if idx < 0 || idx >= len(arr) {
				return value.NewI32(0), nil
			}
			if f, ok := arr[idx].(float64); ok {
				return value.NewI32(int32(f)), nil
			}
			return value.NewI32(0), nil
		}), nil
	case "at_string":
		return value.NewNativeFunc("json.Value.at_string", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeI32 {
				return value.Void, fmt.Errorf("json.Value.at_string: expected i32 index")
			}
			arr, ok := data.([]interface{})
			if !ok {
				// Not an array - return "" as documented
				return value.NewString(""), nil
			}
			idx := int(args[0].AsI32())
			if idx < 0 || idx >= len(arr) {
				return value.NewString(""), nil
			}
			return value.NewString(fmt.Sprintf("%v", arr[idx])), nil
		}), nil
	case "len":
		return value.NewNativeFunc("json.Value.len", func(args []value.Value) (value.Value, error) {
			switch d := data.(type) {
			case []interface{}:
				return value.NewI32(int32(len(d))), nil
			case map[string]interface{}:
				return value.NewI32(int32(len(d))), nil
			}
			return value.NewI32(0), nil
		}), nil
	case "keys":
		return value.NewNativeFunc("json.Value.keys", func(args []value.Value) (value.Value, error) {
			m, ok := data.(map[string]interface{})
			if !ok {
				return value.NewArray(nil), nil
			}
			elems := make([]value.Value, 0, len(m))
			for k := range m {
				elems = append(elems, value.NewString(k))
			}
			return value.NewArray(elems), nil
		}), nil
	case "is_object":
		return value.NewNativeFunc("json.Value.is_object", func(args []value.Value) (value.Value, error) {
			_, ok := data.(map[string]interface{})
			return value.NewBool(ok), nil
		}), nil
	case "is_array":
		return value.NewNativeFunc("json.Value.is_array", func(args []value.Value) (value.Value, error) {
			_, ok := data.([]interface{})
			return value.NewBool(ok), nil
		}), nil
	case "to_string":
		return value.NewNativeFunc("json.Value.to_string", func(args []value.Value) (value.Value, error) {
			return value.NewString(fmt.Sprintf("%v", data)), nil
		}), nil
	}
	return value.Void, fmt.Errorf("line %d: json.Value has no method %q", line, method)
}

func baslToGo(v value.Value) interface{} {
	switch v.T {
	case value.TypeI32:
		return float64(v.AsI32())
	case value.TypeI64:
		return float64(v.AsI64())
	case value.TypeF64:
		return v.AsF64()
	case value.TypeBool:
		return v.AsBool()
	case value.TypeString:
		return v.AsString()
	case value.TypeArray:
		arr := v.AsArray()
		out := make([]interface{}, len(arr.Elems))
		for i, e := range arr.Elems {
			out[i] = baslToGo(e)
		}
		return out
	case value.TypeMap:
		m := v.AsMap()
		out := make(map[string]interface{})
		for i, k := range m.Keys {
			out[k.String()] = baslToGo(m.Values[i])
		}
		return out
	default:
		return v.String()
	}
}

func (interp *Interpreter) makeJsonModule() *Env {
	env := NewEnv(nil)
	env.Define("parse", value.NewNativeFunc("json.parse", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("json.parse: expected string")
		}
		var data interface{}
		if err := json.Unmarshal([]byte(args[0].AsString()), &data); err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(err.Error(), value.ErrKindParse)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{interp.wrapJsonValue(data), value.Ok}}
	}))
	env.Define("stringify", value.NewNativeFunc("json.stringify", func(args []value.Value) (value.Value, error) {
		if len(args) < 1 {
			return value.Void, fmt.Errorf("json.stringify: expected value")
		}
		var data interface{}
		if args[0].T == value.TypeObject && args[0].AsObject().ClassName == "json.Value" {
			data = extractJson(args[0])
		} else {
			data = baslToGo(args[0])
		}
		b, err := json.Marshal(data)
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error(), value.ErrKindParse)}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(string(b)), value.Ok}}
	}))
	return env
}
