# Implement Strong Typing in BASL

## Problem

BASL declares types on variables, parameters, and return values but never enforces them.
This compiles and runs without error today:

```
i32 x = "hello";       // no error — x holds a string
string y = 42;          // no error — y holds an i32
fn add(i32 a, i32 b) -> i32 { return "oops"; }  // no error
```

The type annotations are cosmetic. Additionally, any identifier can be used as a type name
(e.g. `auto x = 42` works because the parser treats `auto` as a class name), which means
there is no type safety at all.

## Goal

Every type annotation becomes a runtime contract. Mismatches produce clear errors:

```
error: line 3: type mismatch — expected i32, received string
```

## Design Principles

- Enforcement is at runtime (BASL is interpreted, not compiled)
- No implicit conversions — `i32 x = 3.14` is an error, use explicit `i32(3.14)`
- No `auto` keyword — types must be explicit (transparent, no magic)
- Class types are checked by class name
- `err` type accepts any err value (ok or error)
- `fn` type is loose — any callable (no function signature matching yet)

## Type Compatibility Rules

| Declared Type | Accepts |
|---|---|
| `i32` | `value.TypeI32` only |
| `i64` | `value.TypeI64` only |
| `f64` | `value.TypeF64` only |
| `u8` | `value.TypeU8` only |
| `u32` | `value.TypeU32` only |
| `u64` | `value.TypeU64` only |
| `bool` | `value.TypeBool` only |
| `string` | `value.TypeString` only |
| `void` | `value.TypeVoid` only |
| `err` | `value.TypeErr` only |
| `ptr` | `value.TypePtr` only |
| `array<T>` | `value.TypeArray` (element types checked on insert — Phase 2) |
| `map<K,V>` | `value.TypeMap` (key/value types checked on insert — Phase 2) |
| `fn` | `value.TypeFunc` or `value.TypeNativeFunc` |
| `ClassName` | `value.TypeObject` where `obj.ClassName == ClassName` |

## Implementation Plan

### Phase 1: Core type checking helper

Add a single function that all enforcement points call:

**File: `pkg/basl/interp/typecheck.go`** (new file)

```go
// checkType returns an error if val does not match the declared type.
// declType is the *ast.TypeExpr from the declaration.
// line is for error reporting.
func (interp *Interpreter) checkType(declType *ast.TypeExpr, val value.Value, line int) error
```

Mapping logic:
- Map `declType.Name` to expected `value.Type` using a switch
- For class names (not a builtin type), check `val.T == TypeObject && val.AsObject().ClassName == declType.Name`
- For `array` and `map`, check the container type (element/key/value types deferred to Phase 2)
- Return `nil` on match, descriptive error on mismatch

### Phase 2: Enforce at variable declarations

**File: `pkg/basl/interp/stmts.go`** — `execStmt`, case `*ast.VarStmt`:

```go
case *ast.VarStmt:
    val, err := interp.evalExpr(s.Init, env)
    if err != nil {
        return err
    }
    if err := interp.checkType(s.Type, val, s.Line); err != nil {
        return err
    }
    env.Define(s.Name, val)
```

**File: `pkg/basl/interp/interp.go`** — `execTopDecl`, case `*ast.VarDecl`:

Same pattern — add `checkType` call after evaluating init expression.

### Phase 3: Enforce at assignments

Requires knowing the declared type of a variable. Two options:

**Option A: Store types in Env** (recommended)

Add a `types` map to `Env`:

```go
type Env struct {
    vars   map[string]value.Value
    types  map[string]*ast.TypeExpr  // declared type per variable
    consts map[string]bool
    parent *Env
    defers *[]deferredCall
}
```

Update `Define` to accept an optional type, or add `DefineTyped(name, val, typeExpr)`.
Update `Set` to check type before allowing reassignment.

**Option B: Check at assignment sites only**

Look up the variable's current value type and reject if the new value's type differs.
Simpler but less precise — doesn't catch class name mismatches if both are TypeObject.

Recommendation: Option A. It's more work but correct.

Enforcement in `assign()` (stmts.go):
```go
case *ast.Ident:
    if declType, ok := env.GetType(t.Name); ok && declType != nil {
        if err := interp.checkType(declType, val, t.Line); err != nil {
            return err
        }
    }
```

### Phase 4: Enforce at function parameters

**File: `pkg/basl/interp/exprs.go`** — `callFuncMulti`:

After binding args to params, check each:

```go
for i, p := range fn.Params {
    if i < len(args) {
        // p.Type is a string like "i32" — need to convert to TypeExpr or check directly
        if err := interp.checkParamType(p.Type, args[i], callLine); err != nil {
            return nil, err
        }
        fnEnv.Define(p.Name, args[i])
    }
}
```

Note: `FuncParam.Type` is a `string`, not `*ast.TypeExpr`. Either:
- Add a `checkTypeByName(typeName string, val value.Value, line int) error` variant
- Or change `FuncParam` to store `*ast.TypeExpr` (bigger refactor)

Recommendation: Add `checkTypeByName` — minimal change.

### Phase 5: Enforce at return statements

**File: `pkg/basl/interp/stmts.go`** — `ReturnStmt` handling:

This is tricky because the return statement doesn't know the enclosing function's return type.
Options:
- Store the current function's return type in the interpreter or env
- Add a `currentReturnType` field to `Interpreter` that gets set on function entry

```go
// On function entry (callFuncMulti):
interp.currentReturn = fn.Return  // *ast.ReturnType

// On return:
case *ast.ReturnStmt:
    // ... evaluate values ...
    if interp.currentReturn != nil {
        for i, rt := range interp.currentReturn.Types {
            if i < len(vals) {
                if err := interp.checkType(rt, vals[i], s.Line); err != nil {
                    return err
                }
            }
        }
    }
```

Need a stack for nested calls — use a slice:
```go
type Interpreter struct {
    // ...
    returnTypeStack []*ast.ReturnType
}
```

### Phase 6: Enforce at tuple bindings

**File: `pkg/basl/interp/stmts.go`** — `execTupleBind`:

```go
for i, b := range s.Bindings {
    if !b.Discard {
        if err := interp.checkType(b.Type, vals[i], s.Line); err != nil {
            return err
        }
        env.DefineTyped(b.Name, vals[i], b.Type)
    }
}
```

### Phase 7: Block `auto` as a type name

In `checkType` (or in the parser), reject unknown type names:

```go
if !isBuiltinType(declType.Name) && !interp.isClassName(declType.Name) {
    return fmt.Errorf("line %d: unknown type %q", line, declType.Name)
}
```

This kills `auto` and any other accidental type names.

### Phase 8: Exempt stdlib/native functions

Native functions (`value.TypeNativeFunc`) don't have typed params — they receive
`[]value.Value` and do their own validation. No changes needed for these.

Class `init` methods are called via `constructObject` — params there are `FuncParam`
with type strings, so Phase 4 covers them.

### Phase 9: Fix broken examples

After each phase, run:

```sh
make test
for f in $(find examples/kick_tires -name 'main.basl'); do
    go run ./cmd/basl "$f" < /dev/null 2>&1 | head -3
done
```

Fix any programs that relied on loose typing. Common fixes:
- Add explicit type conversions: `i32(x)`, `f64(x)`, `string(x)`
- Fix mismatched declarations

## Execution Order

| Step | What | Files | Risk |
|---|---|---|---|
| 1 | `checkType` + `checkTypeByName` helpers | new `typecheck.go` | None — unused until wired in |
| 2 | Var declarations (VarStmt + VarDecl) | `stmts.go`, `interp.go` | Medium — will break sloppy code |
| 3 | Add types map to Env, enforce assignments | `interp.go` (Env), `stmts.go` | Medium |
| 4 | Function parameters | `exprs.go` | High — touches every function call |
| 5 | Return statements | `stmts.go`, `exprs.go` | Medium |
| 6 | Tuple bindings | `stmts.go` | Low |
| 7 | Block unknown type names | `typecheck.go` | Low |
| 8 | Fix examples + tests | `examples/`, `*_test.go` | Tedious but safe |

Run `make test` and fix breakage after each step before proceeding.

## Not In Scope (Future Work)

- Generic type checking for `array<T>` elements and `map<K,V>` entries on insert/access
- Function signature types (`fn(i32, i32) -> i32` as a type)
- Interface satisfaction checking at assignment (currently only checked at class declaration)
- Compile-time / static type checking (would require a type-checking pass before execution)
