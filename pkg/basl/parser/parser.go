package parser

import (
	"fmt"

	"github.com/bluesentinelsec/basl/pkg/basl/ast"
	"github.com/bluesentinelsec/basl/pkg/basl/lexer"
)

type Parser struct {
	tokens []lexer.Token
	pos    int
}

func New(tokens []lexer.Token) *Parser {
	return &Parser{tokens: tokens, pos: 0}
}

func (p *Parser) peek() lexer.Token {
	if p.pos >= len(p.tokens) {
		return lexer.Token{Type: lexer.TOKEN_EOF}
	}
	return p.tokens[p.pos]
}

func (p *Parser) advance() lexer.Token {
	tok := p.peek()
	if p.pos < len(p.tokens) {
		p.pos++
	}
	return tok
}

func (p *Parser) expect(tt lexer.TokenType) (lexer.Token, error) {
	tok := p.peek()
	if tok.Type != tt {
		return tok, fmt.Errorf("line %d:%d: expected %s, got %s%s", tok.Line, tok.Col, tt, tok.Type, expectHint(tt, tok))
	}
	return p.advance(), nil
}

func (p *Parser) match(tt lexer.TokenType) bool {
	if p.peek().Type == tt {
		p.advance()
		return true
	}
	return false
}

func (p *Parser) errAt(tok lexer.Token, msg string) error {
	return fmt.Errorf("line %d:%d: %s", tok.Line, tok.Col, msg)
}

// expectHint returns a contextual suggestion for common parser errors.
func expectHint(expected lexer.TokenType, got lexer.Token) string {
	switch {
	case expected == lexer.TOKEN_SEMICOLON:
		return " — try adding ; at end of statement"
	case expected == lexer.TOKEN_LBRACE:
		return " — try adding { to open a block"
	case expected == lexer.TOKEN_RBRACE:
		return " — try adding } to close the block"
	case expected == lexer.TOKEN_LPAREN:
		return " — try adding ( for the parameter list"
	case expected == lexer.TOKEN_RPAREN:
		return " — try adding ) to close the parameter list"
	case expected == lexer.TOKEN_IDENT && got.Type == lexer.TOKEN_OK:
		return " — 'ok' is a reserved keyword and cannot be used as a name"
	case expected == lexer.TOKEN_IDENT && isKeyword(got.Type):
		return fmt.Sprintf(" — '%s' is a reserved keyword and cannot be used as a name", got.Literal)
	case expected == lexer.TOKEN_ASSIGN:
		return " — try adding = to assign a value"
	case expected == lexer.TOKEN_ARROW:
		return " — try adding -> before the return type"
	}
	return ""
}

func isKeyword(tt lexer.TokenType) bool {
	switch tt {
	case lexer.TOKEN_FN, lexer.TOKEN_RETURN, lexer.TOKEN_IF, lexer.TOKEN_ELSE,
		lexer.TOKEN_WHILE, lexer.TOKEN_FOR, lexer.TOKEN_BREAK, lexer.TOKEN_CONTINUE,
		lexer.TOKEN_CLASS, lexer.TOKEN_PUB, lexer.TOKEN_SELF, lexer.TOKEN_IMPORT,
		lexer.TOKEN_AS, lexer.TOKEN_TRUE, lexer.TOKEN_FALSE, lexer.TOKEN_OK,
		lexer.TOKEN_DEFER, lexer.TOKEN_IN, lexer.TOKEN_CONST, lexer.TOKEN_ENUM,
		lexer.TOKEN_SWITCH, lexer.TOKEN_CASE, lexer.TOKEN_DEFAULT,
		lexer.TOKEN_INTERFACE, lexer.TOKEN_IMPLEMENTS:
		return true
	}
	return false
}

// expectGT consumes a > token. If the current token is >> (SHR),
// it splits it: consumes one > and rewrites the token to a single >.
func (p *Parser) expectGT() error {
	tok := p.peek()
	if tok.Type == lexer.TOKEN_GT {
		p.advance()
		return nil
	}
	if tok.Type == lexer.TOKEN_SHR {
		// Rewrite >> into > by advancing and inserting a > token
		p.advance()
		gt := lexer.Token{Type: lexer.TOKEN_GT, Literal: ">", Line: tok.Line, Col: tok.Col + 1}
		// Insert gt before current position
		p.tokens = append(p.tokens[:p.pos], append([]lexer.Token{gt}, p.tokens[p.pos:]...)...)
		return nil
	}
	return fmt.Errorf("line %d:%d: expected >, got %s", tok.Line, tok.Col, tok.Type)
}

func (p *Parser) Parse() (*ast.Program, error) {
	prog := &ast.Program{}
	for p.peek().Type != lexer.TOKEN_EOF {
		d, err := p.parseTopDecl()
		if err != nil {
			return nil, err
		}
		prog.Decls = append(prog.Decls, d)
	}
	return prog, nil
}

// ParseReplLine parses a single REPL input. Returns (decl, nil, nil) for
// declarations, (nil, stmt, nil) for statements/expressions.
func (p *Parser) ParseReplLine() (ast.Decl, ast.Stmt, error) {
	tok := p.peek()
	switch tok.Type {
	case lexer.TOKEN_IMPORT, lexer.TOKEN_FN, lexer.TOKEN_PUB,
		lexer.TOKEN_CLASS, lexer.TOKEN_INTERFACE, lexer.TOKEN_ENUM:
		d, err := p.parseTopDecl()
		return d, nil, err
	case lexer.TOKEN_CONST:
		// const at top level is a decl
		d, err := p.parseTopDecl()
		return d, nil, err
	default:
		s, err := p.parseStmt()
		return nil, s, err
	}
}

func (p *Parser) parseTopDecl() (ast.Decl, error) {
	tok := p.peek()
	switch tok.Type {
	case lexer.TOKEN_IMPORT:
		return p.parseImport()
	case lexer.TOKEN_FN:
		return p.parseFnDecl(false)
	case lexer.TOKEN_PUB:
		return p.parsePubDecl()
	case lexer.TOKEN_CLASS:
		return p.parseClassDecl(false)
	case lexer.TOKEN_INTERFACE:
		return p.parseInterfaceDecl(false)
	case lexer.TOKEN_ENUM:
		return p.parseEnumDecl(false)
	case lexer.TOKEN_CONST:
		return p.parseConstDecl(false)
	default:
		// Could be a top-level var decl: type ident = expr;
		if isTypeToken(tok.Type) {
			return p.parseTopVarDecl(false)
		}
		return nil, p.errAt(tok, fmt.Sprintf("unexpected %s at top level — expected fn, class, interface, enum, const, import, or a variable declaration", tok.Type))
	}
}

func (p *Parser) parsePubDecl() (ast.Decl, error) {
	p.advance() // consume pub
	tok := p.peek()
	switch tok.Type {
	case lexer.TOKEN_FN:
		return p.parseFnDecl(true)
	case lexer.TOKEN_CLASS:
		return p.parseClassDecl(true)
	case lexer.TOKEN_INTERFACE:
		return p.parseInterfaceDecl(true)
	case lexer.TOKEN_ENUM:
		return p.parseEnumDecl(true)
	case lexer.TOKEN_CONST:
		return p.parseConstDecl(true)
	default:
		if isTypeToken(tok.Type) {
			return p.parseTopVarDecl(true)
		}
		return nil, p.errAt(tok, "expected fn, class, interface, enum, const, or type after pub")
	}
}

func (p *Parser) parseImport() (*ast.ImportDecl, error) {
	tok := p.advance() // consume import
	pathTok, err := p.expect(lexer.TOKEN_STRING)
	if err != nil {
		return nil, err
	}
	alias := ""
	if p.peek().Type == lexer.TOKEN_AS {
		p.advance()
		aliasTok, err := p.expect(lexer.TOKEN_IDENT)
		if err != nil {
			return nil, err
		}
		alias = aliasTok.Literal
	}
	if _, err := p.expect(lexer.TOKEN_SEMICOLON); err != nil {
		return nil, err
	}
	return &ast.ImportDecl{Path: pathTok.Literal, Alias: alias, Line: tok.Line}, nil
}

func isTypeToken(tt lexer.TokenType) bool {
	switch tt {
	case lexer.TOKEN_BOOL_TYPE, lexer.TOKEN_I32, lexer.TOKEN_I64, lexer.TOKEN_F64,
		lexer.TOKEN_U8, lexer.TOKEN_U32, lexer.TOKEN_U64,
		lexer.TOKEN_STRING_TYPE, lexer.TOKEN_VOID, lexer.TOKEN_ERR,
		lexer.TOKEN_ARRAY, lexer.TOKEN_MAP, lexer.TOKEN_FN, lexer.TOKEN_IDENT:
		return true
	}
	return false
}
