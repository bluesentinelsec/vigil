package formatter

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/bluesentinelsec/basl/pkg/basl/ast"
)

// stringLitStr formats a string literal, always using double quotes
func stringLitStr(s string) string {
	return strconv.Quote(s)
}

func (f *formatter) exprStr(e ast.Expr) string {
	if e == nil {
		return ""
	}
	switch e := e.(type) {
	case *ast.IntLit:
		return strconv.FormatInt(e.Value, 10)
	case *ast.FloatLit:
		s := strconv.FormatFloat(e.Value, 'f', -1, 64)
		// Ensure there's a decimal point
		if !strings.Contains(s, ".") {
			s += ".0"
		}
		return s
	case *ast.StringLit:
		// Use single quotes for single-character strings (character literals)
		// Count runes (Unicode characters), not bytes
		runes := []rune(e.Value)
		if len(runes) == 1 {
			ch := runes[0]
			switch ch {
			case '\n':
				return `'\n'`
			case '\t':
				return `'\t'`
			case '\r':
				return `'\r'`
			case '\\':
				return `'\\'`
			case '\'':
				return `'\''`
			default:
				return "'" + e.Value + "'"
			}
		}
		return strconv.Quote(e.Value)
	case *ast.BoolLit:
		if e.Value {
			return "true"
		}
		return "false"
	case *ast.Ident:
		return e.Name
	case *ast.SelfExpr:
		return "self"
	case *ast.UnaryExpr:
		operand := f.exprStr(e.Operand)
		// Parenthesize ternary in unary operand
		if _, ok := e.Operand.(*ast.TernaryExpr); ok {
			operand = "(" + operand + ")"
		}
		return e.Op + operand
	case *ast.BinaryExpr:
		return f.binaryStr(e)
	case *ast.TernaryExpr:
		cond := f.exprStr(e.Condition)
		// Parenthesize ternary used as condition
		if _, ok := e.Condition.(*ast.TernaryExpr); ok {
			cond = "(" + cond + ")"
		}
		return cond + " ? " + f.exprStr(e.TrueExpr) + " : " + f.exprStr(e.FalseExpr)
	case *ast.CallExpr:
		args := make([]string, len(e.Args))
		for i, a := range e.Args {
			args[i] = f.exprStr(a)
		}
		callee := f.exprStr(e.Callee)
		// Parenthesize ternary used as callee
		if _, ok := e.Callee.(*ast.TernaryExpr); ok {
			callee = "(" + callee + ")"
		}
		return callee + "(" + joinComma(args) + ")"
	case *ast.MemberExpr:
		obj := f.exprStr(e.Object)
		// Parenthesize ternary used as object
		if _, ok := e.Object.(*ast.TernaryExpr); ok {
			obj = "(" + obj + ")"
		}
		return obj + "." + e.Field
	case *ast.IndexExpr:
		obj := f.exprStr(e.Object)
		// Parenthesize ternary used as object
		if _, ok := e.Object.(*ast.TernaryExpr); ok {
			obj = "(" + obj + ")"
		}
		return obj + "[" + f.exprStr(e.Index) + "]"
	case *ast.ArrayLit:
		elems := make([]string, len(e.Elems))
		for i, el := range e.Elems {
			elems[i] = f.exprStr(el)
		}
		return "[" + joinComma(elems) + "]"
	case *ast.MapLit:
		if len(e.Keys) == 0 {
			return "{}"
		}
		pairs := make([]string, len(e.Keys))
		for i := range e.Keys {
			// Map keys should always be formatted as strings (double quotes)
			key := f.exprStr(e.Keys[i])
			if strLit, ok := e.Keys[i].(*ast.StringLit); ok {
				key = stringLitStr(strLit.Value)
			}
			pairs[i] = key + ": " + f.exprStr(e.Values[i])
		}
		return "{" + joinComma(pairs) + "}"
	case *ast.TupleExpr:
		return "(" + f.tupleStr(e) + ")"
	case *ast.TypeConvExpr:
		return typeExprStr(e.Target) + "(" + f.exprStr(e.Arg) + ")"
	case *ast.ErrExpr:
		return "err(" + f.exprStr(e.Msg) + ", " + f.exprStr(e.Kind) + ")"
	case *ast.FnLitExpr:
		return f.fnLitStr(e)
	case *ast.FStringExpr:
		return f.fstringStr(e)
	}
	return "/* unknown expr */"
}

func (f *formatter) tupleStr(t *ast.TupleExpr) string {
	parts := make([]string, len(t.Elems))
	for i, e := range t.Elems {
		parts[i] = f.exprStr(e)
	}
	return joinComma(parts)
}

func (f *formatter) binaryStr(e *ast.BinaryExpr) string {
	left := f.exprStr(e.Left)
	right := f.exprStr(e.Right)
	// Parenthesize nested binary exprs with lower precedence
	if inner, ok := e.Left.(*ast.BinaryExpr); ok && precedence(inner.Op) < precedence(e.Op) {
		left = "(" + left + ")"
	}
	// Parenthesize ternary in left operand (ternary has lowest precedence)
	if _, ok := e.Left.(*ast.TernaryExpr); ok {
		left = "(" + left + ")"
	}
	if inner, ok := e.Right.(*ast.BinaryExpr); ok && precedence(inner.Op) < precedence(e.Op) {
		right = "(" + right + ")"
	}
	// Parenthesize ternary in right operand (ternary has lowest precedence)
	if _, ok := e.Right.(*ast.TernaryExpr); ok {
		right = "(" + right + ")"
	}
	return left + " " + e.Op + " " + right
}

func precedence(op string) int {
	switch op {
	case "||":
		return 1
	case "&&":
		return 2
	case "==", "!=":
		return 3
	case "|":
		return 4
	case "^":
		return 5
	case "&":
		return 6
	case "<<", ">>":
		return 7
	case "<", ">", "<=", ">=":
		return 8
	case "+", "-":
		return 9
	case "*", "/", "%":
		return 10
	}
	return 0
}

func (f *formatter) fnLitStr(e *ast.FnLitExpr) string {
	d := e.Decl
	var sb strings.Builder
	sb.WriteString("fn(")
	for i, p := range d.Params {
		if i > 0 {
			sb.WriteString(", ")
		}
		sb.WriteString(typeExprStr(p.Type))
		sb.WriteString(" ")
		sb.WriteString(p.Name)
	}
	sb.WriteString(")")
	if d.Return != nil {
		sb.WriteString(" -> ")
		sb.WriteString(returnTypeStr(d.Return))
	}
	// Try inline for single simple statements
	if d.Body != nil && len(d.Body.Stmts) == 1 {
		if inline := f.inlineStmt(d.Body.Stmts[0]); inline != "" {
			sb.WriteString(" { ")
			sb.WriteString(inline)
			sb.WriteString(" }")
			return sb.String()
		}
	}
	// Fall back to multi-line block format
	sb.WriteString(" {\n")
	saved := f.indent
	f.indent++
	if d.Body != nil {
		for _, s := range d.Body.Stmts {
			sb.WriteString(f.stmtToString(s))
			sb.WriteByte('\n')
		}
	}
	f.indent = saved
	sb.WriteString(strings.Repeat("    ", saved))
	sb.WriteString("}")
	return sb.String()
}

// inlineStmt returns a statement as a single-line string, or "" if it can't be inlined.
func (f *formatter) inlineStmt(s ast.Stmt) string {
	switch s := s.(type) {
	case *ast.ReturnStmt:
		if len(s.Values) == 0 {
			return "return;"
		}
		if len(s.Values) == 1 {
			return "return " + f.exprStr(s.Values[0]) + ";"
		}
	case *ast.ExprStmt:
		return f.exprStr(s.Expr) + ";"
	case *ast.VarStmt:
		return fmt.Sprintf("%s %s = %s;", typeExprStr(s.Type), s.Name, f.exprStr(s.Init))
	}
	return ""
}

func (f *formatter) fstringStr(e *ast.FStringExpr) string {
	var sb strings.Builder
	sb.WriteString("f\"")
	for _, p := range e.Parts {
		if p.IsExpr {
			sb.WriteString("{")
			sb.WriteString(f.exprStr(p.Expr))
			if p.Format != "" {
				sb.WriteString(":")
				sb.WriteString(p.Format)
			}
			sb.WriteString("}")
		} else {
			// Escape special chars in text parts
			sb.WriteString(escapeFStringText(p.Text))
		}
	}
	sb.WriteString("\"")
	return sb.String()
}

func escapeFStringText(s string) string {
	var sb strings.Builder
	for _, ch := range s {
		switch ch {
		case '\n':
			sb.WriteString("\\n")
		case '\t':
			sb.WriteString("\\t")
		case '\\':
			sb.WriteString("\\\\")
		case '"':
			sb.WriteString("\\\"")
		case '{':
			sb.WriteString("{{") // Escape literal { as {{
		case '}':
			sb.WriteString("}}") // Escape literal } as }}
		default:
			sb.WriteRune(ch)
		}
	}
	return sb.String()
}
