# Source positions are byte offsets into ASCII source

Every Token and syntax node stores a half-open byte Span `[start, end)` rather
than line/column coordinates. Line and column are derived only when rendering a
Diagnostic, by binary-searching a line-start table built once when the Source is
loaded. Source outside strings and comments is restricted to ASCII, so a byte
offset and a display column are the same number.

## Considered options

**Storing line and column on every node** was rejected: it forces the lexer to
count newlines continuously, grows every node by a field, and still requires the
Source text to render a caret — so it pays the cost without avoiding the
dependency.

**Full UTF-8 column handling** was rejected on scope. It needs a decoder in the
rendering path, and the failure mode of getting it wrong — silently misaligned
carets — is invisible to golden-file tests, which compare text and cannot see
that a caret points at the wrong character.

## Consequences

A Span is two 32-bit offsets, cheap enough that every node carries one without
thought.

Offsets are zero-based and displayed positions are one-based, so exactly one
place in the codebase applies the `+1`: the diagnostic renderer. A `+1` anywhere
else is a bug.

The ASCII restriction surfaces as a Diagnostic rather than as silent corruption,
so the limitation is visible to the user instead of producing misaligned output.

Tabs are the one case where offset and display column still diverge — a tab is
one byte but several columns. The renderer expands tabs when building the
display line and computes the caret column against the expanded text.
