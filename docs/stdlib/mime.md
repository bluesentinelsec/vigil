# mime

MIME type lookup by file extension.

```
import "mime";
```

## Functions

### mime.type_by_ext(string ext) -> string

Returns the MIME type for a file extension. Automatically prepends `"."` if missing.

- Returns `""` if the extension is not recognized.

```c
string t = mime.type_by_ext(".json");  // "application/json"
string u = mime.type_by_ext("html");   // "text/html; charset=utf-8"
string v = mime.type_by_ext(".png");   // "image/png"
string w = mime.type_by_ext(".xyz");   // ""
```
