package interp

import (
	"database/sql"
	"fmt"

	_ "modernc.org/sqlite"

	"github.com/bluesentinelsec/basl/pkg/basl/value"
)

// ── sqlite module ──

func (interp *Interpreter) makeSqliteModule() *Env {
	env := NewEnv(nil)

	env.Define("open", value.NewNativeFunc("sqlite.open", func(args []value.Value) (value.Value, error) {
		if len(args) != 1 || args[0].T != value.TypeString {
			return value.Void, fmt.Errorf("sqlite.open: expected string path")
		}
		db, err := sql.Open("sqlite", args[0].AsString())
		if err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(err.Error(), value.ErrKindIO)}}
		}
		if err := db.Ping(); err != nil {
			return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(err.Error(), value.ErrKindIO)}}
		}
		obj := &value.ObjectVal{
			ClassName: "SqliteDB",
			Fields:    map[string]value.Value{"__db": {T: value.TypeString, Data: db}},
		}
		return value.Void, &MultiReturnVal{Values: []value.Value{{T: value.TypeObject, Data: obj}, value.Ok}}
	}))

	return env
}

func (interp *Interpreter) sqliteDBMethod(obj value.Value, method string, line int) (value.Value, error) {
	o := obj.AsObject()
	db, ok := o.Fields["__db"].Data.(*sql.DB)
	if !ok {
		return value.Void, fmt.Errorf("line %d: SqliteDB is invalid", line)
	}
	switch method {
	case "exec":
		return value.NewNativeFunc("SqliteDB.exec", func(args []value.Value) (value.Value, error) {
			if len(args) < 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("SqliteDB.exec: expected string sql, ...params")
			}
			params := toSqlParams(args[1:])
			_, err := db.Exec(args[0].AsString(), params...)
			if err != nil {
				return value.NewErr(err.Error(), value.ErrKindIO), nil
			}
			return value.Ok, nil
		}), nil
	case "query":
		return value.NewNativeFunc("SqliteDB.query", func(args []value.Value) (value.Value, error) {
			if len(args) < 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("SqliteDB.query: expected string sql, ...params")
			}
			params := toSqlParams(args[1:])
			rows, err := db.Query(args[0].AsString(), params...)
			if err != nil {
				return value.Void, &MultiReturnVal{Values: []value.Value{value.Void, value.NewErr(err.Error(), value.ErrKindIO)}}
			}
			cols, _ := rows.Columns()
			rObj := &value.ObjectVal{
				ClassName: "SqliteRows",
				Fields: map[string]value.Value{
					"__rows": {T: value.TypeString, Data: rows},
					"__cols": {T: value.TypeString, Data: cols},
				},
			}
			return value.Void, &MultiReturnVal{Values: []value.Value{{T: value.TypeObject, Data: rObj}, value.Ok}}
		}), nil
	case "close":
		return value.NewNativeFunc("SqliteDB.close", func(args []value.Value) (value.Value, error) {
			if err := db.Close(); err != nil {
				return value.NewErr(err.Error(), value.ErrKindIO), nil
			}
			return value.Ok, nil
		}), nil
	default:
		return value.Void, fmt.Errorf("line %d: SqliteDB has no method '%s'", line, method)
	}
}

func (interp *Interpreter) sqliteRowsMethod(obj value.Value, method string, line int) (value.Value, error) {
	o := obj.AsObject()
	rows, ok := o.Fields["__rows"].Data.(*sql.Rows)
	if !ok {
		return value.Void, fmt.Errorf("line %d: SqliteRows is invalid", line)
	}
	cols, _ := o.Fields["__cols"].Data.([]string)
	switch method {
	case "next":
		return value.NewNativeFunc("SqliteRows.next", func(args []value.Value) (value.Value, error) {
			return value.NewBool(rows.Next()), nil
		}), nil
	case "get":
		return value.NewNativeFunc("SqliteRows.get", func(args []value.Value) (value.Value, error) {
			if len(args) != 1 || args[0].T != value.TypeString {
				return value.Void, fmt.Errorf("SqliteRows.get: expected string column_name")
			}
			colName := args[0].AsString()
			vals := make([]interface{}, len(cols))
			ptrs := make([]interface{}, len(cols))
			for i := range vals {
				ptrs[i] = &vals[i]
			}
			if err := rows.Scan(ptrs...); err != nil {
				return value.NewString(""), nil
			}
			for i, c := range cols {
				if c == colName {
					return value.NewString(fmt.Sprintf("%v", vals[i])), nil
				}
			}
			return value.NewString(""), nil
		}), nil
	case "close":
		return value.NewNativeFunc("SqliteRows.close", func(args []value.Value) (value.Value, error) {
			rows.Close()
			return value.Ok, nil
		}), nil
	default:
		return value.Void, fmt.Errorf("line %d: SqliteRows has no method '%s'", line, method)
	}
}

func toSqlParams(args []value.Value) []interface{} {
	params := make([]interface{}, len(args))
	for i, a := range args {
		switch a.T {
		case value.TypeString:
			params[i] = a.AsString()
		case value.TypeI32:
			params[i] = int64(a.AsI32())
		case value.TypeI64:
			params[i] = a.AsI64()
		case value.TypeF64:
			params[i] = a.AsF64()
		case value.TypeBool:
			params[i] = a.AsBool()
		default:
			params[i] = fmt.Sprintf("%v", a.Data)
		}
	}
	return params
}

// ── args module ──
