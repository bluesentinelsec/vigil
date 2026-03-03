package parser

import (
	"fmt"

	"github.com/bluesentinelsec/basl/pkg/basl/ast"
	"github.com/bluesentinelsec/basl/pkg/basl/lexer"
)

func (p *Parser) parseBlock() (*ast.Block, error) {
	if _, err := p.expect(lexer.TOKEN_LBRACE); err != nil {
		return nil, err
	}
	block := &ast.Block{}
	for p.peek().Type != lexer.TOKEN_RBRACE && p.peek().Type != lexer.TOKEN_EOF {
		s, err := p.parseStmt()
		if err != nil {
			return nil, err
		}
		block.Stmts = append(block.Stmts, s)
	}
	if _, err := p.expect(lexer.TOKEN_RBRACE); err != nil {
		return nil, err
	}
	return block, nil
}

func (p *Parser) parseStmt() (ast.Stmt, error) {
	tok := p.peek()
	switch tok.Type {
	case lexer.TOKEN_IF:
		return p.parseIfStmt()
	case lexer.TOKEN_WHILE:
		return p.parseWhileStmt()
	case lexer.TOKEN_FOR:
		return p.parseForStmt()
	case lexer.TOKEN_RETURN:
		return p.parseReturnStmt()
	case lexer.TOKEN_BREAK:
		p.advance()
		if _, err := p.expect(lexer.TOKEN_SEMICOLON); err != nil {
			return nil, err
		}
		return &ast.BreakStmt{Line: tok.Line}, nil
	case lexer.TOKEN_CONTINUE:
		p.advance()
		if _, err := p.expect(lexer.TOKEN_SEMICOLON); err != nil {
			return nil, err
		}
		return &ast.ContinueStmt{Line: tok.Line}, nil
	case lexer.TOKEN_DEFER:
		return p.parseDeferStmt()
	case lexer.TOKEN_SWITCH:
		return p.parseSwitchStmt()
	case lexer.TOKEN_CONST:
		return p.parseConstStmt()
	case lexer.TOKEN_FN:
		// fn ident(...) -> ... { } is a local function definition
		// fn ident = expr; is a var decl (fn as type)
		// fn(...) { } is an anonymous function expression (e.g. IIFE)
		// fn(type, type) -> type name = expr; is a typed fn var decl
		if p.pos+1 < len(p.tokens) && p.tokens[p.pos+1].Type == lexer.TOKEN_IDENT {
			if p.pos+2 < len(p.tokens) && p.tokens[p.pos+2].Type == lexer.TOKEN_LPAREN {
				return p.parseLocalFnDecl()
			}
			return p.parseVarDeclStmt()
		}
		// fn( — try anonymous function first, fall back to typed var decl
		if p.pos+1 < len(p.tokens) && p.tokens[p.pos+1].Type == lexer.TOKEN_LPAREN {
			saved := p.pos
			if stmt, err := p.parseExprOrAssignStmt(); err == nil {
				return stmt, nil
			}
			p.pos = saved
		}
		return p.parseVarDeclStmt()
	case lexer.TOKEN_LPAREN:
		// Could be tuple binding: (type name, type name) = expr;
		return p.parseTupleBindOrExprStmt()
	default:
		// type ident = expr; (var decl) OR expr; OR assignment
		if isTypeToken(tok.Type) && tok.Type != lexer.TOKEN_IDENT {
			return p.parseVarDeclStmt()
		}
		// For TOKEN_IDENT, we need lookahead to distinguish var decl from expr/assign
		if tok.Type == lexer.TOKEN_IDENT {
			return p.parseIdentStartStmt()
		}
		return p.parseExprOrAssignStmt()
	}
}

func (p *Parser) parseVarDeclStmt() (ast.Stmt, error) {
	typ, err := p.parseType()
	if err != nil {
		return nil, err
	}
	nameTok := p.peek()
	var name string
	var discard bool
	if nameTok.Literal == "_" && nameTok.Type == lexer.TOKEN_IDENT {
		discard = true
		name = "_"
		p.advance()
	} else {
		nt, err := p.expect(lexer.TOKEN_IDENT)
		if err != nil {
			return nil, err
		}
		name = nt.Literal
	}

	// If comma follows, this is a tuple binding: type name, type name = expr;
	if p.peek().Type == lexer.TOKEN_COMMA {
		bindings := []ast.TupleBindItem{{Type: typ, Name: name, Discard: discard}}
		for p.match(lexer.TOKEN_COMMA) {
			bt, err := p.parseType()
			if err != nil {
				return nil, err
			}
			bn := p.peek()
			var bname string
			var bdisc bool
			if bn.Literal == "_" && bn.Type == lexer.TOKEN_IDENT {
				bdisc = true
				bname = "_"
				p.advance()
			} else {
				bnt, err := p.expect(lexer.TOKEN_IDENT)
				if err != nil {
					return nil, err
				}
				bname = bnt.Literal
			}
			bindings = append(bindings, ast.TupleBindItem{Type: bt, Name: bname, Discard: bdisc})
		}
		if _, err := p.expect(lexer.TOKEN_ASSIGN); err != nil {
			return nil, err
		}
		val, err := p.parseExpr()
		if err != nil {
			return nil, err
		}
		if _, err := p.expect(lexer.TOKEN_SEMICOLON); err != nil {
			return nil, err
		}
		return &ast.TupleBindStmt{Bindings: bindings, Value: val, Line: nameTok.Line}, nil
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
	return &ast.VarStmt{Type: typ, Name: name, Init: init, Line: nameTok.Line}, nil
}

// parseIdentStartStmt handles statements starting with an identifier.
// Could be: var decl (ClassName x = ...), tuple bind (ClassName x, err e = ...),
// module-qualified type (mod.Type x = ...), assignment (x = ...), or expr stmt (x.foo();)
func (p *Parser) parseIdentStartStmt() (ast.Stmt, error) {
	// Lookahead: if ident followed by ident, it's a var decl or tuple bind
	if p.pos+1 < len(p.tokens) && p.tokens[p.pos+1].Type == lexer.TOKEN_IDENT {
		return p.parseVarDeclStmt()
	}
	// Check for module-qualified type: ident.ident ident (e.g. point.Point p)
	if p.pos+2 < len(p.tokens) &&
		p.tokens[p.pos+1].Type == lexer.TOKEN_DOT &&
		p.pos+3 < len(p.tokens) &&
		p.tokens[p.pos+2].Type == lexer.TOKEN_IDENT &&
		p.tokens[p.pos+3].Type == lexer.TOKEN_IDENT {
		return p.parseVarDeclStmt()
	}
	return p.parseExprOrAssignStmt()
}

func (p *Parser) parseExprOrAssignStmt() (ast.Stmt, error) {
	startLine := p.peek().Line
	expr, err := p.parseExpr()
	if err != nil {
		return nil, err
	}
	tok := p.peek()
	switch tok.Type {
	case lexer.TOKEN_ASSIGN:
		p.advance()
		val, err := p.parseExpr()
		if err != nil {
			return nil, err
		}
		if _, err := p.expect(lexer.TOKEN_SEMICOLON); err != nil {
			return nil, err
		}
		return &ast.AssignStmt{Target: expr, Value: val, Line: tok.Line}, nil
	case lexer.TOKEN_PLUS_ASSIGN, lexer.TOKEN_MINUS_ASSIGN, lexer.TOKEN_STAR_ASSIGN,
		lexer.TOKEN_SLASH_ASSIGN, lexer.TOKEN_PERCENT_ASSIGN:
		p.advance()
		val, err := p.parseExpr()
		if err != nil {
			return nil, err
		}
		if _, err := p.expect(lexer.TOKEN_SEMICOLON); err != nil {
			return nil, err
		}
		return &ast.CompoundAssignStmt{Target: expr, Op: tok.Literal, Value: val, Line: tok.Line}, nil
	case lexer.TOKEN_INC, lexer.TOKEN_DEC:
		p.advance()
		if _, err := p.expect(lexer.TOKEN_SEMICOLON); err != nil {
			return nil, err
		}
		return &ast.IncDecStmt{Target: expr, Op: tok.Literal, Line: tok.Line}, nil
	}
	if _, err := p.expect(lexer.TOKEN_SEMICOLON); err != nil {
		return nil, err
	}
	return &ast.ExprStmt{Expr: expr, Line: startLine}, nil
}

func (p *Parser) parseIfStmt() (*ast.IfStmt, error) {
	tok := p.advance() // consume if
	if _, err := p.expect(lexer.TOKEN_LPAREN); err != nil {
		return nil, err
	}
	cond, err := p.parseExpr()
	if err != nil {
		return nil, err
	}
	if _, err := p.expect(lexer.TOKEN_RPAREN); err != nil {
		return nil, err
	}
	then, err := p.parseBlock()
	if err != nil {
		return nil, err
	}
	var elseStmt ast.Stmt
	if p.peek().Type == lexer.TOKEN_ELSE {
		p.advance()
		if p.peek().Type == lexer.TOKEN_IF {
			elseStmt, err = p.parseIfStmt()
		} else {
			elseStmt, err = p.parseBlock()
		}
		if err != nil {
			return nil, err
		}
	}
	return &ast.IfStmt{Cond: cond, Then: then, Else: elseStmt, Line: tok.Line}, nil
}

func (p *Parser) parseWhileStmt() (*ast.WhileStmt, error) {
	tok := p.advance() // consume while
	if _, err := p.expect(lexer.TOKEN_LPAREN); err != nil {
		return nil, err
	}
	cond, err := p.parseExpr()
	if err != nil {
		return nil, err
	}
	if _, err := p.expect(lexer.TOKEN_RPAREN); err != nil {
		return nil, err
	}
	body, err := p.parseBlock()
	if err != nil {
		return nil, err
	}
	return &ast.WhileStmt{Cond: cond, Body: body, Line: tok.Line}, nil
}

func (p *Parser) parseForStmt() (ast.Stmt, error) {
	tok := p.advance() // consume for

	// for-in: "for ident in expr {" or "for ident, ident in expr {"
	if p.peek().Type == lexer.TOKEN_IDENT {
		return p.parseForInStmt(tok)
	}

	// C-style: "for (init; cond; post) {"
	if _, err := p.expect(lexer.TOKEN_LPAREN); err != nil {
		return nil, err
	}
	init, err := p.parseForInit()
	if err != nil {
		return nil, err
	}
	cond, err := p.parseExpr()
	if err != nil {
		return nil, err
	}
	if _, err := p.expect(lexer.TOKEN_SEMICOLON); err != nil {
		return nil, err
	}
	post, err := p.parseForPost()
	if err != nil {
		return nil, err
	}
	if _, err := p.expect(lexer.TOKEN_RPAREN); err != nil {
		return nil, err
	}
	body, err := p.parseBlock()
	if err != nil {
		return nil, err
	}
	return &ast.ForStmt{Init: init, Cond: cond, Post: post, Body: body, Line: tok.Line}, nil
}

func (p *Parser) parseForInStmt(tok lexer.Token) (ast.Stmt, error) {
	firstName := p.advance() // first ident
	var keyName, valName string

	if p.peek().Type == lexer.TOKEN_COMMA {
		// for key, val in expr
		p.advance() // consume comma
		secondName, err := p.expect(lexer.TOKEN_IDENT)
		if err != nil {
			return nil, err
		}
		keyName = firstName.Literal
		valName = secondName.Literal
	} else {
		// for val in expr
		valName = firstName.Literal
	}

	if _, err := p.expect(lexer.TOKEN_IN); err != nil {
		return nil, err
	}
	iter, err := p.parseExpr()
	if err != nil {
		return nil, err
	}
	body, err := p.parseBlock()
	if err != nil {
		return nil, err
	}
	return &ast.ForInStmt{KeyName: keyName, ValName: valName, Iter: iter, Body: body, Line: tok.Line}, nil
}

func (p *Parser) parseForInit() (ast.Stmt, error) {
	// Could be typed var decl or assignment
	tok := p.peek()
	if isTypeToken(tok.Type) && tok.Type != lexer.TOKEN_IDENT {
		return p.parseVarDeclStmt()
	}
	// ident: could be var decl (ClassName x = ...) or assignment (x = ...)
	if tok.Type == lexer.TOKEN_IDENT && p.pos+1 < len(p.tokens) && p.tokens[p.pos+1].Type == lexer.TOKEN_IDENT {
		return p.parseVarDeclStmt()
	}
	return p.parseExprOrAssignStmt()
}

func (p *Parser) parseForPost() (ast.Stmt, error) {
	expr, err := p.parseExpr()
	if err != nil {
		return nil, err
	}
	tok := p.peek()
	switch tok.Type {
	case lexer.TOKEN_ASSIGN:
		p.advance()
		val, err := p.parseExpr()
		if err != nil {
			return nil, err
		}
		return &ast.AssignStmt{Target: expr, Value: val}, nil
	case lexer.TOKEN_PLUS_ASSIGN, lexer.TOKEN_MINUS_ASSIGN, lexer.TOKEN_STAR_ASSIGN,
		lexer.TOKEN_SLASH_ASSIGN, lexer.TOKEN_PERCENT_ASSIGN:
		p.advance()
		val, err := p.parseExpr()
		if err != nil {
			return nil, err
		}
		return &ast.CompoundAssignStmt{Target: expr, Op: tok.Literal, Value: val, Line: tok.Line}, nil
	case lexer.TOKEN_INC, lexer.TOKEN_DEC:
		p.advance()
		return &ast.IncDecStmt{Target: expr, Op: tok.Literal, Line: tok.Line}, nil
	}
	return &ast.ExprStmt{Expr: expr}, nil
}

func (p *Parser) parseReturnStmt() (*ast.ReturnStmt, error) {
	tok := p.advance() // consume return
	if p.peek().Type == lexer.TOKEN_SEMICOLON {
		p.advance()
		return &ast.ReturnStmt{Line: tok.Line}, nil
	}
	// Check for tuple return: return (expr, expr);
	if p.peek().Type == lexer.TOKEN_LPAREN {
		saved := p.pos
		p.advance() // consume (
		first, err := p.parseExpr()
		if err != nil {
			// backtrack
			p.pos = saved
			goto singleReturn
		}
		if p.peek().Type == lexer.TOKEN_COMMA {
			// It's a tuple return
			exprs := []ast.Expr{first}
			for p.match(lexer.TOKEN_COMMA) {
				e, err := p.parseExpr()
				if err != nil {
					return nil, err
				}
				exprs = append(exprs, e)
			}
			if _, err := p.expect(lexer.TOKEN_RPAREN); err != nil {
				return nil, err
			}
			if _, err := p.expect(lexer.TOKEN_SEMICOLON); err != nil {
				return nil, err
			}
			return &ast.ReturnStmt{Values: []ast.Expr{&ast.TupleExpr{Elems: exprs, Line: tok.Line}}, Line: tok.Line}, nil
		}
		// Not a tuple, backtrack and parse as single expression
		p.pos = saved
	}
singleReturn:
	expr, err := p.parseExpr()
	if err != nil {
		return nil, err
	}
	if _, err := p.expect(lexer.TOKEN_SEMICOLON); err != nil {
		return nil, err
	}
	return &ast.ReturnStmt{Values: []ast.Expr{expr}, Line: tok.Line}, nil
}

func (p *Parser) parseDeferStmt() (*ast.DeferStmt, error) {
	tok := p.advance() // consume defer
	expr, err := p.parseExpr()
	if err != nil {
		return nil, err
	}
	if _, err := p.expect(lexer.TOKEN_SEMICOLON); err != nil {
		return nil, err
	}
	return &ast.DeferStmt{Call: expr, Line: tok.Line}, nil
}

func (p *Parser) parseLocalFnDecl() (ast.Stmt, error) {
	fnDecl, err := p.parseFnDecl(false)
	if err != nil {
		return nil, err
	}
	return &ast.VarStmt{
		Type: &ast.TypeExpr{Name: "fn"},
		Name: fnDecl.Name,
		Init: &ast.FnLitExpr{Decl: fnDecl, Line: fnDecl.Line},
	}, nil
}

func (p *Parser) parseSwitchStmt() (ast.Stmt, error) {
	tok := p.advance() // consume switch
	tag, err := p.parseExpr()
	if err != nil {
		return nil, err
	}
	if _, err := p.expect(lexer.TOKEN_LBRACE); err != nil {
		return nil, err
	}
	var cases []ast.SwitchCase
	for p.peek().Type != lexer.TOKEN_RBRACE && p.peek().Type != lexer.TOKEN_EOF {
		if p.peek().Type == lexer.TOKEN_CASE {
			p.advance()
			var vals []ast.Expr
			v, err := p.parseExpr()
			if err != nil {
				return nil, err
			}
			vals = append(vals, v)
			for p.peek().Type == lexer.TOKEN_COMMA {
				p.advance()
				v, err := p.parseExpr()
				if err != nil {
					return nil, err
				}
				vals = append(vals, v)
			}
			if _, err := p.expect(lexer.TOKEN_COLON); err != nil {
				return nil, err
			}
			body, err := p.parseCaseBody()
			if err != nil {
				return nil, err
			}
			cases = append(cases, ast.SwitchCase{Values: vals, Body: body})
		} else if p.peek().Type == lexer.TOKEN_DEFAULT {
			p.advance()
			if _, err := p.expect(lexer.TOKEN_COLON); err != nil {
				return nil, err
			}
			body, err := p.parseCaseBody()
			if err != nil {
				return nil, err
			}
			cases = append(cases, ast.SwitchCase{Values: nil, Body: body})
		} else {
			return nil, p.errAt(p.peek(), "expected case or default")
		}
	}
	if _, err := p.expect(lexer.TOKEN_RBRACE); err != nil {
		return nil, err
	}
	return &ast.SwitchStmt{Tag: tag, Cases: cases, Line: tok.Line}, nil
}

func (p *Parser) parseCaseBody() ([]ast.Stmt, error) {
	var stmts []ast.Stmt
	for p.peek().Type != lexer.TOKEN_CASE && p.peek().Type != lexer.TOKEN_DEFAULT &&
		p.peek().Type != lexer.TOKEN_RBRACE && p.peek().Type != lexer.TOKEN_EOF {
		s, err := p.parseStmt()
		if err != nil {
			return nil, err
		}
		stmts = append(stmts, s)
	}
	return stmts, nil
}

func (p *Parser) parseConstStmt() (ast.Stmt, error) {
	p.advance() // consume const
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
	return &ast.VarStmt{Type: typ, Name: nameTok.Literal, Init: init, Const: true}, nil
}

func (p *Parser) parseTupleBindOrExprStmt() (ast.Stmt, error) {
	// Try to parse as tuple binding: (type name, type name) = call_expr;
	saved := p.pos
	tok := p.peek()

	p.advance() // consume (
	var bindings []ast.TupleBindItem
	for {
		typ, err := p.parseType()
		if err != nil {
			// Not a tuple binding, backtrack
			p.pos = saved
			return p.parseExprOrAssignStmt()
		}
		nameTok := p.peek()
		var name string
		var discard bool
		if nameTok.Literal == "_" && nameTok.Type == lexer.TOKEN_IDENT {
			discard = true
			name = "_"
			p.advance()
		} else if nameTok.Type == lexer.TOKEN_IDENT {
			name = nameTok.Literal
			p.advance()
		} else {
			// Not a tuple binding
			p.pos = saved
			return p.parseExprOrAssignStmt()
		}
		bindings = append(bindings, ast.TupleBindItem{Type: typ, Name: name, Discard: discard})
		if !p.match(lexer.TOKEN_COMMA) {
			break
		}
	}
	if p.peek().Type != lexer.TOKEN_RPAREN {
		p.pos = saved
		return p.parseExprOrAssignStmt()
	}
	p.advance() // consume )
	if p.peek().Type != lexer.TOKEN_ASSIGN {
		p.pos = saved
		return p.parseExprOrAssignStmt()
	}
	p.advance() // consume =
	val, err := p.parseExpr()
	if err != nil {
		return nil, err
	}
	if _, err := p.expect(lexer.TOKEN_SEMICOLON); err != nil {
		return nil, fmt.Errorf("line %d:%d: expected ; after tuple binding", tok.Line, tok.Col)
	}
	return &ast.TupleBindStmt{Bindings: bindings, Value: val, Line: tok.Line}, nil
}
