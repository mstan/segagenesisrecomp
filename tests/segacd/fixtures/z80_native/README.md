# Generated native Z80 fixtures

These are flat-step outputs from the synthetic byte images embedded in
`tests/segacd/z80_native_test.c`: `ED A0 00 00` (LDI followed by NOPs) and
`FB 76 DB 01 D3 01 00 00` (EI; HALT; `IN A,(1)`; `OUT (1),A`; NOPs). They
contain no game or BIOS bytes.

Generator source: `F:/Projects/smsggrecomp/smsggrecomp/recompiler/src`, commit
`224d5bb2c150a2c295033d35dec629ef9ee42940`. The private generator executable
used for the checked-in outputs was
`build/qa-segacd-z80-program-identity/bin/SmsRecomp-flat.exe`, SHA-256
`C5E6301D9E49B60B947A383FE0E87FFBA7BF870B39E41D1FAEB4A8B7F9230CDF`.

Reproduce each fixture by writing its listed bytes to a `.bin` file and using
a TOML file with `[game]`, `output_prefix = "proof_ldi"` (or
`"proof_control"`), `platform = "sms"`, and `rom = "<matching .bin>"`, then
run `SmsRecomp-flat.exe --game <toml> --flat-step`. The outputs are generated
code; instruction semantics are not handwritten in the test.
