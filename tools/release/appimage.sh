# Shared pinned AppImage tools. Title staging, notices and layout tests stay in games.
genesis_appimage_tools() {
    local tools="$1/appimage-tools"
    mkdir -p "$tools"
    LINUXDEPLOY_BIN="$tools/linuxdeploy-x86_64.AppImage"
    APPIMAGETOOL_BIN="$tools/appimagetool-x86_64.AppImage"
    genesis_fetch_tool https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage \
        c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d "$LINUXDEPLOY_BIN"
    genesis_fetch_tool https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage \
        ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0 "$APPIMAGETOOL_BIN"
    RUNTIME="$tools/runtime-x86_64"
    local offset
    offset=$("$APPIMAGETOOL_BIN" --appimage-offset)
    [[ "$offset" =~ ^[1-9][0-9]*$ ]] || return 1
    head -c "$offset" "$APPIMAGETOOL_BIN" > "$RUNTIME"
}
genesis_fetch_tool() {
    local url="$1" sha="$2" dest="$3"
    if [ ! -f "$dest" ] || [ "$(sha256sum "$dest" | cut -d' ' -f1)" != "$sha" ]; then
        curl -fL --retry 3 "$url" -o "$dest.tmp"
        printf '%s  %s\n' "$sha" "$dest.tmp" | sha256sum -c - >/dev/null
        mv "$dest.tmp" "$dest"
    fi
    chmod +x "$dest"
}
genesis_package_appimage() {
    ARCH=x86_64 "$APPIMAGETOOL_BIN" --appimage-extract-and-run --runtime-file "$RUNTIME" "$1" "$2"
    chmod +x "$2"
}
