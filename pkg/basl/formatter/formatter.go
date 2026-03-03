package formatter

import (
	"bytes"
	"sort"
	"strings"

	"github.com/bluesentinelsec/basl/pkg/basl/ast"
	"github.com/bluesentinelsec/basl/pkg/basl/lexer"
)

// Comment holds a comment with its attachment point.
type Comment struct {
	// NextTokenLine is the line of the first non-comment token after this comment.
	// This makes comment placement stable across reformatting.
	NextTokenLine int
	Text          string
	Block         bool
}

// Format formats a BASL program. Takes the AST and the token stream (with comments).
func Format(prog *ast.Program, tokens []lexer.Token) []byte {
	f := &formatter{
		comments: extractComments(tokens),
	}
	f.program(prog)
	return f.bytes()
}

type formatter struct {
	buf      bytes.Buffer
	indent   int
	comments []Comment
	comIdx   int
}

// extractComments builds comments attached to the next non-comment token's line.
// This ensures stable placement: after reformatting, the AST node line numbers
// and NextTokenLine both shift together, keeping comments attached to the same code.
func extractComments(tokens []lexer.Token) []Comment {
	type pending struct {
		text  string
		block bool
	}
	var pend []pending
	var out []Comment
	for _, t := range tokens {
		switch t.Type {
		case lexer.TOKEN_LINE_COMMENT:
			pend = append(pend, pending{t.Literal, false})
		case lexer.TOKEN_BLOCK_COMMENT:
			pend = append(pend, pending{t.Literal, true})
		default:
			for _, p := range pend {
				out = append(out, Comment{NextTokenLine: t.Line, Text: p.text, Block: p.block})
			}
			pend = pend[:0]
		}
	}
	// Trailing comments after all code
	for _, p := range pend {
		out = append(out, Comment{NextTokenLine: 1<<31 - 1, Text: p.text, Block: p.block})
	}
	return out
}

func (f *formatter) bytes() []byte {
	b := f.buf.Bytes()
	b = bytes.TrimRight(b, "\n")
	return append(b, '\n')
}

func (f *formatter) write(s string)           { f.buf.WriteString(s) }
func (f *formatter) writeln(s string)         { f.buf.WriteString(s); f.buf.WriteByte('\n') }
func (f *formatter) nl()                      { f.buf.WriteByte('\n') }
func (f *formatter) indentStr() string        { return strings.Repeat("    ", f.indent) }
func (f *formatter) writeIndent()             { f.write(f.indentStr()) }
func (f *formatter) writeIndented(s string)   { f.writeIndent(); f.write(s) }
func (f *formatter) writelnIndented(s string) { f.writeIndent(); f.writeln(s) }

// stmtToString formats a statement by capturing output to a temporary buffer.
func (f *formatter) stmtToString(s ast.Stmt) string {
	saved := f.buf
	f.buf = bytes.Buffer{}
	f.stmt(s)
	result := strings.TrimRight(f.buf.String(), "\n")
	f.buf = saved
	return result
}

// emitCommentsBefore writes all comments whose NextTokenLine <= the given line.
func (f *formatter) emitCommentsBefore(line int) {
	for f.comIdx < len(f.comments) && f.comments[f.comIdx].NextTokenLine <= line {
		c := f.comments[f.comIdx]
		f.comIdx++
		f.writeIndent()
		f.writeln(c.Text)
	}
}

// emitRemainingComments writes any comments not yet emitted.
func (f *formatter) emitRemainingComments() {
	for f.comIdx < len(f.comments) {
		c := f.comments[f.comIdx]
		f.comIdx++
		f.writeIndent()
		f.writeln(c.Text)
	}
}

// program formats the top-level program.
func (f *formatter) program(prog *ast.Program) {
	var imports []*ast.ImportDecl
	var others []ast.Decl
	for _, d := range prog.Decls {
		if imp, ok := d.(*ast.ImportDecl); ok {
			imports = append(imports, imp)
		} else {
			others = append(others, d)
		}
	}

	// Sort imports alphabetically
	sort.Slice(imports, func(i, j int) bool {
		return imports[i].Path < imports[j].Path
	})

	for _, imp := range imports {
		f.emitCommentsBefore(imp.Line)
		f.importDecl(imp)
	}

	if len(imports) > 0 && len(others) > 0 {
		f.nl()
	}

	for i, d := range others {
		f.emitCommentsBefore(declLine(d))
		if i > 0 {
			f.nl()
		}
		f.decl(d)
	}

	f.emitRemainingComments()
}

func declLine(d ast.Decl) int {
	switch d := d.(type) {
	case *ast.ImportDecl:
		return d.Line
	case *ast.FnDecl:
		return d.Line
	case *ast.ClassDecl:
		return d.Line
	case *ast.VarDecl:
		return d.Line
	case *ast.ConstDecl:
		return d.Line
	case *ast.EnumDecl:
		return d.Line
	case *ast.InterfaceDecl:
		return d.Line
	}
	return 0
}
