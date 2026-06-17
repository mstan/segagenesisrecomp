// launcher_gui.cpp — RmlUi pre-boot launcher (see launcher_gui.h).
//
// Structure ported from nesrecomp/runner/src/launcher/launcher_gui.cpp, adapted
// to the Genesis: Genesis-header metadata + CRC32/SHA-1 verification, a Display/
// Audio/Widescreen settings view, per-player input device dropdowns, a SAVE RAM
// panel, and — beyond the sibling launchers — a full per-button keyboard + gamepad
// REBIND view with 3/6-button selection. Controller bindings are edited straight
// into the live g_input_map; display/audio go back through the Settings struct.

#include "launcher_gui.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include "RmlUi_Renderer_GL3.h"
#include "RmlUi_Platform_SDL.h"

#include <functional>
#include <memory>

#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <filesystem>

#include "stb_image.h"

extern "C" {
#include "input_map.h"
}

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <commdlg.h>
#endif

namespace fs = std::filesystem;

namespace genesis_launcher {
namespace {

// ----------------------------------------------------------------------------
// Small helpers
// ----------------------------------------------------------------------------

std::string basename_of(const std::string& p) {
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

std::string human_size(long bytes) {
    char buf[64];
    if (bytes >= 1024 * 1024)
        std::snprintf(buf, sizeof(buf), "%ld KB (%.2f MB)", bytes / 1024, bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024)
        std::snprintf(buf, sizeof(buf), "%ld KB", bytes / 1024);
    else
        std::snprintf(buf, sizeof(buf), "%ld B", bytes);
    return buf;
}

std::string hex32(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof(b), "%08X", v);
    return b;
}

std::vector<uint8_t> read_file(const std::string& path) {
    std::vector<uint8_t> data;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return data;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::rewind(f);
    if (sz > 0) {
        data.resize((size_t)sz);
        if (std::fread(data.data(), 1, (size_t)sz, f) != (size_t)sz) data.clear();
    }
    std::fclose(f);
    return data;
}

// ---- CRC32 (IEEE, No-Intro-style over the raw ROM) -------------------------
uint32_t crc32_compute(const uint8_t* p, size_t n) {
    static uint32_t tbl[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            tbl[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = tbl[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// ---- SHA-1 (display only) --------------------------------------------------
std::string sha1_hex(const uint8_t* msg, size_t len) {
    uint32_t h0=0x67452301,h1=0xEFCDAB89,h2=0x98BADCFE,h3=0x10325476,h4=0xC3D2E1F0;
    auto rol=[](uint32_t v,int b){ return (v<<b)|(v>>(32-b)); };
    uint64_t ml = (uint64_t)len * 8;
    std::vector<uint8_t> m(msg, msg + len);
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 7; i >= 0; i--) m.push_back((uint8_t)((ml >> (i*8)) & 0xFF));
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = (m[off+i*4]<<24)|(m[off+i*4+1]<<16)|(m[off+i*4+2]<<8)|m[off+i*4+3];
        for (int i = 16; i < 80; i++) w[i] = rol(w[i-3]^w[i-8]^w[i-14]^w[i-16], 1);
        uint32_t a=h0,b=h1,c=h2,d=h3,e=h4;
        for (int i = 0; i < 80; i++) {
            uint32_t f,k;
            if (i<20){f=(b&c)|((~b)&d);k=0x5A827999;}
            else if (i<40){f=b^c^d;k=0x6ED9EBA1;}
            else if (i<60){f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDC;}
            else {f=b^c^d;k=0xCA62C1D6;}
            uint32_t t=rol(a,5)+f+e+k+w[i];
            e=d;d=c;c=rol(b,30);b=a;a=t;
        }
        h0+=a;h1+=b;h2+=c;h3+=d;h4+=e;
    }
    char out[64];
    std::snprintf(out, sizeof(out), "%08X%08X%08X%08X%08X", h0,h1,h2,h3,h4);
    return out;
}

// Trim trailing spaces/NULs from a fixed-width Genesis header field.
std::string header_field(const uint8_t* data, size_t size, size_t off, size_t len) {
    if (off + len > size) return "";
    std::string s((const char*)data + off, len);
    size_t e = s.find_last_not_of(" \0\t");
    if (e == std::string::npos) return "";
    s = s.substr(0, e + 1);
    size_t b = s.find_first_not_of(" \0\t");
    return b == std::string::npos ? "" : s.substr(b);
}

std::string decode_region(const uint8_t* data, size_t size) {
    if (0x1F0 + 3 > size) return "";
    std::string r((const char*)data + 0x1F0, 3);
    if (r.find('U') != std::string::npos) return "NTSC-U (USA)";
    if (r.find('J') != std::string::npos) return "NTSC-J (Japan)";
    if (r.find('E') != std::string::npos) return "PAL (Europe)";
    // Newer carts use a hex bitfield at $1F0: bit0 Japan, bit2 USA, bit3 Europe.
    int v = (r[0] >= '0' && r[0] <= '9') ? r[0]-'0'
          : (r[0] >= 'A' && r[0] <= 'F') ? r[0]-'A'+10 : -1;
    if (v >= 0) {
        if (v & 4) return "NTSC-U (USA)";
        if (v & 1) return "NTSC-J (Japan)";
        if (v & 8) return "PAL (Europe)";
    }
    return "";
}

#ifdef _WIN32
bool pick_file(const char* title, const char* filter, char* out, size_t max_len) {
    OPENFILENAMEA ofn;
    std::memset(&ofn, 0, sizeof(ofn));
    out[0] = '\0';
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = out;
    ofn.nMaxFile    = (DWORD)max_len;
    ofn.lpstrTitle  = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn) != 0;
}
#else
bool pick_file(const char*, const char*, char* out, size_t) { out[0] = '\0'; return false; }
#endif

// RenderInterface_GL3 only decodes TGA; override LoadTexture to decode PNG via
// stb_image so the dashboard can show box / controller / console art.
class LauncherRenderInterface : public RenderInterface_GL3 {
public:
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dims, const Rml::String& source) override {
        Rml::FileInterface* fi = Rml::GetFileInterface();
        Rml::FileHandle fh = fi ? fi->Open(source) : Rml::FileHandle(0);
        if (!fh) return RenderInterface_GL3::LoadTexture(dims, source);
        fi->Seek(fh, 0, SEEK_END);
        const size_t sz = (size_t)fi->Tell(fh);
        fi->Seek(fh, 0, SEEK_SET);
        std::vector<unsigned char> buf(sz);
        fi->Read(buf.data(), sz, fh);
        fi->Close(fh);

        int w = 0, h = 0, comp = 0;
        unsigned char* px = stbi_load_from_memory(buf.data(), (int)sz, &w, &h, &comp, 4);
        if (!px) return RenderInterface_GL3::LoadTexture(dims, source);  // maybe a TGA
        const size_t n = (size_t)w * (size_t)h;
        for (size_t i = 0; i < n; i++) {  // straight -> premultiplied alpha
            const unsigned a = px[i * 4 + 3];
            px[i * 4 + 0] = (unsigned char)(px[i * 4 + 0] * a / 255);
            px[i * 4 + 1] = (unsigned char)(px[i * 4 + 1] * a / 255);
            px[i * 4 + 2] = (unsigned char)(px[i * 4 + 2] * a / 255);
        }
        dims.x = w; dims.y = h;
        Rml::TextureHandle th = GenerateTexture({px, n * 4}, dims);
        stbi_image_free(px);
        return th;
    }
};

// One entry in a controller-source dropdown.
struct SrcOption { Rml::String value; Rml::String label; };

// Device <-> dropdown token.
const char* device_to_value(int d) {
    return d == INPUT_DEV_KEYBOARD ? "kbd"
         : d == INPUT_DEV_GAMEPAD  ? "pad"
         : d == INPUT_DEV_BOTH     ? "both" : "none";
}
int value_to_device(const Rml::String& v) {
    if (v == "kbd")  return INPUT_DEV_KEYBOARD;
    if (v == "pad")  return INPUT_DEV_GAMEPAD;
    if (v == "both") return INPUT_DEV_BOTH;
    return INPUT_DEV_NONE;
}

std::string key_label(int sc) {
    if (sc <= 0) return "\xE2\x80\x94";  // em dash
    const char* n = SDL_GetScancodeName((SDL_Scancode)sc);
    return (n && *n) ? n : "key";
}
std::string pad_label(const GamepadBind& b) {
    if (b.kind == GP_BIND_BUTTON) {
        const char* n = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)b.code);
        return n ? n : "button";
    }
    if (b.kind == GP_BIND_AXIS) {
        const char* n = SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)b.code);
        std::string s = n ? n : "axis";
        s += (b.axis_dir < 0) ? " -" : " +";
        return s;
    }
    return "\xE2\x80\x94";
}

// ----------------------------------------------------------------------------
// View model
// ----------------------------------------------------------------------------

struct Model {
    Rml::String view = "dashboard";

    Rml::String game_name, game_region;
    bool has_boxart = false;
    Rml::String boxart_html;   // injected <img> with the per-game boxart src

    bool rom_loaded = false;
    Rml::String rom_title, rom_file, rom_size, rom_header, rom_crc, rom_sha;
    bool crc_known = false, crc_match = false;

    std::vector<std::string> pad_names;
    Rml::String p1_status = "Enabled", p2_status = "Disabled";
    bool p1_enabled = true, p2_enabled = false;
    std::vector<SrcOption> p1_options, p2_options;

    bool uses_sram = false;
    Rml::String save_file = "(none)", save_size = "0 KB";

    bool skip_launcher = false, show_skip_modal = false;

    // settings
    Rml::String scale_label;
    bool filter = false;
    int  volume = 100;
    bool widescreen = false, widescreen_supported = false;
    Rml::String ws_cells_label = "8";

    // controller config / rebind view
    int cfg_player = 0;
    Rml::String cfg_player_label = "1";
    Rml::String cfg_padtype_label = "3-button";
    int cfg_deadzone = 25;
    Rml::String bind_table;                 // injected via data-rml
    bool capturing = false;
    Rml::String capture_prompt;
};

std::vector<std::string> enumerate_pads() {
    std::vector<std::string> names;
    int n = SDL_NumJoysticks();
    for (int i = 0; i < n; i++) {
        const char* nm = SDL_IsGameController(i) ? SDL_GameControllerNameForIndex(i)
                                                 : SDL_JoystickNameForIndex(i);
        names.emplace_back(nm && *nm ? nm : "Controller");
    }
    return names;
}

void build_src_options(std::vector<SrcOption>& opts, int player,
                       const std::vector<std::string>& pads) {
    opts.clear();
    opts.push_back({ "none", "None" });
    opts.push_back({ "kbd",  "Keyboard" });
    if (player >= 0 && player < (int)pads.size())
        opts.push_back({ "pad",  pads[player] });
}

void refresh_player_status(Model& m) {
    m.p1_enabled = g_input_map.p[0].device != INPUT_DEV_NONE;
    m.p2_enabled = g_input_map.p[1].device != INPUT_DEV_NONE;
    m.p1_status = m.p1_enabled ? "Enabled" : "Disabled";
    m.p2_status = m.p2_enabled ? "Enabled" : "Disabled";
}

void refresh_settings_labels(Model& m, const Settings& s) {
    char b[48];
    int sc = s.window_scale < 1 ? 1 : s.window_scale;
    // "640 \xC3\x97 448  (2\xC3\x97)" — resolution + scale (\xC3\x97 = U+00D7 multiply sign).
    std::snprintf(b, sizeof(b), "%d \xC3\x97 %d  (%d\xC3\x97)", 320 * sc, 224 * sc, sc);
    m.scale_label = b;
    m.filter = s.linear_filter;
    m.volume = s.volume;
    m.widescreen = s.widescreen;
    std::snprintf(b, sizeof(b), "%d", s.widescreen_cells);
    m.ws_cells_label = b;
}

// Rebuild the rebind-table markup for the player currently being configured.
void rebuild_bind_table(Model& m) {
    const PlayerInput* pi = &g_input_map.p[m.cfg_player];
    int count = (pi->pad_type == PAD_6BUTTON) ? GB_COUNT : GB_START + 1;  // 8 or 12
    m.cfg_padtype_label = (pi->pad_type == PAD_6BUTTON) ? "6-button" : "3-button";
    m.cfg_deadzone = pi->deadzone_pct;

    Rml::String s;
    for (int b = 0; b < count; b++) {
        char row[512];
        std::snprintf(row, sizeof(row),
            "<div class='bind-row'>"
              "<span class='bind-name'>%s</span>"
              "<span class='bind-key'>%s</span>"
              "<button class='bind-btn' data-event-click='rebind_key(%d)'>Set key</button>"
              "<span class='bind-pad'>%s</span>"
              "<button class='bind-btn' data-event-click='rebind_pad(%d)'>Set pad</button>"
            "</div>",
            input_button_name((GenesisButton)b),
            key_label(pi->key[b]).c_str(), b,
            pad_label(pi->pad[b]).c_str(), b);
        s += row;
    }
    m.bind_table = s;
}

void load_rom_info(Model& m, const GameInfo& g, const std::string& path) {
    m.rom_loaded = false;
    m.crc_match = false;
    if (path.empty()) { m.rom_file = "(none)"; m.rom_title = m.game_name; return; }

    std::vector<uint8_t> data = read_file(path);
    if (data.size() < 0x200) {
        m.rom_file = basename_of(path) + " (too small / unreadable)";
        return;
    }

    m.rom_file   = basename_of(path);
    m.rom_size   = human_size((long)data.size());
    m.rom_header = header_field(data.data(), data.size(), 0x100, 16);  // "SEGA GENESIS"
    // Title is the canonical per-mode name from the game spec — NOT the ROM
    // header title. The S3&K lock-on cart and standalone S&K share one S&K
    // header ("SONIC & KNUCKLES"); the mode (Sonic3KRecomp vs
    // SonicAndKnucklesRecomp) is what disambiguates them, so the spec name wins.
    m.rom_title  = g.name ? g.name : m.rom_file;
    std::string region = decode_region(data.data(), data.size());
    m.game_region = region.empty() ? (g.region ? g.region : "") : region;

    uint32_t crc = crc32_compute(data.data(), data.size());
    m.rom_crc   = hex32(crc);
    m.rom_sha   = sha1_hex(data.data(), data.size());
    m.crc_known = g.has_expected_crc;
    m.crc_match = g.has_expected_crc && crc == g.expected_crc;

    // Save RAM: a cart declares battery SRAM via the "RA" marker at $1B0 of the
    // Genesis header (what the runtime reads), OR the game spec forces it on for
    // carts whose header doesn't self-describe (the S3&K lock-on override).
    bool header_ra = data[0x1B0] == 'R' && data[0x1B1] == 'A';
    m.uses_sram = g.uses_sram || header_ra;

    m.rom_loaded = true;
}

void refresh_save_info(Model& m, const fs::path& exe_dir, const std::string& rom_path) {
    if (!m.uses_sram || rom_path.empty()) {
        m.save_file = "(none)"; m.save_size = "0 KB";
        return;
    }
    std::string base = basename_of(rom_path);
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    fs::path srm = exe_dir / (base + ".srm");
    m.save_file = srm.filename().generic_string();
    std::error_code ec;
    if (fs::exists(srm, ec))
        m.save_size = human_size((long)fs::file_size(srm, ec));
    else
        m.save_size = "0 KB";
}

bool load_fonts(const fs::path& assets) {
    bool any = false;
    const char* faces[] = { "fonts/LatoLatin-Regular.ttf", "fonts/LatoLatin-Bold.ttf" };
    for (const char* f : faces) {
        fs::path p = assets / f;
        if (fs::exists(p) && Rml::LoadFontFace(p.generic_string())) any = true;
    }
#ifdef _WIN32
    if (!any && Rml::LoadFontFace("C:/Windows/Fonts/segoeui.ttf")) any = true;
    Rml::LoadFontFace("C:/Windows/Fonts/seguisym.ttf", /*fallback_face=*/true);
#endif
    return any;
}

// First arg of a data-event expression as an int (e.g. rebind_key(5) -> 5).
int arg_int(const Rml::VariantList& args, int fallback = 0) {
    if (args.empty()) return fallback;
    return args[0].Get<int>(fallback);
}

} // namespace

// ----------------------------------------------------------------------------
// run()
// ----------------------------------------------------------------------------

Result run(SDL_Window* window, void* /*gl_context*/,
           Settings& io, const GameInfo& game,
           const char* assets_dir, const char* initial_rom,
           char* out_rom_path, size_t out_rom_path_len) {

    if (out_rom_path && out_rom_path_len) out_rom_path[0] = '\0';

    Rml::String gl_msg;
    if (!RmlGL3::Initialize(&gl_msg)) {
        std::fprintf(stderr, "launcher: RmlGL3::Initialize failed: %s\n", gl_msg.c_str());
        return Result::Unavailable;
    }

    SystemInterface_SDL system_interface;
    system_interface.SetWindow(window);
    LauncherRenderInterface render_interface;
    Rml::SetSystemInterface(&system_interface);
    Rml::SetRenderInterface(&render_interface);
    if (!Rml::Initialise()) {
        std::fprintf(stderr, "launcher: Rml::Initialise failed\n");
        RmlGL3::Shutdown();
        return Result::Unavailable;
    }

    const fs::path assets = assets_dir ? fs::path(assets_dir) : fs::current_path();
    const fs::path exe_dir = assets.parent_path();
    if (!load_fonts(assets))
        std::fprintf(stderr, "launcher: warning — no font face loaded; text will not render\n");

    int win_w = 0, win_h = 0;
    SDL_GL_GetDrawableSize(window, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) { win_w = 1024; win_h = 768; }
    render_interface.SetViewport(win_w, win_h);

    Rml::Context* context = Rml::CreateContext("launcher", Rml::Vector2i(win_w, win_h));
    if (!context) {
        Rml::Shutdown(); RmlGL3::Shutdown();
        return Result::Unavailable;
    }

    Model m;
    m.game_name   = game.name ? game.name : "Genesis Game";
    m.game_region = game.region ? game.region : "";
    m.uses_sram   = game.uses_sram;
    m.widescreen_supported = game.widescreen_supported;
    // Per-game box art: img/boxart-<short_name>.png (lowercased), so several
    // game targets sharing one launcher/ asset dir each show their own cover;
    // fall back to the generic img/boxart.png (single-game repos).
    {
        std::string boxsrc;
        if (game.short_name && *game.short_name) {
            std::string key = game.short_name;
            for (char& ch : key) if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
            std::string cand = "img/boxart-" + key + ".png";
            if (fs::exists(assets / cand)) boxsrc = cand;
        }
        if (boxsrc.empty() && fs::exists(assets / "img" / "boxart.png"))
            boxsrc = "img/boxart.png";
        m.has_boxart = !boxsrc.empty();
        if (m.has_boxart)
            m.boxart_html = "<img class=\"boxart-img\" src=\"" + Rml::String(boxsrc.c_str()) + "\"/>";
    }

    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    m.pad_names = enumerate_pads();
    std::vector<SDL_GameController*> open_pads;
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i))
            if (SDL_GameController* gc = SDL_GameControllerOpen(i))
                open_pads.push_back(gc);
    for (int p = 0; p < 2; p++) {
        // Device selection is exclusive (None/Keyboard/Gamepad). Migrate any
        // legacy combined value so the dropdown shows a valid selection.
        if (g_input_map.p[p].device == INPUT_DEV_BOTH)
            g_input_map.p[p].device = INPUT_DEV_KEYBOARD;
        // A gamepad source only makes sense when a controller is plugged in.
        int dev = g_input_map.p[p].device;
        if ((dev & INPUT_DEV_GAMEPAD) && p >= (int)m.pad_names.size())
            g_input_map.p[p].device = INPUT_DEV_KEYBOARD;
    }
    build_src_options(m.p1_options, 0, m.pad_names);
    build_src_options(m.p2_options, 1, m.pad_names);

    refresh_settings_labels(m, io);
    refresh_player_status(m);

    std::string rom_path = initial_rom ? initial_rom : "";
    load_rom_info(m, game, rom_path);
    refresh_save_info(m, exe_dir, rom_path);
    m.skip_launcher = io.skip_launcher;
    rebuild_bind_table(m);

    Rml::DataModelConstructor c = context->CreateDataModel("launcher");
    if (!c) { Rml::Shutdown(); RmlGL3::Shutdown(); return Result::Unavailable; }

    c.Bind("view", &m.view);
    c.Bind("game_name", &m.game_name);
    c.Bind("game_region", &m.game_region);
    c.Bind("has_boxart", &m.has_boxart);
    c.Bind("boxart_html", &m.boxart_html);
    c.Bind("rom_loaded", &m.rom_loaded);
    c.Bind("rom_title", &m.rom_title);
    c.Bind("rom_file", &m.rom_file);
    c.Bind("rom_size", &m.rom_size);
    c.Bind("rom_header", &m.rom_header);
    c.Bind("rom_crc", &m.rom_crc);
    c.Bind("rom_sha", &m.rom_sha);
    c.Bind("crc_known", &m.crc_known);
    c.Bind("crc_match", &m.crc_match);
    c.Bind("p1_status", &m.p1_status);
    c.Bind("p2_status", &m.p2_status);
    c.Bind("p1_enabled", &m.p1_enabled);
    c.Bind("p2_enabled", &m.p2_enabled);
    c.Bind("uses_sram", &m.uses_sram);
    c.Bind("save_file", &m.save_file);
    c.Bind("save_size", &m.save_size);
    c.Bind("skip_launcher", &m.skip_launcher);
    c.Bind("show_skip_modal", &m.show_skip_modal);
    c.Bind("scale_label", &m.scale_label);
    c.Bind("filter", &m.filter);
    c.Bind("volume", &m.volume);
    c.Bind("widescreen", &m.widescreen);
    c.Bind("widescreen_supported", &m.widescreen_supported);
    c.Bind("ws_cells_label", &m.ws_cells_label);
    c.Bind("cfg_player_label", &m.cfg_player_label);
    c.Bind("cfg_padtype_label", &m.cfg_padtype_label);
    c.Bind("cfg_deadzone", &m.cfg_deadzone);
    c.Bind("bind_table", &m.bind_table);
    c.Bind("capturing", &m.capturing);
    c.Bind("capture_prompt", &m.capture_prompt);

    Rml::DataModelHandle handle = c.GetModelHandle();
    Result result = Result::Quit;
    bool running = true;
    auto dirty_all = [&]() { handle.DirtyAllVariables(); };

    // ---- navigation ----
    c.BindEventCallback("show_dashboard", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        m.view = "dashboard"; m.capturing = false; dirty_all();
    });
    c.BindEventCallback("show_settings", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        m.view = "settings"; dirty_all();
    });
    auto open_cfg = [&](int p) {
        m.cfg_player = p; m.cfg_player_label = p == 0 ? "1" : "2";
        rebuild_bind_table(m);
        m.view = "controller"; m.capturing = false; dirty_all();
    };
    c.BindEventCallback("config_p1", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { open_cfg(0); });
    c.BindEventCallback("config_p2", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { open_cfg(1); });

    // ---- ROM ----
    c.BindEventCallback("change_rom", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        char buf[1024];
        if (pick_file("Select Genesis / Mega Drive ROM",
                      "Genesis ROMs (*.bin;*.md;*.gen;*.smd)\0*.bin;*.md;*.gen;*.smd\0All Files (*.*)\0*.*\0",
                      buf, sizeof(buf))) {
            rom_path = buf;
            load_rom_info(m, game, rom_path);
            refresh_save_info(m, exe_dir, rom_path);
            dirty_all();
        }
    });

    // ---- saves ----
    auto srm_path = [&]() -> fs::path {
        if (rom_path.empty()) return {};
        std::string base = basename_of(rom_path);
        size_t dot = base.find_last_of('.');
        if (dot != std::string::npos) base = base.substr(0, dot);
        return exe_dir / (base + ".srm");
    };
    c.BindEventCallback("save_import", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        if (!m.uses_sram) return;
        char buf[1024];
        if (!pick_file("Import SRAM Save", "Genesis saves (*.srm;*.sav)\0*.srm;*.sav\0All Files (*.*)\0*.*\0",
                       buf, sizeof(buf))) return;
        std::error_code ec;
        fs::path dst = srm_path();
        if (dst.empty()) return;
        if (fs::exists(dst, ec)) fs::copy_file(dst, fs::path(dst.string()+".bak"),
                                               fs::copy_options::overwrite_existing, ec);
        fs::copy_file(fs::path(buf), dst, fs::copy_options::overwrite_existing, ec);
        refresh_save_info(m, exe_dir, rom_path); dirty_all();
    });
    c.BindEventCallback("save_clear", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        if (!m.uses_sram) return;
        std::error_code ec;
        fs::path dst = srm_path();
        if (!dst.empty() && fs::exists(dst, ec)) {
            fs::copy_file(dst, fs::path(dst.string()+".bak"), fs::copy_options::overwrite_existing, ec);
            fs::remove(dst, ec);
        }
        refresh_save_info(m, exe_dir, rom_path); dirty_all();
    });

    // ---- skip launcher ----
    c.BindEventCallback("toggle_skip_launcher", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        if (io.skip_launcher) { io.skip_launcher = false; m.skip_launcher = false; }
        else                  { m.show_skip_modal = true; }
        dirty_all();
    });
    c.BindEventCallback("skip_modal_confirm", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        io.skip_launcher = true; m.skip_launcher = true; m.show_skip_modal = false; dirty_all();
    });
    c.BindEventCallback("skip_modal_cancel", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        m.show_skip_modal = false; dirty_all();
    });

    // ---- display / audio settings ----
    c.BindEventCallback("cycle_scale", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        io.window_scale = io.window_scale >= 6 ? 1 : io.window_scale + 1; refresh_settings_labels(m, io); dirty_all();
    });
    c.BindEventCallback("toggle_filter", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        io.linear_filter = !io.linear_filter; m.filter = io.linear_filter; dirty_all();
    });
    c.BindEventCallback("toggle_widescreen", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        io.widescreen = !io.widescreen; m.widescreen = io.widescreen; dirty_all();
    });
    c.BindEventCallback("ws_cells_down", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        io.widescreen_cells = io.widescreen_cells <= 1 ? 1 : io.widescreen_cells - 1; refresh_settings_labels(m, io); dirty_all();
    });
    c.BindEventCallback("ws_cells_up", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        io.widescreen_cells = io.widescreen_cells >= 16 ? 16 : io.widescreen_cells + 1; refresh_settings_labels(m, io); dirty_all();
    });
    c.BindEventCallback("vol_up", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        io.volume = io.volume >= 100 ? 100 : io.volume + 5; m.volume = io.volume; dirty_all();
    });
    c.BindEventCallback("vol_down", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        io.volume = io.volume <= 0 ? 0 : io.volume - 5; m.volume = io.volume; dirty_all();
    });

    // ---- controller config: pad type, deadzone, rebind, reset ----
    c.BindEventCallback("toggle_padtype", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        PlayerInput* pi = &g_input_map.p[m.cfg_player];
        pi->pad_type = (pi->pad_type == PAD_6BUTTON) ? PAD_3BUTTON : PAD_6BUTTON;
        rebuild_bind_table(m); dirty_all();
    });
    c.BindEventCallback("cfg_dz_up", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        PlayerInput* pi = &g_input_map.p[m.cfg_player];
        pi->deadzone_pct = pi->deadzone_pct >= 100 ? 100 : pi->deadzone_pct + 5;
        m.cfg_deadzone = pi->deadzone_pct; dirty_all();
    });
    c.BindEventCallback("cfg_dz_down", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        PlayerInput* pi = &g_input_map.p[m.cfg_player];
        pi->deadzone_pct = pi->deadzone_pct <= 0 ? 0 : pi->deadzone_pct - 5;
        m.cfg_deadzone = pi->deadzone_pct; dirty_all();
    });
    c.BindEventCallback("cfg_reset", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        // Re-derive defaults, then keep only this player's portion.
        InputMap saved = g_input_map;
        input_map_init_defaults();
        InputMap defaults = g_input_map;
        g_input_map = saved;
        g_input_map.p[m.cfg_player] = defaults.p[m.cfg_player];
        rebuild_bind_table(m); refresh_player_status(m); dirty_all();
    });

    int capture_button = 0; int capture_kind = 0; // 1 = key, 2 = pad
    c.BindEventCallback("rebind_key", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a) {
        capture_button = arg_int(a); capture_kind = 1; m.capturing = true;
        char p[96]; std::snprintf(p, sizeof(p), "Press a key for \xE2\x80\x9C%s\xE2\x80\x9D \xE2\x80\x94 Esc to cancel",
                                  input_button_name((GenesisButton)capture_button));
        m.capture_prompt = p; dirty_all();
    });
    c.BindEventCallback("rebind_pad", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a) {
        capture_button = arg_int(a); capture_kind = 2; m.capturing = true;
        char p[96]; std::snprintf(p, sizeof(p), "Press a controller button/axis for \xE2\x80\x9C%s\xE2\x80\x9D \xE2\x80\x94 Esc to cancel",
                                  input_button_name((GenesisButton)capture_button));
        m.capture_prompt = p; dirty_all();
    });

    // ---- play / quit ----
    c.BindEventCallback("play", [&](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        if (rom_path.empty()) {
            char buf[1024];
            if (pick_file("Select Genesis / Mega Drive ROM",
                          "Genesis ROMs (*.bin;*.md;*.gen;*.smd)\0*.bin;*.md;*.gen;*.smd\0All Files (*.*)\0*.*\0",
                          buf, sizeof(buf))) {
                rom_path = buf; load_rom_info(m, game, rom_path); refresh_save_info(m, exe_dir, rom_path);
            } else { return; }
        }
        if (out_rom_path && out_rom_path_len)
            std::snprintf(out_rom_path, out_rom_path_len, "%s", rom_path.c_str());
        result = Result::Launch; running = false;
    });

    Rml::ElementDocument* doc = context->LoadDocument((assets / "launcher.rml").generic_string());
    if (!doc) {
        std::fprintf(stderr, "launcher: failed to load launcher.rml — booting without launcher\n");
        Rml::Shutdown(); RmlGL3::Shutdown();
        return Result::Unavailable;
    }
    doc->Show();

    // ---- device <select> dropdowns ----
    struct SelListener : Rml::EventListener {
        std::function<void()> on_change;
        void ProcessEvent(Rml::Event&) override { if (on_change) on_change(); }
    };
    std::vector<std::unique_ptr<SelListener>> sel_listeners;
    auto setup_select = [&](const char* id, int p) {
        auto* sel = rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(doc->GetElementById(id));
        if (!sel) return;
        sel->RemoveAll();
        const std::vector<SrcOption>& opts = (p == 0) ? m.p1_options : m.p2_options;
        Rml::String cur = device_to_value(g_input_map.p[p].device);
        int selected = 0;
        for (int i = 0; i < (int)opts.size(); i++) {
            sel->Add(opts[i].label, opts[i].value);
            if (opts[i].value == cur) selected = i;
        }
        sel->SetSelection(selected);
        auto lis = std::make_unique<SelListener>();
        lis->on_change = [&, sel, p]() {
            g_input_map.p[p].device = value_to_device(sel->GetValue());
            refresh_player_status(m); dirty_all();
        };
        sel->AddEventListener(Rml::EventId::Change, lis.get());
        sel_listeners.push_back(std::move(lis));
    };
    setup_select("p1src", 0);
    setup_select("p2src", 1);

    if (auto* pb = doc->GetElementById("play")) pb->Focus();

    // ---- gamepad menu navigation ----
    auto nav_back = [&]() { if (m.view != "dashboard") { m.view = "dashboard"; m.capturing = false; dirty_all(); } };
    auto pad_move = [&](int dir) { context->ProcessKeyDown(Rml::Input::KI_TAB, dir < 0 ? (int)Rml::Input::KM_SHIFT : 0); };
    int pad_zone_x = 0, pad_zone_y = 0;
    auto handle_pad_nav = [&](const SDL_Event& e) {
        if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: pad_move(+1); break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  pad_move(-1); break;
                case SDL_CONTROLLER_BUTTON_A: context->ProcessKeyDown(Rml::Input::KI_RETURN, 0); break;
                case SDL_CONTROLLER_BUTTON_B: nav_back(); break;
                case SDL_CONTROLLER_BUTTON_START: if (auto* pb = doc->GetElementById("play")) pb->Click(); break;
                default: break;
            }
        } else if (e.type == SDL_CONTROLLERAXISMOTION) {
            const int TH = 18000;
            if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                int z = e.caxis.value > TH ? 1 : e.caxis.value < -TH ? -1 : 0;
                if (z != pad_zone_y) { pad_zone_y = z; if (z) pad_move(z); }
            } else if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
                int z = e.caxis.value > TH ? 1 : e.caxis.value < -TH ? -1 : 0;
                if (z != pad_zone_x) { pad_zone_x = z; if (z) pad_move(z); }
            }
        }
    };

    // Finish a rebind capture with the captured key/pad input.
    auto finish_capture = [&](int kind, int code, int axis_dir) {
        PlayerInput* pi = &g_input_map.p[m.cfg_player];
        if (kind == 1) pi->key[capture_button] = code;
        else { pi->pad[capture_button].kind = (axis_dir==0)?GP_BIND_BUTTON:GP_BIND_AXIS;
               pi->pad[capture_button].code = code; pi->pad[capture_button].axis_dir = axis_dir; }
        m.capturing = false; rebuild_bind_table(m); dirty_all();
    };

    // ---- main loop ----
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { result = Result::Quit; running = false; }
            else if (ev.type == SDL_WINDOWEVENT &&
                     (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                      ev.window.event == SDL_WINDOWEVENT_RESIZED)) {
                SDL_GL_GetDrawableSize(window, &win_w, &win_h);
                render_interface.SetViewport(win_w, win_h);
                context->SetDimensions(Rml::Vector2i(win_w, win_h));
                RmlSDL::InputEventHandler(context, ev);
            }
            // ---- rebind capture: swallow the next input, assign it ----
            else if (m.capturing && capture_kind == 1 && ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) { m.capturing = false; dirty_all(); }
                else finish_capture(1, ev.key.keysym.scancode, 0);
            }
            else if (m.capturing && capture_kind == 2 && ev.type == SDL_CONTROLLERBUTTONDOWN) {
                finish_capture(2, ev.cbutton.button, 0);
            }
            else if (m.capturing && capture_kind == 2 && ev.type == SDL_CONTROLLERAXISMOTION &&
                     (ev.caxis.value > 20000 || ev.caxis.value < -20000)) {
                finish_capture(2, ev.caxis.axis, ev.caxis.value < 0 ? -1 : +1);
            }
            else if (!m.capturing && (ev.type == SDL_CONTROLLERBUTTONDOWN ||
                                      ev.type == SDL_CONTROLLERAXISMOTION)) {
                handle_pad_nav(ev);
            }
            else if (ev.type == SDL_CONTROLLERDEVICEADDED) {
                if (SDL_GameController* gc = SDL_GameControllerOpen(ev.cdevice.which)) open_pads.push_back(gc);
            }
            else {
                RmlSDL::InputEventHandler(context, ev);
            }
        }

        context->Update();
        render_interface.Clear();
        render_interface.BeginFrame();
        context->Render();
        render_interface.EndFrame();
        SDL_GL_SwapWindow(window);
        SDL_Delay(8);
    }

    for (SDL_GameController* gc : open_pads) SDL_GameControllerClose(gc);
    Rml::Shutdown();
    RmlGL3::Shutdown();
    return result;
}

} // namespace genesis_launcher

// ----------------------------------------------------------------------------
// C entry point (launcher_capi.h) — owns the launcher window/GL context.
// ----------------------------------------------------------------------------

#include "launcher_capi.h"

extern "C" int genesis_launcher_run_window(const char* window_title,
                                           GenesisLauncherCSettings* io,
                                           const GenesisLauncherCGameInfo* game,
                                           const char* assets_dir,
                                           const char* initial_rom,
                                           char* out_rom_path,
                                           size_t out_rom_path_len) {
    using namespace genesis_launcher;
    if (!io || !game) return 2;

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "launcher: SDL video init failed: %s\n", SDL_GetError());
        return 2;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* win = SDL_CreateWindow(
        window_title ? window_title : "Sega Genesis Launcher",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1100, 740,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!win) { std::fprintf(stderr, "launcher: window creation failed: %s\n", SDL_GetError()); return 2; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { std::fprintf(stderr, "launcher: GL context creation failed: %s\n", SDL_GetError());
                SDL_DestroyWindow(win); return 2; }
    SDL_GL_MakeCurrent(win, ctx);
    SDL_GL_SetSwapInterval(1);

    Settings s;
    s.window_scale     = io->window_scale;
    s.fullscreen       = io->fullscreen;
    s.linear_filter    = io->linear_filter != 0;
    s.widescreen       = io->widescreen != 0;
    s.widescreen_cells = io->widescreen_cells;
    s.volume           = io->volume;
    s.skip_launcher    = io->skip_launcher != 0;

    GameInfo g;
    g.name             = game->name;
    g.short_name       = game->short_name;
    g.region           = game->region;
    g.expected_crc     = game->expected_crc;
    g.has_expected_crc = game->has_expected_crc != 0;
    g.expected_size    = game->expected_size;
    g.uses_sram        = game->uses_sram != 0;
    g.widescreen_supported = game->widescreen_supported != 0;

    Result r = run(win, ctx, s, g, assets_dir, initial_rom, out_rom_path, out_rom_path_len);

    io->window_scale     = s.window_scale;
    io->fullscreen       = s.fullscreen;
    io->linear_filter    = s.linear_filter;
    io->widescreen       = s.widescreen;
    io->widescreen_cells = s.widescreen_cells;
    io->volume           = s.volume;
    io->skip_launcher    = s.skip_launcher;

    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_GL_ResetAttributes();

    return r == Result::Launch ? 0 : (r == Result::Quit ? 1 : 2);
}
