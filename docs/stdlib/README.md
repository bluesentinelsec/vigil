# BASL Standard Library Reference

All stdlib modules are imported with `import "module_name";`.

Detailed documentation for each module is in `docs/stdlib/`.

## Modules

| Module | Description | Doc |
|--------|-------------|-----|
| [fmt](fmt.md) | Formatted output | `fmt.print`, `fmt.println`, `fmt.sprintf`, `fmt.dollar` |
| [os](os.md) | OS interaction | `os.env`, `os.exec`, `os.cwd`, `os.platform`, `os.exit` |
| [math](math.md) | Math functions | `sqrt`, `pow`, `sin`, `cos`, `floor`, `ceil`, `pi`, `e` |
| [file](file.md) | Filesystem | `read_all`, `write_all`, `open`, `stat`, `mkdir`, `list_dir` |
| [strings](strings.md) | String utilities | `join`, `repeat` |
| [parse](parse.md) | Fallible string parsing | `i32`, `i64`, `f64`, `u8`, `u32`, `u64`, `bool` |
| [time](time.md) | Time operations | `now`, `sleep`, `since`, `format`, `parse` |
| [path](path.md) | Path manipulation | `join`, `dir`, `base`, `ext`, `abs` |
| [log](log.md) | Leveled logging | `debug`, `info`, `warn`, `error`, `fatal`, `set_level` |
| [regex](regex.md) | Regular expressions | `match`, `find`, `find_all`, `replace`, `split` |
| [json](json.md) | JSON parse/stringify | `parse`, `stringify`, `json.Value` methods |
| [xml](xml.md) | XML parsing | `parse`, `xml.Value` tree API |
| [base64](base64.md) | Base64 encoding | `encode`, `decode` |
| [hex](hex.md) | Hex encoding | `encode`, `decode` |
| [csv](csv.md) | CSV parse/stringify | `parse`, `stringify` |
| [sort](sort.md) | Array sorting | `ints`, `strings`, `by` |
| [io](io.md) | Console input | `read_line`, `input`, `read_i32`, `read_f64` |
| [compress](compress.md) | Compression | `gzip`, `gunzip`, `zlib`, `unzlib` |
| [archive](archive.md) | Tar/zip archives | `tar_create`, `tar_extract`, `zip_create`, `zip_extract` |
| [hash](hash.md) | Hash functions | `sha256`, `sha512`, `sha1`, `md5`, `hmac_sha256` |
| [crypto](crypto.md) | Cryptography | AES-256-GCM, RSA encrypt/sign/verify |
| [rand](rand.md) | Secure random | `bytes`, `int` |
| [tcp](tcp.md) | TCP networking | `connect`, `listen`, `TcpConn` |
| [udp](udp.md) | UDP networking | `send`, `listen`, `UdpConn` |
| [http](http.md) | HTTP client/server | `get`, `post`, `request`, `listen` |
| [mime](mime.md) | MIME types | `type_by_ext` |
| [sqlite](sqlite.md) | SQLite database | `open`, `exec`, `query`, `SqliteRows` |
| [args](args.md) | Argument parsing | `parser`, `flag`, `arg`, `parse` |
| [thread](thread.md) | OS threads | `spawn`, `join`, `sleep` |
| [mutex](mutex.md) | Mutual exclusion | `new`, `lock`, `unlock`, `destroy` |
| [test](test.md) | Test framework | `t.assert`, `t.fail`, `basl test` CLI |
| [unsafe](unsafe.md) | Low-level memory | `alloc`, `Buffer`, `Layout`, `Struct`, `null` |
| [ffi](ffi.md) | Native FFI | `load`, `bind`, C interop |
| rl | Raylib bindings | Graphics, audio, input (see raylib source) |
