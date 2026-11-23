# Native sequence and PCM providers

`runner/audio/smps_hle` consumes immutable, normalized host sequence data and
emits typed note, instrument, modulation and control events. It executes no
68000 or Z80 instructions. The consumer owns original-image validation,
decoding and selection of music, effects and waveform data; these allocations
are never installed as a cartridge image or ROM overlay.

`smps_hle_synth` translates those events into FM/PSG register operations and
PCM requests. Callbacks return failure for missing output or unsupported data.
Unsupported commands and invalid bounds fail explicitly rather than invoking
a guest driver. The normalized format retains distinct relative-address bases
for calls and jumps, timing dividers, channel envelopes, modulation, loops and
interrupting music. Full-song event selections can restore their saved music
clock at the lead channel's first top-level loop.

Effects own their listed physical tracks. A consumer keeps its music clock
advancing while suppressing writes to tracks owned by effects, then reinstalls
the current music articulation when ownership ends. Only effects classified
as continuous by the consumer should refresh an existing effect clock.

`pcm_hle_events` sends unsigned waveform bytes at native master-clock offsets.
Feed them through the ordinary audio event queue and YM2612/filter path. DAC
and FM6 share hardware: the binding must release DAC mode and restore FM6
panning when a sample ends. The optional stereo mixer is useful for isolated
provider tests; it does not model the chip output filter.

For a title using this provider, compile the machine with
`GENESIS_AUDIO_HLE=1` and register its native waveform clock using
`machine_set_hle_audio`. This disables uploaded Z80 execution for that build.
Sound requests, required guest reads/waits, pause behavior and their ordering
belong to the title binding's observation contract. Native saves and player
settings remain independent of execution snapshots.

Current-build snapshots must include both sequence clocks, synth articulation,
PCM phase, pending requests and controls, chip state and queued audio events.
Bind immutable data again before validating and applying a snapshot. These
structures provide no backward-compatible save-state format and do not alone
qualify cross-build or cross-platform netplay. Keep the existing exact build
identity checks and qualify actual built consumers.
