# BASL Standard Library Reference

All stdlib modules are imported with `import "module_name";`.

Detailed documentation for each module is in `docs/stdlib/`.

## Modules

| Module | Description | Doc |
|--------|-------------|-----|
| [fmt](stdlib/fmt.md) | Formatted output | `fmt.print`, `fmt.println`, `fmt.sprintf`, `fmt.dollar` |
| [os](stdlib/os.md) | OS interaction | `os.env`, `os.exec`, `os.cwd`, `os.platform`, `os.exit` |
| [math](stdlib/math.md) | Math functions | `sqrt`, `pow`, `sin`, `cos`, `floor`, `ceil`, `pi`, `e` |
| [file](stdlib/file.md) | Filesystem | `read_all`, `write_all`, `open`, `stat`, `mkdir`, `list_dir` |
| [strings](stdlib/strings.md) | String utilities | `join`, `repeat` |
| [time](stdlib/time.md) | Time operations | `now`, `sleep`, `since`, `format`, `parse` |
| [path](stdlib/path.md) | Path manipulation | `join`, `dir`, `base`, `ext`, `abs` |
| [log](stdlib/log.md) | Leveled logging | `debug`, `info`, `warn`, `error`, `fatal`, `set_level` |
| [regex](stdlib/regex.md) | Regular expressions | `match`, `find`, `find_all`, `replace`, `split` |
| [json](stdlib/json.md) | JSON parse/stringify | `parse`, `stringify`, `json.Value` methods |
| [xml](stdlib/xml.md) | XML parsing | `parse`, `xml.Value` tree API |
| [base64](stdlib/base64.md) | Base64 encoding | `encode`, `decode` |
| [hex](stdlib/hex.md) | Hex encoding | `encode`, `decode` |
| [csv](stdlib/csv.md) | CSV parse/stringify | `parse`, `stringify` |
| [sort](stdlib/sort.md) | Array sorting | `ints`, `strings`, `by` |
| [io](stdlib/io.md) | Console input | `read_line`, `input`, `read_i32`, `read_f64` |
| [compress](stdlib/compress.md) | Compression | `gzip`, `gunzip`, `zlib`, `unzlib` |
| [archive](stdlib/archive.md) | Tar/zip archives | `tar_create`, `tar_extract`, `zip_create`, `zip_extract` |
| [hash](stdlib/hash.md) | Hash functions | `sha256`, `sha512`, `sha1`, `md5`, `hmac_sha256` |
| [crypto](stdlib/crypto.md) | Cryptography | AES-256-GCM, RSA encrypt/sign/verify |
| [rand](stdlib/rand.md) | Secure random | `bytes`, `int` |
| [tcp](stdlib/tcp.md) | TCP networking | `connect`, `listen`, `TcpConn` |
| [udp](stdlib/udp.md) | UDP networking | `send`, `listen`, `UdpConn` |
| [http](stdlib/http.md) | HTTP client/server | `get`, `post`, `request`, `listen` |
| [mime](stdlib/mime.md) | MIME types | `type_by_ext` |
| [sqlite](stdlib/sqlite.md) | SQLite database | `open`, `exec`, `query`, `SqliteRows` |
| [args](stdlib/args.md) | Argument parsing | `parser`, `flag`, `arg`, `parse` |
| [thread](stdlib/thread.md) | OS threads | `spawn`, `join`, `sleep` |
| [mutex](stdlib/mutex.md) | Mutual exclusion | `new`, `lock`, `unlock`, `destroy` |
| [test](stdlib/test.md) | Test framework | `t.assert`, `t.fail`, `basl test` CLI |
| [unsafe](stdlib/unsafe.md) | Low-level memory | `alloc`, `Buffer`, `Layout`, `Struct`, `null` |
| [ffi](stdlib/ffi.md) | Native FFI | `load`, `bind`, C interop |
| rl | Raylib bindings | Graphics, audio, input (see raylib source) |
