package value

import "fmt"

type Type int

func (t Type) String() string {
	switch t {
	case TypeVoid:
		return "void"
	case TypeBool:
		return "bool"
	case TypeI32:
		return "i32"
	case TypeI64:
		return "i64"
	case TypeF64:
		return "f64"
	case TypeU8:
		return "u8"
	case TypeU32:
		return "u32"
	case TypeU64:
		return "u64"
	case TypePtr:
		return "ptr"
	case TypeString:
		return "string"
	case TypeErr:
		return "err"
	case TypeArray:
		return "array"
	case TypeMap:
		return "map"
	case TypeObject:
		return "object"
	case TypeFunc:
		return "fn"
	case TypeNativeFunc:
		return "fn"
	case TypeModule:
		return "module"
	case TypeClass:
		return "class"
	}
	return fmt.Sprintf("type(%d)", int(t))
}

const (
	TypeVoid Type = iota
	TypeBool
	TypeI32
	TypeI64
	TypeF64
	TypeU8
	TypeU32
	TypeU64
	TypePtr
	TypeString
	TypeErr
	TypeArray
	TypeMap
	TypeObject
	TypeFunc
	TypeNativeFunc
	TypeModule
	TypeClass
)

type Value struct {
	T    Type
	Data interface{}
}

// Standard error kinds. These are the only valid kinds for err values.
// User code accesses them as err.not_found, err.permission, etc.
const (
	ErrKindNotFound   = "not_found"
	ErrKindPermission = "permission"
	ErrKindExists     = "exists"
	ErrKindEOF        = "eof"
	ErrKindIO         = "io"
	ErrKindParse      = "parse"
	ErrKindBounds     = "bounds"
	ErrKindType       = "type"
	ErrKindArg        = "arg"
	ErrKindTimeout    = "timeout"
	ErrKindClosed     = "closed"
	ErrKindState      = "state"
)

// ValidErrKinds is the set of valid error kinds.
var ValidErrKinds = map[string]bool{
	ErrKindNotFound:   true,
	ErrKindPermission: true,
	ErrKindExists:     true,
	ErrKindEOF:        true,
	ErrKindIO:         true,
	ErrKindParse:      true,
	ErrKindBounds:     true,
	ErrKindType:       true,
	ErrKindArg:        true,
	ErrKindTimeout:    true,
	ErrKindClosed:     true,
	ErrKindState:      true,
}

// Concrete data types
type ErrVal struct {
	Message string // "" means ok
	Kind    string // "" means ok, otherwise a standard error kind
}

type ArrayVal struct {
	Elems    []Value
	ElemType string // for type checking
}

type MapVal struct {
	Keys    []Value
	Values  []Value
	KeyType string // for type checking
	ValType string // for type checking
}

type FuncVal struct {
	Name   string
	Params []FuncParam
	Body   interface{} // *ast.Block
	Return interface{} // *ast.ReturnType
	// Closure environment captured at definition time
	Closure interface{}
}

type FuncParam struct {
	Type     string
	TypeExpr interface{} // *ast.TypeExpr, for full signature checking
	Name     string
}

type NativeFuncVal struct {
	Name string
	Fn   func(args []Value) (Value, error)
}

type ObjectVal struct {
	ClassName  string
	Implements []string // interface names this class implements
	Fields     map[string]Value
	Methods    map[string]*FuncVal
	NativeData interface{} // For storing native Go objects (e.g., *regexp.Regexp)
}

// ClassVal is a class descriptor — calling it constructs an instance.
type ClassVal struct {
	Name       string
	ModuleName string   // module this class belongs to (empty for top-level)
	Implements []string // interface names
	Fields     []ClassFieldDef
	Methods    map[string]*FuncVal
	Init       *FuncVal    // nil if no init method
	Closure    interface{} // defining environment
}

type ClassFieldDef struct {
	Name string
	Type string
	Pub  bool
}

// InterfaceVal describes an interface — a set of required method signatures.
type InterfaceVal struct {
	Name    string
	Methods []InterfaceMethodSig
}

type InterfaceMethodSig struct {
	Name        string
	ParamTypes  []string // parameter type names
	ReturnTypes []string // return type names (nil/empty for void)
}

// StructLayout describes a C-compatible struct layout.
type StructLayout struct {
	Fields    []StructFieldDef
	TotalSize int
}

// StructFieldDef describes one field in a C struct.
type StructFieldDef struct {
	Type   string
	Offset int
	Size   int
}

// StructVal is a C-layout struct backed by a raw byte buffer.
type StructVal struct {
	Layout *StructLayout
	Buf    []byte
}

// BufferVal is a raw byte buffer for passing to C APIs.
type BufferVal struct {
	Data []byte
}

// Constructors
var Void = Value{T: TypeVoid}
var True = Value{T: TypeBool, Data: true}
var False = Value{T: TypeBool, Data: false}
var Ok = Value{T: TypeErr, Data: &ErrVal{Message: ""}}
var NullPtr = Value{T: TypePtr, Data: uintptr(0)}

func NewI32(v int32) Value     { return Value{T: TypeI32, Data: v} }
func NewI64(v int64) Value     { return Value{T: TypeI64, Data: v} }
func NewF64(v float64) Value   { return Value{T: TypeF64, Data: v} }
func NewF32(v float32) Value   { return Value{T: TypeF64, Data: float64(v)} } // stored as f64 internally
func NewU8(v uint8) Value      { return Value{T: TypeU8, Data: v} }
func NewU32(v uint32) Value    { return Value{T: TypeU32, Data: v} }
func NewU64(v uint64) Value    { return Value{T: TypeU64, Data: v} }
func NewPtr(v uintptr) Value   { return Value{T: TypePtr, Data: v} }
func NewString(v string) Value { return Value{T: TypeString, Data: v} }
func NewBool(v bool) Value {
	if v {
		return True
	}
	return False
}
func NewErr(msg, kind string) Value {
	if msg == "" {
		panic("error message must not be empty")
	}
	if !ValidErrKinds[kind] {
		panic("invalid error kind: " + kind)
	}
	return Value{T: TypeErr, Data: &ErrVal{Message: msg, Kind: kind}}
}
func NewArray(elems []Value) Value { return Value{T: TypeArray, Data: &ArrayVal{Elems: elems}} }
func NewMap() Value                { return Value{T: TypeMap, Data: &MapVal{}} }
func NewFunc(f *FuncVal) Value     { return Value{T: TypeFunc, Data: f} }
func NewNativeFunc(name string, fn func([]Value) (Value, error)) Value {
	return Value{T: TypeNativeFunc, Data: &NativeFuncVal{Name: name, Fn: fn}}
}
func NewClass(c *ClassVal) Value { return Value{T: TypeClass, Data: c} }

// NewStruct allocates a zeroed C-layout struct.
func NewStruct(layout *StructLayout) *StructVal {
	return &StructVal{Layout: layout, Buf: make([]byte, layout.TotalSize)}
}

// Accessors
func (v Value) AsBool() bool                 { return v.Data.(bool) }
func (v Value) AsI32() int32                 { return v.Data.(int32) }
func (v Value) AsI64() int64                 { return v.Data.(int64) }
func (v Value) AsF64() float64               { return v.Data.(float64) }
func (v Value) AsU8() uint8                  { return v.Data.(uint8) }
func (v Value) AsU32() uint32                { return v.Data.(uint32) }
func (v Value) AsU64() uint64                { return v.Data.(uint64) }
func (v Value) AsPtr() uintptr               { return v.Data.(uintptr) }
func (v Value) AsString() string             { return v.Data.(string) }
func (v Value) AsErr() *ErrVal               { return v.Data.(*ErrVal) }
func (v Value) AsArray() *ArrayVal           { return v.Data.(*ArrayVal) }
func (v Value) AsMap() *MapVal               { return v.Data.(*MapVal) }
func (v Value) AsFunc() *FuncVal             { return v.Data.(*FuncVal) }
func (v Value) AsNativeFunc() *NativeFuncVal { return v.Data.(*NativeFuncVal) }
func (v Value) AsObject() *ObjectVal         { return v.Data.(*ObjectVal) }
func (v Value) AsClass() *ClassVal           { return v.Data.(*ClassVal) }

func (v Value) IsOk() bool {
	if v.T != TypeErr {
		return false
	}
	return v.AsErr().Message == ""
}

func (v Value) String() string {
	switch v.T {
	case TypeVoid:
		return "void"
	case TypeBool:
		return fmt.Sprintf("%t", v.AsBool())
	case TypeI32:
		return fmt.Sprintf("%d", v.AsI32())
	case TypeI64:
		return fmt.Sprintf("%d", v.AsI64())
	case TypeF64:
		return fmt.Sprintf("%g", v.AsF64())
	case TypeU8:
		return fmt.Sprintf("%d", v.AsU8())
	case TypeU32:
		return fmt.Sprintf("%d", v.AsU32())
	case TypeU64:
		return fmt.Sprintf("%d", v.AsU64())
	case TypePtr:
		if v.AsPtr() == 0 {
			return "null"
		}
		return fmt.Sprintf("ptr(0x%x)", v.AsPtr())
	case TypeString:
		return v.AsString()
	case TypeErr:
		e := v.AsErr()
		if e.Message == "" {
			return "ok"
		}
		return fmt.Sprintf("err(%q, %q)", e.Message, e.Kind)
	case TypeArray:
		return fmt.Sprintf("array[%d]", len(v.AsArray().Elems))
	case TypeMap:
		return fmt.Sprintf("map[%d]", len(v.AsMap().Keys))
	case TypeFunc:
		return fmt.Sprintf("fn<%s>", v.AsFunc().Name)
	case TypeNativeFunc:
		return fmt.Sprintf("native<%s>", v.AsNativeFunc().Name)
	case TypeObject:
		return fmt.Sprintf("<%s>", v.AsObject().ClassName)
	case TypeClass:
		return fmt.Sprintf("class<%s>", v.AsClass().Name)
	}
	return "?"
}

// DefaultValue returns the zero value for a type name.
func DefaultValue(typeName string) Value {
	switch typeName {
	case "bool":
		return False
	case "i32":
		return NewI32(0)
	case "i64":
		return NewI64(0)
	case "f64":
		return NewF64(0)
	case "u8":
		return NewU8(0)
	case "u32":
		return NewU32(0)
	case "u64":
		return NewU64(0)
	case "string":
		return NewString("")
	case "err":
		return Ok
	case "void":
		return Void
	case "unsafe.Ptr":
		return NullPtr
	}
	return Void
}
