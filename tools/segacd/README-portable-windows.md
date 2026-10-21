# Sonic CD portable Windows package

Run `SonicCD.cmd` to start the game. The first launch asks for a Sega CD BIOS
and an SGCD image or CUE/BIN set. The launcher stores only their paths; it does
not copy either owner-supplied input into the package. Choose an SGCD image to
avoid requiring Python. CUE conversion requires Python 3 and supports quoted
`BINARY` sources with `MODE1/2352` or `AUDIO` tracks.

This runner targets the qualified US release inputs:

- Sega CD BIOS: 131,072 bytes, SHA-256
  `807384678ef259494d943eddb6954dfc7069693d0dde0c424e2028488d88e81a`.
- Sonic CD US disc: SGCD payload SHA-256
  `4e822010e42092681b238ddc7cbff41802c0cfc006c9b1510dcebd6b9dd452b1`.

The runner checks its input identities at startup. A different BIOS or disc
revision is rejected by this qualified build.

The first run creates a windowed profile with normal audio enabled at 100%
volume and adaptive widescreen. Later launches keep the user's settings and
save. Settings, save data, logs, temporary files, and converted CUE images live
under the package's `userdata` directory by default. To keep those files in a
different location, launch with:

```powershell
.\SonicCD.cmd -DataRoot 'G:\Games\SonicCD-data'
```

Keyboard controls are the arrow keys for directions, Z for A, X for B, C for
C, and Enter for Start. A controller uses its D-pad, X for A, A for B, B for C,
and Start. Use the original game's title and menu screens to begin and continue
play.

Press Esc or F1, or the controller's Back button, to open recomp-ui. Display and
Graphics settings include window mode and adaptive widescreen / Fit to screen;
resize the window to change the aspect. The game starts windowed on each launch.

The optional package seed is only for a new blank backup RAM file and must be
exactly 8,192 bytes. Existing save data is never replaced. The package includes
runtime license texts, but package creation does not include a BIOS, original
disc image, or save from the owner. Selecting a CUE creates a content-verified
SGCD conversion cache under `userdata/disc` for the runner to use.

The engine uses the PolyForm Noncommercial license included as `LICENSE`.
Third-party terms remain separate in `THIRD-PARTY-LICENSES.md` and `licenses/`.
Generated game code does not grant rights to the original game; provide your
own legally obtained BIOS and disc. Engine source and build instructions are
in [segagenesisrecomp](https://github.com/mstan/segagenesisrecomp),
`docs/SONIC_CD_NATIVE_BUILD.md`.
