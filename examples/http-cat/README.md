# httpcat - HTTP Debugger CLI

A curl-like HTTP client written in BASL that makes HTTP requests and prints detailed response reports.

## Features

- Support for GET, POST, PUT, DELETE methods
- Custom headers and query parameters
- Request body from command line or JSON file
- Basic authentication
- Response timing and size reporting
- Save response to file
- Clean, deterministic output format

## Usage

```bash
# Simple GET request
basl main.basl https://httpbin.org/get

# POST with JSON data
basl main.basl --method POST --data '{"key":"value"}' https://httpbin.org/post

# POST with JSON file
basl main.basl --method POST --json request.json https://httpbin.org/post

# With basic authentication
basl main.basl --auth user:pass https://httpbin.org/basic-auth/user/pass

# Save response to file
basl main.basl --out response.txt https://httpbin.org/get

# Pretty-print JSON (flag accepted, formatting TBD)
basl main.basl --pretty https://httpbin.org/get
```

## Options

- `--method METHOD` - HTTP method (GET, POST, PUT, DELETE) [default: GET]
- `--data STRING` - Request body
- `--json FILE` - Read JSON from file as body (sets Content-Type)
- `--out FILE` - Save response body to file
- `--pretty` - Pretty-print JSON responses (accepted but not yet implemented)
- `--auth user:pass` - Basic authentication

## Output Format

```
HTTP 200
Time: 123ms
Size: 271 bytes

Headers:
  Content-Type: application/json
  Content-Length: 271
  ...

Body:
{response body}
```

## Testing

Run the test suite:

```bash
basl test
```

## Project Structure

- `main.basl` - CLI entry point with argument parsing
- `lib/httpcat.basl` - Core HTTP client library
- `test/httpcat_test.basl` - Unit tests

## Implementation Notes

This project demonstrates:
- Multi-return error handling patterns
- Typed maps and arrays for headers/params
- String interpolation and formatting
- Module organization with lib/ directory
- Comprehensive unit testing
- Factory functions to work around module type system limitations

All errors are treated as fatal and result in immediate exit with error message.
