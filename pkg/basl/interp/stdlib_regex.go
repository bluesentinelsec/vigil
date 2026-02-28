package interp

import (
	"fmt"
	"regexp"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

func (interp *Interpreter) makeRegexModule() *Env {
	env := NewEnv(nil)
	env.Define("match", value.NewNativeFunc("regex.match", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("regex.match: expected (string pattern, string s)")
		}
		matched, err := regexp.MatchString(args[0].AsString(), args[1].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.False, value.NewErr(err.Error())}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewBool(matched), value.Ok}}
	}))
	env.Define("find", value.NewNativeFunc("regex.find", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("regex.find: expected (string pattern, string s)")
		}
		re, err := regexp.Compile(args[0].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error())}}
		}
		m := re.FindString(args[1].AsString())
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(m), value.Ok}}
	}))
	env.Define("find_all", value.NewNativeFunc("regex.find_all", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("regex.find_all: expected (string pattern, string s)")
		}
		re, err := regexp.Compile(args[0].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewArray(nil), value.NewErr(err.Error())}}
		}
		matches := re.FindAllString(args[1].AsString(), -1)
		elems := make([]value.Value, len(matches))
		for i, m := range matches {
			elems[i] = value.NewString(m)
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewArray(elems), value.Ok}}
	}))
	env.Define("replace", value.NewNativeFunc("regex.replace", func(args []value.Value) (value.Value, error) {
		if len(args) != 3 || args[0].T != value.TypeString || args[1].T != value.TypeString || args[2].T != value.TypeString {
			return value.Void, fmt.Errorf("regex.replace: expected (string pattern, string s, string repl)")
		}
		re, err := regexp.Compile(args[0].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr(err.Error())}}
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(re.ReplaceAllString(args[1].AsString(), args[2].AsString())), value.Ok}}
	}))
	env.Define("split", value.NewNativeFunc("regex.split", func(args []value.Value) (value.Value, error) {
		if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
			return value.Void, fmt.Errorf("regex.split: expected (string pattern, string s)")
		}
		re, err := regexp.Compile(args[0].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewArray(nil), value.NewErr(err.Error())}}
		}
		parts := re.Split(args[1].AsString(), -1)
		elems := make([]value.Value, len(parts))
		for i, p := range parts {
			elems[i] = value.NewString(p)
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewArray(elems), value.Ok}}
	}))
	return env
}
