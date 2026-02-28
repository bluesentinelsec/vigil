# xml

XML parsing with a DOM-like tree API. Parsed nodes are wrapped in `xml.Value` objects.

```
import "xml";
```

## Functions

### xml.parse(string s) -> (xml.Value, err)

Parses an XML string and returns the first root element.

- Returns `(root, ok)` on success.
- Returns `(void, err("empty XML"))` if the input has no elements.
- Returns `(void, err(message))` on malformed XML.
- Internal `__text` nodes are created for text content but filtered from `children()`.

```c
xml.Value root, err e = xml.parse("<book><title>Hello</title></book>");
```

## xml.Value Methods

### v.tag() -> string

Returns the element's tag name.

```c
string t = root.tag();  // "book"
```

### v.text() -> string

Returns the text content of the element. Checks the node's own text first, then looks for the first `__text` child node. Returns `""` if no text content.

```c
xml.Value title, err e = root.find_one("title");
string t = title.text();  // "Hello"
```

### v.attr(string name) -> (string, bool)

Returns an attribute value.

- Returns `(value, true)` if the attribute exists.
- Returns `("", false)` if the attribute does not exist.

```c
xml.Value item, err e = xml.parse("<item id=\"42\" />");
string id, bool found = item.attr("id");      // "42", true
string x, bool missing = item.attr("nope");   // "", false
```

### v.children() -> array\<xml.Value\>

Returns all child elements, excluding internal `__text` nodes.

```c
array<xml.Value> kids = root.children();
```

### v.find(string tag) -> array\<xml.Value\>

Returns all direct children matching the given tag name.

```c
array<xml.Value> items = root.find("item");
```

### v.find_one(string tag) -> (xml.Value, err)

Returns the first direct child matching the given tag name.

- Returns `(child, ok)` if found.
- Returns `(void, err("not found: TAG"))` if no match.

```c
xml.Value title, err e = root.find_one("title");
```

### v.len() -> i32

Returns the number of child elements (excluding `__text` nodes).

```c
i32 count = root.len();
```
