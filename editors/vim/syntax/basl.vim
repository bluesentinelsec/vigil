" Vim syntax file
" Language: BASL (Blazingly Awesome Scripting Language)
" Maintainer: auto-generated
" Latest Revision: 2026-02-26

if exists("b:current_syntax")
  finish
endif

" Keywords
syn keyword baslKeyword     fn return if else while for break continue
syn keyword baslKeyword     class pub self import as defer in const enum
syn keyword baslKeyword     switch case default interface implements

" Types
syn keyword baslType        bool i32 i64 f64 u8 u32 u64 string void err array map

" Booleans and special values
syn keyword baslBoolean     true false
syn keyword baslConstant    ok

" Numbers
syn match   baslNumber      '\<\d\+\>'
syn match   baslNumber      '\<0[xX][0-9a-fA-F]\+\>'
syn match   baslFloat       '\<\d\+\.\d*\>'
syn match   baslFloat       '\<\d\+[eE][+-]\?\d\+\>'

" Strings
syn region  baslString      start='"' skip='\\"' end='"' contains=baslEscape,baslFmtExpr
syn region  baslRawString   start='`' end='`'
syn match   baslEscape      '\\[nrt\\"]' contained

" F-string expressions: {expr} inside strings
syn region  baslFmtExpr     start='{' end='}' contained contains=TOP

" Comments
syn match   baslComment     '//.*$'
syn region  baslComment     start='/\*' end='\*/'

" Highlight links
hi def link baslKeyword     Statement
hi def link baslType        Type
hi def link baslBoolean     Boolean
hi def link baslConstant    Constant
hi def link baslNumber      Number
hi def link baslFloat       Float
hi def link baslString      String
hi def link baslRawString   String
hi def link baslEscape      SpecialChar
hi def link baslFmtExpr     Special
hi def link baslComment     Comment

let b:current_syntax = "basl"
