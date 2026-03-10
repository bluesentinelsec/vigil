package interp

import (
	"bufio"
	"fmt"
	"os"
	"sort"
	"strconv"
	"strings"

	"github.com/bluesentinelsec/basl/pkg/basl/ast"
	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// Debugger provides interactive breakpoint debugging for BASL programs.
type Debugger struct {
	breakpoints map[int]bool // line numbers with breakpoints
	stepping    bool         // true = break on next statement
	stepOver    bool         // true = break on next statement at same or lower depth
	stepDepth   int          // call depth when step-over was requested
	callStack   []DebugFrame
	interp      *Interpreter
	scanner     *bufio.Scanner
	sourceLines []string // source split by line for display
	file        string   // source file name
	quit        bool
}

// DebugFrame represents one entry in the call stack.
type DebugFrame struct {
	Func string
	Line int
}

// NewDebugger creates a debugger attached to an interpreter.
func NewDebugger(interp *Interpreter, file string, source string) *Debugger {
	return &Debugger{
		breakpoints: make(map[int]bool),
		interp:      interp,
		scanner:     bufio.NewScanner(os.Stdin),
		sourceLines: strings.Split(source, "\n"),
		file:        file,
	}
}

// SetBreakpoint adds a breakpoint at the given line.
func (d *Debugger) SetBreakpoint(line int) {
	d.breakpoints[line] = true
}

// StepFromStart causes the debugger to break on the first statement.
func (d *Debugger) StepFromStart() {
	d.stepping = true
}

// stmtLine extracts the line number from any statement node.
func stmtLine(s ast.Stmt) int {
	switch s := s.(type) {
	case *ast.VarStmt:
		return s.Line
	case *ast.AssignStmt:
		return s.Line
	case *ast.ExprStmt:
		return s.Line
	case *ast.ReturnStmt:
		return s.Line
	case *ast.IfStmt:
		return s.Line
	case *ast.WhileStmt:
		return s.Line
	case *ast.ForStmt:
		return s.Line
	case *ast.ForInStmt:
		return s.Line
	case *ast.BreakStmt:
		return s.Line
	case *ast.ContinueStmt:
		return s.Line
	case *ast.DeferStmt:
		return s.Line
	case *ast.GuardStmt:
		return s.Line
	case *ast.SwitchStmt:
		return s.Line
	case *ast.CompoundAssignStmt:
		return s.Line
	case *ast.IncDecStmt:
		return s.Line
	case *ast.TupleBindStmt:
		return s.Line
	}
	return 0
}

// Hook is called before each statement. Returns an error to abort execution.
func (d *Debugger) Hook(s ast.Stmt, env *Env) error {
	if d.quit {
		return fmt.Errorf("debugger: quit")
	}
	line := stmtLine(s)
	if line == 0 {
		return nil
	}

	shouldBreak := false
	if d.breakpoints[line] {
		shouldBreak = true
	} else if d.stepping {
		if d.stepOver {
			if len(d.callStack) <= d.stepDepth {
				shouldBreak = true
			}
		} else {
			shouldBreak = true
		}
	}

	if !shouldBreak {
		return nil
	}

	d.stepping = false
	d.stepOver = false
	d.showContext(line)
	return d.prompt(env)
}

// PushFrame records entering a function.
func (d *Debugger) PushFrame(name string, line int, env *Env) {
	_ = env
	d.callStack = append(d.callStack, DebugFrame{Func: name, Line: line})
}

// PopFrame records leaving a function.
func (d *Debugger) PopFrame() {
	if len(d.callStack) > 0 {
		d.callStack = d.callStack[:len(d.callStack)-1]
	}
}

func (d *Debugger) showContext(line int) {
	fmt.Printf("→ %s:%d\n", d.file, line)
	start := line - 2
	end := line + 2
	if start < 1 {
		start = 1
	}
	if end > len(d.sourceLines) {
		end = len(d.sourceLines)
	}
	for i := start; i <= end; i++ {
		marker := "  "
		if i == line {
			marker = "→ "
		}
		bp := " "
		if d.breakpoints[i] {
			bp = "●"
		}
		fmt.Printf("%s%s %3d  %s\n", marker, bp, i, d.sourceLines[i-1])
	}
}

func (d *Debugger) prompt(env *Env) error {
	for {
		fmt.Print("(basl) ")
		if !d.scanner.Scan() {
			d.quit = true
			return fmt.Errorf("debugger: quit")
		}
		input := strings.TrimSpace(d.scanner.Text())
		if input == "" {
			continue
		}

		parts := strings.SplitN(input, " ", 2)
		cmd := parts[0]
		arg := ""
		if len(parts) > 1 {
			arg = strings.TrimSpace(parts[1])
		}

		switch cmd {
		case "c", "continue":
			return nil
		case "n", "next":
			d.stepping = true
			d.stepOver = true
			d.stepDepth = len(d.callStack)
			return nil
		case "s", "step":
			d.stepping = true
			d.stepOver = false
			return nil
		case "q", "quit":
			d.quit = true
			return fmt.Errorf("debugger: quit")
		case "b", "break":
			d.cmdBreak(arg)
		case "d", "delete":
			d.cmdDelete(arg)
		case "p", "print":
			d.cmdPrint(arg, env)
		case "locals":
			d.cmdLocals(env)
		case "bt", "backtrace":
			d.cmdBacktrace()
		case "l", "list":
			d.cmdList(arg)
		case "h", "help":
			d.cmdHelp()
		default:
			fmt.Printf("unknown command %q — type h for help\n", cmd)
		}
	}
}

func (d *Debugger) cmdBreak(arg string) {
	if arg == "" {
		// List breakpoints
		if len(d.breakpoints) == 0 {
			fmt.Println("no breakpoints set")
			return
		}
		lines := make([]int, 0, len(d.breakpoints))
		for l := range d.breakpoints {
			lines = append(lines, l)
		}
		sort.Ints(lines)
		for _, l := range lines {
			fmt.Printf("  ● line %d\n", l)
		}
		return
	}
	n, err := strconv.Atoi(arg)
	if err != nil || n < 1 {
		fmt.Println("usage: b <line>")
		return
	}
	d.breakpoints[n] = true
	fmt.Printf("breakpoint set at line %d\n", n)
}

func (d *Debugger) cmdDelete(arg string) {
	if arg == "" {
		fmt.Println("usage: d <line>")
		return
	}
	n, err := strconv.Atoi(arg)
	if err != nil {
		fmt.Println("usage: d <line>")
		return
	}
	if d.breakpoints[n] {
		delete(d.breakpoints, n)
		fmt.Printf("breakpoint removed at line %d\n", n)
	} else {
		fmt.Printf("no breakpoint at line %d\n", n)
	}
}

func (d *Debugger) cmdPrint(arg string, env *Env) {
	if arg == "" {
		fmt.Println("usage: p <variable>")
		return
	}
	v, ok := env.Get(arg)
	if !ok {
		// Try globals
		v, ok = d.interp.globals.Get(arg)
	}
	if !ok {
		fmt.Printf("%s not found\n", arg)
		return
	}
	fmt.Printf("%s = %s\n", arg, formatDebugValue(v))
}

func (d *Debugger) cmdLocals(env *Env) {
	names := make([]string, 0, len(env.vars))
	for k := range env.vars {
		names = append(names, k)
	}
	sort.Strings(names)
	if len(names) == 0 {
		fmt.Println("no local variables")
		return
	}
	for _, k := range names {
		fmt.Printf("  %s = %s\n", k, formatDebugValue(env.vars[k]))
	}
}

func (d *Debugger) cmdBacktrace() {
	if len(d.callStack) == 0 {
		fmt.Println("  (empty call stack)")
		return
	}
	for i := len(d.callStack) - 1; i >= 0; i-- {
		f := d.callStack[i]
		fmt.Printf("  #%d %s (line %d)\n", len(d.callStack)-1-i, f.Func, f.Line)
	}
}

func (d *Debugger) cmdList(arg string) {
	center := 1
	if arg != "" {
		n, err := strconv.Atoi(arg)
		if err == nil && n > 0 {
			center = n
		}
	}
	start := center - 5
	end := center + 5
	if start < 1 {
		start = 1
	}
	if end > len(d.sourceLines) {
		end = len(d.sourceLines)
	}
	for i := start; i <= end; i++ {
		bp := " "
		if d.breakpoints[i] {
			bp = "●"
		}
		fmt.Printf("%s %3d  %s\n", bp, i, d.sourceLines[i-1])
	}
}

func (d *Debugger) cmdHelp() {
	fmt.Print(`Commands:
  b [line]     set breakpoint (no arg = list breakpoints)
  d <line>     delete breakpoint
  n            step over (next statement, skip into calls)
  s            step into (next statement, enter calls)
  c            continue to next breakpoint
  p <var>      print variable value
  locals       show all local variables
  bt           show call stack (backtrace)
  l [line]     list source around line
  q            quit debugger
  h            show this help
`)
}

func formatDebugValue(v value.Value) string {
	switch v.T {
	case value.TypeString:
		return fmt.Sprintf("%q", v.AsString())
	default:
		return v.String()
	}
}
