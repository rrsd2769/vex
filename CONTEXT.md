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
name, or the checker-internal `Unknown` sentinel meaning "already reported,
or not yet checkable" -- never a real value's type, and never itself the
source of a mismatch diagnostic, so one bad subexpression doesn't cascade
into a wall of downstream errors.
_Avoid_: kind (for this meaning -- TokenKind and ExprNode's variant are
unrelated "kinds")

## Runtime

**Constant**:
An entry in a compiled function's constant pool. Produced from a Literal, but a
distinct object belonging to a later stage.
_Avoid_: literal (at this stage)

**Instruction**:
A single operation in compiled bytecode: an opcode together with its operands.
_Avoid_: op, command, bytecode (for an individual one)

## Pipeline verbs

**compile**:
The entire Source-to-bytecode pipeline. Never used for a single stage.

**emit**:
Append an Instruction to the bytecode being built.
