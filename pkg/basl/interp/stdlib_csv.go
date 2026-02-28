package interp

import (
	"encoding/csv"
	"fmt"
	"strings"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// --- CSV ---

func (interp *Interpreter) makeCsvModule() *Env {
	env := NewEnv(nil)
	env.Define("parse", value.NewNativeFunc("csv.parse", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("csv.parse: expected string")
		}
		r := csv.NewReader(strings.NewReader(args[0].AsString()))
		records, err := r.ReadAll()
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewArray(nil), value.NewErr(err.Error())}}
		}
		rows := make([]value.Value, len(records))
		for i, rec := range records {
			cols := make([]value.Value, len(rec))
			for j, c := range rec {
				cols[j] = value.NewString(c)
			}
			rows[i] = value.NewArray(cols)
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{value.NewArray(rows), value.Ok}}
	}))
	env.Define("stringify", value.NewNativeFunc("csv.stringify", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeArray {
			return value.Void, fmt.Errorf("csv.stringify: expected array<array<string>>")
		}
		var buf strings.Builder
		w := csv.NewWriter(&buf)
		for _, row := range args[0].AsArray().Elems {
			if row.T != value.TypeArray {
				continue
			}
			rec := make([]string, len(row.AsArray().Elems))
			for j, c := range row.AsArray().Elems {
				rec[j] = c.String()
			}
			w.Write(rec)
		}
		w.Flush()
		return value.NewString(buf.String()), nil
	}))
	return env
}
