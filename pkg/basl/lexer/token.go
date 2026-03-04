package lexer

import "fmt"

type TokenType int

const (
	// Special
	TOKEN_EOF TokenType = iota
	TOKEN_ILLEGAL

	// Literals
	TOKEN_INT    // 123, 0xFF
	TOKEN_FLOAT  // 1.23, 1e6
	TOKEN_STRING // "..."

	// Identifiers
	TOKEN_IDENT

	// Keywords
	TOKEN_FN
	TOKEN_RETURN
	TOKEN_IF
	TOKEN_ELSE
	TOKEN_WHILE
	TOKEN_FOR
	TOKEN_BREAK
	TOKEN_CONTINUE
	TOKEN_CLASS
	TOKEN_PUB
	TOKEN_SELF
	TOKEN_IMPORT
	TOKEN_AS
	TOKEN_TRUE
	TOKEN_FALSE
	TOKEN_OK
	TOKEN_DEFER
	TOKEN_GUARD
	TOKEN_IN
	TOKEN_CONST
	TOKEN_ENUM
	TOKEN_SWITCH
	TOKEN_CASE
	TOKEN_DEFAULT
	TOKEN_INTERFACE
	TOKEN_IMPLEMENTS

	// Types (used as identifiers but recognized for convenience)
	TOKEN_BOOL_TYPE
	TOKEN_I32
	TOKEN_I64
	TOKEN_F64
	TOKEN_U8
	TOKEN_U32
	TOKEN_U64
	TOKEN_STRING_TYPE
	TOKEN_VOID
	TOKEN_ERR

	// Composite type keywords
	TOKEN_ARRAY // array
	TOKEN_MAP   // map

	// Operators
	TOKEN_PLUS    // +
	TOKEN_MINUS   // -
	TOKEN_STAR    // *
	TOKEN_SLASH   // /
	TOKEN_PERCENT // %
	TOKEN_BANG    // !
	TOKEN_AMP     // &
	TOKEN_PIPE    // |
	TOKEN_SHL     // <<
	TOKEN_SHR     // >>
	TOKEN_ASSIGN  // =
	TOKEN_EQ      // ==
	TOKEN_NEQ     // !=
	TOKEN_LT      // <
	TOKEN_GT      // >
	TOKEN_LTE     // <=
	TOKEN_GTE     // >=
	TOKEN_AND     // &&
	TOKEN_OR      // ||
	TOKEN_DOT     // .

	// Compound assignment
	TOKEN_PLUS_ASSIGN    // +=
	TOKEN_MINUS_ASSIGN   // -=
	TOKEN_STAR_ASSIGN    // *=
	TOKEN_SLASH_ASSIGN   // /=
	TOKEN_PERCENT_ASSIGN // %=
	TOKEN_INC            // ++
	TOKEN_DEC            // --

	// Additional operators
	TOKEN_CARET // ^
	TOKEN_TILDE // ~

	// Delimiters
	TOKEN_LPAREN    // (
	TOKEN_RPAREN    // )
	TOKEN_LBRACE    // {
	TOKEN_RBRACE    // }
	TOKEN_LBRACKET  // [
	TOKEN_RBRACKET  // ]
	TOKEN_COMMA     // ,
	TOKEN_SEMICOLON // ;
	TOKEN_ARROW     // ->
	TOKEN_COLON     // :
	TOKEN_QUESTION  // ?
	TOKEN_FSTRING   // f"..."

	// Comments (only emitted by TokenizeWithComments)
	TOKEN_LINE_COMMENT  // // ...
	TOKEN_BLOCK_COMMENT // /* ... */
)

var tokenNames = map[TokenType]string{
	TOKEN_EOF: "EOF", TOKEN_ILLEGAL: "ILLEGAL",
	TOKEN_INT: "INT", TOKEN_FLOAT: "FLOAT", TOKEN_STRING: "STRING",
	TOKEN_IDENT: "IDENT",
	TOKEN_FN:    "fn", TOKEN_RETURN: "return", TOKEN_IF: "if", TOKEN_ELSE: "else",
	TOKEN_WHILE: "while", TOKEN_FOR: "for", TOKEN_BREAK: "break", TOKEN_CONTINUE: "continue",
	TOKEN_CLASS: "class", TOKEN_PUB: "pub", TOKEN_SELF: "self",
	TOKEN_IMPORT: "import", TOKEN_AS: "as", TOKEN_TRUE: "true", TOKEN_FALSE: "false", TOKEN_OK: "ok",
	TOKEN_DEFER:      "defer",
	TOKEN_GUARD:      "guard",
	TOKEN_IN:         "in",
	TOKEN_CONST:      "const",
	TOKEN_ENUM:       "enum",
	TOKEN_SWITCH:     "switch",
	TOKEN_CASE:       "case",
	TOKEN_DEFAULT:    "default",
	TOKEN_INTERFACE:  "interface",
	TOKEN_IMPLEMENTS: "implements",
	TOKEN_BOOL_TYPE:  "bool", TOKEN_I32: "i32", TOKEN_I64: "i64", TOKEN_F64: "f64",
	TOKEN_U8: "u8", TOKEN_U32: "u32", TOKEN_U64: "u64",
	TOKEN_STRING_TYPE: "string", TOKEN_VOID: "void", TOKEN_ERR: "err",
	TOKEN_ARRAY: "array", TOKEN_MAP: "map",
	TOKEN_PLUS: "+", TOKEN_MINUS: "-", TOKEN_STAR: "*", TOKEN_SLASH: "/", TOKEN_PERCENT: "%",
	TOKEN_BANG: "!", TOKEN_AMP: "&", TOKEN_PIPE: "|", TOKEN_SHL: "<<", TOKEN_SHR: ">>",
	TOKEN_CARET: "^", TOKEN_TILDE: "~",
	TOKEN_PLUS_ASSIGN: "+=", TOKEN_MINUS_ASSIGN: "-=", TOKEN_STAR_ASSIGN: "*=",
	TOKEN_SLASH_ASSIGN: "/=", TOKEN_PERCENT_ASSIGN: "%=",
	TOKEN_INC: "++", TOKEN_DEC: "--",
	TOKEN_ASSIGN: "=", TOKEN_EQ: "==", TOKEN_NEQ: "!=",
	TOKEN_LT: "<", TOKEN_GT: ">", TOKEN_LTE: "<=", TOKEN_GTE: ">=",
	TOKEN_AND: "&&", TOKEN_OR: "||", TOKEN_DOT: ".",
	TOKEN_LPAREN: "(", TOKEN_RPAREN: ")", TOKEN_LBRACE: "{", TOKEN_RBRACE: "}",
	TOKEN_LBRACKET: "[", TOKEN_RBRACKET: "]",
	TOKEN_COMMA: ",", TOKEN_SEMICOLON: ";", TOKEN_ARROW: "->", TOKEN_COLON: ":", TOKEN_QUESTION: "?",
	TOKEN_FSTRING:       "FSTRING",
	TOKEN_LINE_COMMENT:  "LINE_COMMENT",
	TOKEN_BLOCK_COMMENT: "BLOCK_COMMENT",
}

func (t TokenType) String() string {
	if s, ok := tokenNames[t]; ok {
		return s
	}
	return fmt.Sprintf("TokenType(%d)", int(t))
}

type Token struct {
	Type    TokenType
	Literal string
	Line    int
	Col     int
}

func (t Token) String() string {
	return fmt.Sprintf("%s(%q) at %d:%d", t.Type, t.Literal, t.Line, t.Col)
}

var keywords = map[string]TokenType{
	"fn": TOKEN_FN, "return": TOKEN_RETURN, "if": TOKEN_IF, "else": TOKEN_ELSE,
	"while": TOKEN_WHILE, "for": TOKEN_FOR, "break": TOKEN_BREAK, "continue": TOKEN_CONTINUE,
	"class": TOKEN_CLASS, "pub": TOKEN_PUB, "self": TOKEN_SELF,
	"import": TOKEN_IMPORT, "as": TOKEN_AS, "true": TOKEN_TRUE, "false": TOKEN_FALSE, "ok": TOKEN_OK,
	"defer":      TOKEN_DEFER,
	"guard":      TOKEN_GUARD,
	"in":         TOKEN_IN,
	"const":      TOKEN_CONST,
	"enum":       TOKEN_ENUM,
	"switch":     TOKEN_SWITCH,
	"case":       TOKEN_CASE,
	"default":    TOKEN_DEFAULT,
	"interface":  TOKEN_INTERFACE,
	"implements": TOKEN_IMPLEMENTS,
	"bool":       TOKEN_BOOL_TYPE, "i32": TOKEN_I32, "i64": TOKEN_I64, "f64": TOKEN_F64,
	"u8": TOKEN_U8, "u32": TOKEN_U32, "u64": TOKEN_U64,
	"string": TOKEN_STRING_TYPE, "void": TOKEN_VOID, "err": TOKEN_ERR,
	"array": TOKEN_ARRAY, "map": TOKEN_MAP,
}

func LookupIdent(ident string) TokenType {
	if tok, ok := keywords[ident]; ok {
		return tok
	}
	return TOKEN_IDENT
}
