package interp

import (
	"fmt"
	"sort"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// --- Sort ---

func (interp *Interpreter) makeSortModule() *Env {
	env := NewEnv(nil)
	env.Define("ints", value.NewNativeFunc("sort.ints", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeArray {
			return value.Void, fmt.Errorf("sort.ints: expected array<i32>")
		}
		arr := args[0].AsArray()
		sort.Slice(arr.Elems, func(i, j int) bool {
			return arr.Elems[i].AsI32() < arr.Elems[j].AsI32()
		})
		return value.Void, nil
	}))
	env.Define("strings", value.NewNativeFunc("sort.strings", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeArray {
			return value.Void, fmt.Errorf("sort.strings: expected array<string>")
		}
		arr := args[0].AsArray()
		sort.Slice(arr.Elems, func(i, j int) bool {
			return arr.Elems[i].AsString() < arr.Elems[j].AsString()
		})
		return value.Void, nil
	}))
	env.Define("by", value.NewNativeFunc("sort.by", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeArray {
			return value.Void, fmt.Errorf("sort.by: expected (array, fn comparator)")
		}
		if args[1].T != value.TypeFunc && args[1].T != value.TypeNativeFunc {
			return value.Void, fmt.Errorf("sort.by: second arg must be a function")
		}
		arr := args[0].AsArray()
		cmp := args[1]
		sort.Slice(arr.Elems, func(i, j int) bool {
			result, _ := interp.callFunc(cmp, []value.Value{arr.Elems[i], arr.Elems[j]})
			return result.T == value.TypeBool && result.AsBool()
		})
		return value.Void, nil
	}))
	return env
}
