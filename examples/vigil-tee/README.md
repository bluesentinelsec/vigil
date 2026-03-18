# vigil-tee

A VIGIL implementation of the Unix `tee` utility.

## Usage

```bash
vigil main.vigil [OPTIONS] [FILE...]
```

Read from stdin and write to stdout and files simultaneously.

### Options

- `-a`, `--append` - Append to files instead of overwriting

### Arguments

- `FILE...` - One or more files to write to

### Examples

```bash
# Write to stdout and one file
echo "test" | vigil main.vigil output.txt

# Write to multiple files
cat input.txt | vigil main.vigil file1.txt file2.txt

# Append mode
echo "more" | vigil main.vigil -a log.txt

# Use in pipeline
ls -la | vigil main.vigil listing.txt | grep ".txt"
```

## Implementation Notes

### Current Limitations

**No chunked stdin reading**: VIGIL currently lacks `io.read(count)` for incremental stdin processing. The implementation uses `io.read_all()`, which buffers the entire input in memory.

**Impact**: 
- Not suitable for very large inputs or infinite streams
- Cannot process data incrementally
- Memory usage scales with input size

**Workaround**: For large inputs, consider processing in chunks externally or using native `tee`.

### What Works

- ✅ Exact byte preservation (no normalization)
- ✅ Multiple output files
- ✅ Append mode (`-a`)
- ✅ Proper error handling and exit codes (including close errors)
- ✅ Cross-platform implementation (standard VIGIL APIs)

### Comparison to Unix tee

| Feature | Unix tee | vigil-tee |
|---------|----------|----------|
| Stdin → stdout | ✅ | ✅ |
| Stdin → files | ✅ | ✅ |
| Multiple files | ✅ | ✅ |
| Append mode | ✅ | ✅ |
| Streaming | ✅ | ❌ (buffers all) |
| Large files | ✅ | ⚠️ (memory limited) |
| Byte preservation | ✅ | ✅ |

## Testing

```bash
vigil test                  # Run all tests (4 tests)
```

Tests validate:
- Single file output
- Multiple file output
- Append mode
- Empty input
- Stdout mirroring
- Path-independent execution (repo root and example directory)

**Note**: Tests use shell redirection (\`sh -c\`) and are skipped on Windows. The implementation itself is cross-platform (uses standard VIGIL APIs), but automated testing currently requires Unix shell.

## Gaps Identified

This implementation revealed a significant stdlib gap:

### Missing: Chunked stdin reading

**Problem**: No `io.read(i32 count)` equivalent for stdin.

**Current APIs**:
- `io.read_all()` - Buffers entire input (unsuitable for streams)
- `io.read_line()` - Strips newlines (unsuitable for byte preservation)

**Impact**: Cannot implement proper streaming utilities like `tee`, `grep`, `sed`, etc.

**Proposed solution**: Add `io.read(i32 count) -> (string, err)` that:
- Reads up to `count` bytes from stdin
- Returns actual bytes read (may be less at EOF)
- Preserves exact bytes (no normalization)
- Matches `File.read()` semantics

This would enable true streaming implementations of Unix filter utilities.

## Success Criteria

✅ **Implemented**: A functional `tee` that handles common use cases
✅ **Documented**: Clear limitations and gaps
✅ **Tested**: 4 tests covering core functionality
❌ **Streaming**: Blocked by missing `io.read(count)` API

The implementation validates that VIGIL can handle multi-destination I/O and file handle ergonomics, but exposes the critical need for chunked stdin reading to support real streaming utilities.
