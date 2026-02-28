# Debugger

BASL includes a built-in interactive debugger for stepping through programs, inspecting variables, and tracing execution.

## Quick Start

```sh
# Step through from the start
basl debug main.basl

# Break at a specific line
basl debug -b 10 main.basl

# Multiple breakpoints
basl debug -b 5 -b 12 -b 20 main.basl
```

## Commands

| Command | Short | Description |
|---------|-------|-------------|
| `continue` | `c` | Run until the next breakpoint |
| `next` | `n` | Step over — execute current line, skip into function calls |
| `step` | `s` | Step into — execute current line, enter function calls |
| `break [line]` | `b` | Set breakpoint at line, or list all breakpoints |
| `delete <line>` | `d` | Remove breakpoint at line |
| `print <var>` | `p` | Print a variable's value |
| `locals` | | Show all variables in the current scope |
| `backtrace` | `bt` | Show the call stack |
| `list [line]` | `l` | Show source code around a line |
| `quit` | `q` | Exit the debugger |
| `help` | `h` | Show command help |

## Stepping

### Step Over (`n`)

Executes the current statement. If it contains a function call, the entire call runs without stopping inside it.

```
→ main.basl:5
      3  fn main() -> i32 {
      4      string name = "world";
→     5      string msg = greet(name);
      6      fmt.println(msg);
      7      return 0;
(basl) n
→ main.basl:6
      4      string name = "world";
      5      string msg = greet(name);
→     6      fmt.println(msg);
      7      return 0;
      8  }
```

### Step Into (`s`)

Executes the current statement. If it contains a function call, stops at the first line inside that function.

```
→ main.basl:5
→     5      string msg = greet(name);
(basl) s
→ main.basl:9
      8  fn greet(string name) -> string {
→     9      return "hello, " + name;
     10  }
```

## Breakpoints

Set breakpoints from the command line with `-b`:

```sh
basl debug -b 10 -b 25 main.basl
```

Or interactively during a session:

```
(basl) b 15
breakpoint set at line 15
(basl) b
  ● line 10
  ● line 15
  ● line 25
(basl) d 15
breakpoint removed at line 15
```

Breakpoints are shown with `●` in the source listing.

## Inspecting State

### Print a variable

```
(basl) p name
name = "world"
(basl) p count
count = 42
```

### Show all locals

```
(basl) locals
  count = 42
  name = "world"
  total = 100
```

### Call stack

```
(basl) bt
  #0 process_item (line 15)
  #1 run_batch (line 8)
  #2 main (line 3)
```

## Behavior

- With no `-b` flags, the debugger steps from the first statement in `main()`
- With `-b` flags, the debugger runs until a breakpoint is hit
- The `→` marker shows the current line (about to execute, not yet executed)
- `n` and `s` execute the marked line, then stop at the next one
- `c` runs until the next breakpoint or program exit
- `q` exits immediately, ending the program

## Source Display

The debugger shows 2 lines of context above and below the current line:

```
→ main.basl:5
      3  fn main() -> i32 {
      4      string name = "world";
→     5      string msg = greet(name);
      6      fmt.println(msg);
      7      return 0;
```

Use `l <line>` to view other parts of the file:

```
(basl) l 20
     15      i32 total = 0;
     16      for (i32 i = 0; i < len(items); i++) {
     17          total = total + items[i];
     18      }
     19      return total;
     20  }
     21
     22  fn main() -> i32 {
     23      array<i32> nums = [1, 2, 3, 4, 5];
     24      i32 sum = add_all(nums);
     25      fmt.println(string(sum));
```

## Threading

The debugger operates under the GIL. When execution pauses at a breakpoint, all threads are paused. This matches the behavior of most scripting language debuggers.
