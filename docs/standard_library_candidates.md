- Desktop: Windows, macOS, Linux  
- Mobile: Android, iOS  
- Web: Emscripten / Wasm  

The philosophy here is **"get real work done without reaching for crates/npm/pypi/cargo/go get"** for at least 80–90% of everyday application logic, CLI tools, servers, scripts, and many client apps — while still allowing easy extension via the ecosystem.

### Core Language-adjacent (usually builtins or very tightly integrated)
- Rich string & unicode handling (graphemes, normalization, case folding, bidirectional)
- Regex (PCRE2-level or better, with good Unicode support)
- High-quality arbitrary-precision math → BigInt / BigDecimal
- Rational numbers
- Complex numbers
- Good random → cryptographically secure RNG + seeded deterministic PRNG

### Data Structures & Algorithms
- Dynamic arrays / vectors
- Hash maps / hash sets (with good default hasher)
- Ordered maps / ordered sets (tree or skiplist)
- Priority queues / heaps
- Deques / ring buffers
- Immutable persistent data structures (at least HAMT-style maps & vectors)
- Bloom filters, cuckoo filters, count-min sketches (approximate)
- Small-string / small-vector optimizations

### Text & Serialization
- JSON (read/write, streaming, lossless numbers)
- TOML
- YAML (1.2 compliant, safe by default)
- CSV (robust reader/writer)
- MessagePack
- CBOR
- Protobuf-lite support (or at least a very simple binary schema language)
- URL / URI parsing & building (WHATWG compliant)
- MIME type handling & multipart parsing
- HTML5 / XML parser (safe subset + sanitizer)

### Filesystem & OS Abstraction
- Modern path type (cross-platform separator handling, clean/normalize)
- Filesystem operations (copy, move, symlink, hardlink, metadata, watching)
- Directory walking / glob / recursive listing
- Temporary files & directories (guaranteed cleanup)
- Standard locations (config, cache, data, downloads — XDG / Apple / Windows)
- Process spawning + pipes + exit status + environment
- Cross-platform child process groups / job objects

### Networking & HTTP
- TCP / UDP client & server (with happy-eyeballs)
- TLS client & server (modern defaults: TLS 1.3, good cipher suites)
- HTTP/1.1 client (with connection pooling, redirects, cookies)
- HTTP/2 client (strongly desired)
- Async HTTP server (with routing basics)
- WebSocket client & server
- Multipart form data & file uploads
- OAuth2 client flows (PKCE, authorization code, client credentials)
- gRPC client (very desirable in 2025)

### Cryptography
- Hash functions (SHA-2, SHA-3, BLAKE3, xxHash)
- HMAC
- AES-GCM, ChaCha20-Poly1305
- Ed25519 / X25519
- ECDSA (P-256, P-384)
- HKDF, PBKDF2, Argon2id
- Secure random & zeroizing memory

### Date/Time
- Modern timezone-aware datetime type (IANA tz database included or easily loadable)
- Duration / Period
- Relative time formatting ("3 hours ago")
- Parsing most common formats + strftime-style formatting

### Compression & Archives
- gzip / deflate / zlib
- zstd (very high priority in 2025)
- brotli
- lz4
- zip reader/writer (deflate + store)
- tar reader (with ustar/pax support)

### Concurrency & Async
- Threads + thread pool
- Async runtime primitives (promises/futures/tasks)
- Channels (bounded + unbounded)
- Mutex / RwLock / OnceCell
- Atomics
- Cancellation tokens / contexts
- async iterators / streams

### Logging & Diagnostics
- Hierarchical logger (levels: trace/debug/info/warn/error)
- Structured logging (key-value + JSON output)
- Backtraces / stack traces (symbolicated when possible)
- Metrics / counters / histograms (Prometheus exposition format)

### Testing & Benchmarking
- Unit test framework (with fixtures / parameterized)
- Benchmark harness (with statistical analysis)
- Fuzzing primitives / coverage-guided fuzz targets
- Property-based testing helpers

### GUI / Cross-platform UI (optional but very differentiating if included)
- Minimal retained-mode immediate-mode UI toolkit (Dear ImGui style or Flutter-like widgets)
- Or at least window creation, OpenGL/Vulkan/Metal/WebGPU context, input events, clipboard

### Mobile / WASM Specific
- Secure storage (Keychain / Keystore / IndexedDB abstraction)
- Notifications (local & push if feasible)
- Haptics / vibration
- Camera / microphone / location access abstractions
- File picker / share sheet intents
- WASM fetch / WebSocket / Web Workers polyfill layer

### Other High-value "nice-to-have" inclusions
- Command-line argument parser (with subcommands, help, shell completions)
- Color support (ANSI + truecolor detection)
- Progress bars / spinners / rich console output
- Diff / patch algorithms
- UUID / ULID / Snowflake / KSUID generators
- SemVer parsing & comparison
- Basic statistics (mean, median, stddev, quantiles)
- Unit-aware quantities (optional but loved in scientific domains)
- Internationalization basics (plural rules, number formatting)

This list draws inspiration from Python's breadth, Go's practicality, Rust's safety + performance focus, Swift's modernity, Zig's pragmatism, and recent trends toward including UUID, Argon2, zstd, structured logging, etc. directly in the standard library.

A language that ships **most** of the above (especially up through compression, HTTP/2, modern crypto, async I/O, and good datetime) while staying small/fast on embedded/WASM/mobile would be considered **very** competitive in 2026.
