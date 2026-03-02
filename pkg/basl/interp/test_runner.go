package interp

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/bluesentinelsec/basl/pkg/basl/ast"
	"github.com/bluesentinelsec/basl/pkg/basl/lexer"
	"github.com/bluesentinelsec/basl/pkg/basl/parser"
	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// TestResult holds the outcome of a single test function.
type TestResult struct {
	Name    string
	Passed  bool
	Message string // failure message (empty on pass)
	Elapsed time.Duration
}

// ExecTestFile parses a _test.basl file, discovers test_ functions, and runs
// each one in isolation. The filter (if non-empty) is a |-separated list of
// substrings; a test runs only if its name contains at least one substring.
func ExecTestFile(path string, src []byte, filter string, searchPaths []string) ([]TestResult, error) {
	tokens, err := lexer.New(string(src)).Tokenize()
	if err != nil {
		return nil, err
	}
	prog, err := parser.New(tokens).Parse()
	if err != nil {
		return nil, err
	}

	// Collect test function names.
	var testNames []string
	for _, d := range prog.Decls {
		fn, ok := d.(*ast.FnDecl)
		if !ok || !strings.HasPrefix(fn.Name, "test_") {
			continue
		}
		// Accept only single test.T parameter
		if len(fn.Params) != 1 {
			continue
		}
		if filter != "" && !matchFilter(fn.Name, filter) {
			continue
		}
		testNames = append(testNames, fn.Name)
	}

	dir, _ := filepath.Abs(filepath.Dir(path))

	var results []TestResult
	for _, name := range testNames {
		r := runOneTest(prog, name, dir, searchPaths)
		results = append(results, r)
	}
	return results, nil
}

func runOneTest(prog *ast.Program, name string, dir string, searchPaths []string) TestResult {
	// Change to test file directory so relative paths work
	origDir, err := os.Getwd()
	if err != nil {
		return TestResult{Name: name, Passed: false, Message: fmt.Sprintf("failed to get cwd: %v", err)}
	}
	if err := os.Chdir(dir); err != nil {
		return TestResult{Name: name, Passed: false, Message: fmt.Sprintf("failed to chdir to %s: %v", dir, err)}
	}
	defer os.Chdir(origDir)

	vm := New()
	vm.AddSearchPath(dir)
	for _, sp := range searchPaths {
		vm.AddSearchPath(sp)
	}
	// Register the test module as "test"
	testMod := vm.makeTestModule()
	vm.builtinModules["test"] = testMod

	vm.gil.Lock()
	defer vm.gil.Unlock()

	// Register all top-level declarations.
	for _, d := range prog.Decls {
		if err := vm.execTopDecl(d); err != nil {
			return TestResult{Name: name, Passed: false, Message: err.Error()}
		}
	}

	fnVal, ok := vm.globals.Get(name)
	if !ok {
		return TestResult{Name: name, Passed: false, Message: "test function not found"}
	}

	// Create a test.T object
	tObj := &value.ObjectVal{
		ClassName: "test.T",
		Fields:    map[string]value.Value{},
	}
	callArgs := []value.Value{{T: value.TypeObject, Data: tObj}}

	start := time.Now()
	_, callErr := vm.callFunc(fnVal, callArgs)
	elapsed := time.Since(start)

	if callErr != nil {
		if tf, ok := callErr.(*signalTestFail); ok {
			return TestResult{Name: name, Passed: false, Message: tf.Message, Elapsed: elapsed}
		}
		return TestResult{Name: name, Passed: false, Message: callErr.Error(), Elapsed: elapsed}
	}
	return TestResult{Name: name, Passed: true, Elapsed: elapsed}
}

func matchFilter(name, filter string) bool {
	for _, part := range strings.Split(filter, "|") {
		if strings.Contains(name, strings.TrimSpace(part)) {
			return true
		}
	}
	return false
}

// EnableTestModule registers the "test" module so test files can import "test".
func (interp *Interpreter) EnableTestModule() {
	interp.builtinModules["test"] = interp.makeTestModule()
}
