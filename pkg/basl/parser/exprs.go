package parser

import (
	"fmt"
	"strconv"
	"strings"

	"github.com/bluesentinelsec/basl/pkg/basl/ast"
	"github.com/bluesentinelsec/basl/pkg/basl/lexer"
)

// Precedence levels (lowest to highest)
func (p *Parser) parseExpr() (ast.Expr, error) {
	return p.parseTernary()
}

func (p *Parser) parseTernary() (ast.Expr, error) {
	condition, err := p.parseOr()
	if err != nil {
		return nil, err
	}

	// Check for ternary operator: condition ? trueExpr : falseExpr
	if p.peek().Type == lexer.TOKEN_QUESTION {
		line := p.advance().Line
		trueExpr, err := p.parseTernary()
		if err != nil {
			return nil, err
		}
		if p.peek().Type != lexer.TOKEN_COLON {
			return nil, fmt.Errorf("line %d: expected ':' in ternary expression", p.peek().Line)
		}
		p.advance() // consume ':'
		falseExpr, err := p.parseTernary()
		if err != nil {
			return nil, err
		}
		return &ast.TernaryExpr{
			Condition: condition,
			TrueExpr:  trueExpr,
			FalseExpr: falseExpr,
			Line:      line,
		}, nil
	}

	return condition, nil
}

func (p *Parser) parseOr() (ast.Expr, error) {
	left, err := p.parseAnd()
	if err != nil {
		return nil, err
	}
	for p.peek().Type == lexer.TOKEN_OR {
		op := p.advance()
		right, err := p.parseAnd()
		if err != nil {
			return nil, err
		}
		left = &ast.BinaryExpr{Op: op.Literal, Left: left, Right: right, Line: op.Line}
	}
	return left, nil
}

func (p *Parser) parseAnd() (ast.Expr, error) {
	left, err := p.parseEquality()
	if err != nil {
		return nil, err
	}
	for p.peek().Type == lexer.TOKEN_AND {
		op := p.advance()
		right, err := p.parseEquality()
		if err != nil {
			return nil, err
		}
		left = &ast.BinaryExpr{Op: op.Literal, Left: left, Right: right, Line: op.Line}
	}
	return left, nil
}

func (p *Parser) parseEquality() (ast.Expr, error) {
	left, err := p.parseBitOr()
	if err != nil {
		return nil, err
	}
	for p.peek().Type == lexer.TOKEN_EQ || p.peek().Type == lexer.TOKEN_NEQ {
		op := p.advance()
		right, err := p.parseBitOr()
		if err != nil {
			return nil, err
		}
		left = &ast.BinaryExpr{Op: op.Literal, Left: left, Right: right, Line: op.Line}
	}
	return left, nil
}

func (p *Parser) parseBitOr() (ast.Expr, error) {
	left, err := p.parseBitXor()
	if err != nil {
		return nil, err
	}
	for p.peek().Type == lexer.TOKEN_PIPE {
		op := p.advance()
		right, err := p.parseBitXor()
		if err != nil {
			return nil, err
		}
		left = &ast.BinaryExpr{Op: op.Literal, Left: left, Right: right, Line: op.Line}
	}
	return left, nil
}

func (p *Parser) parseBitXor() (ast.Expr, error) {
	left, err := p.parseBitAnd()
	if err != nil {
		return nil, err
	}
	for p.peek().Type == lexer.TOKEN_CARET {
		op := p.advance()
		right, err := p.parseBitAnd()
		if err != nil {
			return nil, err
		}
		left = &ast.BinaryExpr{Op: op.Literal, Left: left, Right: right, Line: op.Line}
	}
	return left, nil
}

func (p *Parser) parseBitAnd() (ast.Expr, error) {
	left, err := p.parseShift()
	if err != nil {
		return nil, err
	}
	for p.peek().Type == lexer.TOKEN_AMP {
		op := p.advance()
		right, err := p.parseShift()
		if err != nil {
			return nil, err
		}
		left = &ast.BinaryExpr{Op: op.Literal, Left: left, Right: right, Line: op.Line}
	}
	return left, nil
}

func (p *Parser) parseShift() (ast.Expr, error) {
	left, err := p.parseComparison()
	if err != nil {
		return nil, err
	}
	for p.peek().Type == lexer.TOKEN_SHL || p.peek().Type == lexer.TOKEN_SHR {
		op := p.advance()
		right, err := p.parseComparison()
		if err != nil {
			return nil, err
		}
		left = &ast.BinaryExpr{Op: op.Literal, Left: left, Right: right, Line: op.Line}
	}
	return left, nil
}

func (p *Parser) parseComparison() (ast.Expr, error) {
	left, err := p.parseAddSub()
	if err != nil {
		return nil, err
	}
	for p.peek().Type == lexer.TOKEN_LT || p.peek().Type == lexer.TOKEN_GT ||
		p.peek().Type == lexer.TOKEN_LTE || p.peek().Type == lexer.TOKEN_GTE {
		op := p.advance()
		right, err := p.parseAddSub()
		if err != nil {
			return nil, err
		}
		left = &ast.BinaryExpr{Op: op.Literal, Left: left, Right: right, Line: op.Line}
	}
	return left, nil
}

func (p *Parser) parseAddSub() (ast.Expr, error) {
	left, err := p.parseMulDiv()
	if err != nil {
		return nil, err
	}
	for p.peek().Type == lexer.TOKEN_PLUS || p.peek().Type == lexer.TOKEN_MINUS {
		op := p.advance()
		right, err := p.parseMulDiv()
		if err != nil {
			return nil, err
		}
		left = &ast.BinaryExpr{Op: op.Literal, Left: left, Right: right, Line: op.Line}
	}
	return left, nil
}

func (p *Parser) parseMulDiv() (ast.Expr, error) {
	left, err := p.parseUnary()
	if err != nil {
		return nil, err
	}
	for p.peek().Type == lexer.TOKEN_STAR || p.peek().Type == lexer.TOKEN_SLASH || p.peek().Type == lexer.TOKEN_PERCENT {
		op := p.advance()
		right, err := p.parseUnary()
		if err != nil {
			return nil, err
		}
		left = &ast.BinaryExpr{Op: op.Literal, Left: left, Right: right, Line: op.Line}
	}
	return left, nil
}

func (p *Parser) parseUnary() (ast.Expr, error) {
	if p.peek().Type == lexer.TOKEN_BANG || p.peek().Type == lexer.TOKEN_MINUS || p.peek().Type == lexer.TOKEN_TILDE {
		op := p.advance()
		operand, err := p.parseUnary()
		if err != nil {
			return nil, err
		}
		return &ast.UnaryExpr{Op: op.Literal, Operand: operand, Line: op.Line}, nil
	}
	return p.parsePostfix()
}

func (p *Parser) parsePostfix() (ast.Expr, error) {
	expr, err := p.parsePrimary()
	if err != nil {
		return nil, err
	}
	for {
		switch p.peek().Type {
		case lexer.TOKEN_DOT:
			p.advance()
			field, err := p.expect(lexer.TOKEN_IDENT)
			if err != nil {
				return nil, err
			}
			expr = &ast.MemberExpr{Object: expr, Field: field.Literal, Line: field.Line}
		case lexer.TOKEN_LPAREN:
			p.advance()
			args, err := p.parseArgList()
			if err != nil {
				return nil, err
			}
			if _, err := p.expect(lexer.TOKEN_RPAREN); err != nil {
				return nil, err
			}
			expr = &ast.CallExpr{Callee: expr, Args: args, Line: p.peek().Line}
		case lexer.TOKEN_LBRACKET:
			p.advance()
			idx, err := p.parseExpr()
			if err != nil {
				return nil, err
			}
			if _, err := p.expect(lexer.TOKEN_RBRACKET); err != nil {
				return nil, err
			}
			expr = &ast.IndexExpr{Object: expr, Index: idx, Line: p.peek().Line}
		default:
			return expr, nil
		}
	}
}

func (p *Parser) parseArgList() ([]ast.Expr, error) {
	var args []ast.Expr
	if p.peek().Type == lexer.TOKEN_RPAREN {
		return args, nil
	}
	for {
		e, err := p.parseExpr()
		if err != nil {
			return nil, err
		}
		args = append(args, e)
		if !p.match(lexer.TOKEN_COMMA) {
			break
		}
	}
	return args, nil
}

func (p *Parser) parsePrimary() (ast.Expr, error) {
	tok := p.peek()
	switch tok.Type {
	case lexer.TOKEN_INT:
		p.advance()
		val, err := strconv.ParseInt(tok.Literal, 0, 64)
		if err != nil {
			return nil, p.errAt(tok, fmt.Sprintf("invalid integer: %s", tok.Literal))
		}
		return &ast.IntLit{Value: val, Line: tok.Line}, nil
	case lexer.TOKEN_FLOAT:
		p.advance()
		val, err := strconv.ParseFloat(tok.Literal, 64)
		if err != nil {
			return nil, p.errAt(tok, fmt.Sprintf("invalid float: %s", tok.Literal))
		}
		return &ast.FloatLit{Value: val, Line: tok.Line}, nil
	case lexer.TOKEN_STRING:
		p.advance()
		return &ast.StringLit{Value: tok.Literal, Line: tok.Line}, nil
	case lexer.TOKEN_FSTRING:
		return p.parseFString()
	case lexer.TOKEN_TRUE:
		p.advance()
		return &ast.BoolLit{Value: true, Line: tok.Line}, nil
	case lexer.TOKEN_FALSE:
		p.advance()
		return &ast.BoolLit{Value: false, Line: tok.Line}, nil
	case lexer.TOKEN_OK:
		p.advance()
		return &ast.Ident{Name: "ok", Line: tok.Line}, nil
	case lexer.TOKEN_SELF:
		p.advance()
		return &ast.SelfExpr{Line: tok.Line}, nil
	case lexer.TOKEN_ERR:
		// err("message") builtin or just the identifier 'err' (for comparisons like e != ok)
		p.advance()
		if p.peek().Type == lexer.TOKEN_LPAREN {
			p.advance()
			msg, err := p.parseExpr()
			if err != nil {
				return nil, err
			}
			if _, err := p.expect(lexer.TOKEN_RPAREN); err != nil {
				return nil, err
			}
			return &ast.ErrExpr{Msg: msg, Line: tok.Line}, nil
		}
		// Just the identifier 'err' — shouldn't normally appear as expression
		return &ast.Ident{Name: "err", Line: tok.Line}, nil
	case lexer.TOKEN_IDENT:
		p.advance()
		// Check for 'ok' special identifier
		return &ast.Ident{Name: tok.Literal, Line: tok.Line}, nil
	// Type conversion expressions: i32("123"), string(42), f64("1.5")
	case lexer.TOKEN_I32, lexer.TOKEN_I64, lexer.TOKEN_F64, lexer.TOKEN_STRING_TYPE,
		lexer.TOKEN_U8, lexer.TOKEN_U32, lexer.TOKEN_U64:
		if p.pos+1 < len(p.tokens) && p.tokens[p.pos+1].Type == lexer.TOKEN_LPAREN {
			p.advance() // consume type token
			p.advance() // consume (
			arg, err := p.parseExpr()
			if err != nil {
				return nil, err
			}
			if _, err := p.expect(lexer.TOKEN_RPAREN); err != nil {
				return nil, err
			}
			return &ast.TypeConvExpr{Target: &ast.TypeExpr{Name: tok.Literal}, Arg: arg, Line: tok.Line}, nil
		}
		return nil, p.errAt(tok, fmt.Sprintf("unexpected type keyword '%s' in expression — did you mean to declare a variable? try: %s name = value;", tok.Literal, tok.Literal))
	case lexer.TOKEN_LBRACKET:
		return p.parseArrayLit()
	case lexer.TOKEN_LBRACE:
		return p.parseMapLit()
	case lexer.TOKEN_LPAREN:
		p.advance()
		expr, err := p.parseExpr()
		if err != nil {
			return nil, err
		}
		if _, err := p.expect(lexer.TOKEN_RPAREN); err != nil {
			return nil, err
		}
		return expr, nil
	default:
		return nil, p.errAt(tok, fmt.Sprintf("unexpected %s — expected an expression (variable, literal, or function call)", tok.Type))
	}
}

func (p *Parser) parseArrayLit() (ast.Expr, error) {
	tok := p.advance() // consume [
	var elems []ast.Expr
	if p.peek().Type != lexer.TOKEN_RBRACKET {
		for {
			e, err := p.parseExpr()
			if err != nil {
				return nil, err
			}
			elems = append(elems, e)
			if !p.match(lexer.TOKEN_COMMA) {
				break
			}
		}
	}
	if _, err := p.expect(lexer.TOKEN_RBRACKET); err != nil {
		return nil, err
	}
	return &ast.ArrayLit{Elems: elems, Line: tok.Line}, nil
}

func (p *Parser) parseMapLit() (ast.Expr, error) {
	tok := p.advance() // consume {
	var keys, vals []ast.Expr
	if p.peek().Type != lexer.TOKEN_RBRACE {
		for {
			k, err := p.parseExpr()
			if err != nil {
				return nil, err
			}
			if _, err := p.expect(lexer.TOKEN_COLON); err != nil {
				return nil, err
			}
			v, err := p.parseExpr()
			if err != nil {
				return nil, err
			}
			keys = append(keys, k)
			vals = append(vals, v)
			if !p.match(lexer.TOKEN_COMMA) {
				break
			}
		}
	}
	if _, err := p.expect(lexer.TOKEN_RBRACE); err != nil {
		return nil, err
	}
	return &ast.MapLit{Keys: keys, Values: vals, Line: tok.Line}, nil
}

func (p *Parser) parseFString() (ast.Expr, error) {
	tok := p.advance() // consume FSTRING token
	raw := tok.Literal
	var parts []ast.FStringPart
	i := 0
	for i < len(raw) {
		if raw[i] == 0x00 {
			// Expression part: find matching \x01
			j := i + 1
			for j < len(raw) && raw[j] != 0x01 {
				j++
			}
			chunk := raw[i+1 : j]
			// Split on \x02 for optional format spec
			var exprSrc, fmtSpec string
			if idx := strings.IndexByte(chunk, 0x02); idx >= 0 {
				exprSrc = chunk[:idx]
				fmtSpec = chunk[idx+1:]
			} else {
				exprSrc = chunk
			}
			exprTokens, err := lexer.New(exprSrc).Tokenize()
			if err != nil {
				return nil, fmt.Errorf("line %d: f-string expression error: %s", tok.Line, err)
			}
			subParser := New(exprTokens)
			expr, err := subParser.parseExpr()
			if err != nil {
				return nil, fmt.Errorf("line %d: f-string expression error: %s", tok.Line, err)
			}
			parts = append(parts, ast.FStringPart{IsExpr: true, Expr: expr, Format: fmtSpec})
			i = j + 1 // skip past \x01
		} else {
			// Text part: collect until next \x00 or end
			j := i
			for j < len(raw) && raw[j] != 0x00 {
				j++
			}
			parts = append(parts, ast.FStringPart{Text: raw[i:j]})
			i = j
		}
	}
	return &ast.FStringExpr{Parts: parts, Line: tok.Line}, nil
}
