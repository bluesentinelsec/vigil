package lexer

import (
	"testing"
)

func TestCarriageReturnEscape(t *testing.T) {
	tests := []struct {
		name     string
		input    string
		expected byte
	}{
		{
			name:     "regular string with \\r",
			input:    `"hello\rworld"`,
			expected: '\r',
		},
		{
			name:     "f-string with \\r",
			input:    `f"hello\rworld"`,
			expected: '\r',
		},
		{
			name:     "CRLF sequence",
			input:    `"line1\r\nline2"`,
			expected: '\r',
		},
		{
			name:     "only \\r",
			input:    `"\r"`,
			expected: '\r',
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			l := New(tt.input)
			tok, err := l.nextToken()
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}

			if tok.Type != TOKEN_STRING && tok.Type != TOKEN_FSTRING {
				t.Fatalf("expected string token, got %v", tok.Type)
			}

			// Check that the string contains a carriage return
			found := false
			for i := 0; i < len(tok.Literal); i++ {
				if tok.Literal[i] == tt.expected {
					found = true
					break
				}
			}

			if !found {
				t.Errorf("expected string to contain carriage return (0x%02x), got: %q", tt.expected, tok.Literal)
			}
		})
	}
}

func TestCarriageReturnInBASL(t *testing.T) {
	// Test that \r works in actual BASL code
	input := `
		import "fmt";
		fn main() -> i32 {
			string crlf = "\r\n";
			if (crlf.len() == 2) {
				return 0;
			}
			return 1;
		}
	`

	l := New(input)
	tokens := []Token{}

	for {
		tok, err := l.nextToken()
		if err != nil {
			t.Fatalf("lexer error: %v", err)
		}
		tokens = append(tokens, tok)
		if tok.Type == TOKEN_EOF {
			break
		}
	}

	// Just verify it lexes without error
	if len(tokens) == 0 {
		t.Error("expected tokens, got none")
	}
}
