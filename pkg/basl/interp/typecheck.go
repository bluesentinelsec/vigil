package interp

import (
	"fmt"

	"github.com/bluesentinelsec/basl/pkg/basl/ast"
	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// checkType returns an error if val does not match the declared type.
func (interp *Interpreter) checkType(declType *ast.TypeExpr, val value.Value, line int) error {
	if declType == nil {
		return nil
	}
	// Check collection element/key/value types at declaration
	if declType.Name == "array" && declType.ElemType != nil && val.T == value.TypeArray {
		arr := val.AsArray()
		arr.ElemType = declType.ElemType.Name
		for i, elem := range arr.Elems {
			if err := interp.checkType(declType.ElemType, elem, line); err != nil {
				return fmt.Errorf("line %d: array element %d: %s", line, i, stripLinePrefix(err))
			}
		}
		return nil
	}
	if declType.Name == "map" && val.T == value.TypeMap {
		m := val.AsMap()
		if declType.KeyType != nil {
			m.KeyType = declType.KeyType.Name
		}
		if declType.ValType != nil {
			m.ValType = declType.ValType.Name
		}
		for i := range m.Keys {
			if declType.KeyType != nil {
				if err := interp.checkType(declType.KeyType, m.Keys[i], line); err != nil {
					return fmt.Errorf("line %d: map key %d: %s", line, i, stripLinePrefix(err))
				}
			}
			if declType.ValType != nil {
				if err := interp.checkType(declType.ValType, m.Values[i], line); err != nil {
					return fmt.Errorf("line %d: map value %d: %s", line, i, stripLinePrefix(err))
				}
			}
		}
		return nil
	}
	if declType.Name == "fn" && (declType.ParamTypes != nil || declType.ReturnType != nil) {
		return interp.checkFnSignature(declType, val, line)
	}
	return interp.checkTypeByName(declType.Name, val, line)
}

// checkFnSignature checks a function value against a typed fn(...) -> R signature.
func (interp *Interpreter) checkFnSignature(declType *ast.TypeExpr, val value.Value, line int) error {
	if val.T == value.TypeNativeFunc {
		return nil // native functions can't be signature-checked
	}
	if val.T != value.TypeFunc {
		return fmtTypeMismatch("fn", val, line)
	}
	fn := val.AsFunc()
	if declType.ParamTypes != nil {
		if len(fn.Params) != len(declType.ParamTypes) {
			return fmt.Errorf("type mismatch — expected fn with %d params, received fn with %d params", len(declType.ParamTypes), len(fn.Params))
		}
		for i, pt := range declType.ParamTypes {
			if fn.Params[i].Type != pt.Name {
				return fmt.Errorf("type mismatch — fn param %d: expected %s, received %s", i, pt.Name, fn.Params[i].Type)
			}
		}
	}
	if declType.ReturnType != nil {
		if fn.Return == nil {
			return fmt.Errorf("type mismatch — expected fn returning %s, received fn returning void", declType.ReturnType.Name)
		}
		rt := fn.Return.(*ast.ReturnType)
		if len(rt.Types) > 0 && rt.Types[0].Name != declType.ReturnType.Name {
			return fmt.Errorf("type mismatch — expected fn returning %s, received fn returning %s", declType.ReturnType.Name, rt.Types[0].Name)
		}
	}
	return nil
}

// stripLinePrefix removes "line N: " prefix from nested errors.
func stripLinePrefix(err error) string {
	s := err.Error()
	if len(s) > 5 && s[:5] == "line " {
		for i := 5; i < len(s); i++ {
			if s[i] == ':' && i+2 < len(s) {
				return s[i+2:]
			}
		}
	}
	return s
}

// checkTypeByName checks val against a type name string (used for function params).
func (interp *Interpreter) checkTypeByName(typeName string, val value.Value, line int) error {
	expected, ok := builtinTypeMap[typeName]
	if ok {
		if typeName == "fn" {
			if val.T != value.TypeFunc && val.T != value.TypeNativeFunc {
				return fmtTypeMismatch(typeName, val, line)
			}
			return nil
		}
		if val.T != expected {
			return fmtTypeMismatch(typeName, val, line)
		}
		return nil
	}
	// Class/interface name — must be an object with matching class name or interface
	if val.T != value.TypeObject {
		return fmtTypeMismatch(typeName, val, line)
	}
	obj := val.AsObject()
	if obj.ClassName == typeName {
		return nil
	}
	for _, iface := range obj.Implements {
		if iface == typeName {
			return nil
		}
	}
	return fmtTypeMismatch(typeName, val, line)
}

func fmtTypeMismatch(expected string, val value.Value, line int) error {
	received := val.T.String()
	if val.T == value.TypeObject {
		received = val.AsObject().ClassName
	}
	if line > 0 {
		return fmt.Errorf("line %d: type mismatch — expected %s, received %s", line, expected, received)
	}
	return fmt.Errorf("type mismatch — expected %s, received %s", expected, received)
}

var builtinTypeMap = map[string]value.Type{
	"i32":    value.TypeI32,
	"i64":    value.TypeI64,
	"f64":    value.TypeF64,
	"u8":     value.TypeU8,
	"u32":    value.TypeU32,
	"u64":    value.TypeU64,
	"bool":   value.TypeBool,
	"string": value.TypeString,
	"void":   value.TypeVoid,
	"err":    value.TypeErr,
	"ptr":    value.TypePtr,
	"array":  value.TypeArray,
	"map":    value.TypeMap,
	"fn":     value.TypeFunc,
}

// checkReturnTypes validates return values against the current function's return type.
func (interp *Interpreter) checkReturnTypes(vals []value.Value, env *Env, line int) error {
	if env == nil {
		return nil
	}
	retType := env.CurrentReturnType()
	if retType == nil {
		return nil
	}
	for i, t := range retType.Types {
		if i < len(vals) {
			if err := interp.checkType(t, vals[i], line); err != nil {
				return err
			}
		}
	}
	return nil
}

// isKnownClass checks if a name is a registered class or interface.
func (interp *Interpreter) isKnownClass(name string) bool {
	if v, ok := interp.globals.Get(name); ok {
		return v.T == value.TypeClass
	}
	if _, ok := interp.interfaces[name]; ok {
		return true
	}
	return false
}

// getFieldType returns the declared type of a class field, or "" if unknown.
func (interp *Interpreter) getFieldType(className, fieldName string) string {
	v, ok := interp.globals.Get(className)
	if !ok || v.T != value.TypeClass {
		return ""
	}
	for _, f := range v.AsClass().Fields {
		if f.Name == fieldName {
			return f.Type
		}
	}
	return ""
}
