package formatter

import (
	"fmt"
	"strings"

	"github.com/bluesentinelsec/basl/pkg/basl/ast"
)

func (f *formatter) decl(d ast.Decl) {
	switch d := d.(type) {
	case *ast.ImportDecl:
		f.importDecl(d)
	case *ast.FnDecl:
		prefix := ""
		if d.Pub {
			prefix = "pub "
		}
		f.fnDecl(d, prefix)
	case *ast.ClassDecl:
		f.classDecl(d)
	case *ast.VarDecl:
		f.varDecl(d)
	case *ast.ConstDecl:
		f.constDecl(d)
	case *ast.EnumDecl:
		f.enumDecl(d)
	case *ast.InterfaceDecl:
		f.interfaceDecl(d)
	}
}

func (f *formatter) importDecl(d *ast.ImportDecl) {
	if d.Alias != "" {
		f.writelnIndented(fmt.Sprintf("import %q as %s;", d.Path, d.Alias))
	} else {
		f.writelnIndented(fmt.Sprintf("import %q;", d.Path))
	}
}

func (f *formatter) fnDecl(d *ast.FnDecl, pubPrefix string) {
	var sb strings.Builder
	sb.WriteString(pubPrefix)
	sb.WriteString("fn ")
	sb.WriteString(d.Name)
	sb.WriteString("(")
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
	sb.WriteString(" {")
	f.writelnIndented(sb.String())
	f.indent++
	f.block(d.Body)
	f.indent--
	f.writelnIndented("}")
}

func (f *formatter) classDecl(d *ast.ClassDecl) {
	var sb strings.Builder
	if d.Pub {
		sb.WriteString("pub ")
	}
	sb.WriteString("class ")
	sb.WriteString(d.Name)
	if len(d.Implements) > 0 {
		sb.WriteString(" implements ")
		sb.WriteString(strings.Join(d.Implements, ", "))
	}
	sb.WriteString(" {")
	f.writelnIndented(sb.String())
	f.indent++

	// Fields
	for i, fld := range d.Fields {
		if i == 0 {
			// Check for comments before first field
		}
		f.emitCommentsBefore(fld.Line)
		prefix := ""
		if fld.Pub {
			prefix = "pub "
		}
		f.writelnIndented(fmt.Sprintf("%s%s %s;", prefix, typeExprStr(fld.Type), fld.Name))
	}

	// Methods
	for i, m := range d.Methods {
		// Blank line before methods (after fields or between methods)
		if i == 0 && len(d.Fields) > 0 {
			f.nl()
		} else if i > 0 {
			f.nl()
		}
		f.emitCommentsBefore(m.Line)
		prefix := ""
		if m.Pub {
			prefix = "pub "
		}
		f.fnDecl(m, prefix)
	}

	f.indent--
	f.writelnIndented("}")
}

func (f *formatter) varDecl(d *ast.VarDecl) {
	prefix := ""
	if d.Pub {
		prefix = "pub "
	}
	f.writelnIndented(fmt.Sprintf("%s%s %s = %s;", prefix, typeExprStr(d.Type), d.Name, f.exprStr(d.Init)))
}

func (f *formatter) constDecl(d *ast.ConstDecl) {
	prefix := ""
	if d.Pub {
		prefix = "pub "
	}
	f.writelnIndented(fmt.Sprintf("%sconst %s %s = %s;", prefix, typeExprStr(d.Type), d.Name, f.exprStr(d.Init)))
}

func (f *formatter) enumDecl(d *ast.EnumDecl) {
	prefix := ""
	if d.Pub {
		prefix = "pub "
	}
	f.writelnIndented(fmt.Sprintf("%senum %s {", prefix, d.Name))
	f.indent++
	for i, v := range d.Variants {
		if v.Value != nil {
			f.writeIndented(fmt.Sprintf("%s = %s", v.Name, f.exprStr(v.Value)))
		} else {
			f.writeIndented(v.Name)
		}
		if i < len(d.Variants)-1 {
			f.write(",")
		}
		f.nl()
	}
	f.indent--
	f.writelnIndented("}")
}

func (f *formatter) interfaceDecl(d *ast.InterfaceDecl) {
	prefix := ""
	if d.Pub {
		prefix = "pub "
	}
	f.writelnIndented(fmt.Sprintf("%sinterface %s {", prefix, d.Name))
	f.indent++
	for _, m := range d.Methods {
		var sb strings.Builder
		sb.WriteString("fn ")
		sb.WriteString(m.Name)
		sb.WriteString("(")
		for i, p := range m.Params {
			if i > 0 {
				sb.WriteString(", ")
			}
			sb.WriteString(typeExprStr(p.Type))
			sb.WriteString(" ")
			sb.WriteString(p.Name)
		}
		sb.WriteString(")")
		if m.Return != nil {
			sb.WriteString(" -> ")
			sb.WriteString(returnTypeStr(m.Return))
		}
		sb.WriteString(";")
		f.writelnIndented(sb.String())
	}
	f.indent--
	f.writelnIndented("}")
}

// typeExprStr converts a TypeExpr to its string representation.
func typeExprStr(t *ast.TypeExpr) string {
	if t == nil {
		return "void"
	}
	switch t.Name {
	case "array":
		return "array<" + typeExprStr(t.ElemType) + ">"
	case "map":
		return "map<" + typeExprStr(t.KeyType) + ", " + typeExprStr(t.ValType) + ">"
	case "fn":
		if len(t.ParamTypes) == 0 && t.ReturnType == nil {
			return "fn"
		}
		var sb strings.Builder
		sb.WriteString("fn(")
		for i, p := range t.ParamTypes {
			if i > 0 {
				sb.WriteString(", ")
			}
			sb.WriteString(typeExprStr(p))
		}
		sb.WriteString(")")
		if t.ReturnType != nil {
			sb.WriteString(" -> ")
			sb.WriteString(typeExprStr(t.ReturnType))
		}
		return sb.String()
	default:
		return t.Name
	}
}

func returnTypeStr(rt *ast.ReturnType) string {
	if rt == nil {
		return "void"
	}
	if len(rt.Types) == 1 {
		return typeExprStr(rt.Types[0])
	}
	parts := make([]string, len(rt.Types))
	for i, t := range rt.Types {
		parts[i] = typeExprStr(t)
	}
	return "(" + strings.Join(parts, ", ") + ")"
}
