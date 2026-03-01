package interp

import (
	"fmt"
	"os"
	"strconv"
	"strings"

	"github.com/bluesentinelsec/basl/pkg/basl/ast"
	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

func (interp *Interpreter) evalExpr(expr ast.Expr, env *Env) (value.Value, error) {
	switch e := expr.(type) {
	case *ast.IntLit:
		return value.NewI32(int32(e.Value)), nil
	case *ast.FloatLit:
		return value.NewF64(e.Value), nil
	case *ast.StringLit:
		return value.NewString(e.Value), nil
	case *ast.BoolLit:
		return value.NewBool(e.Value), nil
	case *ast.Ident:
		v, ok := env.Get(e.Name)
		if !ok {
			return value.Void, fmt.Errorf("line %d: undefined variable %q — check spelling or make sure it is declared before use", e.Line, e.Name)
		}
		return v, nil
	case *ast.UnaryExpr:
		return interp.evalUnary(e, env)
	case *ast.BinaryExpr:
		return interp.evalBinary(e, env)
	case *ast.TernaryExpr:
		return interp.evalTernary(e, env)
	case *ast.CallExpr:
		return interp.evalCall(e, env)
	case *ast.MemberExpr:
		return interp.evalMember(e, env)
	case *ast.ArrayLit:
		var elems []value.Value
		for _, el := range e.Elems {
			v, err := interp.evalExpr(el, env)
			if err != nil {
				return value.Void, err
			}
			elems = append(elems, v)
		}
		return value.NewArray(elems), nil
	case *ast.MapLit:
		m := value.NewMap()
		mv := m.AsMap()
		for i := range e.Keys {
			k, err := interp.evalExpr(e.Keys[i], env)
			if err != nil {
				return value.Void, err
			}
			v, err := interp.evalExpr(e.Values[i], env)
			if err != nil {
				return value.Void, err
			}
			mv.Keys = append(mv.Keys, k)
			mv.Values = append(mv.Values, v)
		}
		return m, nil
	case *ast.TupleExpr:
		if len(e.Elems) > 0 {
			return interp.evalExpr(e.Elems[0], env)
		}
		return value.Void, nil
	case *ast.TypeConvExpr:
		return interp.evalTypeConv(e, env)
	case *ast.ErrExpr:
		msg, err := interp.evalExpr(e.Msg, env)
		if err != nil {
			return value.Void, err
		}
		return value.NewErr(msg.String()), nil
	case *ast.SelfExpr:
		v, ok := env.Get("self")
		if !ok {
			return value.Void, fmt.Errorf("line %d: 'self' used outside of a class method — self is only available inside methods", e.Line)
		}
		return v, nil
	case *ast.IndexExpr:
		return interp.evalIndex(e, env)
	case *ast.FnLitExpr:
		fv := &value.FuncVal{
			Name:    e.Decl.Name,
			Body:    e.Decl.Body,
			Return:  e.Decl.Return,
			Closure: env,
		}
		for _, p := range e.Decl.Params {
			fv.Params = append(fv.Params, value.FuncParam{Type: p.Type.Name, TypeExpr: p.Type, Name: p.Name})
		}
		return value.NewFunc(fv), nil
	case *ast.FStringExpr:
		var buf string
		for _, part := range e.Parts {
			if part.IsExpr {
				v, err := interp.evalExpr(part.Expr, env)
				if err != nil {
					return value.Void, err
				}
				if part.Format != "" {
					buf += fmt.Sprintf("%"+part.Format, v.Data)
				} else {
					buf += v.String()
				}
			} else {
				buf += part.Text
			}
		}
		return value.NewString(buf), nil
	default:
		return value.Void, fmt.Errorf("unknown expression type: %T", expr)
	}
}

func (interp *Interpreter) evalUnary(e *ast.UnaryExpr, env *Env) (value.Value, error) {
	operand, err := interp.evalExpr(e.Operand, env)
	if err != nil {
		return value.Void, err
	}
	switch e.Op {
	case "!":
		if operand.T != value.TypeBool {
			return value.Void, fmt.Errorf("line %d: '!' requires bool operand, got %s", e.Line, operand.T)
		}
		return value.NewBool(!operand.AsBool()), nil
	case "-":
		switch operand.T {
		case value.TypeI32:
			return value.NewI32(-operand.AsI32()), nil
		case value.TypeI64:
			return value.NewI64(-operand.AsI64()), nil
		case value.TypeF64:
			return value.NewF64(-operand.AsF64()), nil
		default:
			return value.Void, fmt.Errorf("line %d: '-' requires numeric operand, got %s", e.Line, operand.T)
		}
	case "~":
		switch operand.T {
		case value.TypeI32:
			return value.NewI32(^operand.AsI32()), nil
		case value.TypeI64:
			return value.NewI64(^operand.AsI64()), nil
		case value.TypeU32:
			return value.NewU32(^operand.AsU32()), nil
		case value.TypeU64:
			return value.NewU64(^operand.AsU64()), nil
		default:
			return value.Void, fmt.Errorf("line %d: '~' requires integer operand, got %s", e.Line, operand.T)
		}
	}
	return value.Void, fmt.Errorf("line %d: unknown unary op %q", e.Line, e.Op)
}

func (interp *Interpreter) evalTernary(e *ast.TernaryExpr, env *Env) (value.Value, error) {
	condition, err := interp.evalExpr(e.Condition, env)
	if err != nil {
		return value.Void, err
	}

	if condition.T != value.TypeBool {
		return value.Void, fmt.Errorf("line %d: ternary condition must be bool, got %s", e.Line, condition.T)
	}

	if condition.AsBool() {
		return interp.evalExpr(e.TrueExpr, env)
	}
	return interp.evalExpr(e.FalseExpr, env)
}

func (interp *Interpreter) evalBinary(e *ast.BinaryExpr, env *Env) (value.Value, error) {
	left, err := interp.evalExpr(e.Left, env)
	if err != nil {
		return value.Void, err
	}

	// Short-circuit
	if e.Op == "&&" {
		if left.T != value.TypeBool {
			return value.Void, fmt.Errorf("line %d: '&&' requires bool operands, got %s", e.Line, left.T)
		}
		if !left.AsBool() {
			return value.False, nil
		}
		right, err := interp.evalExpr(e.Right, env)
		if err != nil {
			return value.Void, err
		}
		return right, nil
	}
	if e.Op == "||" {
		if left.T != value.TypeBool {
			return value.Void, fmt.Errorf("line %d: '||' requires bool operands, got %s", e.Line, left.T)
		}
		if left.AsBool() {
			return value.True, nil
		}
		right, err := interp.evalExpr(e.Right, env)
		if err != nil {
			return value.Void, err
		}
		return right, nil
	}

	right, err := interp.evalExpr(e.Right, env)
	if err != nil {
		return value.Void, err
	}

	// String concatenation
	if e.Op == "+" && left.T == value.TypeString && right.T == value.TypeString {
		return value.NewString(left.AsString() + right.AsString()), nil
	}

	// err comparison
	if left.T == value.TypeErr && right.T == value.TypeErr {
		switch e.Op {
		case "==":
			return value.NewBool(left.IsOk() == right.IsOk()), nil
		case "!=":
			return value.NewBool(left.IsOk() != right.IsOk()), nil
		}
	}

	// Numeric
	if left.T == value.TypeI32 && right.T == value.TypeI32 {
		return evalI32BinOp(e.Op, left.AsI32(), right.AsI32(), e.Line)
	}
	if left.T == value.TypeI64 && right.T == value.TypeI64 {
		return evalI64BinOp(e.Op, left.AsI64(), right.AsI64(), e.Line)
	}
	if left.T == value.TypeF64 && right.T == value.TypeF64 {
		return evalF64BinOp(e.Op, left.AsF64(), right.AsF64(), e.Line)
	}
	if left.T == value.TypeU8 && right.T == value.TypeU8 {
		return evalU32BinOp(e.Op, uint32(left.AsU8()), uint32(right.AsU8()), e.Line, value.TypeU8)
	}
	if left.T == value.TypeU32 && right.T == value.TypeU32 {
		return evalU32BinOp(e.Op, left.AsU32(), right.AsU32(), e.Line, value.TypeU32)
	}
	if left.T == value.TypeU64 && right.T == value.TypeU64 {
		return evalU64BinOp(e.Op, left.AsU64(), right.AsU64(), e.Line)
	}

	// Bool equality
	if left.T == value.TypeBool && right.T == value.TypeBool {
		switch e.Op {
		case "==":
			return value.NewBool(left.AsBool() == right.AsBool()), nil
		case "!=":
			return value.NewBool(left.AsBool() != right.AsBool()), nil
		}
	}

	// String equality
	if left.T == value.TypeString && right.T == value.TypeString {
		switch e.Op {
		case "==":
			return value.NewBool(left.AsString() == right.AsString()), nil
		case "!=":
			return value.NewBool(left.AsString() != right.AsString()), nil
		case "<":
			return value.NewBool(left.AsString() < right.AsString()), nil
		case "<=":
			return value.NewBool(left.AsString() <= right.AsString()), nil
		case ">":
			return value.NewBool(left.AsString() > right.AsString()), nil
		case ">=":
			return value.NewBool(left.AsString() >= right.AsString()), nil
		}
	}

	return value.Void, fmt.Errorf("line %d: cannot apply %q to %s and %s — operands must be the same numeric type", e.Line, e.Op, left.T, right.T)
}

func evalI32BinOp(op string, a, b int32, line int) (value.Value, error) {
	switch op {
	case "+":
		return value.NewI32(a + b), nil
	case "-":
		return value.NewI32(a - b), nil
	case "*":
		return value.NewI32(a * b), nil
	case "/":
		if b == 0 {
			return value.Void, fmt.Errorf("line %d: division by zero", line)
		}
		return value.NewI32(a / b), nil
	case "%":
		if b == 0 {
			return value.Void, fmt.Errorf("line %d: modulo by zero", line)
		}
		return value.NewI32(a % b), nil
	case "&":
		return value.NewI32(a & b), nil
	case "|":
		return value.NewI32(a | b), nil
	case "<<":
		return value.NewI32(a << uint(b)), nil
	case ">>":
		return value.NewI32(a >> uint(b)), nil
	case "^":
		return value.NewI32(a ^ b), nil
	case "<":
		return value.NewBool(a < b), nil
	case ">":
		return value.NewBool(a > b), nil
	case "<=":
		return value.NewBool(a <= b), nil
	case ">=":
		return value.NewBool(a >= b), nil
	case "==":
		return value.NewBool(a == b), nil
	case "!=":
		return value.NewBool(a != b), nil
	}
	return value.Void, fmt.Errorf("line %d: unsupported i32 op %q", line, op)
}

func evalI64BinOp(op string, a, b int64, line int) (value.Value, error) {
	switch op {
	case "+":
		return value.NewI64(a + b), nil
	case "-":
		return value.NewI64(a - b), nil
	case "*":
		return value.NewI64(a * b), nil
	case "/":
		if b == 0 {
			return value.Void, fmt.Errorf("line %d: division by zero", line)
		}
		return value.NewI64(a / b), nil
	case "%":
		if b == 0 {
			return value.Void, fmt.Errorf("line %d: modulo by zero", line)
		}
		return value.NewI64(a % b), nil
	case "&":
		return value.NewI64(a & b), nil
	case "|":
		return value.NewI64(a | b), nil
	case "^":
		return value.NewI64(a ^ b), nil
	case "<<":
		return value.NewI64(a << uint(b)), nil
	case ">>":
		return value.NewI64(a >> uint(b)), nil
	case "<":
		return value.NewBool(a < b), nil
	case ">":
		return value.NewBool(a > b), nil
	case "<=":
		return value.NewBool(a <= b), nil
	case ">=":
		return value.NewBool(a >= b), nil
	case "==":
		return value.NewBool(a == b), nil
	case "!=":
		return value.NewBool(a != b), nil
	}
	return value.Void, fmt.Errorf("line %d: unsupported i64 op %q", line, op)
}

func evalU32BinOp(op string, a, b uint32, line int, t value.Type) (value.Value, error) {
	wrap := func(v uint32) value.Value {
		if t == value.TypeU8 {
			return value.NewU8(uint8(v))
		}
		return value.NewU32(v)
	}
	switch op {
	case "+":
		return wrap(a + b), nil
	case "-":
		return wrap(a - b), nil
	case "*":
		return wrap(a * b), nil
	case "/":
		if b == 0 {
			return value.Void, fmt.Errorf("line %d: division by zero", line)
		}
		return wrap(a / b), nil
	case "%":
		if b == 0 {
			return value.Void, fmt.Errorf("line %d: modulo by zero", line)
		}
		return wrap(a % b), nil
	case "&":
		return wrap(a & b), nil
	case "|":
		return wrap(a | b), nil
	case "<<":
		return wrap(a << b), nil
	case ">>":
		return wrap(a >> b), nil
	case "^":
		return wrap(a ^ b), nil
	case "<":
		return value.NewBool(a < b), nil
	case ">":
		return value.NewBool(a > b), nil
	case "<=":
		return value.NewBool(a <= b), nil
	case ">=":
		return value.NewBool(a >= b), nil
	case "==":
		return value.NewBool(a == b), nil
	case "!=":
		return value.NewBool(a != b), nil
	}
	return value.Void, fmt.Errorf("line %d: unsupported u32 op %q", line, op)
}

func evalU64BinOp(op string, a, b uint64, line int) (value.Value, error) {
	switch op {
	case "+":
		return value.NewU64(a + b), nil
	case "-":
		return value.NewU64(a - b), nil
	case "*":
		return value.NewU64(a * b), nil
	case "/":
		if b == 0 {
			return value.Void, fmt.Errorf("line %d: division by zero", line)
		}
		return value.NewU64(a / b), nil
	case "%":
		if b == 0 {
			return value.Void, fmt.Errorf("line %d: modulo by zero", line)
		}
		return value.NewU64(a % b), nil
	case "&":
		return value.NewU64(a & b), nil
	case "|":
		return value.NewU64(a | b), nil
	case "<<":
		return value.NewU64(a << b), nil
	case ">>":
		return value.NewU64(a >> b), nil
	case "^":
		return value.NewU64(a ^ b), nil
	case "<":
		return value.NewBool(a < b), nil
	case ">":
		return value.NewBool(a > b), nil
	case "<=":
		return value.NewBool(a <= b), nil
	case ">=":
		return value.NewBool(a >= b), nil
	case "==":
		return value.NewBool(a == b), nil
	case "!=":
		return value.NewBool(a != b), nil
	}
	return value.Void, fmt.Errorf("line %d: unsupported u64 op %q", line, op)
}

func evalF64BinOp(op string, a, b float64, line int) (value.Value, error) {
	switch op {
	case "+":
		return value.NewF64(a + b), nil
	case "-":
		return value.NewF64(a - b), nil
	case "*":
		return value.NewF64(a * b), nil
	case "/":
		if b == 0 {
			return value.Void, fmt.Errorf("line %d: division by zero", line)
		}
		return value.NewF64(a / b), nil
	case "<":
		return value.NewBool(a < b), nil
	case ">":
		return value.NewBool(a > b), nil
	case "<=":
		return value.NewBool(a <= b), nil
	case ">=":
		return value.NewBool(a >= b), nil
	case "==":
		return value.NewBool(a == b), nil
	case "!=":
		return value.NewBool(a != b), nil
	}
	return value.Void, fmt.Errorf("line %d: unsupported f64 op %q", line, op)
}

func (interp *Interpreter) evalCall(e *ast.CallExpr, env *Env) (value.Value, error) {
	callee, err := interp.evalExpr(e.Callee, env)
	if err != nil {
		return value.Void, err
	}
	var args []value.Value
	for _, a := range e.Args {
		v, err := interp.evalExpr(a, env)
		if err != nil {
			return value.Void, err
		}
		args = append(args, v)
	}
	return interp.callFunc(callee, args)
}

func (interp *Interpreter) callFunc(callee value.Value, args []value.Value) (value.Value, error) {
	switch callee.T {
	case value.TypeFunc:
		vals, err := interp.callFuncMulti(callee, args)
		if err != nil {
			return value.Void, err
		}
		if len(vals) > 0 {
			return vals[0], nil
		}
		return value.Void, nil
	case value.TypeNativeFunc:
		nf := callee.AsNativeFunc()
		v, err := nf.Fn(args)
		if err != nil {
			if mr, ok := err.(*MultiReturnVal); ok {
				if len(mr.Values) > 0 {
					return mr.Values[0], nil
				}
				return value.Void, nil
			}
			return value.Void, err
		}
		return v, nil
	case value.TypeClass:
		obj, err := interp.constructObject(callee.AsClass(), args)
		if err != nil {
			// Check for multi-return (fallible init)
			if mr, ok := err.(*MultiReturnVal); ok {
				if len(mr.Values) > 0 {
					return mr.Values[0], nil
				}
			}
			return value.Void, err
		}
		return obj, nil
	default:
		return value.Void, fmt.Errorf("%s is not callable — expected a function or class constructor", callee)
	}
}

func (interp *Interpreter) callFuncMulti(callee value.Value, args []value.Value) ([]value.Value, error) {
	fn := callee.AsFunc()
	// Use closure environment if present (module-level functions), otherwise globals
	parent := interp.globals
	if fn.Closure != nil {
		parent = fn.Closure.(*Env)
	}
	fnEnv := NewEnv(parent)
	deferStack := make([]deferredCall, 0)
	fnEnv.defers = &deferStack
	if fn.Return != nil {
		fnEnv.returnType = fn.Return.(*ast.ReturnType)
	}
	for i, p := range fn.Params {
		if i < len(args) {
			if p.TypeExpr != nil {
				te := p.TypeExpr.(*ast.TypeExpr)
				if err := interp.checkType(te, args[i], 0); err != nil {
					return nil, fmt.Errorf("fn %s: parameter %q: %s", fn.Name, p.Name, err)
				}
			} else if err := interp.checkTypeByName(p.Type, args[i], 0); err != nil {
				return nil, fmt.Errorf("fn %s: parameter %q: %s", fn.Name, p.Name, err)
			}
			fnEnv.Define(p.Name, args[i])
		} else {
			fnEnv.Define(p.Name, value.DefaultValue(p.Type))
		}
	}
	body := fn.Body.(*ast.Block)
	if interp.debugger != nil {
		line := 0
		if len(body.Stmts) > 0 {
			line = stmtLine(body.Stmts[0])
		}
		interp.debugger.PushFrame(fn.Name, line)
	}
	err := interp.execBlock(body, fnEnv)
	if interp.debugger != nil {
		interp.debugger.PopFrame()
	}

	// Run deferred calls LIFO
	for i := len(deferStack) - 1; i >= 0; i-- {
		d := deferStack[i]
		result, derr := interp.callFunc(d.callee, d.args)
		if derr == nil && result.T == value.TypeErr && !result.IsOk() {
			fmt.Fprintf(os.Stderr, "deferred call error: %s\n", result.AsErr().Message)
		}
	}

	if err != nil {
		if ret, ok := err.(*signalReturn); ok {
			return ret.Values, nil
		}
		return nil, err
	}
	return []value.Value{value.Void}, nil
}

func (interp *Interpreter) evalMember(e *ast.MemberExpr, env *Env) (value.Value, error) {
	obj, err := interp.evalExpr(e.Object, env)
	if err != nil {
		return value.Void, err
	}
	switch obj.T {
	case value.TypeModule:
		// Module member access: look up in module env
		modEnv := obj.Data.(*Env)
		v, ok := modEnv.Get(e.Field)
		if !ok {
			return value.Void, fmt.Errorf("line %d: module has no member %q — check the import or spelling", e.Line, e.Field)
		}
		return v, nil
	case value.TypeString:
		return interp.stringMethod(obj, e.Field, e.Line)
	case value.TypeArray:
		return interp.arrayMethod(obj, e.Field, e.Line)
	case value.TypeMap:
		return interp.mapMethod(obj, e.Field, e.Line)
	case value.TypeErr:
		if e.Field == "message" {
			return value.NewNativeFunc("err.message", func(args []value.Value) (value.Value, error) {
				if len(args) != 0 {
					return value.Void, fmt.Errorf("err.message: expected 0 arguments, got %d", len(args))
				}
				return value.NewString(obj.AsErr().Message), nil
			}), nil
		}
		if e.Field == "is_eof" {
			return value.NewNativeFunc("err.is_eof", func(args []value.Value) (value.Value, error) {
				if len(args) != 0 {
					return value.Void, fmt.Errorf("err.is_eof: expected 0 arguments, got %d", len(args))
				}
				if obj.AsErr().IsEOF {
					return value.True, nil
				}
				return value.False, nil
			}), nil
		}
	case value.TypeObject:
		o := obj.AsObject()
		// Dispatch to json.Value / xml.Value methods
		if o.ClassName == "json.Value" {
			return interp.jsonMethod(obj, e.Field, e.Line)
		}
		if o.ClassName == "xml.Value" {
			return interp.xmlMethod(obj, e.Field, e.Line)
		}
		if o.ClassName == "File" {
			return interp.fileMethod(obj, e.Field, e.Line)
		}
		if o.ClassName == "TcpListener" {
			return interp.tcpListenerMethod(obj, e.Field, e.Line)
		}
		if o.ClassName == "TcpConn" {
			return interp.tcpConnMethod(obj, e.Field, e.Line)
		}
		if o.ClassName == "UdpConn" {
			return interp.udpConnMethod(obj, e.Field, e.Line)
		}
		if o.ClassName == "SqliteDB" {
			return interp.sqliteDBMethod(obj, e.Field, e.Line)
		}
		if o.ClassName == "SqliteRows" {
			return interp.sqliteRowsMethod(obj, e.Field, e.Line)
		}
		if o.ClassName == "ArgParser" {
			return interp.argParserMethod(obj, e.Field, e.Line)
		}
		if o.ClassName == "Thread" {
			return interp.threadMethod(obj, e.Field, e.Line)
		}
		if o.ClassName == "Mutex" {
			return interp.mutexMethod(obj, e.Field, e.Line)
		}
		if v, ok := o.Fields[e.Field]; ok {
			return v, nil
		}
		if m, ok := o.Methods[e.Field]; ok {
			return value.NewNativeFunc(e.Field, func(args []value.Value) (value.Value, error) {
				methodEnv := NewEnv(interp.globals)
				methodEnv.Define("self", obj)
				for i, p := range m.Params {
					if i < len(args) {
						methodEnv.Define(p.Name, args[i])
					}
				}
				if m.Return != nil {
					methodEnv.returnType = m.Return.(*ast.ReturnType)
				}
				body := m.Body.(*ast.Block)
				err := interp.execBlock(body, methodEnv)
				if ret, ok := err.(*signalReturn); ok {
					if len(ret.Values) == 1 {
						return ret.Values[0], nil
					}
				}
				if err != nil {
					return value.Void, err
				}
				return value.Void, nil
			}), nil
		}
	}
	return value.Void, fmt.Errorf("line %d: no field or method %q on %s", e.Line, e.Field, obj.T)
}

func (interp *Interpreter) stringMethod(obj value.Value, method string, line int) (value.Value, error) {
	s := obj.AsString()
	switch method {
	case "len":
		return value.NewNativeFunc("string.len", func(args []value.Value) (value.Value, error) {
			return value.NewI32(int32(len(s))), nil
		}), nil
	case "contains":
		return value.NewNativeFunc("string.contains", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("string.contains: expected string argument")
			}
			return value.NewBool(strings.Contains(s, args[0].AsString())), nil
		}), nil
	case "starts_with":
		return value.NewNativeFunc("string.starts_with", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("string.starts_with: expected string argument")
			}
			return value.NewBool(strings.HasPrefix(s, args[0].AsString())), nil
		}), nil
	case "ends_with":
		return value.NewNativeFunc("string.ends_with", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("string.ends_with: expected string argument")
			}
			return value.NewBool(strings.HasSuffix(s, args[0].AsString())), nil
		}), nil
	case "trim":
		return value.NewNativeFunc("string.trim", func(args []value.Value) (value.Value, error) {
			return value.NewString(strings.TrimSpace(s)), nil
		}), nil
	case "to_upper":
		return value.NewNativeFunc("string.to_upper", func(args []value.Value) (value.Value, error) {
			return value.NewString(strings.ToUpper(s)), nil
		}), nil
	case "to_lower":
		return value.NewNativeFunc("string.to_lower", func(args []value.Value) (value.Value, error) {
			return value.NewString(strings.ToLower(s)), nil
		}), nil
	case "replace":
		return value.NewNativeFunc("string.replace", func(args []value.Value) (value.Value, error) {
			if len(args) != 2 || args[0].T != value.TypeString || args[1].T != value.TypeString {
				return value.Void, fmt.Errorf("string.replace: expected (string old, string new)")
			}
			return value.NewString(strings.ReplaceAll(s, args[0].AsString(), args[1].AsString())), nil
		}), nil
	case "split":
		return value.NewNativeFunc("string.split", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("string.split: expected string separator")
			}
			parts := strings.Split(s, args[0].AsString())
			elems := make([]value.Value, len(parts))
			for i, p := range parts {
				elems[i] = value.NewString(p)
			}
			return value.NewArray(elems), nil
		}), nil
	case "index_of":
		return value.NewNativeFunc("string.index_of", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("string.index_of: expected string argument")
			}
			idx := strings.Index(s, args[0].AsString())
			if idx < 0 {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(0), value.False}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(int32(idx)), value.True}}
		}), nil
	case "substr":
		return value.NewNativeFunc("string.substr", func(args []value.Value) (value.Value, error) {
			if len(args) != 2 || args[0].T != value.TypeI32 || args[1].T != value.TypeI32 {
				return value.Void, fmt.Errorf("string.substr: expected (i32 start, i32 length)")
			}
			start := int(args[0].AsI32())
			length := int(args[1].AsI32())
			if start < 0 || start > len(s) || start+length > len(s) || length < 0 {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr("substr out of bounds")}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(s[start : start+length]), value.Ok}}
		}), nil
	case "bytes":
		return value.NewNativeFunc("string.bytes", func(args []value.Value) (value.Value, error) {
			b := []byte(s)
			elems := make([]value.Value, len(b))
			for i, c := range b {
				elems[i] = value.NewU8(c)
			}
			return value.NewArray(elems), nil
		}), nil
	case "char_at":
		return value.NewNativeFunc("string.char_at", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeI32 {
				return value.Void, fmt.Errorf("string.char_at: expected i32 index")
			}
			idx := int(args[0].AsI32())
			if idx < 0 || idx >= len(s) {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(""), value.NewErr("index out of bounds")}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewString(string(s[idx])), value.Ok}}
		}), nil
	}
	return value.Void, fmt.Errorf("line %d: string has no method %q", line, method)
}

func (interp *Interpreter) arrayMethod(obj value.Value, method string, line int) (value.Value, error) {
	arr := obj.AsArray()
	switch method {
	case "len":
		return value.NewNativeFunc("array.len", func(args []value.Value) (value.Value, error) {
			return value.NewI32(int32(len(arr.Elems))), nil
		}), nil
	case "push":
		return value.NewNativeFunc("array.push", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 {
				return value.Void, fmt.Errorf("array.push: expected 1 argument")
			}
			if arr.ElemType != "" {
				if err := interp.checkTypeByName(arr.ElemType, args[0], 0); err != nil {
					return value.Void, fmt.Errorf("array.push: %s", err)
				}
			}
			arr.Elems = append(arr.Elems, args[0])
			return value.Void, nil
		}), nil
	case "get":
		return value.NewNativeFunc("array.get", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeI32 {
				return value.Void, fmt.Errorf("array.get: expected i32 index")
			}
			idx := int(args[0].AsI32())
			if idx < 0 || idx >= len(arr.Elems) {
				return value.Void, &MultiReturnVal{Values: []value.Value{
					value.DefaultValue(""), value.NewErr("index out of bounds"),
				}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{arr.Elems[idx], value.Ok}}
		}), nil
	case "set":
		return value.NewNativeFunc("array.set", func(args []value.Value) (value.Value, error) {
			if len(args) != 2 || args[0].T != value.TypeI32 {
				return value.Void, fmt.Errorf("array.set: expected (i32, value)")
			}
			idx := int(args[0].AsI32())
			if idx < 0 || idx >= len(arr.Elems) {
				return value.NewErr("index out of bounds"), nil
			}
			if arr.ElemType != "" {
				if err := interp.checkTypeByName(arr.ElemType, args[1], 0); err != nil {
					return value.Void, fmt.Errorf("array.set: %s", err)
				}
			}
			arr.Elems[idx] = args[1]
			return value.Ok, nil
		}), nil
	case "pop":
		return value.NewNativeFunc("array.pop", func(args []value.Value) (value.Value, error) {
			if len(arr.Elems) == 0 {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr("empty array")}}
			}
			last := arr.Elems[len(arr.Elems)-1]
			arr.Elems = arr.Elems[:len(arr.Elems)-1]
			return value.Void, &MultiReturnVal{Values: []value.Value{last, value.Ok}}
		}), nil
	case "slice":
		return value.NewNativeFunc("array.slice", func(args []value.Value) (value.Value, error) {
			if len(args) != 2 || args[0].T != value.TypeI32 || args[1].T != value.TypeI32 {
				return value.Void, fmt.Errorf("array.slice: expected (i32 start, i32 end)")
			}
			start := int(args[0].AsI32())
			end := int(args[1].AsI32())
			if start < 0 {
				start = 0
			}
			if end > len(arr.Elems) {
				end = len(arr.Elems)
			}
			if start > end {
				start = end
			}
			newElems := make([]value.Value, end-start)
			copy(newElems, arr.Elems[start:end])
			return value.NewArray(newElems), nil
		}), nil
	case "contains":
		return value.NewNativeFunc("array.contains", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 {
				return value.Void, fmt.Errorf("array.contains: expected 1 argument")
			}
			for _, e := range arr.Elems {
				if valuesEqual(e, args[0]) {
					return value.True, nil
				}
			}
			return value.False, nil
		}), nil
	}
	return value.Void, fmt.Errorf("line %d: array has no method %q", line, method)
}

func (interp *Interpreter) mapMethod(obj value.Value, method string, line int) (value.Value, error) {
	m := obj.AsMap()
	switch method {
	case "len":
		return value.NewNativeFunc("map.len", func(args []value.Value) (value.Value, error) {
			return value.NewI32(int32(len(m.Keys))), nil
		}), nil
	case "get":
		return value.NewNativeFunc("map.get", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 {
				return value.Void, fmt.Errorf("map.get: expected 1 argument")
			}
			key := args[0]
			for i, k := range m.Keys {
				if k.String() == key.String() {
					return value.Void, &MultiReturnVal{Values: []value.Value{m.Values[i], value.True}}
				}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.False}}
		}), nil
	case "set":
		return value.NewNativeFunc("map.set", func(args []value.Value) (value.Value, error) {
			if len(args) != 2 {
				return value.Void, fmt.Errorf("map.set: expected (key, value)")
			}
			key := args[0]
			for i, k := range m.Keys {
				if k.String() == key.String() {
					m.Values[i] = args[1]
					return value.Ok, nil
				}
			}
			m.Keys = append(m.Keys, key)
			m.Values = append(m.Values, args[1])
			return value.Ok, nil
		}), nil
	case "remove":
		return value.NewNativeFunc("map.remove", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 {
				return value.Void, fmt.Errorf("map.remove: expected 1 argument")
			}
			key := args[0]
			for i, k := range m.Keys {
				if k.String() == key.String() {
					removed := m.Values[i]
					m.Keys = append(m.Keys[:i], m.Keys[i+1:]...)
					m.Values = append(m.Values[:i], m.Values[i+1:]...)
					return value.Void, &MultiReturnVal{Values: []value.Value{removed, value.True}}
				}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.False}}
		}), nil
	case "has":
		return value.NewNativeFunc("map.has", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 {
				return value.Void, fmt.Errorf("map.has: expected 1 argument")
			}
			for _, k := range m.Keys {
				if k.String() == args[0].String() {
					return value.True, nil
				}
			}
			return value.False, nil
		}), nil
	case "keys":
		return value.NewNativeFunc("map.keys", func(args []value.Value) (value.Value, error) {
			elems := make([]value.Value, len(m.Keys))
			copy(elems, m.Keys)
			return value.NewArray(elems), nil
		}), nil
	case "values":
		return value.NewNativeFunc("map.values", func(args []value.Value) (value.Value, error) {
			elems := make([]value.Value, len(m.Values))
			copy(elems, m.Values)
			return value.NewArray(elems), nil
		}), nil
	}
	return value.Void, fmt.Errorf("line %d: map has no method %q", line, method)
}

func (interp *Interpreter) evalTypeConv(e *ast.TypeConvExpr, env *Env) (value.Value, error) {
	arg, err := interp.evalExpr(e.Arg, env)
	if err != nil {
		return value.Void, err
	}
	switch e.Target.Name {
	case "string":
		return value.NewString(arg.String()), nil
	case "i32":
		if arg.T == value.TypeI32 {
			return arg, nil
		}
		if arg.T == value.TypeString {
			n, parseErr := strconv.ParseInt(arg.AsString(), 10, 32)
			if parseErr != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(0), value.NewErr("invalid i32: " + arg.AsString())}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI32(int32(n)), value.Ok}}
		}
		if arg.T == value.TypeI64 {
			return value.NewI32(int32(arg.AsI64())), nil
		}
		if arg.T == value.TypeF64 {
			return value.NewI32(int32(arg.AsF64())), nil
		}
		if arg.T == value.TypeU32 {
			return value.NewI32(int32(arg.AsU32())), nil
		}
		if arg.T == value.TypeU8 {
			return value.NewI32(int32(arg.AsU8())), nil
		}
		if arg.T == value.TypePtr {
			return value.NewI32(int32(arg.AsPtr())), nil
		}
	case "i64":
		if arg.T == value.TypeI64 {
			return arg, nil
		}
		if arg.T == value.TypeString {
			n, parseErr := strconv.ParseInt(arg.AsString(), 10, 64)
			if parseErr != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI64(0), value.NewErr("invalid i64: " + arg.AsString())}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewI64(n), value.Ok}}
		}
		if arg.T == value.TypeI32 {
			return value.NewI64(int64(arg.AsI32())), nil
		}
	case "f64":
		if arg.T == value.TypeF64 {
			return arg, nil
		}
		if arg.T == value.TypeString {
			n, parseErr := strconv.ParseFloat(arg.AsString(), 64)
			if parseErr != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewF64(0), value.NewErr("invalid f64: " + arg.AsString())}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewF64(n), value.Ok}}
		}
		if arg.T == value.TypeI32 {
			return value.NewF64(float64(arg.AsI32())), nil
		}
		if arg.T == value.TypeI64 {
			return value.NewF64(float64(arg.AsI64())), nil
		}
	case "u8":
		if arg.T == value.TypeU8 {
			return arg, nil
		}
		if arg.T == value.TypeI32 {
			return value.NewU8(uint8(arg.AsI32())), nil
		}
		if arg.T == value.TypeU32 {
			return value.NewU8(uint8(arg.AsU32())), nil
		}
		if arg.T == value.TypeU64 {
			return value.NewU8(uint8(arg.AsU64())), nil
		}
	case "u32":
		if arg.T == value.TypeU32 {
			return arg, nil
		}
		if arg.T == value.TypeI32 {
			return value.NewU32(uint32(arg.AsI32())), nil
		}
		if arg.T == value.TypeU8 {
			return value.NewU32(uint32(arg.AsU8())), nil
		}
		if arg.T == value.TypeU64 {
			return value.NewU32(uint32(arg.AsU64())), nil
		}
		if arg.T == value.TypeString {
			n, parseErr := strconv.ParseUint(arg.AsString(), 10, 32)
			if parseErr != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.NewU32(0), value.NewErr("invalid u32: " + arg.AsString())}}
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{value.NewU32(uint32(n)), value.Ok}}
		}
	case "u64":
		if arg.T == value.TypeU64 {
			return arg, nil
		}
		if arg.T == value.TypeI32 {
			return value.NewU64(uint64(arg.AsI32())), nil
		}
		if arg.T == value.TypeU8 {
			return value.NewU64(uint64(arg.AsU8())), nil
		}
		if arg.T == value.TypeU32 {
			return value.NewU64(uint64(arg.AsU32())), nil
		}
	}
	return value.Void, fmt.Errorf("line %d: cannot convert %v to %s", e.Line, arg.T, e.Target.Name)
}

func (interp *Interpreter) evalIndex(e *ast.IndexExpr, env *Env) (value.Value, error) {
	obj, err := interp.evalExpr(e.Object, env)
	if err != nil {
		return value.Void, err
	}
	idx, err := interp.evalExpr(e.Index, env)
	if err != nil {
		return value.Void, err
	}
	if obj.T == value.TypeArray {
		arr := obj.AsArray()
		if idx.T != value.TypeI32 {
			return value.Void, fmt.Errorf("line %d: array index must be i32", e.Line)
		}
		i := int(idx.AsI32())
		if i < 0 || i >= len(arr.Elems) {
			n := len(arr.Elems)
			noun := "elements"
			if n == 1 {
				noun = "element"
			}
			return value.Void, fmt.Errorf("line %d: array index %d out of bounds (array has %d %s)", e.Line, i, n, noun)
		}
		return arr.Elems[i], nil
	}
	if obj.T == value.TypeMap {
		m := obj.AsMap()
		key := idx.String()
		for i, k := range m.Keys {
			if k.String() == key {
				return m.Values[i], nil
			}
		}
		return value.Void, fmt.Errorf("line %d: key %q not found in map", e.Line, key)
	}
	return value.Void, fmt.Errorf("line %d: cannot index %s — only arrays and maps support indexing", e.Line, obj.T)
}

// constructObject creates a new class instance and calls init if present.
func (interp *Interpreter) constructObject(cls *value.ClassVal, args []value.Value) (value.Value, error) {
	// Use fully-qualified name if class is from a module
	className := cls.Name
	if cls.ModuleName != "" {
		className = cls.ModuleName + "." + cls.Name
	}

	obj := &value.ObjectVal{
		ClassName:  className,
		Implements: cls.Implements,
		Fields:     make(map[string]value.Value),
		Methods:    cls.Methods,
	}
	// Initialize fields to zero values
	for _, f := range cls.Fields {
		obj.Fields[f.Name] = value.DefaultValue(f.Type)
	}
	objVal := value.Value{T: value.TypeObject, Data: obj}

	if cls.Init == nil {
		return objVal, nil
	}

	// Call init with self bound
	initEnv := NewEnv(interp.globals)
	initEnv.Define("self", objVal)
	for i, p := range cls.Init.Params {
		if i < len(args) {
			initEnv.Define(p.Name, args[i])
		} else {
			initEnv.Define(p.Name, value.DefaultValue(p.Type))
		}
	}
	body := cls.Init.Body.(*ast.Block)
	if cls.Init.Return != nil {
		initEnv.returnType = cls.Init.Return.(*ast.ReturnType)
	}
	err := interp.execBlock(body, initEnv)
	if err != nil {
		if ret, ok := err.(*signalReturn); ok {
			// init returns err or void
			if len(ret.Values) == 1 && ret.Values[0].T == value.TypeErr {
				if ret.Values[0].IsOk() {
					return objVal, nil
				}
				// Fallible: return (obj, err) as multi-return
				return value.Void, &MultiReturnVal{Values: []value.Value{objVal, ret.Values[0]}}
			}
			return objVal, nil
		}
		return value.Void, err
	}
	return objVal, nil
}

// constructObjectMulti is for tuple binding context — returns (obj, err) for fallible init.
func (interp *Interpreter) constructObjectMulti(cls *value.ClassVal, args []value.Value) ([]value.Value, error) {
	obj := &value.ObjectVal{
		ClassName:  cls.Name,
		Implements: cls.Implements,
		Fields:     make(map[string]value.Value),
		Methods:    cls.Methods,
	}
	for _, f := range cls.Fields {
		obj.Fields[f.Name] = value.DefaultValue(f.Type)
	}
	objVal := value.Value{T: value.TypeObject, Data: obj}

	if cls.Init == nil {
		return []value.Value{objVal}, nil
	}

	initEnv := NewEnv(interp.globals)
	initEnv.Define("self", objVal)
	for i, p := range cls.Init.Params {
		if i < len(args) {
			initEnv.Define(p.Name, args[i])
		} else {
			initEnv.Define(p.Name, value.DefaultValue(p.Type))
		}
	}
	body := cls.Init.Body.(*ast.Block)
	if cls.Init.Return != nil {
		initEnv.returnType = cls.Init.Return.(*ast.ReturnType)
	}
	err := interp.execBlock(body, initEnv)
	if err != nil {
		if ret, ok := err.(*signalReturn); ok {
			if len(ret.Values) == 1 && ret.Values[0].T == value.TypeErr {
				return []value.Value{objVal, ret.Values[0]}, nil
			}
			return []value.Value{objVal}, nil
		}
		return nil, err
	}
	return []value.Value{objVal}, nil
}
