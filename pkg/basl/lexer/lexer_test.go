package lexer

import (
	"strings"
	"testing"
)

func tokenTypes(tokens []Token) []TokenType {
	out := make([]TokenType, len(tokens))
	for i, t := range tokens {
		out[i] = t.Type
	}
	return out
}

func TestTokenize_Literals(t *testing.T) {
	tests := []struct {
		name  string
		input string
		want  []TokenType
	}{
		{"int", "42", []TokenType{TOKEN_INT, TOKEN_EOF}},
		{"hex", "0xFF", []TokenType{TOKEN_INT, TOKEN_EOF}},
		{"float", "3.14", []TokenType{TOKEN_FLOAT, TOKEN_EOF}},
		{"float_exp", "1e6", []TokenType{TOKEN_FLOAT, TOKEN_EOF}},
		{"string", `"hello"`, []TokenType{TOKEN_STRING, TOKEN_EOF}},
		{"char", `'a'`, []TokenType{TOKEN_STRING, TOKEN_EOF}},
		{"ident", "foo", []TokenType{TOKEN_IDENT, TOKEN_EOF}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			tokens, err := New(tt.input).Tokenize()
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			got := tokenTypes(tokens)
			if len(got) != len(tt.want) {
				t.Fatalf("got %d tokens, want %d", len(got), len(tt.want))
			}
			for i := range got {
				if got[i] != tt.want[i] {
					t.Errorf("token[%d] = %v, want %v", i, got[i], tt.want[i])
				}
			}
		})
	}
}

func TestTokenize_Keywords(t *testing.T) {
	tests := []struct {
		input string
		want  TokenType
	}{
		{"fn", TOKEN_FN}, {"return", TOKEN_RETURN}, {"if", TOKEN_IF}, {"else", TOKEN_ELSE},
		{"while", TOKEN_WHILE}, {"for", TOKEN_FOR}, {"break", TOKEN_BREAK}, {"continue", TOKEN_CONTINUE},
		{"class", TOKEN_CLASS}, {"pub", TOKEN_PUB}, {"self", TOKEN_SELF},
		{"import", TOKEN_IMPORT}, {"as", TOKEN_AS}, {"true", TOKEN_TRUE}, {"false", TOKEN_FALSE},
		{"defer", TOKEN_DEFER},
		{"bool", TOKEN_BOOL_TYPE}, {"i32", TOKEN_I32}, {"i64", TOKEN_I64}, {"f64", TOKEN_F64},
		{"string", TOKEN_STRING_TYPE}, {"void", TOKEN_VOID}, {"err", TOKEN_ERR},
		{"array", TOKEN_ARRAY}, {"map", TOKEN_MAP},
	}
	for _, tt := range tests {
		t.Run(tt.input, func(t *testing.T) {
			tokens, err := New(tt.input).Tokenize()
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if tokens[0].Type != tt.want {
				t.Errorf("got %v, want %v", tokens[0].Type, tt.want)
			}
		})
	}
}

func TestTokenize_Operators(t *testing.T) {
	tests := []struct {
		input string
		want  TokenType
	}{
		{"+", TOKEN_PLUS}, {"-", TOKEN_MINUS}, {"*", TOKEN_STAR}, {"/", TOKEN_SLASH},
		{"%", TOKEN_PERCENT}, {"!", TOKEN_BANG}, {"=", TOKEN_ASSIGN},
		{"==", TOKEN_EQ}, {"!=", TOKEN_NEQ},
		{"<", TOKEN_LT}, {">", TOKEN_GT}, {"<=", TOKEN_LTE}, {">=", TOKEN_GTE},
		{"&&", TOKEN_AND}, {"||", TOKEN_OR}, {".", TOKEN_DOT},
		{"->", TOKEN_ARROW},
		{"(", TOKEN_LPAREN}, {")", TOKEN_RPAREN},
		{"{", TOKEN_LBRACE}, {"}", TOKEN_RBRACE},
		{"[", TOKEN_LBRACKET}, {"]", TOKEN_RBRACKET},
		{",", TOKEN_COMMA}, {";", TOKEN_SEMICOLON}, {":", TOKEN_COLON},
	}
	for _, tt := range tests {
		t.Run(tt.input, func(t *testing.T) {
			tokens, err := New(tt.input).Tokenize()
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if tokens[0].Type != tt.want {
				t.Errorf("got %v, want %v", tokens[0].Type, tt.want)
			}
		})
	}
}

func TestTokenize_Comments(t *testing.T) {
	tests := []struct {
		name  string
		input string
		want  []TokenType
	}{
		{"line_comment", "42 // ignore\n7", []TokenType{TOKEN_INT, TOKEN_INT, TOKEN_EOF}},
		{"block_comment", "42 /* skip */ 7", []TokenType{TOKEN_INT, TOKEN_INT, TOKEN_EOF}},
		{"only_comment", "// nothing", []TokenType{TOKEN_EOF}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			tokens, err := New(tt.input).Tokenize()
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			got := tokenTypes(tokens)
			if len(got) != len(tt.want) {
				t.Fatalf("got %d tokens, want %d", len(got), len(tt.want))
			}
			for i := range got {
				if got[i] != tt.want[i] {
					t.Errorf("token[%d] = %v, want %v", i, got[i], tt.want[i])
				}
			}
		})
	}
}

func TestTokenize_Strings(t *testing.T) {
	tests := []struct {
		name    string
		input   string
		wantLit string
	}{
		{"simple", `"hello"`, "hello"},
		{"escape_n", `"a\nb"`, "a\nb"},
		{"escape_t", `"a\tb"`, "a\tb"},
		{"escape_backslash", `"a\\b"`, "a\\b"},
		{"escape_quote", `"a\"b"`, "a\"b"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			tokens, err := New(tt.input).Tokenize()
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if tokens[0].Literal != tt.wantLit {
				t.Errorf("got %q, want %q", tokens[0].Literal, tt.wantLit)
			}
		})
	}
}

func TestTokenize_Numbers(t *testing.T) {
	tests := []struct {
		name    string
		input   string
		wantTyp TokenType
		wantLit string
	}{
		{"decimal", "123", TOKEN_INT, "123"},
		{"hex_lower", "0xff", TOKEN_INT, "0xff"},
		{"hex_upper", "0XAB", TOKEN_INT, "0XAB"},
		{"float_dot", "3.14", TOKEN_FLOAT, "3.14"},
		{"float_exp", "1e10", TOKEN_FLOAT, "1e10"},
		{"float_exp_neg", "1e-3", TOKEN_FLOAT, "1e-3"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			tokens, err := New(tt.input).Tokenize()
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if tokens[0].Type != tt.wantTyp {
				t.Errorf("type = %v, want %v", tokens[0].Type, tt.wantTyp)
			}
			if tokens[0].Literal != tt.wantLit {
				t.Errorf("literal = %q, want %q", tokens[0].Literal, tt.wantLit)
			}
		})
	}
}

func TestTokenize_LineTracking(t *testing.T) {
	input := "a\nb\nc"
	tokens, err := New(input).Tokenize()
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	wantLines := []int{1, 2, 3}
	for i, wl := range wantLines {
		if tokens[i].Line != wl {
			t.Errorf("token[%d].Line = %d, want %d", i, tokens[i].Line, wl)
		}
	}
}

func TestTokenize_Errors(t *testing.T) {
	tests := []struct {
		name       string
		input      string
		wantErrSub string
	}{
		{"unexpected_char", "§", "unexpected character"},
		{"unterminated_string", `"hello`, "unterminated string"},
		{"unterminated_block_comment", "/* oops", "unterminated block comment"},
		{"unknown_escape", `"\q"`, "unknown escape"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_, err := New(tt.input).Tokenize()
			if err == nil {
				t.Fatal("expected error, got nil")
			}
			if !strings.Contains(err.Error(), tt.wantErrSub) {
				t.Errorf("error = %q, want substring %q", err.Error(), tt.wantErrSub)
			}
		})
	}
}

func TestCharLiterals(t *testing.T) {
	tests := []struct {
		name    string
		input   string
		want    string
		wantErr bool
	}{
		{"simple", `'a'`, "a", false},
		{"digit", `'5'`, "5", false},
		{"space", `' '`, " ", false},
		{"escape_n", `'\n'`, "\n", false},
		{"escape_t", `'\t'`, "\t", false},
		{"escape_r", `'\r'`, "\r", false},
		{"escape_backslash", `'\\'`, "\\", false},
		{"escape_quote", `'\''`, "'", false},
		{"utf8_2byte", `'é'`, "é", false},
		{"utf8_3byte", `'€'`, "€", false},
		{"utf8_4byte", `'😀'`, "😀", false},
		{"unterminated", `'a`, "", true},
		{"empty", `''`, "", true},
		{"multi_char", `'ab'`, "", true},
		{"invalid_escape", `'\x'`, "", true},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			tokens, err := New(tt.input).Tokenize()
			if tt.wantErr {
				if err == nil {
					t.Errorf("expected error, got none")
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if len(tokens) < 1 {
				t.Fatalf("expected at least 1 token, got %d", len(tokens))
			}
			if tokens[0].Type != TOKEN_STRING {
				t.Errorf("token type = %v, want TOKEN_STRING", tokens[0].Type)
			}
			if tokens[0].Literal != tt.want {
				t.Errorf("token literal = %q, want %q", tokens[0].Literal, tt.want)
			}
		})
	}
}

func TestCharLiteralRawNewline(t *testing.T) {
	// Raw newlines should be rejected in character literals
	input := `'
'`
	_, err := New(input).Tokenize()
	if err == nil {
		t.Errorf("expected error for raw newline in character literal, got none")
	}
}

func TestCharLiteralInExpression(t *testing.T) {
	input := `ch == 'a'`
	tokens, err := New(input).Tokenize()
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	want := []TokenType{TOKEN_IDENT, TOKEN_EQ, TOKEN_STRING, TOKEN_EOF}
	got := tokenTypes(tokens)
	if len(got) != len(want) {
		t.Fatalf("got %d tokens, want %d", len(got), len(want))
	}
	for i := range got {
		if got[i] != want[i] {
			t.Errorf("token[%d] = %v, want %v", i, got[i], want[i])
		}
	}
	if tokens[2].Literal != "a" {
		t.Errorf("char literal = %q, want %q", tokens[2].Literal, "a")
	}
}
