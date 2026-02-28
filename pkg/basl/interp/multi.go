package interp

import "github.com/bluesentinelsec/basl/pkg/basl/value"

// MultiReturnVal wraps multiple return values from a native function.
// Native functions that need to return tuples return this as a special error.
type MultiReturnVal struct {
	Values []value.Value
}

func (m *MultiReturnVal) Error() string { return "multi-return" }
