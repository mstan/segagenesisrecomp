# Original MC68000 program modules

`M68kStepRecomp` generates instruction-sized native C for original programs,
including RAM overlays. It uses the shared decoder, EA/arithmetic emitter and
portable `m68k_step` context. Each operation returns to the system scheduler
with an explicit guest PC and real guest stack. The cartridge frontend and its
function ABI remain available separately.

Build the tool with `cmake -S recompiler -B build/recompiler`, then build target
`M68kStepRecomp`. Create an output directory and run:

```text
M68kStepRecomp input.bin output_directory module_identifier 0xLOAD_BASE 0xENTRY...
M68kStepRecomp input.bin output_directory module_identifier 0xLOAD_BASE --all
```

Inputs must be nonempty, even-sized images wholly within the 24-bit address
space. The identifier must be a C identifier. The first form follows static
branches/calls and fallthrough, including code after STOP; it cannot discover
unknown computed targets. `--all` considers every aligned word, including data.
That is a coverage option, not proof that every candidate is executable code.

Output consists of a module header, descriptor/entry table, 32 C operation
shards and a CSV coverage report. Compile the C files with the shared step
runtime. Owned program images and their generated C stay in private build
directories. The source package contains no BIOS, disc image or generated
owned program. CRC32 identifies the input diagnostically; use a strong input
hash in qualification and compatibility manifests.

Validate descriptors once with `m68k_step_module_valid()`. Register all applicable
modules, then call `m68k_step_module_execute()` in the CPU scheduler. A PC lookup
is followed by checking **all current opcode and extension bytes** through the
CPU's fetch callback. A mismatch returns `M68K_STEP_MISS` before operation/PC/
cycle mutation; another module or a qualified floor may handle it. Report an
unhandled miss with the actual PC/bytes. Never continue with stale code or a
silent no-op. A physical fetch fault is a fault, not an overlay mismatch.

Explicit MC68000 decoding preserves byte-displacement `$FF` as -1 and treats
MOVEC as a one-word illegal opcode. Invalid control EAs take the architectural
illegal-instruction operation; unknown supported-code emission fails and is
listed as unsupported (tool exit 3). Normal successful generation exits zero;
invalid inputs/output errors exit nonzero.

Timing comes from the existing manual-derived model. Branch/DBcc outcomes,
register-count shifts and register Scc override nominal costs at execution.
Multiply/divide still use documented nominal costs. Group-zero bus/address
frames and the general-system correctness floor are pending. Passing synthetic
module tests does not establish original BIOS/disc execution or complete CPU
timing accuracy.

The synthetic fixture tests compile and execute both CFG and `--all` output,
nonzero load bases, real BSR/RTS loops with exact expected registers/stacks/
cycle totals, independent CPU contexts, opcode/extension replacement rejection,
original-68000 lengths, illegal control EAs and truncated-image exclusion.
They pass on MSVC, MinGW and Linux ASan/UBSan.

Primary ISA/timing references: [MC68000 User's Manual](https://www.nxp.com/docs/en/reference-manual/MC68000UM.pdf),
sections 6 and 8, and [M68000 Programmer's Reference Manual](https://www.nxp.com/docs/en/reference-manual/M68000PRM.pdf).
