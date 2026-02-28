package value

import "testing"

func TestNewConstructors(t *testing.T) {
	tests := []struct {
		name string
		val  Value
		want Type
	}{
		{"i32", NewI32(1), TypeI32},
		{"i64", NewI64(1), TypeI64},
		{"f64", NewF64(1.0), TypeF64},
		{"string", NewString("x"), TypeString},
		{"bool_true", NewBool(true), TypeBool},
		{"bool_false", NewBool(false), TypeBool},
		{"err", NewErr("fail"), TypeErr},
		{"array", NewArray(nil), TypeArray},
		{"map", NewMap(), TypeMap},
		{"func", NewFunc(&FuncVal{Name: "f"}), TypeFunc},
		{"native", NewNativeFunc("n", nil), TypeNativeFunc},
		{"class", NewClass(&ClassVal{Name: "C"}), TypeClass},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if tt.val.T != tt.want {
				t.Errorf("got type %d, want %d", tt.val.T, tt.want)
			}
		})
	}
}

func TestAccessors(t *testing.T) {
	if NewI32(42).AsI32() != 42 {
		t.Error("AsI32")
	}
	if NewI64(100).AsI64() != 100 {
		t.Error("AsI64")
	}
	if NewF64(3.14).AsF64() != 3.14 {
		t.Error("AsF64")
	}
	if NewString("hi").AsString() != "hi" {
		t.Error("AsString")
	}
	if NewBool(true).AsBool() != true {
		t.Error("AsBool")
	}
	if NewErr("oops").AsErr().Message != "oops" {
		t.Error("AsErr")
	}
}

func TestIsOk(t *testing.T) {
	tests := []struct {
		name string
		val  Value
		want bool
	}{
		{"ok", Ok, true},
		{"err", NewErr("fail"), false},
		{"non_err_type", NewI32(0), false},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := tt.val.IsOk(); got != tt.want {
				t.Errorf("IsOk() = %v, want %v", got, tt.want)
			}
		})
	}
}

func TestString(t *testing.T) {
	tests := []struct {
		name string
		val  Value
		want string
	}{
		{"void", Void, "void"},
		{"true", True, "true"},
		{"false", False, "false"},
		{"i32", NewI32(42), "42"},
		{"i64", NewI64(100), "100"},
		{"f64", NewF64(3.14), "3.14"},
		{"string", NewString("hello"), "hello"},
		{"ok", Ok, "ok"},
		{"err", NewErr("bad"), `err("bad")`},
		{"array", NewArray([]Value{NewI32(1), NewI32(2)}), "array[2]"},
		{"map", NewMap(), "map[0]"},
		{"func", NewFunc(&FuncVal{Name: "foo"}), "fn<foo>"},
		{"native", NewNativeFunc("bar", nil), "native<bar>"},
		{"class", NewClass(&ClassVal{Name: "Pt"}), "class<Pt>"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := tt.val.String(); got != tt.want {
				t.Errorf("String() = %q, want %q", got, tt.want)
			}
		})
	}
}

func TestDefaultValue(t *testing.T) {
	tests := []struct {
		typeName string
		wantType Type
	}{
		{"bool", TypeBool},
		{"i32", TypeI32},
		{"i64", TypeI64},
		{"f64", TypeF64},
		{"string", TypeString},
		{"err", TypeErr},
		{"void", TypeVoid},
		{"unknown", TypeVoid},
	}
	for _, tt := range tests {
		t.Run(tt.typeName, func(t *testing.T) {
			v := DefaultValue(tt.typeName)
			if v.T != tt.wantType {
				t.Errorf("DefaultValue(%q).T = %d, want %d", tt.typeName, v.T, tt.wantType)
			}
		})
	}
}

func TestConstants(t *testing.T) {
	if Void.T != TypeVoid {
		t.Error("Void type")
	}
	if True.T != TypeBool || !True.AsBool() {
		t.Error("True")
	}
	if False.T != TypeBool || False.AsBool() {
		t.Error("False")
	}
	if !Ok.IsOk() {
		t.Error("Ok")
	}
}
