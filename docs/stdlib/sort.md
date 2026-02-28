# sort

In-place sorting for arrays.

```
import "sort";
```

## Functions

### sort.ints(array\<i32\> a)

Sorts an array of integers in-place in ascending order.

```c
array<i32> a = [3, 1, 2];
sort.ints(a);
// a = [1, 2, 3]
```

### sort.strings(array\<string\> a)

Sorts an array of strings in-place in lexicographic order.

```c
array<string> a = ["c", "a", "b"];
sort.strings(a);
// a = ["a", "b", "c"]
```

### sort.by(array a, fn comparator)

Sorts an array in-place using a custom comparator function. The comparator receives two elements and must return `bool` — `true` if the first argument should come before the second.

```c
array<i32> a = [3, 1, 2];
sort.by(a, fn(i32 x, i32 y) -> bool { return x > y; });
// a = [3, 2, 1]  (descending)
```
