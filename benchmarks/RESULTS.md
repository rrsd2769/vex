# Benchmark results

`vex` vs. CPython on three programs computing the same thing: `fib(30)`
(call/return overhead), a 50M-iteration `while` sum (dispatch-loop and
local-slot overhead, no calls), and 2M iterations of string-constant
equality (Arena resolution + `Eq`, no concatenation exists in the
language -- see `string_eq.vx`'s header comment). Source for both sides
of each pair lives next to this file; `run.py` runs each program 7 times
(first discarded as warmup), reports the median wall-clock time, and
fails loudly if a pair's stdout disagrees.

Measured 2026-09-02, Apple M4 / macOS 26.6.2, Apple clang 21.0.0,
`vex` built `-O3 -DNDEBUG` (`-DCMAKE_BUILD_TYPE=Release`, no sanitizers),
CPython 3.14.6:

| benchmark   |      vex |   python | ratio             |
|-------------|---------:|---------:|--------------------|
| `fib`       |  ~130 ms |   ~68 ms | Python ~1.9x faster |
| `loop_sum`  | ~1.75 s  | ~2.8 s   | vex ~1.6x faster    |
| `string_eq` |  ~135 ms |  ~200 ms | vex ~1.5x faster    |

Reproduce:

```
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
python3 benchmarks/run.py
```

## Reading these honestly

`vex` wins the two loop-shaped benchmarks and loses the recursive one,
and the loss is worth taking seriously rather than explaining away.
CPython 3.14 shipped a new tail-calling interpreter (and an experimental
JIT) that made function calls specifically much cheaper than in earlier
CPython versions -- `fib` is almost entirely `Call`/`Return` overhead, so
it's exactly the shape of program that change targets. Every earlier
version of CPython this project's author has benchmarked against in the
past was slower than this on recursion; that CPython 3.14 isn't is a
genuinely new fact about the baseline, not a flaw in the comparison.

For `vex` itself, this points at `Call`/`Return` as the place with the
most headroom, not the dispatch loop generally (`loop_sum`, which never
calls a function, already beats CPython). `VM::run()`'s `Call` case
re-fetches `frames_.back()` and does a `push_back` per call (see
`vm.hpp`'s header comment); the natural next things to profile, if this
is ever revisited past week 8's scope, are frame-allocation cost and
whatever a real profiler (not a stopwatch) says about the `Call` case
specifically -- `perf`/Instruments, not another wall-clock number, is
the right tool for finding out *why*, and profiling wasn't attempted
this session.

`string_eq` winning is the least surprising result here: the language
has no runtime string construction at all (documented in `value.hpp`),
so `vex`'s side of that benchmark is doing less real work than it might
look like -- Arena resolution happens once at VM startup
(`resolved_constants_`), not per iteration, and the loop's only per-
iteration string cost is `Eq`'s content comparison. It's an honest
result, just a narrower one than "vex is fast at strings" would suggest.
