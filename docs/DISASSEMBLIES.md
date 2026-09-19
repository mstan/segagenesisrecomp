# Reproducible Sonic disassembly evidence

The engine pins Sonic Retro sources as submodules beside the game configs:
Sonic 1 (`sonicthehedgehog/s1disasm`), Sonic 2
(`sonicthehedgehog2/s2disasm`), and Sonic 3 / S&K (`sonic3k/skdisasm`).
The pinned commits and supported ROM SHA-256 hashes are recorded in
`ghidra/annotations/provenance.json`.

Run with Python 3.12 or newer from the engine root:

```powershell
python tools/sonic_disassembly.py --install
```

Supply the usual private ROMs beside each game.toml. The command exports each
pinned Git revision into an ignored, fresh build tree, uses its bundled Lua/AS
tools on Windows (Lua 5.3+ on other systems), and requires byte-for-byte equality
with the supplied ROM before installing any labels. The original ROM and
submodule source files are never modified. Sonic 1 is explicitly built with
`Revision = 0`; upstream defaults to REV01. S&K uses `Sonic3_Complete=0` and
the combined game concatenates the stock S&K and Sonic 3 builds.

Use `--out build/disassembly-second` for a fresh repeat, or `--reuse` to
revalidate existing outputs. A successful invocation writes deterministic CSV
code annotations, Ghidra-compatible annotation JSON, and provenance. Existing
discovery inputs remain separate: names do not promote labels to function
boundaries. The extractor includes nested 68000 source files and requires a
recognized 68000 instruction and matching ROM bytes at each code label.

The previously local Sonic 1 and S&K development branches contained widescreen
ROM modifications. Their upstream parents are pinned instead; the original
development branches and private builds are preserved. Do not point annotation
generation at a widescreen or other modified listing.

## Ghidra

Use the automatically managed headless MCP server for normal analysis. New
projects can be prepared offline with `tools/ghidra/SonicAnnotationProject.java`,
using the installed Ghidra Java classpath. This batch utility opens no GUI or
server. It verifies ROM identity, creates one project per game, imports code
labels, adds WRAM, and disassembles reset/interrupt entry points. It refuses
existing projects to avoid overwriting human analysis.

Compile with `javac -cp <Ghidra classpath> -d build/ghidra-tools` and run via
`ghidra.Ghidra SonicAnnotationProject <persistent-project-directory> <game-id>
<rom> <annotations.json>`, with `ghidra.GhidraClassLoader` as the system class
loader. Keep databases outside worktrees. Register new projects in the central
registry and restart the MCP client before querying them; the registry is a
startup snapshot. Existing projects use the installed annotation importer and
exporter, preserving user annotations. Do not commit databases or ROMs.

The tracked JSON is reproducible import data, not a claim of comprehensive
decompilation or manual reverse engineering. Record import and runtime
validation in the owning Beads issue (`beads-3vb.4`).
