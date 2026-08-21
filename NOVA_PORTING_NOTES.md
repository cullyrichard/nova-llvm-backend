# Porting the Eclipse LLVM backend to the base Data General Nova

This package extends the sibling `eclipse-llvm-backend` project (a real
LLVM/Clang backend for the Data General Eclipse S/140) to also target the
base Data General Nova (`nova1`/`nova3`/`nova4`, as recognized by
`dgasm`'s own `-t` flag). This document is the record of what's actually
different between Nova and Eclipse from this backend's point of view,
what was verified and how, and what wasn't verified.

## The question this started from

"Does the Eclipse compiler also work for Nova targets?" The honest
answer, established empirically rather than assumed: **almost entirely
yes, with one real gap.**

The backend's own design (see `eclipse-llvm-backend`'s copy of
`README.md`) already targets the *shared* Nova/Eclipse base ISA — 4
accumulators, 16-bit words, no immediate operands, the same `ADD`/`SUB`/
skip-and-branch idioms — not anything Eclipse-specific. `dgasm` itself
recognizes `nova1`, `nova3`, and `nova4` as CPU targets alongside
`eclipse_s140`.

Confirmed by compiling several real example programs and assembling the
*exact same* generated output against all four `dgasm -t` targets: every
instruction this backend emits — `ADD`/`SUB`/`AND`/`COM`/`NEG`/`MOV`/
`DSZ`/`ISZ`/`JMP`/`JSR`/`LDA`/`MOVL#`/`MOV#`/`SUB#`/`SUBZ`/`DIV`/`MUL`/
device I/O — assembles unchanged on `nova1`/`nova3`/`nova4`, **except
two: `IOR` (bitwise OR) and `XOR`**. `dgasm` accepts both for
`eclipse_s140` and rejects both as "unrecognised instruction" for all
three Nova CPUs.

## The fix

A new subtarget feature, `FeatureEIS` ("Extended Instruction Set" — see
`Eclipse.td`), is `true` only for CPU `eclipse_s140` and `false` for
`generic`/`nova1`/`nova3`/`nova4`. `EclipseTargetLowering::
PerformDAGCombine` (`EclipseISelLowering.cpp`) now also handles
`ISD::OR`/`ISD::XOR`: when `!Subtarget.hasEIS()`, it synthesizes them
from `AND` and a "not" primitive via De Morgan's laws (`a|b =
~(~a & ~b)`; `a^b = (a&~b) | (~a&b)`) instead of letting them select to
the native `IORrr`/`XORrr` instructions. When `hasEIS()` is true
(`eclipse_s140`), this new code path never fires at all — confirmed via
a full regression pass (package examples + every repro built while
isolating this) showing byte-identical output to before this change.

**A real pitfall hit along the way, worth recording:** the first version
of the "not" primitive used a plain `xor(x, -1)`. That produced an
infinite loop — `llc` spinning at 100% CPU, never terminating. Cause:
LLVM's *generic* (target-independent) DAGCombiner has its own standard
fold recognizing `not(and(not(a), not(b)))` as De Morgan's `or(a, b)`
and rewriting it straight back — which, since that's exactly the
original node this combine was trying to eliminate, re-triggers the
combine, which resynthesizes the same shape, which gets refolded again,
forever. This is the same *class* of bug `eclipse-llvm-backend`'s
`WORD_ADD`/`HALVE` opcodes already exist to avoid (see its
`DEBUGGING_NOTES.md` bug #5) — a distinct opcode structurally invisible
to generic folds. Fixed the same way here: a new `EclipseISD::COM` node
(not a plain `ISD::XOR`) for the synthesized "not", with a `Pat` mapping
it to the existing `COMrr` instruction. Genuine `~x` in source code still
lowers to a plain `xor(x, -1)` and matches `COMrr`'s own pattern
directly, untouched — `PerformDAGCombine` explicitly recognizes and
skips that shape rather than resynthesizing it.

A second optimization landed alongside this: when the byte-scaled offset
`PerformDAGCombine` needs to halve for runtime array indexing (a
pre-existing fix, see `DEBUGGING_NOTES.md` bug #9) is already exactly
`x+x` (how this backend's `LowerShift` lowers `<<1`), it's used directly
instead of going through a real runtime division instruction — pure
waste otherwise, and on a large enough program the extra instructions
were enough to push a branch target past `dgasm`'s 0-255 page-relative
`JMP` range.

## Verification methodology — and a real mistake made and caught

Two genuinely different checks are needed here, and conflating them
produced a false alarm during development that's worth recording:

1. **Does `dgasm -t nova1`/`nova3`/`nova4` *accept* the generated
   assembly?** This is checked directly — compile, assemble, confirm the
   output file exists.
2. **Is the *logic* actually correct?** `eclipseemu` only simulates the
   Eclipse S/140 — there is no Nova simulator available in this
   environment. **A `dgasm -t nova3`-assembled binary is NOT
   interchangeable with a `dgasm -t eclipse_s140`-assembled one, even
   for byte-identical input assembly text** — they're different
   instruction *encodings*, and running a nova3-encoded binary through
   `eclipseemu` produces garbage (wrong register/memory reads) that has
   nothing to do with whether the underlying logic is sound. This was
   confirmed the hard way: an OR/XOR/AND/NOT test that looked completely
   broken (`or=0 xor=0 and=0`) when its `-t nova3` output was run
   through `eclipseemu` produced the exact correct result (`or=7 xor=6
   and=1 not=-6`) once the *same* generated assembly was re-assembled
   with `-t eclipse_s140` instead and run the same way. Since every
   instruction this backend emits for a Nova target is also valid,
   identically-behaving Eclipse S/140 assembly (Eclipse is a superset —
   nothing here is Nova-specific hardware `eclipseemu` doesn't model),
   re-assembling the same `.s` for `eclipse_s140` and running *that* is
   a legitimate way to verify the logic, distinct from verifying nova1/
   nova3/nova4 actually *accept* the encoding.

Every example in `examples/` is verified both ways: `dgasm -t nova1/
nova3/nova4` all succeed on the exact generated assembly, and the same
assembly re-encoded for `eclipse_s140` produces correct output on
`eclipseemu`.

## What this does NOT verify

- **No real Nova hardware or Nova-specific simulator was used.** Every
  correctness check above ran on `eclipseemu` (Eclipse S/140 only) via
  the re-encoding technique described. `dgasm -t nova1`/`nova3`/`nova4`
  accepting the instructions is real, direct evidence; a real Nova
  actually *executing* them correctly the same way is not something this
  package can confirm without one.
- **`nova1` vs `nova3` vs `nova4` are modeled identically** in
  `Eclipse.td` — this backend doesn't yet distinguish any finer-grained
  differences between them (e.g. hardware `MUL`/`DIV` availability,
  which varied across real Nova models historically). `dgasm` accepting
  `MUL`/`DIV` for all three CPU selections was taken as sufficient
  evidence for this backend's purposes, but that's a assembler-level
  check, not a hardware-availability one.
- Nothing about `eclipse-llvm-backend`'s own already-documented "Known
  limitations" (i32 multiply/divide runtime libcalls, no real FPU
  opcodes, etc. — see its `README.md`) is any more or less true here;
  none of that is Eclipse-vs-Nova-specific.
