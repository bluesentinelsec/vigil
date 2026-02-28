package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/bluesentinelsec/basl/pkg/basl/ast"
	"github.com/bluesentinelsec/basl/pkg/basl/lexer"
	"github.com/bluesentinelsec/basl/pkg/basl/parser"
)

func runDoc(args []string) int {
	if wantsHelp(args) {
		fmt.Print(mustHelpText("doc"))
		return 0
	}

	if len(args) == 0 || len(args) > 2 {
		fmt.Fprintln(os.Stderr, "usage: basl doc <file.basl> [symbol]")
		return 2
	}

	symbol := ""
	if len(args) == 2 {
		symbol = args[1]
	}

	out, err := renderDocFile(args[0], symbol)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error[doc]: %s\n", err)
		return 1
	}

	fmt.Print(out)
	if !strings.HasSuffix(out, "\n") {
		fmt.Println()
	}
	return 0
}

func renderDocFile(path, symbol string) (string, error) {
	src, err := os.ReadFile(path)
	if err != nil {
		return "", err
	}

	lex := lexer.New(string(src))
	tokens, err := lex.Tokenize()
	if err != nil {
		return "", fmt.Errorf("lexer: %w", err)
	}

	p := parser.New(tokens)
	prog, err := p.Parse()
	if err != nil {
		return "", fmt.Errorf("parser: %w", err)
	}

	doc := newSourceDoc(path, string(src), prog)
	if symbol == "" {
		return doc.renderModule(), nil
	}
	return doc.renderSymbol(symbol)
}

type sourceDoc struct {
	path  string
	lines []string
	prog  *ast.Program
}

func newSourceDoc(path, src string, prog *ast.Program) *sourceDoc {
	return &sourceDoc{
		path:  path,
		lines: strings.Split(src, "\n"),
		prog:  prog,
	}
}

func (d *sourceDoc) renderModule() string {
	var sections []string

	sections = append(sections, "MODULE\n  "+d.moduleName())

	if summary := d.moduleSummary(); summary != "" {
		sections = append(sections, "SUMMARY\n"+indentText(summary, 2))
	}

	if items := d.publicConsts(); len(items) > 0 {
		sections = append(sections, d.renderConstSection(items))
	}
	if items := d.publicVars(); len(items) > 0 {
		sections = append(sections, d.renderVarSection(items))
	}
	if items := d.publicEnums(); len(items) > 0 {
		sections = append(sections, d.renderEnumSection(items))
	}
	if items := d.publicInterfaces(); len(items) > 0 {
		sections = append(sections, d.renderInterfaceSection(items))
	}
	if items := d.publicClasses(); len(items) > 0 {
		sections = append(sections, d.renderClassSection(items))
	}
	if items := d.publicFuncs(); len(items) > 0 {
		sections = append(sections, d.renderFuncSection(items))
	}

	return strings.Join(sections, "\n\n")
}

func (d *sourceDoc) renderSymbol(symbol string) (string, error) {
	if left, right, ok := strings.Cut(symbol, "."); ok {
		if out, found := d.renderClassMember(left, right); found {
			return out, nil
		}
		if out, found := d.renderInterfaceMethod(left, right); found {
			return out, nil
		}
		return "", fmt.Errorf("public symbol %q not found", symbol)
	}

	for _, decl := range d.prog.Decls {
		switch decl := decl.(type) {
		case *ast.ConstDecl:
			if decl.Pub && decl.Name == symbol {
				return d.renderConstDetail(decl), nil
			}
		case *ast.VarDecl:
			if decl.Pub && decl.Name == symbol {
				return d.renderVarDetail(decl), nil
			}
		case *ast.EnumDecl:
			if decl.Pub && decl.Name == symbol {
				return d.renderEnumDetail(decl), nil
			}
		case *ast.InterfaceDecl:
			if decl.Pub && decl.Name == symbol {
				return d.renderInterfaceDetail(decl), nil
			}
		case *ast.ClassDecl:
			if decl.Pub && decl.Name == symbol {
				return d.renderClassDetail(decl), nil
			}
		case *ast.FnDecl:
			if decl.Pub && decl.Name == symbol {
				return d.renderFuncDetail(decl), nil
			}
		}
	}

	return "", fmt.Errorf("public symbol %q not found", symbol)
}

func (d *sourceDoc) renderConstSection(decls []*ast.ConstDecl) string {
	var b strings.Builder
	b.WriteString("CONSTANTS\n")
	for i, decl := range decls {
		if i > 0 {
			b.WriteString("\n")
		}
		b.WriteString("  ")
		b.WriteString(decl.Name)
		b.WriteString(" ")
		b.WriteString(docTypeExprStr(decl.Type))
		if doc := d.commentBefore(decl.Line); doc != "" {
			b.WriteString("\n")
			b.WriteString(indentText(doc, 4))
		}
	}
	return b.String()
}

func (d *sourceDoc) renderVarSection(decls []*ast.VarDecl) string {
	var b strings.Builder
	b.WriteString("VARIABLES\n")
	for i, decl := range decls {
		if i > 0 {
			b.WriteString("\n")
		}
		b.WriteString("  ")
		b.WriteString(decl.Name)
		b.WriteString(" ")
		b.WriteString(docTypeExprStr(decl.Type))
		if doc := d.commentBefore(decl.Line); doc != "" {
			b.WriteString("\n")
			b.WriteString(indentText(doc, 4))
		}
	}
	return b.String()
}

func (d *sourceDoc) renderEnumSection(decls []*ast.EnumDecl) string {
	var b strings.Builder
	b.WriteString("ENUMS\n")
	for i, decl := range decls {
		if i > 0 {
			b.WriteString("\n")
		}
		b.WriteString(d.renderEnumBlock(decl, 2))
	}
	return b.String()
}

func (d *sourceDoc) renderInterfaceSection(decls []*ast.InterfaceDecl) string {
	var b strings.Builder
	b.WriteString("INTERFACES\n")
	for i, decl := range decls {
		if i > 0 {
			b.WriteString("\n")
		}
		b.WriteString(d.renderInterfaceBlock(decl, 2))
	}
	return b.String()
}

func (d *sourceDoc) renderClassSection(decls []*ast.ClassDecl) string {
	var b strings.Builder
	b.WriteString("CLASSES\n")
	for i, decl := range decls {
		if i > 0 {
			b.WriteString("\n")
		}
		b.WriteString(d.renderClassBlock(decl, 2))
	}
	return b.String()
}

func (d *sourceDoc) renderFuncSection(decls []*ast.FnDecl) string {
	var b strings.Builder
	b.WriteString("FUNCTIONS\n")
	for i, decl := range decls {
		if i > 0 {
			b.WriteString("\n")
		}
		b.WriteString("  ")
		b.WriteString(docFuncSig(decl.Name, decl.Params, decl.Return))
		if doc := d.commentBefore(decl.Line); doc != "" {
			b.WriteString("\n")
			b.WriteString(indentText(doc, 4))
		}
	}
	return b.String()
}

func (d *sourceDoc) renderConstDetail(decl *ast.ConstDecl) string {
	var b strings.Builder
	b.WriteString(decl.Name)
	b.WriteString(" ")
	b.WriteString(docTypeExprStr(decl.Type))
	if doc := d.commentBefore(decl.Line); doc != "" {
		b.WriteString("\n\n")
		b.WriteString(doc)
	}
	return b.String()
}

func (d *sourceDoc) renderVarDetail(decl *ast.VarDecl) string {
	var b strings.Builder
	b.WriteString(decl.Name)
	b.WriteString(" ")
	b.WriteString(docTypeExprStr(decl.Type))
	if doc := d.commentBefore(decl.Line); doc != "" {
		b.WriteString("\n\n")
		b.WriteString(doc)
	}
	return b.String()
}

func (d *sourceDoc) renderFuncDetail(decl *ast.FnDecl) string {
	var b strings.Builder
	b.WriteString(docFuncSig(decl.Name, decl.Params, decl.Return))
	if doc := d.commentBefore(decl.Line); doc != "" {
		b.WriteString("\n\n")
		b.WriteString(doc)
	}
	return b.String()
}

func (d *sourceDoc) renderEnumDetail(decl *ast.EnumDecl) string {
	return d.renderEnumBlock(decl, 0)
}

func (d *sourceDoc) renderInterfaceDetail(decl *ast.InterfaceDecl) string {
	return d.renderInterfaceBlock(decl, 0)
}

func (d *sourceDoc) renderClassDetail(decl *ast.ClassDecl) string {
	return d.renderClassBlock(decl, 0)
}

func (d *sourceDoc) renderEnumBlock(decl *ast.EnumDecl, indent int) string {
	var b strings.Builder
	pad := strings.Repeat(" ", indent)
	b.WriteString(pad)
	b.WriteString(decl.Name)
	if doc := d.commentBefore(decl.Line); doc != "" {
		b.WriteString("\n\n")
		b.WriteString(indentText(doc, indent+2))
	}
	if len(decl.Variants) > 0 {
		b.WriteString("\n\n")
		b.WriteString(pad)
		b.WriteString("  Variants\n")
		for i, v := range decl.Variants {
			if i > 0 {
				b.WriteString("\n")
			}
			b.WriteString(pad)
			b.WriteString("    ")
			b.WriteString(v.Name)
		}
	}
	return b.String()
}

func (d *sourceDoc) renderInterfaceBlock(decl *ast.InterfaceDecl, indent int) string {
	var b strings.Builder
	pad := strings.Repeat(" ", indent)
	b.WriteString(pad)
	b.WriteString(decl.Name)
	if doc := d.commentBefore(decl.Line); doc != "" {
		b.WriteString("\n\n")
		b.WriteString(indentText(doc, indent+2))
	}

	if len(decl.Methods) > 0 {
		b.WriteString("\n\n")
		b.WriteString(pad)
		b.WriteString("  Methods\n")
		for i, method := range decl.Methods {
			if i > 0 {
				b.WriteString("\n")
			}
			b.WriteString(pad)
			b.WriteString("    ")
			b.WriteString(docFuncSig(method.Name, method.Params, method.Return))
			if doc := d.commentBefore(method.Line); doc != "" {
				b.WriteString("\n")
				b.WriteString(indentText(doc, indent+6))
			}
		}
	}
	return b.String()
}

func (d *sourceDoc) renderClassBlock(decl *ast.ClassDecl, indent int) string {
	var b strings.Builder
	pad := strings.Repeat(" ", indent)
	b.WriteString(pad)
	b.WriteString(decl.Name)
	if len(decl.Implements) > 0 {
		b.WriteString(" implements ")
		b.WriteString(strings.Join(decl.Implements, ", "))
	}
	if doc := d.commentBefore(decl.Line); doc != "" {
		b.WriteString("\n\n")
		b.WriteString(indentText(doc, indent+2))
	}

	fields := publicClassFields(decl)
	if len(fields) > 0 {
		b.WriteString("\n\n")
		b.WriteString(pad)
		b.WriteString("  Fields\n")
		for i, field := range fields {
			if i > 0 {
				b.WriteString("\n")
			}
			b.WriteString(pad)
			b.WriteString("    ")
			b.WriteString(field.Name)
			b.WriteString(" ")
			b.WriteString(docTypeExprStr(field.Type))
			if doc := d.commentBefore(field.Line); doc != "" {
				b.WriteString("\n")
				b.WriteString(indentText(doc, indent+6))
			}
		}
	}

	methods := publicClassMethods(decl)
	if len(methods) > 0 {
		b.WriteString("\n\n")
		b.WriteString(pad)
		b.WriteString("  Methods\n")
		for i, method := range methods {
			if i > 0 {
				b.WriteString("\n")
			}
			b.WriteString(pad)
			b.WriteString("    ")
			b.WriteString(docFuncSig(method.Name, method.Params, method.Return))
			if doc := d.commentBefore(method.Line); doc != "" {
				b.WriteString("\n")
				b.WriteString(indentText(doc, indent+6))
			}
		}
	}

	return b.String()
}

func (d *sourceDoc) renderClassMember(className, memberName string) (string, bool) {
	for _, decl := range d.publicClasses() {
		if decl.Name != className {
			continue
		}
		for _, field := range publicClassFields(decl) {
			if field.Name != memberName {
				continue
			}
			var b strings.Builder
			b.WriteString(className)
			b.WriteString(".")
			b.WriteString(field.Name)
			b.WriteString(" ")
			b.WriteString(docTypeExprStr(field.Type))
			if doc := d.commentBefore(field.Line); doc != "" {
				b.WriteString("\n\n")
				b.WriteString(doc)
			}
			return b.String(), true
		}
		for _, method := range publicClassMethods(decl) {
			if method.Name != memberName {
				continue
			}
			var b strings.Builder
			b.WriteString(className)
			b.WriteString(".")
			b.WriteString(docFuncSig(method.Name, method.Params, method.Return))
			if doc := d.commentBefore(method.Line); doc != "" {
				b.WriteString("\n\n")
				b.WriteString(doc)
			}
			return b.String(), true
		}
	}
	return "", false
}

func (d *sourceDoc) renderInterfaceMethod(interfaceName, methodName string) (string, bool) {
	for _, decl := range d.publicInterfaces() {
		if decl.Name != interfaceName {
			continue
		}
		for _, method := range decl.Methods {
			if method.Name != methodName {
				continue
			}
			var b strings.Builder
			b.WriteString(interfaceName)
			b.WriteString(".")
			b.WriteString(docFuncSig(method.Name, method.Params, method.Return))
			if doc := d.commentBefore(method.Line); doc != "" {
				b.WriteString("\n\n")
				b.WriteString(doc)
			}
			return b.String(), true
		}
	}
	return "", false
}

func (d *sourceDoc) publicConsts() []*ast.ConstDecl {
	var out []*ast.ConstDecl
	for _, decl := range d.prog.Decls {
		if decl, ok := decl.(*ast.ConstDecl); ok && decl.Pub {
			out = append(out, decl)
		}
	}
	return out
}

func (d *sourceDoc) publicVars() []*ast.VarDecl {
	var out []*ast.VarDecl
	for _, decl := range d.prog.Decls {
		if decl, ok := decl.(*ast.VarDecl); ok && decl.Pub {
			out = append(out, decl)
		}
	}
	return out
}

func (d *sourceDoc) publicEnums() []*ast.EnumDecl {
	var out []*ast.EnumDecl
	for _, decl := range d.prog.Decls {
		if decl, ok := decl.(*ast.EnumDecl); ok && decl.Pub {
			out = append(out, decl)
		}
	}
	return out
}

func (d *sourceDoc) publicInterfaces() []*ast.InterfaceDecl {
	var out []*ast.InterfaceDecl
	for _, decl := range d.prog.Decls {
		if decl, ok := decl.(*ast.InterfaceDecl); ok && decl.Pub {
			out = append(out, decl)
		}
	}
	return out
}

func (d *sourceDoc) publicClasses() []*ast.ClassDecl {
	var out []*ast.ClassDecl
	for _, decl := range d.prog.Decls {
		if decl, ok := decl.(*ast.ClassDecl); ok && decl.Pub {
			out = append(out, decl)
		}
	}
	return out
}

func (d *sourceDoc) publicFuncs() []*ast.FnDecl {
	var out []*ast.FnDecl
	for _, decl := range d.prog.Decls {
		if decl, ok := decl.(*ast.FnDecl); ok && decl.Pub {
			out = append(out, decl)
		}
	}
	return out
}

func publicClassFields(decl *ast.ClassDecl) []ast.ClassField {
	var out []ast.ClassField
	for _, field := range decl.Fields {
		if field.Pub {
			out = append(out, field)
		}
	}
	return out
}

func publicClassMethods(decl *ast.ClassDecl) []*ast.FnDecl {
	var out []*ast.FnDecl
	for _, method := range decl.Methods {
		if method.Pub {
			out = append(out, method)
		}
	}
	return out
}

func (d *sourceDoc) moduleName() string {
	base := filepath.Base(d.path)
	return strings.TrimSuffix(base, filepath.Ext(base))
}

func (d *sourceDoc) moduleSummary() string {
	firstDeclLine := len(d.lines) + 1
	for _, decl := range d.prog.Decls {
		if line := declLine(decl); line > 0 && line < firstDeclLine {
			firstDeclLine = line
		}
	}
	if firstDeclLine == len(d.lines)+1 {
		firstDeclLine = len(d.lines) + 1
	}

	i := 0
	for i < firstDeclLine-1 && strings.TrimSpace(d.lines[i]) == "" {
		i++
	}

	if i >= firstDeclLine-1 || !isLineComment(d.lines[i]) {
		return ""
	}

	var comments []string
	for i < firstDeclLine-1 && isLineComment(d.lines[i]) {
		comments = append(comments, stripLineComment(d.lines[i]))
		i++
	}
	return strings.Join(comments, "\n")
}

func (d *sourceDoc) commentBefore(line int) string {
	if line <= 1 || line-2 >= len(d.lines) {
		return ""
	}

	i := line - 2
	if i < 0 || !isLineComment(d.lines[i]) {
		return ""
	}

	var comments []string
	for i >= 0 && isLineComment(d.lines[i]) {
		comments = append(comments, stripLineComment(d.lines[i]))
		i--
	}

	for left, right := 0, len(comments)-1; left < right; left, right = left+1, right-1 {
		comments[left], comments[right] = comments[right], comments[left]
	}
	return strings.Join(comments, "\n")
}

func declLine(decl ast.Decl) int {
	switch decl := decl.(type) {
	case *ast.ImportDecl:
		return decl.Line
	case *ast.FnDecl:
		return decl.Line
	case *ast.VarDecl:
		return decl.Line
	case *ast.ClassDecl:
		return decl.Line
	case *ast.ConstDecl:
		return decl.Line
	case *ast.EnumDecl:
		return decl.Line
	case *ast.InterfaceDecl:
		return decl.Line
	default:
		return 0
	}
}

func isLineComment(line string) bool {
	return strings.HasPrefix(strings.TrimSpace(line), "//")
}

func stripLineComment(line string) string {
	trimmed := strings.TrimSpace(line)
	if !strings.HasPrefix(trimmed, "//") {
		return ""
	}
	text := strings.TrimPrefix(trimmed, "//")
	text = strings.TrimPrefix(text, " ")
	return text
}

func indentText(text string, spaces int) string {
	pad := strings.Repeat(" ", spaces)
	lines := strings.Split(text, "\n")
	for i, line := range lines {
		if line == "" {
			lines[i] = ""
			continue
		}
		lines[i] = pad + line
	}
	return strings.Join(lines, "\n")
}

func docFuncSig(name string, params []ast.Param, ret *ast.ReturnType) string {
	var b strings.Builder
	b.WriteString(name)
	b.WriteString("(")
	for i, p := range params {
		if i > 0 {
			b.WriteString(", ")
		}
		b.WriteString(docTypeExprStr(p.Type))
		b.WriteString(" ")
		b.WriteString(p.Name)
	}
	b.WriteString(")")
	if ret != nil {
		b.WriteString(" -> ")
		b.WriteString(docReturnTypeStr(ret))
	}
	return b.String()
}

func docTypeExprStr(t *ast.TypeExpr) string {
	if t == nil {
		return "void"
	}
	switch t.Name {
	case "array":
		return "array<" + docTypeExprStr(t.ElemType) + ">"
	case "map":
		return "map<" + docTypeExprStr(t.KeyType) + ", " + docTypeExprStr(t.ValType) + ">"
	case "fn":
		if len(t.ParamTypes) == 0 && t.ReturnType == nil {
			return "fn"
		}
		var b strings.Builder
		b.WriteString("fn(")
		for i, p := range t.ParamTypes {
			if i > 0 {
				b.WriteString(", ")
			}
			b.WriteString(docTypeExprStr(p))
		}
		b.WriteString(")")
		if t.ReturnType != nil {
			b.WriteString(" -> ")
			b.WriteString(docTypeExprStr(t.ReturnType))
		}
		return b.String()
	default:
		return t.Name
	}
}

func docReturnTypeStr(rt *ast.ReturnType) string {
	if rt == nil {
		return "void"
	}
	if len(rt.Types) == 1 {
		return docTypeExprStr(rt.Types[0])
	}
	parts := make([]string, len(rt.Types))
	for i, t := range rt.Types {
		parts[i] = docTypeExprStr(t)
	}
	return "(" + strings.Join(parts, ", ") + ")"
}
