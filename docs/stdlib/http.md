# http

HTTP client and server. Client uses a 30-second timeout.

```
import "http";
```

## Client Functions

### http.get(string url) -> (HttpResponse, err)

Sends an HTTP GET request.

- Returns `(response, ok)` on success.
- Returns `(void, err(message, err.io))` on failure (network error, timeout).

```c
HttpResponse resp, err e = http.get("https://example.com/api");
fmt.println(resp.body);
```

### http.post(string url, string body) -> (HttpResponse, err)

Sends an HTTP POST request with the given body.

- Returns `(response, ok)` on success.
- Returns `(void, err(message, err.io))` on failure.

```c
HttpResponse resp, err e = http.post("https://example.com/api", "{\"key\":\"value\"}");
```

### http.request(string method, string url, map\<string,string\> headers, string body) -> (HttpResponse, err)

Sends a custom HTTP request with full control over method, headers, and body.

- Returns `(response, ok)` on success.
- Returns `(void, err(message, err.io))` on failure.

```c
map<string, string> hdrs = {};
hdrs["Authorization"] = "Bearer token123";
hdrs["Content-Type"] = "application/json";
HttpResponse resp, err e = http.request("PUT", "https://example.com/api", hdrs, "{\"x\":1}");
```

## HttpResponse Fields

| Field | Type | Description |
|-------|------|-------------|
| `status` | `i32` | HTTP status code (e.g., 200, 404) |
| `body` | `string` | Response body as a string |
| `headers` | `map<string, string>` | Response headers |

```c
HttpResponse resp, err e = http.get("https://example.com");
fmt.println(resp.status);   // 200
fmt.println(resp.body);     // "<html>..."
```

## Server

### http.listen(string addr, fn handler)

Starts a blocking HTTP server. The handler function receives an `HttpRequest` object and must return an `HttpResponse`-like value.

- `addr` format: `"host:port"` (e.g., `":8080"`, `"0.0.0.0:3000"`).
- This function blocks forever (until the process is killed).
- Error if args are not `(string, fn)`: `"http.listen: expected (string addr, fn handler)"`.

### HttpRequest Fields

| Field | Type | Description |
|-------|------|-------------|
| `method` | `string` | HTTP method (`"GET"`, `"POST"`, etc.) |
| `path` | `string` | URL path (e.g., `"/api/users"`) |
| `query` | `string` | Raw query string (e.g., `"page=1&limit=10"`) |
| `body` | `string` | Request body |
| `headers` | `map<string, string>` | Request headers |
