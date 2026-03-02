package lexer

import "fmt"

type Lexer struct {
	input        []byte
	pos          int
	line         int
	col          int
	tokens       []Token
	emitComments bool
}

func New(input string) *Lexer {
	return &Lexer{input: []byte(input), pos: 0, line: 1, col: 1}
}

func (l *Lexer) Tokenize() ([]Token, error) {
	for {
		tok, err := l.nextToken()
		if err != nil {
			return nil, err
		}
		l.tokens = append(l.tokens, tok)
		if tok.Type == TOKEN_EOF {
			break
		}
	}
	return l.tokens, nil
}

// TokenizeWithComments returns all tokens including comments.
func (l *Lexer) TokenizeWithComments() ([]Token, error) {
	l.emitComments = true
	return l.Tokenize()
}

func (l *Lexer) peek() byte {
	if l.pos >= len(l.input) {
		return 0
	}
	return l.input[l.pos]
}

func (l *Lexer) advance() byte {
	ch := l.input[l.pos]
	l.pos++
	if ch == '\n' {
		l.line++
		l.col = 1
	} else {
		l.col++
	}
	return ch
}

func (l *Lexer) skipWhitespace() {
	for l.pos < len(l.input) {
		ch := l.input[l.pos]
		if ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' {
			l.advance()
		} else {
			break
		}
	}
}

func (l *Lexer) skipLineComment() string {
	start := l.pos
	for l.pos < len(l.input) && l.input[l.pos] != '\n' {
		l.advance()
	}
	return string(l.input[start:l.pos])
}

func (l *Lexer) skipBlockComment() (string, error) {
	startLine, startCol := l.line, l.col
	start := l.pos
	l.advance() // skip /
	l.advance() // skip *
	for {
		if l.pos >= len(l.input) {
			return "", fmt.Errorf("%d:%d: unterminated block comment — expected closing */ before end of file", startLine, startCol)
		}
		if l.input[l.pos] == '*' && l.pos+1 < len(l.input) && l.input[l.pos+1] == '/' {
			l.advance()
			l.advance()
			return string(l.input[start:l.pos]), nil
		}
		l.advance()
	}
}

func (l *Lexer) readString() (Token, error) {
	startLine, startCol := l.line, l.col
	l.advance() // skip opening "
	var buf []byte
	for {
		if l.pos >= len(l.input) {
			return Token{}, fmt.Errorf("%d:%d: unterminated string — expected closing \" before end of file", startLine, startCol)
		}
		ch := l.advance()
		if ch == '"' {
			return Token{TOKEN_STRING, string(buf), startLine, startCol}, nil
		}
		if ch == '\\' {
			if l.pos >= len(l.input) {
				return Token{}, fmt.Errorf("%d:%d: unterminated escape sequence in string — expected a character after \\", startLine, startCol)
			}
			esc := l.advance()
			switch esc {
			case 'n':
				buf = append(buf, '\n')
			case 't':
				buf = append(buf, '\t')
			case 'r':
				buf = append(buf, '\r')
			case '\\':
				buf = append(buf, '\\')
			case '"':
				buf = append(buf, '"')
			default:
				return Token{}, fmt.Errorf("%d:%d: unknown escape sequence \\%c — valid escapes are \\n, \\t, \\r, \\\\, \\\"", l.line, l.col, esc)
			}
		} else {
			buf = append(buf, ch)
		}
	}
}

func (l *Lexer) readCharLiteral() (Token, error) {
	startLine, startCol := l.line, l.col
	l.advance() // skip opening '

	if l.pos >= len(l.input) {
		return Token{}, fmt.Errorf("%d:%d: unterminated character literal — expected character after '", startLine, startCol)
	}

	var ch byte
	if l.peek() == '\\' {
		// Escape sequence
		l.advance() // skip \
		if l.pos >= len(l.input) {
			return Token{}, fmt.Errorf("%d:%d: unterminated escape sequence in character literal", startLine, startCol)
		}
		esc := l.advance()
		switch esc {
		case 'n':
			ch = '\n'
		case 't':
			ch = '\t'
		case 'r':
			ch = '\r'
		case '\\':
			ch = '\\'
		case '\'':
			ch = '\''
		default:
			return Token{}, fmt.Errorf("%d:%d: unknown escape sequence \\%c in character literal — valid escapes are \\n, \\t, \\r, \\\\, \\'", startLine, startCol, esc)
		}
	} else {
		ch = l.advance()
	}

	if l.pos >= len(l.input) || l.peek() != '\'' {
		return Token{}, fmt.Errorf("%d:%d: unterminated character literal — expected closing ' after character", startLine, startCol)
	}
	l.advance() // skip closing '

	// Character literals are syntactic sugar for single-character strings
	return Token{TOKEN_STRING, string(ch), startLine, startCol}, nil
}

func (l *Lexer) readRawString() (Token, error) {
	startLine, startCol := l.line, l.col
	l.advance() // consume opening backtick
	var buf []byte
	for l.pos < len(l.input) {
		ch := l.input[l.pos]
		if ch == '`' {
			l.advance()
			return Token{TOKEN_STRING, string(buf), startLine, startCol}, nil
		}
		if ch == '\n' {
			l.line++
			l.col = 0
		}
		buf = append(buf, ch)
		l.advance()
	}
	return Token{}, fmt.Errorf("%d:%d: unterminated raw string — expected closing backtick ` before end of file", startLine, startCol)
}

func (l *Lexer) readFString() (Token, error) {
	startLine, startCol := l.line, l.col
	l.advance() // skip f
	l.advance() // skip "
	var buf []byte
	for {
		if l.pos >= len(l.input) {
			return Token{}, fmt.Errorf("%d:%d: unterminated f-string — expected closing \" before end of file", startLine, startCol)
		}
		ch := l.input[l.pos]
		if ch == '{' {
			if l.pos+1 < len(l.input) && l.input[l.pos+1] == '{' {
				// escaped {{ -> literal {
				buf = append(buf, '{')
				l.advance()
				l.advance()
				continue
			}
			// Expression start — use \x00 as marker
			buf = append(buf, 0x00)
			l.advance() // skip {
			depth := 1
			inStr := false
			foundColon := false
			for l.pos < len(l.input) && depth > 0 {
				c := l.input[l.pos]
				if inStr {
					if c == '\\' {
						buf = append(buf, c)
						l.advance()
						if l.pos < len(l.input) {
							buf = append(buf, l.input[l.pos])
							l.advance()
						}
						continue
					}
					if c == '"' {
						inStr = false
					}
				} else {
					if c == '"' {
						inStr = true
					} else if c == '{' {
						depth++
					} else if c == '}' {
						depth--
						if depth == 0 {
							break
						}
					} else if c == ':' && depth == 1 && !foundColon {
						buf = append(buf, 0x02) // format spec separator
						l.advance()
						foundColon = true
						continue
					}
				}
				buf = append(buf, c)
				l.advance()
			}
			if depth != 0 {
				return Token{}, fmt.Errorf("%d:%d: unterminated { in f-string", startLine, startCol)
			}
			buf = append(buf, 0x01) // expression end marker
			l.advance()             // skip }
			continue
		}
		if ch == '}' {
			if l.pos+1 < len(l.input) && l.input[l.pos+1] == '}' {
				// escaped }} -> literal }
				buf = append(buf, '}')
				l.advance()
				l.advance()
				continue
			}
			return Token{}, fmt.Errorf("%d:%d: unexpected } in f-string — use }} for a literal brace", l.line, l.col)
		}
		if ch == '"' {
			l.advance()
			return Token{TOKEN_FSTRING, string(buf), startLine, startCol}, nil
		}
		if ch == '\\' {
			l.advance()
			if l.pos >= len(l.input) {
				return Token{}, fmt.Errorf("%d:%d: unterminated escape in f-string", startLine, startCol)
			}
			esc := l.input[l.pos]
			switch esc {
			case 'n':
				buf = append(buf, '\n')
			case 't':
				buf = append(buf, '\t')
			case 'r':
				buf = append(buf, '\r')
			case '\\':
				buf = append(buf, '\\')
			case '"':
				buf = append(buf, '"')
			default:
				return Token{}, fmt.Errorf("%d:%d: unknown escape sequence \\%c in f-string — valid escapes are \\n, \\t, \\r, \\\\, \\\"", l.line, l.col, esc)
			}
			l.advance()
			continue
		}
		buf = append(buf, ch)
		l.advance()
	}
}

func (l *Lexer) readNumber() Token {
	startLine, startCol := l.line, l.col
	start := l.pos
	isFloat := false

	// hex
	if l.peek() == '0' && l.pos+1 < len(l.input) && (l.input[l.pos+1] == 'x' || l.input[l.pos+1] == 'X') {
		l.advance()
		l.advance()
		for l.pos < len(l.input) && isHexDigit(l.input[l.pos]) {
			l.advance()
		}
		return Token{TOKEN_INT, string(l.input[start:l.pos]), startLine, startCol}
	}

	// binary
	if l.peek() == '0' && l.pos+1 < len(l.input) && (l.input[l.pos+1] == 'b' || l.input[l.pos+1] == 'B') {
		l.advance()
		l.advance()
		for l.pos < len(l.input) && (l.input[l.pos] == '0' || l.input[l.pos] == '1') {
			l.advance()
		}
		return Token{TOKEN_INT, string(l.input[start:l.pos]), startLine, startCol}
	}

	// octal
	if l.peek() == '0' && l.pos+1 < len(l.input) && (l.input[l.pos+1] == 'o' || l.input[l.pos+1] == 'O') {
		l.advance()
		l.advance()
		for l.pos < len(l.input) && l.input[l.pos] >= '0' && l.input[l.pos] <= '7' {
			l.advance()
		}
		return Token{TOKEN_INT, string(l.input[start:l.pos]), startLine, startCol}
	}

	for l.pos < len(l.input) && isDigit(l.input[l.pos]) {
		l.advance()
	}
	if l.pos < len(l.input) && l.input[l.pos] == '.' {
		// check next char is digit to distinguish from method call
		if l.pos+1 < len(l.input) && isDigit(l.input[l.pos+1]) {
			isFloat = true
			l.advance() // .
			for l.pos < len(l.input) && isDigit(l.input[l.pos]) {
				l.advance()
			}
		}
	}
	// exponent
	if l.pos < len(l.input) && (l.input[l.pos] == 'e' || l.input[l.pos] == 'E') {
		isFloat = true
		l.advance()
		if l.pos < len(l.input) && (l.input[l.pos] == '+' || l.input[l.pos] == '-') {
			l.advance()
		}
		for l.pos < len(l.input) && isDigit(l.input[l.pos]) {
			l.advance()
		}
	}

	tt := TOKEN_INT
	if isFloat {
		tt = TOKEN_FLOAT
	}
	return Token{tt, string(l.input[start:l.pos]), startLine, startCol}
}

func (l *Lexer) readIdent() Token {
	startLine, startCol := l.line, l.col
	start := l.pos
	for l.pos < len(l.input) && isIdentChar(l.input[l.pos]) {
		l.advance()
	}
	lit := string(l.input[start:l.pos])
	return Token{LookupIdent(lit), lit, startLine, startCol}
}

func (l *Lexer) nextToken() (Token, error) {
	l.skipWhitespace()
	if l.pos >= len(l.input) {
		return Token{TOKEN_EOF, "", l.line, l.col}, nil
	}

	ch := l.input[l.pos]
	line, col := l.line, l.col

	// comments
	if ch == '/' {
		if l.pos+1 < len(l.input) {
			next := l.input[l.pos+1]
			if next == '/' {
				cLine, cCol := l.line, l.col
				text := l.skipLineComment()
				if l.emitComments {
					return Token{TOKEN_LINE_COMMENT, text, cLine, cCol}, nil
				}
				return l.nextToken()
			}
			if next == '*' {
				cLine, cCol := l.line, l.col
				text, err := l.skipBlockComment()
				if err != nil {
					return Token{}, err
				}
				if l.emitComments {
					return Token{TOKEN_BLOCK_COMMENT, text, cLine, cCol}, nil
				}
				return l.nextToken()
			}
		}
	}

	// string
	if ch == '"' {
		return l.readString()
	}

	// character literal (single quotes)
	if ch == '\'' {
		return l.readCharLiteral()
	}

	// raw string (backtick)
	if ch == '`' {
		return l.readRawString()
	}

	// number
	if isDigit(ch) {
		return l.readNumber(), nil
	}

	// ident/keyword
	if isIdentStart(ch) {
		// f-string: f"..."
		if ch == 'f' && l.pos+1 < len(l.input) && l.input[l.pos+1] == '"' {
			return l.readFString()
		}
		return l.readIdent(), nil
	}

	// operators and delimiters
	l.advance()
	switch ch {
	case '+':
		if l.pos < len(l.input) && l.input[l.pos] == '=' {
			l.advance()
			return Token{TOKEN_PLUS_ASSIGN, "+=", line, col}, nil
		}
		if l.pos < len(l.input) && l.input[l.pos] == '+' {
			l.advance()
			return Token{TOKEN_INC, "++", line, col}, nil
		}
		return Token{TOKEN_PLUS, "+", line, col}, nil
	case '*':
		if l.pos < len(l.input) && l.input[l.pos] == '=' {
			l.advance()
			return Token{TOKEN_STAR_ASSIGN, "*=", line, col}, nil
		}
		return Token{TOKEN_STAR, "*", line, col}, nil
	case '%':
		if l.pos < len(l.input) && l.input[l.pos] == '=' {
			l.advance()
			return Token{TOKEN_PERCENT_ASSIGN, "%=", line, col}, nil
		}
		return Token{TOKEN_PERCENT, "%", line, col}, nil
	case '^':
		return Token{TOKEN_CARET, "^", line, col}, nil
	case '~':
		return Token{TOKEN_TILDE, "~", line, col}, nil
	case '.':
		return Token{TOKEN_DOT, ".", line, col}, nil
	case '(':
		return Token{TOKEN_LPAREN, "(", line, col}, nil
	case ')':
		return Token{TOKEN_RPAREN, ")", line, col}, nil
	case '{':
		return Token{TOKEN_LBRACE, "{", line, col}, nil
	case '}':
		return Token{TOKEN_RBRACE, "}", line, col}, nil
	case '[':
		return Token{TOKEN_LBRACKET, "[", line, col}, nil
	case ']':
		return Token{TOKEN_RBRACKET, "]", line, col}, nil
	case ',':
		return Token{TOKEN_COMMA, ",", line, col}, nil
	case ';':
		return Token{TOKEN_SEMICOLON, ";", line, col}, nil
	case ':':
		return Token{TOKEN_COLON, ":", line, col}, nil
	case '?':
		return Token{TOKEN_QUESTION, "?", line, col}, nil
	case '-':
		if l.pos < len(l.input) && l.input[l.pos] == '>' {
			l.advance()
			return Token{TOKEN_ARROW, "->", line, col}, nil
		}
		if l.pos < len(l.input) && l.input[l.pos] == '=' {
			l.advance()
			return Token{TOKEN_MINUS_ASSIGN, "-=", line, col}, nil
		}
		if l.pos < len(l.input) && l.input[l.pos] == '-' {
			l.advance()
			return Token{TOKEN_DEC, "--", line, col}, nil
		}
		return Token{TOKEN_MINUS, "-", line, col}, nil
	case '/':
		if l.pos < len(l.input) && l.input[l.pos] == '=' {
			l.advance()
			return Token{TOKEN_SLASH_ASSIGN, "/=", line, col}, nil
		}
		return Token{TOKEN_SLASH, "/", line, col}, nil
	case '=':
		if l.pos < len(l.input) && l.input[l.pos] == '=' {
			l.advance()
			return Token{TOKEN_EQ, "==", line, col}, nil
		}
		return Token{TOKEN_ASSIGN, "=", line, col}, nil
	case '!':
		if l.pos < len(l.input) && l.input[l.pos] == '=' {
			l.advance()
			return Token{TOKEN_NEQ, "!=", line, col}, nil
		}
		return Token{TOKEN_BANG, "!", line, col}, nil
	case '<':
		if l.pos < len(l.input) && l.input[l.pos] == '=' {
			l.advance()
			return Token{TOKEN_LTE, "<=", line, col}, nil
		}
		if l.pos < len(l.input) && l.input[l.pos] == '<' {
			l.advance()
			return Token{TOKEN_SHL, "<<", line, col}, nil
		}
		return Token{TOKEN_LT, "<", line, col}, nil
	case '>':
		if l.pos < len(l.input) && l.input[l.pos] == '=' {
			l.advance()
			return Token{TOKEN_GTE, ">=", line, col}, nil
		}
		if l.pos < len(l.input) && l.input[l.pos] == '>' {
			l.advance()
			return Token{TOKEN_SHR, ">>", line, col}, nil
		}
		return Token{TOKEN_GT, ">", line, col}, nil
	case '&':
		if l.pos < len(l.input) && l.input[l.pos] == '&' {
			l.advance()
			return Token{TOKEN_AND, "&&", line, col}, nil
		}
		return Token{TOKEN_AMP, "&", line, col}, nil
	case '|':
		if l.pos < len(l.input) && l.input[l.pos] == '|' {
			l.advance()
			return Token{TOKEN_OR, "||", line, col}, nil
		}
		return Token{TOKEN_PIPE, "|", line, col}, nil
	}

	return Token{}, fmt.Errorf("%d:%d: unexpected character %q", line, col, ch)
}

func isDigit(ch byte) bool { return ch >= '0' && ch <= '9' }
func isHexDigit(ch byte) bool {
	return isDigit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')
}
func isIdentStart(ch byte) bool {
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_'
}
func isIdentChar(ch byte) bool { return isIdentStart(ch) || isDigit(ch) }
