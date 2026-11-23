# macOS app bundles

`cmake/GenesisRecompMacOS.cmake` turns an existing runner executable target
into a self-contained macOS `.app`:

```cmake
include("${RECOMP_ROOT}/cmake/GenesisRecompMacOS.cmake")
genesisrecomp_macos_bundle(GameRecomp
    BUNDLE_IDENTIFIER org.example.game
    BUNDLE_NAME "Game"
    VERSION "${PROJECT_VERSION}")
```

The helper sets the bundle metadata, embeds non-system runtime libraries with
CMake's `BundleUtilities`, and ad-hoc signs the finished bundle. Set
`GENESIS_MACOS_ADHOC_SIGN=OFF` when another release pipeline will sign the
bundle. Ad-hoc signing is suitable for local builds; it does not notarize the
application for distribution.

`ICON` accepts an `.icns` file. Additional files listed in `RESOURCES` are
copied into `Contents/Resources`. Libraries loaded dynamically rather than
through Mach-O load commands can be listed in `RUNTIME_LIBRARIES`.
For example, an application linked to `sdl2-compat` should pass the unversioned
`libSDL3.dylib` path so the helper preserves SDL3's symlink chain in
`Contents/Frameworks`.

Bundled runners store writable settings, saves, replay logs, and evidence in
`~/Library/Application Support/GenesisRecomp/<short_name>/`. Set the existing
`GENESIS_USER_DIR` environment variable to an absolute path to override this,
including for automated tests. Launcher assets belong in
`Contents/Resources/assets`.
