# vex

vex is a statically typed imperative language: a compiler and bytecode virtual
machine written from scratch. This glossary fixes the vocabulary used across
the pipeline, because compiler terms are heavily overloaded — the same word
routinely means different things at different stages.

## Source and position

**Source**:
The full text of one `.vx` file, owned for the lifetime of a compilation.
_Avoid_: input, buffer, text, file contents

**Offset**:
A byte index into a Source. Zero-based.
_Avoid_: index, position, pos, loc

**Span**:
A half-open byte range `[start, end)` within a Source. Every Token and every
syntax node carries one. An empty Span has `start == end`.
_Avoid_: range, location, extent, region

**Line**, **Column**:
Display coordinates derived from an Offset, computed only when rendering a
Diagnostic. Zero-based in code; converted to one-based at the point of display.
_Avoid_: row, lineno

## Lexical

**Token**:
A classified lexical unit: a TokenKind plus the Span it covers. Carries neither
text nor a parsed value.
_Avoid_: lexeme, symbol, word, atom

**Lexeme**:
The Source text that a Token spans. Distinct from the Token itself, and
obtained from its Span on demand.
_Avoid_: using this as a synonym for Token

**TokenKind**:
The classification of a Token — identifier, literal, keyword, punctuation.
_Avoid_: TokenType, TokenTag, TokenClass

**Literal**:
A Token whose Lexeme denotes a value written directly in Source, such as `42`.
A source-level concept only, and distinct from a Constant.
_Avoid_: constant (at this stage)

## Diagnostics

**Diagnostic**:
One reported problem: a Severity, a message, a primary Label, zero or more
secondary Labels, and an optional Suggestion.
_Avoid_: error, warning, message (as the general term)

**Severity**:
Whether a Diagnostic is an Error or a Warning. There are exactly two — "note"
is not a Severity.
_Avoid_: level, kind

**Label**:
A message attached to a Span. The primary Label marks where the problem is;
secondary Labels supply supporting context, such as where a conflicting
declaration appeared.
_Avoid_: annotation, marker, highlight, note

**Suggestion**:
A proposed edit that would resolve a Diagnostic, expressed as replacement
source.
_Avoid_: fix, quickfix, hint, help

## Semantics

**Symbol**:
A named binding introduced by a declaration — a variable, parameter, function,
or struct. Reserved strictly for this meaning: punctuation Tokens are never
called symbols.
_Avoid_: binding, entry, identifier (for the bound thing)

**Scope**:
A region of the program over which a set of Symbols is visible.
_Avoid_: environment, frame, context

**Type**:
What a Symbol or an expression's value is checked against: one of the
primitive types (`int`, `float`, `bool`, `string`), a struct referenced by
name, a fixed-size array of an element Type (`int[5]`), the `Void`
sentinel (only ever a function's declared-or-absent return type, never a
value's own type), or the checker-internal `Unknown` sentinel meaning
"already reported, or not checkable" -- never a real value's type, and
never itself the source of a mismatch diagnostic, so one bad subexpression
doesn't cascade into a wall of downstream errors.
_Avoid_: kind (for this meaning -- TokenKind and ExprNode's variant are
unrelated "kinds")

**Builtin**:
A callable the language provides without a `fn` declaration -- currently
just `print`. Resolved by name in the type checker before Symbol/Scope
lookup or the function table, since there's no declaration syntax that
could put it in either.
_Avoid_: intrinsic, native function

## Runtime

**Constant**:
An entry in a compiled function's constant pool. Produced from a Literal, but a
distinct object belonging to a later stage.
_Avoid_: literal (at this stage)

**Instruction**:
A single operation in compiled bytecode: an opcode together with its operands.
_Avoid_: op, command, bytecode (for an individual one)

**Opcode**:
The operation an Instruction performs, without its operands -- `OpCode::Add`,
not "Add plus its two popped operands." An Instruction is an Opcode plus
whatever fixed-width operands it encodes (e.g. `Constant`'s pool index,
`Jump`'s target offset).
_Avoid_: op (as a synonym for Instruction -- see Instruction's own _Avoid_)

**Chunk**:
One function's compiled Instructions plus the Constant pool they index into.
Every jump target is an absolute byte offset within its own Chunk.
_Avoid_: bytecode (too general -- a Chunk is one function's worth)

**Slot**:
One fixed-size unit of space on the VM's value stack, addressed by a
frame-relative index. A local variable occupies a contiguous run of slots
whose count ("width") is its Type's size: 1 for a primitive, the sum of a
struct's fields' widths for a Struct, element-width times length for an
Array -- see `bytecode_compiler.hpp`'s header comment on why this needs no
heap for structs or arrays, only for a string's dynamic content.
_Avoid_: register (this is a stack machine, not a register machine),
word (a Slot is a logical unit, not necessarily a machine word)

## Pipeline verbs

**compile**:
The entire Source-to-bytecode pipeline. Never used for a single stage.

**emit**:
Append an Instruction to the bytecode being built.
