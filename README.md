# vex

A statically typed imperative language: a hand-written lexer, recursive-descent
parser, type checker, bytecode compiler, and stack-based virtual machine, all
written from scratch in C++23. No LLVM, no parser generators, no libraries
beyond the standard library.

```
source.vx -> lexer -> parser -> AST -> type checker -> bytecode -> VM -> output
```

## Why this exists

Most small compilers stop at `parse error at line 12`. The goal here was
diagnostics that look like they came out of `rustc`: multi-span errors,
carets pointing at the exact offending token, secondary spans explaining
*why* something is wrong, and did-you-mean suggestions for typos. See
[Error messages](#error-messages) below for real output, not a mockup.

## Quick start

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/vex examples/fib.vx
```

The Debug build above compiles with ASan and UBSan on and is what this
project is developed against day to day. For timing or anything performance
sensitive, build Release instead:

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
./build-release/vex examples/fib.vx
```

## The pipeline

| Stage | Files | Input -> output |
|---|---|---|
| Lexer | `lexer.hpp/.cpp` | source text -> tokens |
| Parser | `parser.hpp/.cpp` | tokens -> AST, with error recovery so one run can report several syntax errors |
| Type checker | `type_checker.hpp/.cpp` | AST -> typed AST, or diagnostics |
| Bytecode compiler | `bytecode_compiler.hpp/.cpp` | typed AST -> a stack-machine instruction set (`bytecode.hpp`) |
| VM | `vm.hpp/.cpp` | bytecode -> program output, or a runtime diagnostic |

Every stage after the lexer can fail with one or more `Diagnostic`s, all
rendered through the same `render_diagnostic()` (`diagnostic_renderer.hpp`),
so a type error and a division by zero at runtime look like the same kind of
thing to the person reading them, because they are.

## Language at a glance

```
struct Point { x: int, y: int }

fn fib(n: int) -> int {
    if n < 2 { return n; }
    return fib(n - 1) + fib(n - 2);
}

fn main() {
    var total = 0;
    for i in 0..10 {
        total = total + fib(i);
    }
    print(total);
}
```

Types: `int`, `float`, `bool`, `string`, structs, and fixed-size arrays
(`int[5]`). `let` is immutable, `var` is mutable, and `var x = 5` infers
`int` from the initializer. Top-level functions, recursion, `if`/`else`,
`while`, `for i in a..b`, and lexical scoping. No closures, no generics, no
modules, no garbage collector: see [Scope](#scope-and-what-was-cut) for why.

## Error messages

A typo'd identifier gets a Levenshtein-based suggestion:

```
$ ./build/vex tests/errors/did_you_mean_variable.vx
error: undefined variable `counnt`
  --> tests/errors/did_you_mean_variable.vx:4:13
  |
4 |     let x = counnt;
  |             ^^^^^^ not found in this scope

help: did you mean `count`?
```

A type mismatch points at the specific offending expression, not the whole
statement, and shows a secondary span explaining where the conflicting type
came from:

```
$ ./build/vex tests/type/type_errors.vx
error: cannot assign value of type `string` to variable of type `int`
  --> tests/type/type_errors.vx:7:18
  |
7 |     let x: int = "hello";
  |            --- declared as `int` here
7 |     let x: int = "hello";
  |                  ^^^^^^^ this is `string`
```

A runtime failure, like an array index that's only known to be out of
bounds once the program is actually running, goes through the exact same
renderer, not a bare `terminate called after throwing...`:

```
$ ./build/vex some_program.vx
error: array index 5 out of bounds for array of size 3
  --> some_program.vx:4:13
  |
4 |     var x = scores[i];
  |             ^^^^^^^^^ here
```

Runtime failures like this one exit with code `70` (`EX_SOFTWARE`), distinct
from `65` (`EX_DATAERR`) for a program that failed to compile, so the two
failure classes are distinguishable from a shell script or CI job without
parsing stderr. More examples live in `tests/errors/`.

## Design decisions

The choices below came out of actually building each stage, not out of
planning ahead of it. Full reasoning for each lives in the named header's
top comment.

| Decision | Choice | Why |
|---|---|---|
| Source positions | Byte offset pairs on every token and AST node, not line/column | Two `uint32_t`s, cheap to copy everywhere. Line/column is computed on demand at render time; computing it eagerly means the lexer counts newlines forever for no benefit most tokens never need |
| Parse errors | Recover by synchronizing to the next statement boundary and continuing | One run can report every syntax error in a file instead of dying on the first |
| Type-error cascades | An internal `Unknown` type that never itself triggers a mismatch | One bad subexpression doesn't cascade into a wall of downstream errors about the same root cause |
| Instruction set | A stack machine, not a register machine | Simpler to encode, decode, and disassemble; easier to defend given the project's scope |
| Local and struct storage | Locals live directly on the value stack as a run of slots (width equals the type's size); struct construction and array literals compile to no opcode at all | A value's representation is exactly its fields'/elements' slots in evaluation order, which evaluation order already produces for free. No heap needed for structs or arrays, only for a string's dynamic content |
| Field and array addressing | Resolved to a single frame-relative address at compile time, with one dynamic (non-literal-index) component allowed per chain | Keeps addressing O(1) with one instruction instead of a general pointer-arithmetic op. The one documented gap is two dynamic indices in the same chain |
| Call frames | Index-based, always re-fetched at the top of the dispatch loop, never held across a `Call` or `Return` | Both can reallocate the frame or value stack, which would leave a held reference dangling |
| Calling convention | A callee's frame base is `stack.size() - arg_width`; arguments already on the stack become its parameter locals with no copying | Falls straight out of the bytecode's own operand design, discovered rather than planned when the VM was actually built |
| Memory management | A bump allocator (`Arena`) that never frees until the VM exits, not a garbage collector | No cycles are constructible and programs are short-lived; a documented scope cut, not a gap to apologize for |
| Runtime errors | Reported through the same `Diagnostic`/renderer as compile-time errors, with a distinct exit code | Keeps the diagnostics differentiator true at runtime, and lets a caller distinguish "didn't compile" from "compiled and crashed" without parsing stderr |
| Closures | Cut entirely; top-level functions only | The hardest single feature in a small language: one to two weeks alone in a project with eight |
| Generics, modules | Cut entirely | Generics explode type-checker complexity; modules add a resolution pass neither differentiator (diagnostics, a real VM) needed |

## Benchmarks

`vex` versus CPython 3.14 on `fib(30)` (call overhead), a 50M-iteration loop
(dispatch-loop overhead), and 2M string-equality checks:

| benchmark | vex | python | result |
|---|---|---|---|
| `fib` | ~130ms | ~68ms | Python ~1.9x faster |
| `loop_sum` | ~1.75s | ~2.8s | vex ~1.6x faster |
| `string_eq` | ~135ms | ~200ms | vex ~1.5x faster |

`vex` wins both loop-shaped benchmarks and loses the recursive one, and the
full writeup in [`benchmarks/RESULTS.md`](benchmarks/RESULTS.md) says why
honestly rather than picking benchmarks that flatter it: CPython 3.14
shipped a new tail-calling interpreter that made function calls
specifically much cheaper, and `fib` is almost entirely `Call`/`Return`
overhead. Reproduce with `python3 benchmarks/run.py` after building Release.

## Testing

```bash
./tests/run_tests.sh          # golden-file tests: compile and run a .vx file, diff its output
./build/vex_unit_tests         # unit tests for individual stages
```

15 golden-file programs cover the pipeline end to end, including three
deliberately bad ones (a bad character, three syntax errors in one file,
three independent type errors) that prove error recovery actually recovers.
127 unit tests assert against individual stages directly, including a
struct/array-equality compiler limitation and a stack-use-after-scope bug
that ASan caught in the test suite itself before it could hide anywhere
real.

## Scope and what was cut

**In:** `int`/`float`/`bool`/`string`, structs, fixed-size arrays, `let`/`var`
with local inference, top-level functions and recursion, `if`/`while`/`for`,
lexical scoping, and diagnostics with spans, carets, and suggestions.

**Out, deliberately:**

| Cut | Why |
|---|---|
| Closures | Hardest single feature in a small language; top-level functions only |
| Garbage collector | Replaced by arena allocation. No cycles are constructible and programs are short-lived, so a tracing GC isn't needed |
| Generics | Type checker complexity explodes |
| Modules / imports | Single file per program |
| Native codegen / JIT | Architected for (see the `Call`/`Return` calling convention above), not built |

Cuts are not failures: scoping something out to guarantee the core was
complete is a stronger outcome than a half-finished version of it.

## Project layout

```
include/vex/    public headers, one per stage, each with a header comment
                explaining that stage's design calls and why
src/            implementation
tests/
  unit/         per-stage unit tests (127 tests)
  lex/ parse/ type/ bytecode/ errors/ smoke/
                golden-file .vx programs plus their expected output
benchmarks/     vex vs. CPython programs, a runner, and RESULTS.md
examples/       fib.vx, the project's own "done when" target program
CONTEXT.md      glossary fixing the vocabulary used across the pipeline
ROADMAP.md      the week-by-week plan this project was built against
```
