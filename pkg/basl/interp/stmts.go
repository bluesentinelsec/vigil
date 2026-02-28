package interp

import (
	"fmt"

	"github.com/bluesentinelsec/basl/pkg/basl/ast"
	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

func (interp *Interpreter) execBlock(block *ast.Block, env *Env) error {
	for _, s := range block.Stmts {
		if interp.debugger != nil {
			if err := interp.debugger.Hook(s, env); err != nil {
				return err
			}
		}
		if err := interp.execStmt(s, env); err != nil {
			return err
		}
	}
	return nil
}

func (interp *Interpreter) execStmt(s ast.Stmt, env *Env) error {
	switch s := s.(type) {
	case *ast.VarStmt:
		val, err := interp.evalExpr(s.Init, env)
		if err != nil {
			return err
		}
		if err := interp.checkType(s.Type, val, s.Line); err != nil {
			return err
		}
		env.DefineTyped(s.Name, val, s.Type)
		if s.Const {
			env.MarkConst(s.Name)
		}
	case *ast.AssignStmt:
		val, err := interp.evalExpr(s.Value, env)
		if err != nil {
			return err
		}
		return interp.assign(s.Target, val, env)
	case *ast.ExprStmt:
		_, err := interp.evalExpr(s.Expr, env)
		return err
	case *ast.ReturnStmt:
		if len(s.Values) == 0 {
			return &signalReturn{Values: []value.Value{value.Void}}
		}
		var vals []value.Value
		for _, e := range s.Values {
			v, err := interp.evalExpr(e, env)
			if err != nil {
				return err
			}
			if te, ok := e.(*ast.TupleExpr); ok {
				_ = v
				for _, elem := range te.Elems {
					ev, err := interp.evalExpr(elem, env)
					if err != nil {
						return err
					}
					vals = append(vals, ev)
				}
				if err := interp.checkReturnTypes(vals, env, s.Line); err != nil {
					return err
				}
				return &signalReturn{Values: vals}
			}
			vals = append(vals, v)
		}
		if err := interp.checkReturnTypes(vals, env, s.Line); err != nil {
			return err
		}
		return &signalReturn{Values: vals}
	case *ast.IfStmt:
		cond, err := interp.evalExpr(s.Cond, env)
		if err != nil {
			return err
		}
		if cond.T != value.TypeBool {
			return fmt.Errorf("line %d: if condition must be bool, got %s — try a comparison like x > 0 or x == true", s.Line, cond.T)
		}
		if cond.AsBool() {
			return interp.execBlock(s.Then, NewEnv(env))
		} else if s.Else != nil {
			switch e := s.Else.(type) {
			case *ast.Block:
				return interp.execBlock(e, NewEnv(env))
			case *ast.IfStmt:
				return interp.execStmt(e, env)
			}
		}
	case *ast.WhileStmt:
		for {
			cond, err := interp.evalExpr(s.Cond, env)
			if err != nil {
				return err
			}
			if cond.T != value.TypeBool {
				return fmt.Errorf("line %d: while condition must be bool, got %s — try a comparison like x > 0", s.Line, cond.T)
			}
			if !cond.AsBool() {
				break
			}
			err = interp.execBlock(s.Body, NewEnv(env))
			if err != nil {
				if _, ok := err.(*signalBreak); ok {
					break
				}
				if _, ok := err.(*signalContinue); ok {
					continue
				}
				return err
			}
		}
	case *ast.ForStmt:
		loopEnv := NewEnv(env)
		if s.Init != nil {
			if err := interp.execStmt(s.Init, loopEnv); err != nil {
				return err
			}
		}
		for {
			cond, err := interp.evalExpr(s.Cond, loopEnv)
			if err != nil {
				return err
			}
			if cond.T != value.TypeBool {
				return fmt.Errorf("line %d: for condition must be bool, got %s — try a comparison like i < 10", s.Line, cond.T)
			}
			if !cond.AsBool() {
				break
			}
			err = interp.execBlock(s.Body, NewEnv(loopEnv))
			if err != nil {
				if _, ok := err.(*signalBreak); ok {
					break
				}
				if _, ok := err.(*signalContinue); ok {
					// fall through to post
				} else {
					return err
				}
			}
			if s.Post != nil {
				if err := interp.execStmt(s.Post, loopEnv); err != nil {
					return err
				}
			}
		}
	case *ast.BreakStmt:
		return &signalBreak{}
	case *ast.ContinueStmt:
		return &signalContinue{}
	case *ast.ForInStmt:
		iter, err := interp.evalExpr(s.Iter, env)
		if err != nil {
			return err
		}
		switch iter.T {
		case value.TypeArray:
			arr := iter.AsArray()
			for _, elem := range arr.Elems {
				loopEnv := NewEnv(env)
				loopEnv.Define(s.ValName, elem)
				err := interp.execBlock(s.Body, loopEnv)
				if err != nil {
					if _, ok := err.(*signalBreak); ok {
						break
					}
					if _, ok := err.(*signalContinue); ok {
						continue
					}
					return err
				}
			}
		case value.TypeMap:
			m := iter.AsMap()
			for i, k := range m.Keys {
				loopEnv := NewEnv(env)
				if s.KeyName != "" {
					loopEnv.Define(s.KeyName, k)
				}
				loopEnv.Define(s.ValName, m.Values[i])
				err := interp.execBlock(s.Body, loopEnv)
				if err != nil {
					if _, ok := err.(*signalBreak); ok {
						break
					}
					if _, ok := err.(*signalContinue); ok {
						continue
					}
					return err
				}
			}
		default:
			return fmt.Errorf("line %d: for-in requires array or map, got %s", s.Line, iter.T)
		}
	case *ast.TupleBindStmt:
		return interp.execTupleBind(s, env)
	case *ast.DeferStmt:
		call, ok := s.Call.(*ast.CallExpr)
		if !ok {
			return fmt.Errorf("line %d: defer requires a function call — try: defer f()", s.Line)
		}
		callee, err := interp.evalExpr(call.Callee, env)
		if err != nil {
			return err
		}
		var args []value.Value
		for _, a := range call.Args {
			v, err := interp.evalExpr(a, env)
			if err != nil {
				return err
			}
			args = append(args, v)
		}
		if env.defers != nil {
			*env.defers = append(*env.defers, deferredCall{callee: callee, args: args})
		}
	case *ast.Block:
		return interp.execBlock(s, NewEnv(env))
	case *ast.SwitchStmt:
		return interp.execSwitch(s, env)
	case *ast.CompoundAssignStmt:
		return interp.execCompoundAssign(s, env)
	case *ast.IncDecStmt:
		return interp.execIncDec(s, env)
	}
	return nil
}

func (interp *Interpreter) assign(target ast.Expr, val value.Value, env *Env) error {
	switch t := target.(type) {
	case *ast.Ident:
		if env.IsConst(t.Name) {
			return fmt.Errorf("line %d: cannot assign to const %q — constants cannot be changed after declaration", t.Line, t.Name)
		}
		if dt := env.GetType(t.Name); dt != nil {
			if err := interp.checkType(dt, val, t.Line); err != nil {
				return err
			}
		}
		if !env.Set(t.Name, val) {
			return fmt.Errorf("line %d: undefined variable %q — declare it first with: type %s = value;", t.Line, t.Name, t.Name)
		}
	case *ast.MemberExpr:
		obj, err := interp.evalExpr(t.Object, env)
		if err != nil {
			return err
		}
		if obj.T == value.TypeObject {
			o := obj.AsObject()
			// Check field type from class definition
			if fieldType := interp.getFieldType(o.ClassName, t.Field); fieldType != "" {
				if err := interp.checkTypeByName(fieldType, val, t.Line); err != nil {
					return err
				}
			}
			o.Fields[t.Field] = val
		} else {
			return fmt.Errorf("line %d: cannot assign to field of non-object — left side is %s", t.Line, obj.T)
		}
	case *ast.IndexExpr:
		obj, err := interp.evalExpr(t.Object, env)
		if err != nil {
			return err
		}
		idx, err := interp.evalExpr(t.Index, env)
		if err != nil {
			return err
		}
		switch obj.T {
		case value.TypeArray:
			arr := obj.AsArray()
			i := int(idx.AsI32())
			if i < 0 || i >= len(arr.Elems) {
				n := len(arr.Elems)
				noun := "elements"
				if n == 1 {
					noun = "element"
				}
				return fmt.Errorf("line %d: array index %d out of bounds (array has %d %s)", t.Line, i, n, noun)
			}
			if arr.ElemType != "" {
				if err := interp.checkTypeByName(arr.ElemType, val, t.Line); err != nil {
					return err
				}
			}
			arr.Elems[i] = val
		case value.TypeMap:
			m := obj.AsMap()
			if m.KeyType != "" {
				if err := interp.checkTypeByName(m.KeyType, idx, t.Line); err != nil {
					return fmt.Errorf("line %d: map key: %s", t.Line, stripLinePrefix(err))
				}
			}
			if m.ValType != "" {
				if err := interp.checkTypeByName(m.ValType, val, t.Line); err != nil {
					return fmt.Errorf("line %d: map value: %s", t.Line, stripLinePrefix(err))
				}
			}
			for j, k := range m.Keys {
				if k.AsString() == idx.AsString() {
					m.Values[j] = val
					return nil
				}
			}
			m.Keys = append(m.Keys, idx)
			m.Values = append(m.Values, val)
		default:
			return fmt.Errorf("line %d: cannot index-assign to %s — only arrays and maps support index assignment", t.Line, obj.T)
		}
	default:
		return fmt.Errorf("invalid assignment target")
	}
	return nil
}

func (interp *Interpreter) execTupleBind(s *ast.TupleBindStmt, env *Env) error {
	vals, err := interp.evalCallMulti(s.Value, env)
	if err != nil {
		return err
	}
	if len(vals) != len(s.Bindings) {
		return fmt.Errorf("line %d: tuple binding expects %d values, got %d — the function returned a different number of values", s.Line, len(s.Bindings), len(vals))
	}
	for i, b := range s.Bindings {
		if !b.Discard {
			if vals[i].T != value.TypeVoid {
				if err := interp.checkType(b.Type, vals[i], s.Line); err != nil {
					return err
				}
			}
			env.DefineTyped(b.Name, vals[i], b.Type)
		}
	}
	return nil
}

// evalCallMulti evaluates a call expression and returns multiple values.
func (interp *Interpreter) evalCallMulti(expr ast.Expr, env *Env) ([]value.Value, error) {
	// Handle type conversion expressions (they return multi-values)
	if tc, ok := expr.(*ast.TypeConvExpr); ok {
		_, err := interp.evalTypeConv(tc, env)
		if err != nil {
			if mr, ok := err.(*MultiReturnVal); ok {
				return mr.Values, nil
			}
			return nil, err
		}
		// Single value conversion (non-string to non-string)
		v, err := interp.evalExpr(expr, env)
		if err != nil {
			if mr, ok := err.(*MultiReturnVal); ok {
				return mr.Values, nil
			}
			return nil, err
		}
		return []value.Value{v}, nil
	}

	call, ok := expr.(*ast.CallExpr)
	if !ok {
		v, err := interp.evalExpr(expr, env)
		if err != nil {
			return nil, err
		}
		return []value.Value{v}, nil
	}

	callee, err := interp.evalExpr(call.Callee, env)
	if err != nil {
		return nil, err
	}

	var args []value.Value
	for _, a := range call.Args {
		v, err := interp.evalExpr(a, env)
		if err != nil {
			return nil, err
		}
		args = append(args, v)
	}

	switch callee.T {
	case value.TypeFunc:
		return interp.callFuncMulti(callee, args)
	case value.TypeNativeFunc:
		nf := callee.AsNativeFunc()
		v, err := nf.Fn(args)
		if err != nil {
			if mr, ok := err.(*MultiReturnVal); ok {
				return mr.Values, nil
			}
			return nil, err
		}
		return []value.Value{v}, nil
	case value.TypeClass:
		return interp.constructObjectMulti(callee.AsClass(), args)
	default:
		return nil, fmt.Errorf("%s is not callable — expected a function or class constructor", callee)
	}
}

func (interp *Interpreter) execSwitch(s *ast.SwitchStmt, env *Env) error {
	tag, err := interp.evalExpr(s.Tag, env)
	if err != nil {
		return err
	}
	for _, c := range s.Cases {
		if c.Values == nil {
			// default case
			for _, stmt := range c.Body {
				if err := interp.execStmt(stmt, env); err != nil {
					return err
				}
			}
			return nil
		}
		for _, v := range c.Values {
			cv, err := interp.evalExpr(v, env)
			if err != nil {
				return err
			}
			if valuesEqual(tag, cv) {
				for _, stmt := range c.Body {
					if err := interp.execStmt(stmt, env); err != nil {
						return err
					}
				}
				return nil
			}
		}
	}
	return nil
}

func valuesEqual(a, b value.Value) bool {
	if a.T != b.T {
		return false
	}
	switch a.T {
	case value.TypeI32:
		return a.AsI32() == b.AsI32()
	case value.TypeI64:
		return a.AsI64() == b.AsI64()
	case value.TypeF64:
		return a.AsF64() == b.AsF64()
	case value.TypeString:
		return a.AsString() == b.AsString()
	case value.TypeBool:
		return a.AsBool() == b.AsBool()
	}
	return false
}

func (interp *Interpreter) execCompoundAssign(s *ast.CompoundAssignStmt, env *Env) error {
	left, err := interp.evalExpr(s.Target, env)
	if err != nil {
		return err
	}
	right, err := interp.evalExpr(s.Value, env)
	if err != nil {
		return err
	}
	var op string
	switch s.Op {
	case "+=":
		op = "+"
	case "-=":
		op = "-"
	case "*=":
		op = "*"
	case "/=":
		op = "/"
	case "%=":
		op = "%"
	}
	var val value.Value
	switch {
	case left.T == value.TypeI32 && right.T == value.TypeI32:
		val, err = evalI32BinOp(op, left.AsI32(), right.AsI32(), s.Line)
	case left.T == value.TypeI64 && right.T == value.TypeI64:
		val, err = evalI64BinOp(op, left.AsI64(), right.AsI64(), s.Line)
	case left.T == value.TypeF64 && right.T == value.TypeF64:
		val, err = evalF64BinOp(op, left.AsF64(), right.AsF64(), s.Line)
	case left.T == value.TypeString && right.T == value.TypeString && op == "+":
		val = value.NewString(left.AsString() + right.AsString())
	default:
		return fmt.Errorf("line %d: cannot apply %s to %s — compound assignment requires matching numeric types", s.Line, s.Op, left.T)
	}
	if err != nil {
		return err
	}
	return interp.assign(s.Target, val, env)
}

func (interp *Interpreter) execIncDec(s *ast.IncDecStmt, env *Env) error {
	cur, err := interp.evalExpr(s.Target, env)
	if err != nil {
		return err
	}
	var val value.Value
	switch cur.T {
	case value.TypeI32:
		if s.Op == "++" {
			val = value.NewI32(cur.AsI32() + 1)
		} else {
			val = value.NewI32(cur.AsI32() - 1)
		}
	case value.TypeI64:
		if s.Op == "++" {
			val = value.NewI64(cur.AsI64() + 1)
		} else {
			val = value.NewI64(cur.AsI64() - 1)
		}
	case value.TypeF64:
		if s.Op == "++" {
			val = value.NewF64(cur.AsF64() + 1)
		} else {
			val = value.NewF64(cur.AsF64() - 1)
		}
	default:
		return fmt.Errorf("line %d: %s requires numeric type, got %s", s.Line, s.Op, cur.T)
	}
	return interp.assign(s.Target, val, env)
}
