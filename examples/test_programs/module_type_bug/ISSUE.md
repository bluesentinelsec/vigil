# Type Mismatch: Module-Qualified Class Types Not Resolved Correctly

## Description

When instantiating a class from a module and storing it in a typed variable, BASL's type checker incorrectly treats `module.ClassName` (the type annotation) as different from `ClassName` (the actual type returned by the constructor), even though they refer to the same class.

## Expected Behavior

The following code should work:

```c
import "mymodule";

fn main() -> i32 {
    mymodule.MyClass obj = mymodule.MyClass();
    return 0;
}
```

The type annotation `mymodule.MyClass` should match the type `MyClass` returned by the constructor since they both refer to the same class defined in the `mymodule` module.

## Actual Behavior

Runtime error:
```
error[runtime]: line 7: type mismatch — expected mymodule.MyClass, received MyClass
```

## Minimal Reproduction

**lib/mymodule.basl:**
```c
pub class MyClass {
    pub string value;
    
    fn init() -> void {
        self.value = "hello";
    }
}
```

**main.basl:**
```c
import "mymodule";
import "fmt";

fn main() -> i32 {
    mymodule.MyClass obj = mymodule.MyClass();
    fmt.println(obj.value);
    return 0;
}
```

**Run:**
```bash
basl main.basl
# error[runtime]: line 7: type mismatch — expected mymodule.MyClass, received MyClass
```

## Current Workaround

Use factory functions that return untyped values:

**lib/mymodule.basl:**
```c
pub class MyClass {
    pub string value;
    
    fn init() -> void {
        self.value = "hello";
    }
}

pub fn new_myclass() {
    return MyClass();
}
```

**main.basl:**
```c
import "mymodule";

fn main() -> i32 {
    auto obj = mymodule.new_myclass();  // Works
    return 0;
}
```

## Impact

This forces developers to:
1. Avoid typed variables when working with module classes
2. Create factory functions as workarounds
3. Lose type safety benefits

## Environment

- BASL version: latest (as of 2026-03-01)
- Platform: macOS

## Root Cause (Suspected)

The type resolution system likely doesn't normalize module-qualified type names when comparing them. When a class constructor returns a type, it returns the unqualified name (`MyClass`), but the type annotation uses the qualified name (`mymodule.MyClass`), and these aren't being recognized as equivalent.
