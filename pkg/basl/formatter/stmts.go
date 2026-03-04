package formatter

import (
	"fmt"

	"github.com/bluesentinelsec/basl/pkg/basl/ast"
)

func (f *formatter) block(b *ast.Block) {
	if b == nil {
		return
	}
	for _, s := range b.Stmts {
		f.emitCommentsBefore(stmtLine(s))
		f.stmt(s)
	}
}

func stmtLine(s ast.Stmt) int {
	switch s := s.(type) {
	case *ast.VarStmt:
		return s.Line
	case *ast.AssignStmt:
		return s.Line
	case *ast.TupleBindStmt:
		return s.Line
	case *ast.IfStmt:
		return s.Line
	case *ast.WhileStmt:
		return s.Line
	case *ast.ForStmt:
		return s.Line
	case *ast.ForInStmt:
		return s.Line
	case *ast.ReturnStmt:
		return s.Line
	case *ast.ExprStmt:
		// ExprStmt.Line is set to p.peek() after the semicolon, which can be
		// the next statement's line. Use the expression's own line instead.
		return exprLine(s.Expr)
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
	}
	return 0
}

func exprLine(e ast.Expr) int {
	switch e := e.(type) {
	case *ast.IntLit:
		return e.Line
	case *ast.FloatLit:
		return e.Line
	case *ast.StringLit:
		return e.Line
	case *ast.BoolLit:
		return e.Line
	case *ast.Ident:
		return e.Line
	case *ast.SelfExpr:
		return e.Line
	case *ast.UnaryExpr:
		return e.Line
	case *ast.BinaryExpr:
		return e.Line
	case *ast.CallExpr:
		return exprLine(e.Callee)
	case *ast.MemberExpr:
		return exprLine(e.Object)
	case *ast.IndexExpr:
		return exprLine(e.Object)
	case *ast.ArrayLit:
		return e.Line
	case *ast.MapLit:
		return e.Line
	case *ast.TupleExpr:
		return e.Line
	case *ast.TypeConvExpr:
		return e.Line
	case *ast.ErrExpr:
		return e.Line
	case *ast.FnLitExpr:
		return e.Line
	case *ast.FStringExpr:
		return e.Line
	}
	return 0
}

func (f *formatter) stmt(s ast.Stmt) {
	switch s := s.(type) {
	case *ast.VarStmt:
		f.varStmt(s)
	case *ast.AssignStmt:
		f.writelnIndented(fmt.Sprintf("%s = %s;", f.exprStr(s.Target), f.exprStr(s.Value)))
	case *ast.TupleBindStmt:
		f.tupleBindStmt(s)
	case *ast.IfStmt:
		f.ifStmt(s)
	case *ast.WhileStmt:
		f.whileStmt(s)
	case *ast.ForStmt:
		f.forStmt(s)
	case *ast.ForInStmt:
		f.forInStmt(s)
	case *ast.ReturnStmt:
		f.returnStmt(s)
	case *ast.ExprStmt:
		f.writelnIndented(f.exprStr(s.Expr) + ";")
	case *ast.BreakStmt:
		f.writelnIndented("break;")
	case *ast.ContinueStmt:
		f.writelnIndented("continue;")
	case *ast.DeferStmt:
		f.writelnIndented("defer " + f.exprStr(s.Call) + ";")
	case *ast.GuardStmt:
		f.guardStmt(s)
	case *ast.SwitchStmt:
		f.switchStmt(s)
	case *ast.CompoundAssignStmt:
		f.writelnIndented(fmt.Sprintf("%s %s %s;", f.exprStr(s.Target), s.Op, f.exprStr(s.Value)))
	case *ast.IncDecStmt:
		f.writelnIndented(fmt.Sprintf("%s%s;", f.exprStr(s.Target), s.Op))
	case *ast.Block:
		// Bare block (shouldn't normally appear at stmt level, but handle it)
		f.writelnIndented("{")
		f.indent++
		f.block(s)
		f.indent--
		f.writelnIndented("}")
	}
}

func (f *formatter) varStmt(s *ast.VarStmt) {
	// Check if this is a local function declaration (named, not anonymous)
	if fnLit, ok := s.Init.(*ast.FnLitExpr); ok && fnLit.Decl != nil && fnLit.Decl.Name != "" {
		// Format as: fn name(params) -> ret { body }
		f.fnDecl(fnLit.Decl, "")
		return
	}

	prefix := ""
	if s.Const {
		prefix = "const "
	}
	f.writelnIndented(fmt.Sprintf("%s%s %s = %s;", prefix, typeExprStr(s.Type), s.Name, f.exprStr(s.Init)))
}

func (f *formatter) tupleBindStmt(s *ast.TupleBindStmt) {
	var parts string
	for i, b := range s.Bindings {
		if i > 0 {
			parts += ", "
		}
		parts += typeExprStr(b.Type) + " " + b.Name
	}
	f.writelnIndented(fmt.Sprintf("%s = %s;", parts, f.exprStr(s.Value)))
}

func (f *formatter) guardStmt(s *ast.GuardStmt) {
	var parts string
	for i, b := range s.Bindings {
		if i > 0 {
			parts += ", "
		}
		parts += typeExprStr(b.Type) + " " + b.Name
	}
	f.writelnIndented(fmt.Sprintf("guard %s = %s {", parts, f.exprStr(s.Value)))
	f.indent++
	f.block(s.Body)
	f.indent--
	f.writelnIndented("}")
}

func (f *formatter) ifStmt(s *ast.IfStmt) {
	// Always multi-line — no single-line ifs
	f.writelnIndented(fmt.Sprintf("if (%s) {", f.exprStr(s.Cond)))
	f.indent++
	f.block(s.Then)
	f.indent--
	if s.Else != nil {
		switch e := s.Else.(type) {
		case *ast.IfStmt:
			f.writeIndented("} else ")
			// Write the rest of the else-if without indent (it starts with "if")
			f.write(fmt.Sprintf("if (%s) {\n", f.exprStr(e.Cond)))
			f.indent++
			f.block(e.Then)
			f.indent--
			if e.Else != nil {
				f.elseChain(e.Else)
			} else {
				f.writelnIndented("}")
			}
		case *ast.Block:
			f.writelnIndented("} else {")
			f.indent++
			f.block(e)
			f.indent--
			f.writelnIndented("}")
		}
	} else {
		f.writelnIndented("}")
	}
}

func (f *formatter) elseChain(s ast.Stmt) {
	switch e := s.(type) {
	case *ast.IfStmt:
		f.writeIndented("} else ")
		f.write(fmt.Sprintf("if (%s) {\n", f.exprStr(e.Cond)))
		f.indent++
		f.block(e.Then)
		f.indent--
		if e.Else != nil {
			f.elseChain(e.Else)
		} else {
			f.writelnIndented("}")
		}
	case *ast.Block:
		f.writelnIndented("} else {")
		f.indent++
		f.block(e)
		f.indent--
		f.writelnIndented("}")
	}
}

func (f *formatter) whileStmt(s *ast.WhileStmt) {
	f.writelnIndented(fmt.Sprintf("while (%s) {", f.exprStr(s.Cond)))
	f.indent++
	f.block(s.Body)
	f.indent--
	f.writelnIndented("}")
}

func (f *formatter) forStmt(s *ast.ForStmt) {
	init := f.forInitStr(s.Init)
	cond := f.exprStr(s.Cond)
	post := f.forPostStr(s.Post)
	f.writelnIndented(fmt.Sprintf("for (%s %s; %s) {", init, cond, post))
	f.indent++
	f.block(s.Body)
	f.indent--
	f.writelnIndented("}")
}

func (f *formatter) forInitStr(s ast.Stmt) string {
	switch s := s.(type) {
	case *ast.VarStmt:
		return fmt.Sprintf("%s %s = %s;", typeExprStr(s.Type), s.Name, f.exprStr(s.Init))
	case *ast.AssignStmt:
		return fmt.Sprintf("%s = %s;", f.exprStr(s.Target), f.exprStr(s.Value))
	case *ast.ExprStmt:
		return f.exprStr(s.Expr) + ";"
	}
	return ";"
}

func (f *formatter) forPostStr(s ast.Stmt) string {
	switch s := s.(type) {
	case *ast.AssignStmt:
		return fmt.Sprintf("%s = %s", f.exprStr(s.Target), f.exprStr(s.Value))
	case *ast.CompoundAssignStmt:
		return fmt.Sprintf("%s %s %s", f.exprStr(s.Target), s.Op, f.exprStr(s.Value))
	case *ast.IncDecStmt:
		return fmt.Sprintf("%s%s", f.exprStr(s.Target), s.Op)
	case *ast.ExprStmt:
		return f.exprStr(s.Expr)
	}
	return ""
}

func (f *formatter) forInStmt(s *ast.ForInStmt) {
	var binding string
	if s.KeyName != "" {
		binding = s.KeyName + ", " + s.ValName
	} else {
		binding = s.ValName
	}
	f.writelnIndented(fmt.Sprintf("for %s in %s {", binding, f.exprStr(s.Iter)))
	f.indent++
	f.block(s.Body)
	f.indent--
	f.writelnIndented("}")
}

func (f *formatter) returnStmt(s *ast.ReturnStmt) {
	if len(s.Values) == 0 {
		f.writelnIndented("return;")
		return
	}
	if len(s.Values) == 1 {
		if tup, ok := s.Values[0].(*ast.TupleExpr); ok {
			f.writelnIndented(fmt.Sprintf("return (%s);", f.tupleStr(tup)))
			return
		}
		f.writelnIndented(fmt.Sprintf("return %s;", f.exprStr(s.Values[0])))
		return
	}
	// Multiple return values
	parts := make([]string, len(s.Values))
	for i, v := range s.Values {
		parts[i] = f.exprStr(v)
	}
	f.writelnIndented("return (" + joinComma(parts) + ");")
}

func (f *formatter) switchStmt(s *ast.SwitchStmt) {
	f.writelnIndented(fmt.Sprintf("switch (%s) {", f.exprStr(s.Tag)))
	f.indent++
	for _, c := range s.Cases {
		if c.Values == nil {
			f.writelnIndented("default:")
		} else {
			vals := make([]string, len(c.Values))
			for i, v := range c.Values {
				vals[i] = f.exprStr(v)
			}
			f.writelnIndented("case " + joinComma(vals) + ":")
		}
		f.indent++
		for _, st := range c.Body {
			f.emitCommentsBefore(stmtLine(st))
			f.stmt(st)
		}
		f.indent--
	}
	f.indent--
	f.writelnIndented("}")
}

func joinComma(parts []string) string {
	result := ""
	for i, p := range parts {
		if i > 0 {
			result += ", "
		}
		result += p
	}
	return result
}
