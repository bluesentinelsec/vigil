package parser

import (
	"github.com/bluesentinelsec/basl/pkg/basl/ast"
	"github.com/bluesentinelsec/basl/pkg/basl/lexer"
)

func (p *Parser) parseType() (*ast.TypeExpr, error) {
	tok := p.peek()
	switch tok.Type {
	case lexer.TOKEN_ARRAY:
		p.advance()
		if _, err := p.expect(lexer.TOKEN_LT); err != nil {
			return nil, err
		}
		elem, err := p.parseType()
		if err != nil {
			return nil, err
		}
		if err := p.expectGT(); err != nil {
			return nil, err
		}
		return &ast.TypeExpr{Name: "array", ElemType: elem}, nil
	case lexer.TOKEN_MAP:
		p.advance()
		if _, err := p.expect(lexer.TOKEN_LT); err != nil {
			return nil, err
		}
		key, err := p.parseType()
		if err != nil {
			return nil, err
		}
		if _, err := p.expect(lexer.TOKEN_COMMA); err != nil {
			return nil, err
		}
		val, err := p.parseType()
		if err != nil {
			return nil, err
		}
		if err := p.expectGT(); err != nil {
			return nil, err
		}
		return &ast.TypeExpr{Name: "map", KeyType: key, ValType: val}, nil
	case lexer.TOKEN_BOOL_TYPE:
		p.advance()
		return &ast.TypeExpr{Name: "bool"}, nil
	case lexer.TOKEN_I32:
		p.advance()
		return &ast.TypeExpr{Name: "i32"}, nil
	case lexer.TOKEN_I64:
		p.advance()
		return &ast.TypeExpr{Name: "i64"}, nil
	case lexer.TOKEN_F64:
		p.advance()
		return &ast.TypeExpr{Name: "f64"}, nil
	case lexer.TOKEN_U8:
		p.advance()
		return &ast.TypeExpr{Name: "u8"}, nil
	case lexer.TOKEN_U32:
		p.advance()
		return &ast.TypeExpr{Name: "u32"}, nil
	case lexer.TOKEN_U64:
		p.advance()
		return &ast.TypeExpr{Name: "u64"}, nil
	case lexer.TOKEN_STRING_TYPE:
		p.advance()
		return &ast.TypeExpr{Name: "string"}, nil
	case lexer.TOKEN_VOID:
		p.advance()
		return &ast.TypeExpr{Name: "void"}, nil
	case lexer.TOKEN_ERR:
		p.advance()
		return &ast.TypeExpr{Name: "err"}, nil
	case lexer.TOKEN_FN:
		p.advance()
		// fn with no parens = any callable
		if p.peek().Type != lexer.TOKEN_LPAREN {
			return &ast.TypeExpr{Name: "fn"}, nil
		}
		// fn(T1, T2, ...) -> R
		p.advance() // consume (
		var params []*ast.TypeExpr
		for p.peek().Type != lexer.TOKEN_RPAREN {
			if len(params) > 0 {
				if _, err := p.expect(lexer.TOKEN_COMMA); err != nil {
					return nil, err
				}
			}
			pt, err := p.parseType()
			if err != nil {
				return nil, err
			}
			params = append(params, pt)
		}
		p.advance() // consume )
		var ret *ast.TypeExpr
		if p.peek().Type == lexer.TOKEN_ARROW {
			p.advance() // consume ->
			var err error
			ret, err = p.parseType()
			if err != nil {
				return nil, err
			}
		}
		return &ast.TypeExpr{Name: "fn", ParamTypes: params, ReturnType: ret}, nil
	case lexer.TOKEN_IDENT:
		p.advance()
		name := tok.Literal
		// Handle module-qualified types: mod.Type
		if p.peek().Type == lexer.TOKEN_DOT {
			p.advance() // consume .
			typeTok := p.peek()
			if !isNameToken(typeTok.Type) {
				return nil, p.errAt(typeTok, "expected type name after '.'")
			}
			p.advance()
			name = name + "." + typeTok.Literal
		}
		return &ast.TypeExpr{Name: name}, nil
	default:
		return nil, p.errAt(tok, "expected type")
	}
}

func (p *Parser) parseReturnType() (*ast.ReturnType, error) {
	// Check for tuple return: (type, type, ...)
	if p.peek().Type == lexer.TOKEN_LPAREN {
		p.advance()
		var types []*ast.TypeExpr
		for {
			t, err := p.parseType()
			if err != nil {
				return nil, err
			}
			types = append(types, t)
			if !p.match(lexer.TOKEN_COMMA) {
				break
			}
		}
		if _, err := p.expect(lexer.TOKEN_RPAREN); err != nil {
			return nil, err
		}
		return &ast.ReturnType{Types: types}, nil
	}
	t, err := p.parseType()
	if err != nil {
		return nil, err
	}
	return &ast.ReturnType{Types: []*ast.TypeExpr{t}}, nil
}
