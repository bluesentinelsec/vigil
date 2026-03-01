package formatter

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/bluesentinelsec/basl/pkg/basl/ast"
)

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
		return e.Op + f.exprStr(e.Operand)
	case *ast.BinaryExpr:
		return f.binaryStr(e)
	case *ast.CallExpr:
		args := make([]string, len(e.Args))
		for i, a := range e.Args {
			args[i] = f.exprStr(a)
		}
		return f.exprStr(e.Callee) + "(" + joinComma(args) + ")"
	case *ast.MemberExpr:
		return f.exprStr(e.Object) + "." + e.Field
	case *ast.IndexExpr:
		return f.exprStr(e.Object) + "[" + f.exprStr(e.Index) + "]"
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
			pairs[i] = f.exprStr(e.Keys[i]) + ": " + f.exprStr(e.Values[i])
		}
		return "{" + joinComma(pairs) + "}"
	case *ast.TupleExpr:
		return "(" + f.tupleStr(e) + ")"
	case *ast.TypeConvExpr:
		return typeExprStr(e.Target) + "(" + f.exprStr(e.Arg) + ")"
	case *ast.ErrExpr:
		return "err(" + f.exprStr(e.Msg) + ")"
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
	if inner, ok := e.Right.(*ast.BinaryExpr); ok && precedence(inner.Op) < precedence(e.Op) {
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
	sb.WriteString(" { ")
	// Inline the body for short lambdas
	if d.Body != nil && len(d.Body.Stmts) == 1 {
		sb.WriteString(f.inlineStmt(d.Body.Stmts[0]))
		sb.WriteString(" }")
		return sb.String()
	}
	// Multi-statement: use block format
	// This is a rare case for fn literals; just inline all stmts
	if d.Body != nil {
		for i, s := range d.Body.Stmts {
			if i > 0 {
				sb.WriteString(" ")
			}
			sb.WriteString(f.inlineStmt(s))
		}
	}
	sb.WriteString(" }")
	return sb.String()
}

// inlineStmt returns a statement as a single-line string (for fn literals in expressions).
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
	return "/* ... */"
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
