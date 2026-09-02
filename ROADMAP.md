# Roadmap — a statically typed language, compiler, and VM in C++

**Name:** `vex`  ·  **File extension:** `.vx`

> **Rename the directory.** It's currently `ai-compiler`, which is now wrong in both halves.
> ```
> mv ~/ai-compiler ~/vex
> ```

---

## What this is

A complete compiler for a small statically typed imperative language, written from scratch in C++23, plus the virtual machine that runs the output.

```
source.vx → lexer → parser → AST → type checker → bytecode → VM → output
```

No LLVM, no parser generators, no libraries beyond the C++ standard library. Every stage is yours.

**The differentiator is the diagnostics.** Most student compilers print `Parse error at line 12`. Yours prints:

```
error: cannot assign value of type `string` to variable of type `int`
  --> examples/demo.vx:4:11
   |
 3 |   let count: int = 0;
   |       -----   --- declared as `int` here
 4 |   count = "hello";
   |           ^^^^^^^ this is a `string`
   |
help: did you mean to declare a new variable?
   |
 4 |   let count2: string = "hello";
   |   +++
```

That is what people stop and look at.

## Timeline: 8 weeks

Scoped so that **something complete and demoable exists at every checkpoint.** Placements can arrive early; you should never be mid-feature with nothing to show.

## Scope

**In:**
- Types: `int`, `float`, `bool`, `string`, structs, fixed-size arrays
- `let` (immutable) / `var` (mutable), with local inference: `var x = 5` infers `int`
- Top-level functions, parameters, returns, recursion
- `if`/`else`, `while`, `for`, `return`, blocks and lexical scoping
- Arithmetic, comparison, logical, and indexing operators
- Excellent diagnostics with spans, carets, and suggestions

**Out — deliberately, with reasons:**

| Cut | Why |
|---|---|
| **Closures** | Hardest single feature in a small language; 1–2 weeks alone. Top-level functions only. |
| **Garbage collector** | Replaced by arena allocation (bump-allocate, free at exit). No cycles are constructible and programs are short-lived, so a tracing GC isn't needed. This is a design decision to defend, not a gap to apologise for. |
| **Generics** | Type checker complexity explodes. |
| **Modules / imports** | Single file per program. |
| **Native codegen / JIT** | Post-placement extension. Architected for, not built. |

Cuts are not failures. "I scoped X out to guarantee the core was complete" is a stronger answer than a half-finished X.

---

## Two decisions that must happen in week 1

Both are miserable to retrofit and cheap to do upfront.

**1. Source spans on everything.** Every token and every AST node carries `{ start_offset, end_offset }`. A `SourceManager` owns the file text and converts offsets to line/column on demand. If you skip this, the diagnostics goal — the whole differentiator — becomes unreachable by week 5. Do it first.

**2. A golden-file test harness.** A directory of `.vx` programs, each with an expected-output file. One command runs all of them and diffs. Because the pipeline is sequentially gated, a change in the parser can silently break the type checker; the harness is how you find out in seconds instead of days.

---

## Weeks

### Week 1 — Foundations

~~CMake + Ninja skeleton, `git init`, golden-file test harness~~ — **done, scaffolded.**

~~1. `SourceManager` — owns the file text; converts byte offsets to line/column.~~ — **done.**

~~2. `Diagnostic` + its renderer — carets, spans, secondary labels. Prove it with a *hand-constructed* fake error before any lexer exists.~~ — **done.**

~~3. The lexer — now just a source of real spans to feed the renderer.~~ — **done.**

Week 1 is complete: `examples/fib.vx` and `tests/smoke/hello.vx` both tokenise clean, and `tests/lex/bad_char.vx` is a deliberately bad character rendering a properly placed caret end to end.

Doing 2 before 3 is deliberate. If you can render a caret under a hardcoded span, the hard part of the diagnostics work is finished and the lexer becomes an easy win rather than two unfinished things at once.

**Design note — the one thing to get right:** tokens and AST nodes store *byte offsets*, not line/column. A span is two `uint32_t`s: cheap to copy, and every node carries one. Line/column is computed on demand at render time by binary-searching a line-start table built once at load. Computing it eagerly means the lexer counts newlines forever and every token grows a third field.

**Done when:** the lexer tokenises every example file, and a deliberately bad character produces a properly rendered error with a caret in the right column.

### Week 2 — Parser: expressions

~~Recursive descent with precedence climbing for binary operators. AST node definitions with spans. Unary ops, calls, indexing, grouping, literals.~~ — **done.**

Week 2 is complete: `1 + 2 * (3 - 4)` parses to `(+ 1 (* 2 (- 3 4)))`, verified by `dump_expr()` in `tests/unit/parser_test.cpp`, alongside the full precedence ladder, associativity, unary, calls, indexing, grouping, and all four literal kinds.

**Done when:** ~~`1 + 2 * (3 - 4)` parses to a correctly shaped tree, verified by an AST-dumping test.~~

### Week 3 — Parser: statements, and error recovery

~~Declarations, assignment, `if`/`while`/`for`/`return`, blocks, function declarations, struct declarations.~~ — **done.**

~~Then **error recovery**: on a parse error, don't die — record the diagnostic, synchronise to the next statement boundary, and keep going, so one run reports many errors.~~ — **done.**

Week 3 is complete: `examples/fib.vx` parses whole, `main.cpp` now runs source all the way through the parser, and `tests/parse/three_errors.vx` proves the recovery bar end to end — three separate syntax errors in one file, all three reported, no cascade.

**Done when:** ~~a file with three separate syntax errors reports all three, not just the first.~~

### Week 4 — Type checker: core

~~Symbol table with lexical scoping, type representation, expression typing, `let`/`var` inference and mutability enforcement.~~ — **done.**

Week 4 is complete: `examples/fib.vx` type-checks clean end to end through `main.cpp`, and `tests/type/type_errors.vx` proves three independent type errors (a `let`/init mismatch, an assignment to an immutable variable, and a non-bool `if` condition) are all reported with a caret on the specific offending operand, not the enclosing statement. Function-call and array-index checking are explicitly deferred to week 5 — see `include/vex/type_checker.hpp`'s header comment for exactly what that means this week.

**Done when:** ~~type errors in expressions are caught with accurate spans on the *offending operand*, not on the whole statement.~~

### Week 5 — Type checker: completion and diagnostics polish
Function signature checking (arity, argument types, return paths), struct field access, array indexing, and definite-return analysis.

Then the diagnostics pass — this is the week that makes the project memorable. Secondary spans ("declared as `int` here"), `help:` suggestions, and Levenshtein-based "did you mean `count`?" for unknown identifiers.

**Done when:** every error in `tests/errors/` renders with primary span, secondary span where relevant, and a suggestion where one is possible.

### Week 6 — Bytecode compiler
Design the instruction set (stack machine — simpler than a register VM and easier to defend). Constant pool, local slot allocation, and jump patching: emit a jump before you know its target, record the hole, backfill it once you do.

**Done when:** a disassembler prints readable bytecode for every example, and control flow lands on the correct offsets.

### Week 7 — Virtual machine
Dispatch loop, value representation (tagged union), call frames, the arena allocator, and the runtime for strings and arrays.

**Done when:** `fib(30)` prints the right number, and every example program runs end to end. **This is the week the project becomes real.**

### Week 8 — Benchmarks, docs, buffer
Time the VM against CPython on `fib`, loops, and string work — you want a number. README with the pipeline diagram, an error-message showcase, and the design-decision table. A short demo GIF.

Treat this as buffer too. Weeks 3 and 5 tend to overrun.

---

## The three rules

These matter more than any technical choice here, because they're what prevent a repeat of the last project.

**1. Never keep code you can't re-derive.** If you look something up, close the tab and write it from memory. If you can't, you don't have it yet — that's information, not failure.

**2. It runs every day.** If it doesn't run, fixing that is the only task on the board.

**3. Keep `DEVLOG.md`.** One paragraph daily: what broke, why, how you fixed it. In eight weeks you'll have ~40 debugging stories written down, and *"tell me about a hard bug"* becomes the easiest question you get asked instead of the one you dread.

Rule 3 is the one everyone skips. It's also your entire interview prep, accumulated for free.

---

## After placements

The core stands alone. When you want to climb:

1. **Closures** with proper upvalue capture — the classic hard problem.
2. **Mark-sweep GC** — replace the arena, trace roots through the VM stack and call frames.
3. **ARM64 native codegen** — you're on Apple Silicon, so fixed-width 32-bit instructions, far more pleasant to encode than x86-64.
4. **A JIT** — profile hot loops, compile them at runtime, patch them in. Highest ceiling; gives you the benchmark story.

---

## Talking about it

> Wrote a statically typed language and its compiler in C++23 — hand-written lexer, recursive-descent parser with error recovery, type checker, bytecode compiler, and stack VM. Focused on Rust-quality diagnostics: multi-span errors with carets and did-you-mean suggestions. Runs `fib(30)` in Xms; benchmarked against CPython.

Real problem, real technique, a number, and no library did the interesting part.
