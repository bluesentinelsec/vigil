package dap

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"sync"

	"github.com/bluesentinelsec/basl/pkg/basl/ast"
	"github.com/bluesentinelsec/basl/pkg/basl/interp"
	"github.com/bluesentinelsec/basl/pkg/basl/lexer"
	"github.com/bluesentinelsec/basl/pkg/basl/parser"
	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

var errDebuggerDisconnected = errors.New("debugger disconnected")

type message struct {
	Seq        int             `json:"seq,omitempty"`
	Type       string          `json:"type"`
	Command    string          `json:"command,omitempty"`
	Arguments  json.RawMessage `json:"arguments,omitempty"`
	RequestSeq int             `json:"request_seq,omitempty"`
	Success    bool            `json:"success,omitempty"`
	Message    string          `json:"message,omitempty"`
	Event      string          `json:"event,omitempty"`
	Body       any             `json:"body,omitempty"`
}

type Server struct {
	mu      sync.Mutex
	sendMu  sync.Mutex
	nextSeq int
	sender  func(message) error
	session *session
}

type session struct {
	server  *Server
	program string
	source  string
	args    []string
	paths   []string
	cwd     string

	mu            sync.Mutex
	configured    bool
	started       bool
	stoppedReason string
	interp        *interp.Interpreter
	debugger      *runtimeDebugger
	frameRefs     map[int]*interp.Env
	varRefs       map[int]varContainer
	nextVarRef    int
	nextFrameRef  int
}

type varContainer struct {
	env   *interp.Env
	value *value.Value
}

type launchArgs struct {
	Program     string   `json:"program"`
	Args        []string `json:"args"`
	Cwd         string   `json:"cwd"`
	StopOnEntry bool     `json:"stopOnEntry"`
	Path        []string `json:"path"`
}

type source struct {
	Name string `json:"name,omitempty"`
	Path string `json:"path,omitempty"`
}

type sourceBreakpoint struct {
	Line         int    `json:"line"`
	Condition    string `json:"condition,omitempty"`
	HitCondition string `json:"hitCondition,omitempty"`
	LogMessage   string `json:"logMessage,omitempty"`
}

type setBreakpointsArgs struct {
	Source      source             `json:"source"`
	Breakpoints []sourceBreakpoint `json:"breakpoints"`
}

type stackFrame struct {
	ID     int    `json:"id"`
	Name   string `json:"name"`
	Line   int    `json:"line"`
	Column int    `json:"column"`
	Source source `json:"source"`
}

type scope struct {
	Name               string `json:"name"`
	VariablesReference int    `json:"variablesReference"`
	Expensive          bool   `json:"expensive"`
}

type variable struct {
	Name               string `json:"name"`
	Value              string `json:"value"`
	Type               string `json:"type,omitempty"`
	VariablesReference int    `json:"variablesReference"`
}

type runtimeFrame struct {
	Name string
	Line int
	Env  *interp.Env
}

type runtimeDebugger struct {
	mu          sync.Mutex
	cond        *sync.Cond
	breakpoints map[int]*runtimeBreakpoint
	callStack   []runtimeFrame
	currentLine int
	currentEnv  *interp.Env
	stepMode    string
	stepDepth   int
	resuming    bool
	file        string
	terminated  bool
	onStop      func(reason string)
	onOutput    func(text string)
}

type runtimeBreakpoint struct {
	line         int
	condition    string
	hitCondition string
	logMessage   string
	hits         int
}

var logpointPattern = regexp.MustCompile(`\{([^{}]+)\}`)

func newRuntimeDebugger(file string, stopOnEntry bool, onStop func(reason string), onOutput func(text string)) *runtimeDebugger {
	d := &runtimeDebugger{
		breakpoints: make(map[int]*runtimeBreakpoint),
		file:        file,
		onStop:      onStop,
		onOutput:    onOutput,
	}
	if stopOnEntry {
		d.stepMode = "entry"
	}
	d.cond = sync.NewCond(&d.mu)
	return d
}

func (d *runtimeDebugger) SetBreakpoints(items []*runtimeBreakpoint) {
	d.mu.Lock()
	defer d.mu.Unlock()
	d.breakpoints = make(map[int]*runtimeBreakpoint, len(items))
	for _, item := range items {
		if item != nil && item.line > 0 {
			d.breakpoints[item.line] = item
		}
	}
}

func (d *runtimeDebugger) Continue() {
	d.resume("", 0)
}

func (d *runtimeDebugger) StepIn() {
	d.resume("in", len(d.callStack))
}

func (d *runtimeDebugger) Next() {
	d.mu.Lock()
	depth := len(d.callStack)
	d.mu.Unlock()
	d.resume("over", depth)
}

func (d *runtimeDebugger) StepOut() {
	d.mu.Lock()
	depth := len(d.callStack) - 1
	if depth < 0 {
		depth = 0
	}
	d.mu.Unlock()
	d.resume("out", depth)
}

func (d *runtimeDebugger) Disconnect() {
	d.mu.Lock()
	d.terminated = true
	d.cond.Broadcast()
	d.mu.Unlock()
}

func (d *runtimeDebugger) resume(mode string, depth int) {
	d.mu.Lock()
	d.stepMode = mode
	d.stepDepth = depth
	d.resuming = true
	d.cond.Broadcast()
	d.mu.Unlock()
}

func (d *runtimeDebugger) Hook(stmt ast.Stmt, env *interp.Env) error {
	line := stmtLine(stmt)
	if line == 0 {
		return nil
	}

	d.mu.Lock()
	if len(d.callStack) > 0 {
		d.callStack[len(d.callStack)-1].Line = line
		d.callStack[len(d.callStack)-1].Env = env
	}
	reason := ""
	logOutput := ""
	switch {
	case d.stepMode == "entry":
		reason = "entry"
	case d.stepMode == "in":
		reason = "step"
	case d.stepMode == "over" && len(d.callStack) <= d.stepDepth:
		reason = "step"
	case d.stepMode == "out" && len(d.callStack) <= d.stepDepth:
		reason = "step"
	}
	if reason == "" {
		if bp, ok := d.breakpoints[line]; ok && bp != nil {
			bp.hits++
			if breakpointActive(bp, env) {
				if bp.logMessage != "" {
					logOutput = renderLogMessage(bp.logMessage, env)
				} else {
					reason = "breakpoint"
				}
			}
		}
	}
	if reason == "" {
		outputFn := d.onOutput
		if d.terminated {
			d.mu.Unlock()
			return errDebuggerDisconnected
		}
		d.mu.Unlock()
		if logOutput != "" && outputFn != nil {
			outputFn(logOutput)
		}
		return nil
	}
	d.currentLine = line
	d.currentEnv = env
	d.stepMode = ""
	d.resuming = false
	onStop := d.onStop
	outputFn := d.onOutput
	d.mu.Unlock()
	if logOutput != "" && outputFn != nil {
		outputFn(logOutput)
	}
	if onStop != nil {
		onStop(reason)
	}

	d.mu.Lock()
	for !d.terminated && !d.resuming {
		d.cond.Wait()
	}
	terminated := d.terminated
	d.resuming = false
	d.mu.Unlock()
	if terminated {
		return errDebuggerDisconnected
	}
	return nil
}

func (d *runtimeDebugger) PushFrame(name string, line int, env *interp.Env) {
	d.mu.Lock()
	defer d.mu.Unlock()
	d.callStack = append(d.callStack, runtimeFrame{Name: name, Line: line, Env: env})
}

func (d *runtimeDebugger) PopFrame() {
	d.mu.Lock()
	defer d.mu.Unlock()
	if len(d.callStack) > 0 {
		d.callStack = d.callStack[:len(d.callStack)-1]
	}
}

func (d *runtimeDebugger) Frames() []runtimeFrame {
	d.mu.Lock()
	defer d.mu.Unlock()
	out := make([]runtimeFrame, len(d.callStack))
	copy(out, d.callStack)
	return out
}

func (d *runtimeDebugger) CurrentEnv() *interp.Env {
	d.mu.Lock()
	defer d.mu.Unlock()
	return d.currentEnv
}

func Serve(ctx context.Context, r io.Reader, w io.Writer) error {
	server := &Server{
		nextSeq: 1,
		sender: func(msg message) error {
			data, err := json.Marshal(msg)
			if err != nil {
				return err
			}
			header := fmt.Sprintf("Content-Length: %d\r\n\r\n", len(data))
			if _, err := io.WriteString(w, header); err != nil {
				return err
			}
			_, err = w.Write(data)
			return err
		},
	}
	reader := bufio.NewReader(r)
	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		default:
		}
		body, err := readMessage(reader)
		if err != nil {
			if err == io.EOF {
				return nil
			}
			return err
		}
		var msg message
		if err := json.Unmarshal(body, &msg); err != nil {
			return err
		}
		if msg.Type != "request" {
			continue
		}
		if err := server.handleRequest(msg); err != nil {
			return err
		}
	}
}

func (s *Server) handleRequest(req message) error {
	switch req.Command {
	case "initialize":
		return s.respond(req, true, map[string]any{
			"supportsConfigurationDoneRequest":  true,
			"supportsConditionalBreakpoints":    true,
			"supportsEvaluateForHovers":         true,
			"supportsHitConditionalBreakpoints": true,
			"supportsLogPoints":                 true,
			"supportsSetVariable":               false,
			"supportsRestartRequest":            false,
		}, "")
	case "launch":
		return s.handleLaunch(req)
	case "setBreakpoints":
		return s.handleSetBreakpoints(req)
	case "setExceptionBreakpoints":
		return s.respond(req, true, map[string]any{"breakpoints": []any{}}, "")
	case "configurationDone":
		return s.handleConfigurationDone(req)
	case "threads":
		return s.respond(req, true, map[string]any{
			"threads": []map[string]any{{"id": 1, "name": "main"}},
		}, "")
	case "stackTrace":
		return s.handleStackTrace(req)
	case "scopes":
		return s.handleScopes(req)
	case "variables":
		return s.handleVariables(req)
	case "continue":
		return s.handleContinue(req)
	case "next":
		return s.handleStep(req, "next")
	case "stepIn":
		return s.handleStep(req, "in")
	case "stepOut":
		return s.handleStep(req, "out")
	case "evaluate":
		return s.handleEvaluate(req)
	case "disconnect", "terminate":
		return s.handleDisconnect(req)
	default:
		return s.respond(req, false, nil, fmt.Sprintf("unsupported command %q", req.Command))
	}
}

func (s *Server) handleLaunch(req message) error {
	var args launchArgs
	if err := json.Unmarshal(req.Arguments, &args); err != nil {
		return s.respond(req, false, nil, err.Error())
	}
	if args.Program == "" {
		return s.respond(req, false, nil, "launch.program is required")
	}
	absProgram, err := filepath.Abs(args.Program)
	if err != nil {
		return s.respond(req, false, nil, err.Error())
	}
	sess := &session{
		server:       s,
		program:      absProgram,
		args:         args.Args,
		cwd:          args.Cwd,
		paths:        args.Path,
		frameRefs:    make(map[int]*interp.Env),
		varRefs:      make(map[int]varContainer),
		nextVarRef:   1,
		nextFrameRef: 1,
	}
	sess.debugger = newRuntimeDebugger(absProgram, args.StopOnEntry, func(reason string) {
		sess.mu.Lock()
		sess.stoppedReason = reason
		sess.mu.Unlock()
		_ = s.event("stopped", map[string]any{
			"reason":            reason,
			"threadId":          1,
			"allThreadsStopped": true,
		})
	}, func(text string) {
		_ = s.event("output", map[string]any{
			"category": "console",
			"output":   text,
		})
	})
	s.mu.Lock()
	s.session = sess
	s.mu.Unlock()
	if err := s.respond(req, true, nil, ""); err != nil {
		return err
	}
	return s.event("initialized", map[string]any{})
}

func (s *Server) handleSetBreakpoints(req message) error {
	sess, err := s.requireSession()
	if err != nil {
		return s.respond(req, false, nil, err.Error())
	}
	var args setBreakpointsArgs
	if err := json.Unmarshal(req.Arguments, &args); err != nil {
		return s.respond(req, false, nil, err.Error())
	}
	breakpoints := make([]*runtimeBreakpoint, 0, len(args.Breakpoints))
	out := make([]map[string]any, 0, len(args.Breakpoints))
	for _, bp := range args.Breakpoints {
		runtimeBP, err := compileBreakpoint(bp)
		if err != nil {
			out = append(out, map[string]any{
				"verified": false,
				"line":     bp.Line,
				"message":  err.Error(),
			})
			continue
		}
		breakpoints = append(breakpoints, runtimeBP)
		out = append(out, map[string]any{"verified": true, "line": bp.Line})
	}
	if samePath(args.Source.Path, sess.program) || args.Source.Path == "" {
		sess.debugger.SetBreakpoints(breakpoints)
	}
	return s.respond(req, true, map[string]any{"breakpoints": out}, "")
}

func (s *Server) handleConfigurationDone(req message) error {
	sess, err := s.requireSession()
	if err != nil {
		return s.respond(req, false, nil, err.Error())
	}
	if err := sess.start(); err != nil {
		return s.respond(req, false, nil, err.Error())
	}
	return s.respond(req, true, nil, "")
}

func (s *Server) handleStackTrace(req message) error {
	sess, err := s.requireSession()
	if err != nil {
		return s.respond(req, false, nil, err.Error())
	}
	frames := sess.stackFrames()
	return s.respond(req, true, map[string]any{
		"stackFrames": frames,
		"totalFrames": len(frames),
	}, "")
}

func (s *Server) handleScopes(req message) error {
	sess, err := s.requireSession()
	if err != nil {
		return s.respond(req, false, nil, err.Error())
	}
	var args struct {
		FrameID int `json:"frameId"`
	}
	if err := json.Unmarshal(req.Arguments, &args); err != nil {
		return s.respond(req, false, nil, err.Error())
	}
	scopes, err := sess.scopes(args.FrameID)
	if err != nil {
		return s.respond(req, false, nil, err.Error())
	}
	return s.respond(req, true, map[string]any{"scopes": scopes}, "")
}

func (s *Server) handleVariables(req message) error {
	sess, err := s.requireSession()
	if err != nil {
		return s.respond(req, false, nil, err.Error())
	}
	var args struct {
		VariablesReference int `json:"variablesReference"`
	}
	if err := json.Unmarshal(req.Arguments, &args); err != nil {
		return s.respond(req, false, nil, err.Error())
	}
	vars, err := sess.variables(args.VariablesReference)
	if err != nil {
		return s.respond(req, false, nil, err.Error())
	}
	return s.respond(req, true, map[string]any{"variables": vars}, "")
}

func (s *Server) handleContinue(req message) error {
	sess, err := s.requireSession()
	if err != nil {
		return s.respond(req, false, nil, err.Error())
	}
	sess.debugger.Continue()
	return s.respond(req, true, map[string]any{"allThreadsContinued": true}, "")
}

func (s *Server) handleStep(req message, mode string) error {
	sess, err := s.requireSession()
	if err != nil {
		return s.respond(req, false, nil, err.Error())
	}
	switch mode {
	case "next":
		sess.debugger.Next()
	case "in":
		sess.debugger.StepIn()
	case "out":
		sess.debugger.StepOut()
	}
	return s.respond(req, true, nil, "")
}

func (s *Server) handleEvaluate(req message) error {
	sess, err := s.requireSession()
	if err != nil {
		return s.respond(req, false, nil, err.Error())
	}
	var args struct {
		Expression string `json:"expression"`
		FrameID    int    `json:"frameId"`
	}
	if err := json.Unmarshal(req.Arguments, &args); err != nil {
		return s.respond(req, false, nil, err.Error())
	}
	result, typ, ref, err := sess.evaluate(args.Expression, args.FrameID)
	if err != nil {
		return s.respond(req, false, nil, err.Error())
	}
	return s.respond(req, true, map[string]any{
		"result":             result,
		"type":               typ,
		"variablesReference": ref,
	}, "")
}

func (s *Server) handleDisconnect(req message) error {
	s.mu.Lock()
	sess := s.session
	s.session = nil
	s.mu.Unlock()
	if sess != nil {
		sess.debugger.Disconnect()
	}
	return s.respond(req, true, nil, "")
}

func (s *Server) respond(req message, success bool, body any, msg string) error {
	return s.send(message{
		Type:       "response",
		RequestSeq: req.Seq,
		Command:    req.Command,
		Success:    success,
		Message:    msg,
		Body:       body,
	})
}

func (s *Server) event(event string, body any) error {
	return s.send(message{
		Type:  "event",
		Event: event,
		Body:  body,
	})
}

func (s *Server) send(msg message) error {
	s.sendMu.Lock()
	defer s.sendMu.Unlock()
	s.mu.Lock()
	msg.Seq = s.nextSeq
	s.nextSeq++
	s.mu.Unlock()
	return s.sender(msg)
}

func (s *Server) requireSession() (*session, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.session == nil {
		return nil, fmt.Errorf("no active debug session")
	}
	return s.session, nil
}

func (s *session) start() error {
	s.mu.Lock()
	if s.started {
		s.mu.Unlock()
		return nil
	}
	s.started = true
	s.mu.Unlock()

	src, err := os.ReadFile(s.program)
	if err != nil {
		return err
	}
	s.source = string(src)
	tokens, err := lexer.New(s.source).Tokenize()
	if err != nil {
		return err
	}
	prog, err := parser.New(tokens).Parse()
	if err != nil {
		return err
	}

	vm := interp.New()
	vm.RegisterScriptArgs(s.args)
	for _, sp := range resolveSearchPaths(s.program, s.paths) {
		vm.AddSearchPath(sp)
	}
	vm.PrintFn = func(text string) {
		_ = s.server.event("output", map[string]any{
			"category": "stdout",
			"output":   text,
		})
	}
	vm.ErrFn = func(text string) {
		_ = s.server.event("output", map[string]any{
			"category": "stderr",
			"output":   text,
		})
	}
	vm.SetDebugger(s.debugger)
	s.mu.Lock()
	s.interp = vm
	s.mu.Unlock()

	go func() {
		code, err := vm.Exec(prog)
		if err != nil && !errors.Is(err, errDebuggerDisconnected) {
			_ = s.server.event("output", map[string]any{
				"category": "stderr",
				"output":   fmt.Sprintf("error[runtime]: %s\n", err),
			})
			code = 1
		}
		_ = s.server.event("exited", map[string]any{"exitCode": code})
		_ = s.server.event("terminated", map[string]any{})
	}()
	return nil
}

func (s *session) stackFrames() []stackFrame {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.frameRefs = make(map[int]*interp.Env)
	frames := s.debugger.Frames()
	out := make([]stackFrame, 0, len(frames))
	for i := len(frames) - 1; i >= 0; i-- {
		frameID := s.nextFrameRef
		s.nextFrameRef++
		s.frameRefs[frameID] = frames[i].Env
		out = append(out, stackFrame{
			ID:     frameID,
			Name:   frames[i].Name,
			Line:   max(frames[i].Line, 1),
			Column: 1,
			Source: source{Name: filepath.Base(s.program), Path: s.program},
		})
	}
	return out
}

func (s *session) scopes(frameID int) ([]scope, error) {
	s.mu.Lock()
	env := s.frameRefs[frameID]
	vm := s.interp
	s.mu.Unlock()
	if env == nil {
		return nil, fmt.Errorf("unknown frame %d", frameID)
	}
	out := []scope{{
		Name:               "Locals",
		VariablesReference: s.newEnvRef(env),
		Expensive:          false,
	}}
	if vm != nil {
		out = append(out, scope{
			Name:               "Globals",
			VariablesReference: s.newEnvRef(vm.GlobalsEnv()),
			Expensive:          false,
		})
	}
	return out, nil
}

func (s *session) variables(ref int) ([]variable, error) {
	s.mu.Lock()
	container, ok := s.varRefs[ref]
	s.mu.Unlock()
	if !ok {
		return nil, fmt.Errorf("unknown variables reference %d", ref)
	}
	switch {
	case container.env != nil:
		return s.envVariables(container.env), nil
	case container.value != nil:
		return s.valueVariables(*container.value), nil
	default:
		return nil, nil
	}
}

func (s *session) envVariables(env *interp.Env) []variable {
	vars := env.SnapshotVars()
	names := make([]string, 0, len(vars))
	for name := range vars {
		names = append(names, name)
	}
	sort.Strings(names)
	out := make([]variable, 0, len(names))
	for _, name := range names {
		out = append(out, s.toVariable(name, vars[name]))
	}
	return out
}

func (s *session) valueVariables(v value.Value) []variable {
	switch v.T {
	case value.TypeArray:
		arr := v.AsArray()
		out := make([]variable, 0, len(arr.Elems))
		for i, item := range arr.Elems {
			out = append(out, s.toVariable(fmt.Sprintf("[%d]", i), item))
		}
		return out
	case value.TypeMap:
		m := v.AsMap()
		out := make([]variable, 0, len(m.Keys))
		for i := range m.Keys {
			name := m.Keys[i].String()
			out = append(out, s.toVariable(name, m.Values[i]))
		}
		sort.Slice(out, func(i, j int) bool { return out[i].Name < out[j].Name })
		return out
	case value.TypeObject:
		obj := v.AsObject()
		names := make([]string, 0, len(obj.Fields))
		for name := range obj.Fields {
			names = append(names, name)
		}
		sort.Strings(names)
		out := make([]variable, 0, len(names))
		for _, name := range names {
			out = append(out, s.toVariable(name, obj.Fields[name]))
		}
		return out
	case value.TypeErr:
		errVal := v.AsErr()
		return []variable{
			{Name: "message", Value: errVal.Message, Type: "string"},
			{Name: "kind", Value: errVal.Kind, Type: "string"},
		}
	default:
		return nil
	}
}

func (s *session) toVariable(name string, v value.Value) variable {
	ref := 0
	switch v.T {
	case value.TypeArray, value.TypeMap, value.TypeObject, value.TypeErr:
		ref = s.newValueRef(v)
	}
	return variable{
		Name:               name,
		Value:              v.String(),
		Type:               v.T.String(),
		VariablesReference: ref,
	}
}

func (s *session) evaluate(expr string, frameID int) (string, string, int, error) {
	s.mu.Lock()
	env := s.frameRefs[frameID]
	vm := s.interp
	s.mu.Unlock()
	if env == nil && vm != nil {
		env = vm.GlobalsEnv()
	}
	if env == nil {
		return "", "", 0, fmt.Errorf("no evaluation context")
	}
	v, err := evalExpression(env, expr)
	if err != nil {
		return "", "", 0, err
	}
	ref := 0
	switch v.T {
	case value.TypeArray, value.TypeMap, value.TypeObject, value.TypeErr:
		ref = s.newValueRef(v)
	}
	return v.String(), v.T.String(), ref, nil
}

func (s *session) newEnvRef(env *interp.Env) int {
	s.mu.Lock()
	defer s.mu.Unlock()
	ref := s.nextVarRef
	s.nextVarRef++
	s.varRefs[ref] = varContainer{env: env}
	return ref
}

func (s *session) newValueRef(v value.Value) int {
	s.mu.Lock()
	defer s.mu.Unlock()
	ref := s.nextVarRef
	s.nextVarRef++
	copy := v
	s.varRefs[ref] = varContainer{value: &copy}
	return ref
}

func evalExpression(env *interp.Env, expr string) (value.Value, error) {
	parts := strings.Split(strings.TrimSpace(expr), ".")
	if len(parts) == 0 || parts[0] == "" {
		return value.Void, fmt.Errorf("empty expression")
	}
	cur, ok := env.Get(parts[0])
	if !ok {
		return value.Void, fmt.Errorf("%s not found", parts[0])
	}
	for _, part := range parts[1:] {
		if cur.T != value.TypeObject {
			return value.Void, fmt.Errorf("%s is not an object", part)
		}
		obj := cur.AsObject()
		field, ok := obj.Fields[part]
		if !ok {
			return value.Void, fmt.Errorf("%s not found", part)
		}
		cur = field
	}
	return cur, nil
}

func compileBreakpoint(bp sourceBreakpoint) (*runtimeBreakpoint, error) {
	if bp.Line <= 0 {
		return nil, fmt.Errorf("breakpoint line must be positive")
	}
	if bp.HitCondition != "" {
		if _, _, err := parseHitCondition(bp.HitCondition); err != nil {
			return nil, err
		}
	}
	return &runtimeBreakpoint{
		line:         bp.Line,
		condition:    strings.TrimSpace(bp.Condition),
		hitCondition: strings.TrimSpace(bp.HitCondition),
		logMessage:   bp.LogMessage,
	}, nil
}

func breakpointActive(bp *runtimeBreakpoint, env *interp.Env) bool {
	if bp == nil {
		return false
	}
	if ok, err := hitConditionSatisfied(bp.hitCondition, bp.hits); err != nil || !ok {
		return false
	}
	if strings.TrimSpace(bp.condition) == "" {
		return true
	}
	v, err := evalExpression(env, bp.condition)
	if err != nil {
		return false
	}
	return truthyValue(v)
}

func hitConditionSatisfied(expr string, hits int) (bool, error) {
	expr = strings.TrimSpace(expr)
	if expr == "" {
		return true, nil
	}
	n, mode, err := parseHitCondition(expr)
	if err != nil {
		return false, err
	}
	switch mode {
	case "ge":
		return hits >= n, nil
	default:
		return hits == n, nil
	}
}

func parseHitCondition(expr string) (int, string, error) {
	expr = strings.TrimSpace(expr)
	mode := "eq"
	if strings.HasPrefix(expr, ">=") {
		mode = "ge"
		expr = strings.TrimSpace(strings.TrimPrefix(expr, ">="))
	}
	n, err := strconv.Atoi(expr)
	if err != nil || n <= 0 {
		return 0, "", fmt.Errorf("unsupported hitCondition %q", expr)
	}
	return n, mode, nil
}

func truthyValue(v value.Value) bool {
	switch v.T {
	case value.TypeBool:
		return v.AsBool()
	case value.TypeI32:
		return v.AsI32() != 0
	case value.TypeI64:
		return v.AsI64() != 0
	case value.TypeU8:
		return v.AsU8() != 0
	case value.TypeU32:
		return v.AsU32() != 0
	case value.TypeU64:
		return v.AsU64() != 0
	case value.TypeF64:
		return v.AsF64() != 0
	case value.TypeString:
		return v.AsString() != ""
	case value.TypeVoid:
		return false
	default:
		return true
	}
}

func renderLogMessage(template string, env *interp.Env) string {
	if env == nil {
		return ensureTrailingNewline(template)
	}
	out := logpointPattern.ReplaceAllStringFunc(template, func(match string) string {
		parts := logpointPattern.FindStringSubmatch(match)
		if len(parts) != 2 {
			return match
		}
		v, err := evalExpression(env, strings.TrimSpace(parts[1]))
		if err != nil {
			return match
		}
		return v.String()
	})
	return ensureTrailingNewline(out)
}

func ensureTrailingNewline(text string) string {
	if strings.HasSuffix(text, "\n") {
		return text
	}
	return text + "\n"
}

func resolveSearchPaths(program string, extra []string) []string {
	var out []string
	add := func(path string) {
		if path == "" {
			return
		}
		abs, err := filepath.Abs(path)
		if err != nil {
			return
		}
		for _, existing := range out {
			if existing == abs {
				return
			}
		}
		out = append(out, abs)
	}
	add(filepath.Dir(program))
	if projectRoot := findProjectRoot(program); projectRoot != "" {
		add(filepath.Join(projectRoot, "lib"))
		add(filepath.Join(projectRoot, "deps"))
	}
	for _, path := range extra {
		add(path)
	}
	return out
}

func findProjectRoot(path string) string {
	current, err := filepath.Abs(path)
	if err != nil {
		return ""
	}
	if info, err := os.Stat(current); err == nil && !info.IsDir() {
		current = filepath.Dir(current)
	}
	for {
		if _, err := os.Stat(filepath.Join(current, "basl.toml")); err == nil {
			return current
		}
		parent := filepath.Dir(current)
		if parent == current {
			return ""
		}
		current = parent
	}
}

func readMessage(r *bufio.Reader) ([]byte, error) {
	var contentLength int
	for {
		line, err := r.ReadString('\n')
		if err != nil {
			return nil, err
		}
		line = strings.TrimRight(line, "\r\n")
		if line == "" {
			break
		}
		if strings.HasPrefix(strings.ToLower(line), "content-length:") {
			fmt.Sscanf(line, "Content-Length: %d", &contentLength)
		}
	}
	if contentLength <= 0 {
		return nil, fmt.Errorf("missing Content-Length header")
	}
	body := make([]byte, contentLength)
	if _, err := io.ReadFull(r, body); err != nil {
		return nil, err
	}
	return body, nil
}

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
	default:
		return 0
	}
}

func samePath(a, b string) bool {
	if a == "" || b == "" {
		return false
	}
	aa, errA := filepath.Abs(a)
	bb, errB := filepath.Abs(b)
	if errA != nil || errB != nil {
		return a == b
	}
	return aa == bb
}

func max(a, b int) int {
	if a < b {
		return b
	}
	return a
}
