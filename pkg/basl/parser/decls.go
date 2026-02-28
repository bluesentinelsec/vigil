package parser

import (
	"fmt"

	"github.com/bluesentinelsec/basl/pkg/basl/ast"
	"github.com/bluesentinelsec/basl/pkg/basl/lexer"
)

func (p *Parser) parseFnDecl(pub bool) (*ast.FnDecl, error) {
	tok := p.advance() // consume fn
	nameTok, err := p.expect(lexer.TOKEN_IDENT)
	if err != nil {
		return nil, err
	}
	if _, err := p.expect(lexer.TOKEN_LPAREN); err != nil {
		return nil, err
	}
	params, err := p.parseParams()
	if err != nil {
		return nil, err
	}
	if _, err := p.expect(lexer.TOKEN_RPAREN); err != nil {
		return nil, err
	}
	var retType *ast.ReturnType
	if p.peek().Type == lexer.TOKEN_ARROW {
		p.advance()
		retType, err = p.parseReturnType()
		if err != nil {
			return nil, err
		}
	}
	body, err := p.parseBlock()
	if err != nil {
		return nil, err
	}
	return &ast.FnDecl{Pub: pub, Name: nameTok.Literal, Params: params, Return: retType, Body: body, Line: tok.Line}, nil
}

func (p *Parser) parseParams() ([]ast.Param, error) {
	var params []ast.Param
	if p.peek().Type == lexer.TOKEN_RPAREN {
		return params, nil
	}
	for {
		typ, err := p.parseType()
		if err != nil {
			return nil, err
		}
		name, err := p.expect(lexer.TOKEN_IDENT)
		if err != nil {
			return nil, err
		}
		params = append(params, ast.Param{Type: typ, Name: name.Literal})
		if !p.match(lexer.TOKEN_COMMA) {
			break
		}
	}
	return params, nil
}

func (p *Parser) parseTopVarDecl(pub bool) (*ast.VarDecl, error) {
	typ, err := p.parseType()
	if err != nil {
		return nil, err
	}
	name, err := p.expect(lexer.TOKEN_IDENT)
	if err != nil {
		return nil, err
	}
	if _, err := p.expect(lexer.TOKEN_ASSIGN); err != nil {
		return nil, err
	}
	init, err := p.parseExpr()
	if err != nil {
		return nil, err
	}
	if _, err := p.expect(lexer.TOKEN_SEMICOLON); err != nil {
		return nil, err
	}
	return &ast.VarDecl{Pub: pub, Type: typ, Name: name.Literal, Init: init, Line: name.Line}, nil
}

func (p *Parser) parseClassDecl(pub bool) (*ast.ClassDecl, error) {
	tok := p.advance() // consume class
	name, err := p.expect(lexer.TOKEN_IDENT)
	if err != nil {
		return nil, err
	}
	cd := &ast.ClassDecl{Pub: pub, Name: name.Literal, Line: tok.Line}
	// Parse optional implements clause
	if p.peek().Type == lexer.TOKEN_IMPLEMENTS {
		p.advance()
		for {
			ifaceTok, err := p.expect(lexer.TOKEN_IDENT)
			if err != nil {
				return nil, err
			}
			cd.Implements = append(cd.Implements, ifaceTok.Literal)
			if !p.match(lexer.TOKEN_COMMA) {
				break
			}
		}
	}
	if _, err := p.expect(lexer.TOKEN_LBRACE); err != nil {
		return nil, err
	}
	for p.peek().Type != lexer.TOKEN_RBRACE && p.peek().Type != lexer.TOKEN_EOF {
		isPub := false
		if p.peek().Type == lexer.TOKEN_PUB {
			isPub = true
			p.advance()
		}
		if p.peek().Type == lexer.TOKEN_FN {
			fn, err := p.parseFnDecl(isPub)
			if err != nil {
				return nil, err
			}
			cd.Methods = append(cd.Methods, fn)
		} else if isTypeToken(p.peek().Type) {
			typ, err := p.parseType()
			if err != nil {
				return nil, err
			}
			nameTok, err := p.expect(lexer.TOKEN_IDENT)
			if err != nil {
				return nil, err
			}
			if _, err := p.expect(lexer.TOKEN_SEMICOLON); err != nil {
				return nil, err
			}
			cd.Fields = append(cd.Fields, ast.ClassField{Pub: isPub, Type: typ, Name: nameTok.Literal, Line: nameTok.Line})
		} else {
			return nil, p.errAt(p.peek(), fmt.Sprintf("unexpected %s in class body", p.peek().Type))
		}
	}
	if _, err := p.expect(lexer.TOKEN_RBRACE); err != nil {
		return nil, err
	}
	return cd, nil
}

func (p *Parser) parseEnumDecl(pub bool) (*ast.EnumDecl, error) {
	tok := p.advance() // consume enum
	nameTok, err := p.expect(lexer.TOKEN_IDENT)
	if err != nil {
		return nil, err
	}
	if _, err := p.expect(lexer.TOKEN_LBRACE); err != nil {
		return nil, err
	}
	var variants []ast.EnumVariant
	for p.peek().Type != lexer.TOKEN_RBRACE && p.peek().Type != lexer.TOKEN_EOF {
		vTok, err := p.expect(lexer.TOKEN_IDENT)
		if err != nil {
			return nil, err
		}
		var val ast.Expr
		if p.peek().Type == lexer.TOKEN_ASSIGN {
			p.advance()
			val, err = p.parseExpr()
			if err != nil {
				return nil, err
			}
		}
		variants = append(variants, ast.EnumVariant{Name: vTok.Literal, Value: val})
		if p.peek().Type == lexer.TOKEN_COMMA {
			p.advance()
		}
	}
	if _, err := p.expect(lexer.TOKEN_RBRACE); err != nil {
		return nil, err
	}
	return &ast.EnumDecl{Pub: pub, Name: nameTok.Literal, Variants: variants, Line: tok.Line}, nil
}

func (p *Parser) parseConstDecl(pub bool) (*ast.ConstDecl, error) {
	tok := p.advance() // consume const
	typ, err := p.parseType()
	if err != nil {
		return nil, err
	}
	nameTok, err := p.expect(lexer.TOKEN_IDENT)
	if err != nil {
		return nil, err
	}
	if _, err := p.expect(lexer.TOKEN_ASSIGN); err != nil {
		return nil, err
	}
	init, err := p.parseExpr()
	if err != nil {
		return nil, err
	}
	if _, err := p.expect(lexer.TOKEN_SEMICOLON); err != nil {
		return nil, err
	}
	return &ast.ConstDecl{Pub: pub, Type: typ, Name: nameTok.Literal, Init: init, Line: tok.Line}, nil
}

func (p *Parser) parseInterfaceDecl(pub bool) (*ast.InterfaceDecl, error) {
	tok := p.advance() // consume interface
	nameTok, err := p.expect(lexer.TOKEN_IDENT)
	if err != nil {
		return nil, err
	}
	if _, err := p.expect(lexer.TOKEN_LBRACE); err != nil {
		return nil, err
	}
	var methods []ast.InterfaceMethod
	for p.peek().Type != lexer.TOKEN_RBRACE && p.peek().Type != lexer.TOKEN_EOF {
		if _, err := p.expect(lexer.TOKEN_FN); err != nil {
			return nil, err
		}
		mName, err := p.expect(lexer.TOKEN_IDENT)
		if err != nil {
			return nil, err
		}
		if _, err := p.expect(lexer.TOKEN_LPAREN); err != nil {
			return nil, err
		}
		params, err := p.parseParams()
		if err != nil {
			return nil, err
		}
		if _, err := p.expect(lexer.TOKEN_RPAREN); err != nil {
			return nil, err
		}
		var retType *ast.ReturnType
		if p.peek().Type == lexer.TOKEN_ARROW {
			p.advance()
			retType, err = p.parseReturnType()
			if err != nil {
				return nil, err
			}
		}
		if _, err := p.expect(lexer.TOKEN_SEMICOLON); err != nil {
			return nil, err
		}
		methods = append(methods, ast.InterfaceMethod{Name: mName.Literal, Params: params, Return: retType, Line: mName.Line})
	}
	if _, err := p.expect(lexer.TOKEN_RBRACE); err != nil {
		return nil, err
	}
	return &ast.InterfaceDecl{Pub: pub, Name: nameTok.Literal, Methods: methods, Line: tok.Line}, nil
}
