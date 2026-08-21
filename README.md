# Nova LLVM Backend — Setup Package

This extends the sibling `eclipse-llvm-backend` project — a real LLVM/
Clang backend for the Data General Eclipse S/140 — to also target the
base Data General Nova (`nova1`/`nova3`/`nova4`, as recognized by
`dgasm`'s own `-t` flag). It's built the same way, from the same
upstream `llvm-project` commit, and shares its runtime library and
post-processing pass unchanged. **Read `NOVA_PORTING_NOTES.md`** — it
covers exactly what's different about targeting Nova versus Eclipse,
what was verified and how (including a real mistake made and caught
along the way, worth knowing before you trust any Nova output), and what
wasn't verified (no real Nova hardware or Nova-specific simulator was
available).

## What's in this package

- `nova-backend.patch` — a git patch containing the Eclipse backend
  (unchanged from `eclipse-llvm-backend`) *plus* the small Nova-support
  addition on top: a subtarget feature gating the two Eclipse-only
  instructions (`IOR`/`XOR`), synthesized from `AND`/`COM` instead when
  targeting a base Nova CPU. See `NOVA_PORTING_NOTES.md` for the full
  story. To apply on top of a clean `eclipse-llvm-backend` checkout
  instead of a fresh `llvm-project`, see that project's own
  `eclipse-backend.patch` — this one is a superset, not a diff against
  it.
- `nova-toolchain/`:
  - `nova-cc` — compiler driver targeting `eclipseemu`/`dgasm`, mirroring
    `eclipse-llvm-backend`'s `eclipse-cc` with one addition: a `-t` flag
    selecting the CPU (`nova1`, `nova3`, `nova4`, or `eclipse_s140`;
    default `nova3`), threaded through to both `llc -mcpu` and
    `dgasm -t`. Usage: `nova-cc [-t nova1|nova3|nova4|eclipse_s140]
    [-o out.simh] file.c [file.c ...]`.
  - `reorder_asm.py`, `rt/` — identical, unmodified copies of
    `eclipse-llvm-backend`'s post-processing pass and C runtime library.
    Nothing about either needed to change for Nova.
- `examples/` — `bitwise_ops.c` (the one place Nova codegen genuinely
  differs from Eclipse — see `NOVA_PORTING_NOTES.md`), plus
  `sizeof_check.c`/`printf_octal_check.c` (copied from
  `eclipse-llvm-backend`'s examples, to confirm ordinary programs are
  unaffected). All three verified per `NOVA_PORTING_NOTES.md`'s
  methodology: `dgasm -t nova1/nova3/nova4` all accept the generated
  assembly, and the same assembly re-encoded for `eclipse_s140` produces
  correct output on `eclipseemu`.
- `NOVA_PORTING_NOTES.md` — what's different about Nova, the fix, a real
  pitfall (an infinite compiler loop) hit and fixed along the way, and
  the verification methodology and its limits.

## 1. Clone llvm-project and apply the patch

Same upstream commit `eclipse-llvm-backend` uses:

```bash
git clone https://github.com/llvm/llvm-project.git
cd llvm-project
git checkout 8307b46d3ad5ace00c21e1fec6ef4ef4284290e9
git apply /path/to/nova-llvm-backend/nova-backend.patch
```

## 2. Configure and build

Identical to `eclipse-llvm-backend`'s own build steps:

```bash
mkdir llvm-build && cd llvm-build
cmake -G "Unix Makefiles" ../llvm-project/llvm \
  -DLLVM_ENABLE_PROJECTS=clang \
  -DLLVM_TARGETS_TO_BUILD=X86 \
  -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=Eclipse \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON \
  -DLLVM_USE_LINKER=gold

make clang llc llvm-link opt -j$(nproc)
```

`dgasm` (the assembler) and `eclipseemu` (the Eclipse simulator, needed
for the verification technique `NOVA_PORTING_NOTES.md` describes) are
the same tools `eclipse-llvm-backend`'s own README documents installing
— see that project's README section 4 if you don't already have them.

## 3. Compile something

```bash
LLVM_BUILD=/path/to/llvm-build \
  nova-toolchain/nova-cc -t nova3 -o /tmp/bitwise.simh nova-toolchain/../examples/bitwise_ops.c
```

`nova-cc` defaults `LLVM_BUILD` to `$HOME/nova-dev/llvm-build` — set the
environment variable if yours lives elsewhere. Omit `-t` for the
`nova3` default, or pass `-t eclipse_s140` to get ordinary
`eclipse-llvm-backend`-equivalent output from the same script.

To actually *run* nova-targeted output, you need real Nova hardware or a
Nova-specific simulator — neither was available while building this
package (see `NOVA_PORTING_NOTES.md`'s "What this does NOT verify"
section). `dgasm -t nova1/nova3/nova4` accepting the assembly is real,
confirmed evidence the encoding is valid; nothing here confirms real
Nova hardware executes it identically to how `eclipseemu` executes the
`eclipse_s140` re-encoding of the same logic.
