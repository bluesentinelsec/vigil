package ast

// TypeExpr represents a type annotation.
type TypeExpr struct {
	Name       string      // "i32", "string", "bool", "err", "void", "f64", "i64", "fn", class name
	ElemType   *TypeExpr   // for array<T>
	KeyType    *TypeExpr   // for map<K,V>
	ValType    *TypeExpr   // for map<K,V>
	ParamTypes []*TypeExpr // for fn(T1, T2) -> R
	ReturnType *TypeExpr   // for fn(T1, T2) -> R
}

// ReturnType can be a single type or a tuple of types (for multi-return).
type ReturnType struct {
	Types []*TypeExpr // len==1 for single, len>1 for multi-return e.g. (i32, err)
}

// --- Top-level declarations ---

type Program struct {
	Decls []Decl
}

type Decl interface{ declNode() }

type ImportDecl struct {
	Path  string // "foo/bar"
	Alias string // "" means use default (last component)
	Line  int
}

type FnDecl struct {
	Pub    bool
	Name   string
	Params []Param
	Return *ReturnType // nil means void
	Body   *Block
	Line   int
}

type Param struct {
	Type *TypeExpr
	Name string
}

type VarDecl struct {
	Pub  bool
	Type *TypeExpr
	Name string
	Init Expr
	Line int
}

type ClassDecl struct {
	Pub        bool
	Name       string
	Implements []string // interface names
	Fields     []ClassField
	Methods    []*FnDecl
	Line       int
}

type ClassField struct {
	Pub  bool
	Type *TypeExpr
	Name string
	Line int
}

func (*ImportDecl) declNode()    {}
func (*FnDecl) declNode()        {}
func (*VarDecl) declNode()       {}
func (*ClassDecl) declNode()     {}
func (*ConstDecl) declNode()     {}
func (*EnumDecl) declNode()      {}
func (*InterfaceDecl) declNode() {}

type InterfaceDecl struct {
	Pub     bool
	Name    string
	Methods []InterfaceMethod
	Line    int
}

type InterfaceMethod struct {
	Name   string
	Params []Param
	Return *ReturnType
	Line   int
}

type ConstDecl struct {
	Pub  bool
	Type *TypeExpr
	Name string
	Init Expr
	Line int
}

type EnumDecl struct {
	Pub      bool
	Name     string
	Variants []EnumVariant
	Line     int
}

type EnumVariant struct {
	Name  string
	Value Expr // nil means auto-increment
}

// --- Statements ---

type Stmt interface{ stmtNode() }

type Block struct {
	Stmts []Stmt
}

type VarStmt struct {
	Type  *TypeExpr
	Name  string
	Init  Expr
	Line  int
	Const bool
}

type AssignStmt struct {
	Target Expr // ident, member access, etc.
	Value  Expr
	Line   int
}

type TupleBindStmt struct {
	Bindings []TupleBindItem
	Value    Expr // must be a call expr
	Line     int
}

type TupleBindItem struct {
	Type    *TypeExpr // type of the binding
	Name    string    // "_" for discard
	Discard bool
}

type IfStmt struct {
	Cond Expr
	Then *Block
	Else Stmt // *Block or *IfStmt (else if) or nil
	Line int
}

type WhileStmt struct {
	Cond Expr
	Body *Block
	Line int
}

type ForStmt struct {
	Init Stmt // VarStmt or AssignStmt
	Cond Expr
	Post Stmt // AssignStmt or ExprStmt
	Body *Block
	Line int
}

type ForInStmt struct {
	KeyName string // "" for arrays (for val in arr), set for maps (for k, v in m)
	ValName string
	Iter    Expr // must evaluate to array or map
	Body    *Block
	Line    int
}

type ReturnStmt struct {
	Values []Expr // 0 for void, 1 for single, or wrapped tuple
	Line   int
}

type ExprStmt struct {
	Expr Expr
	Line int
}

type BreakStmt struct{ Line int }
type ContinueStmt struct{ Line int }

type DeferStmt struct {
	Call Expr // must be a call expression
	Line int
}

func (*Block) stmtNode()              {}
func (*VarStmt) stmtNode()            {}
func (*AssignStmt) stmtNode()         {}
func (*TupleBindStmt) stmtNode()      {}
func (*IfStmt) stmtNode()             {}
func (*WhileStmt) stmtNode()          {}
func (*ForStmt) stmtNode()            {}
func (*ForInStmt) stmtNode()          {}
func (*ReturnStmt) stmtNode()         {}
func (*ExprStmt) stmtNode()           {}
func (*BreakStmt) stmtNode()          {}
func (*ContinueStmt) stmtNode()       {}
func (*DeferStmt) stmtNode()          {}
func (*SwitchStmt) stmtNode()         {}
func (*CompoundAssignStmt) stmtNode() {}
func (*IncDecStmt) stmtNode()         {}

type SwitchStmt struct {
	Tag   Expr // expression being switched on
	Cases []SwitchCase
	Line  int
}

type SwitchCase struct {
	Values []Expr // nil for default
	Body   []Stmt
}

type CompoundAssignStmt struct {
	Target Expr   // ident, member, index
	Op     string // "+=", "-=", "*=", "/=", "%="
	Value  Expr
	Line   int
}

type IncDecStmt struct {
	Target Expr   // ident, member, index
	Op     string // "++" or "--"
	Line   int
}

// --- Expressions ---

type Expr interface{ exprNode() }

type IntLit struct {
	Value int64
	Line  int
}

type FloatLit struct {
	Value float64
	Line  int
}

type StringLit struct {
	Value string
	Line  int
}

type BoolLit struct {
	Value bool
	Line  int
}

type Ident struct {
	Name string
	Line int
}

type SelfExpr struct{ Line int }

type UnaryExpr struct {
	Op      string // "!" or "-"
	Operand Expr
	Line    int
}

type BinaryExpr struct {
	Op    string // "+", "-", "*", "/", "%", "==", "!=", "<", ">", "<=", ">=", "&&", "||"
	Left  Expr
	Right Expr
	Line  int
}

type CallExpr struct {
	Callee Expr
	Args   []Expr
	Line   int
}

type MemberExpr struct {
	Object Expr
	Field  string
	Line   int
}

type IndexExpr struct {
	Object Expr
	Index  Expr
	Line   int
}

type ArrayLit struct {
	Elems []Expr
	Line  int
}

type MapLit struct {
	Keys   []Expr
	Values []Expr
	Line   int
}

// TupleExpr is used in return (val, ok) expressions
type TupleExpr struct {
	Elems []Expr
	Line  int
}

// TypeConvExpr represents explicit type conversions like i32("123"), string(42)
type TypeConvExpr struct {
	Target *TypeExpr
	Arg    Expr
	Line   int
}

// ErrExpr represents err("message") builtin
type ErrExpr struct {
	Msg  Expr
	Line int
}

type FnLitExpr struct {
	Decl *FnDecl
	Line int
}

// FStringExpr represents f"text {expr} text" string interpolation.
type FStringExpr struct {
	Parts []FStringPart // alternating text and expr parts
	Line  int
}

type FStringPart struct {
	IsExpr bool
	Text   string // for literal parts
	Expr   Expr   // for expression parts
	Format string // optional format spec, e.g. ".2f"
}

func (*IntLit) exprNode()       {}
func (*FloatLit) exprNode()     {}
func (*StringLit) exprNode()    {}
func (*BoolLit) exprNode()      {}
func (*Ident) exprNode()        {}
func (*SelfExpr) exprNode()     {}
func (*UnaryExpr) exprNode()    {}
func (*BinaryExpr) exprNode()   {}
func (*CallExpr) exprNode()     {}
func (*MemberExpr) exprNode()   {}
func (*IndexExpr) exprNode()    {}
func (*ArrayLit) exprNode()     {}
func (*MapLit) exprNode()       {}
func (*TupleExpr) exprNode()    {}
func (*TypeConvExpr) exprNode() {}
func (*ErrExpr) exprNode()      {}
func (*FnLitExpr) exprNode()    {}
func (*FStringExpr) exprNode()  {}
