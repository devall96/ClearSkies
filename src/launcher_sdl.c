// Clear Skies: the launcher for Linux (and the Steam Deck). A mod for Savage Wind that adds Cemu's Clarity presets.
//
// The same window as ClearSkies.exe on Windows: pick the Clarity preset and the Savage Wind AppImage, then Play.
// Both are saved to clearskies.ini, and the game is started with the Clear Skies layer turned on for that one process
// (through the Vulkan loader's environment variables: nothing is installed system-wide, and the game starts normally
// without the launcher). Drawn with SDL3 (X11 and Wayland) and stb_truetype, in software, one picture per change.
//
//   clearskies                         the window
//   clearskies --play                  start straight away with the saved settings
//   clearskies --preset <name>         choose a preset (saved), e.g. --preset filmic --play
//   clearskies --game <path>           choose the Savage Wind AppImage (saved)
//   clearskies --list                  list the presets
//   clearskies ... --no-play           only save the choices
//   clearskies ... -- <args>           everything after -- goes to the game (e.g. -- --config)
//   clearskies <command...>            run that command with the layer on: Steam's launch options, "... %command%"
//
// Settings: ~/.config/ClearSkies/clearskies.ini ($XDG_CONFIG_HOME), because an AppImage can't change itself; or
// clearskies.ini next to the program, when there is one and it can be written (a portable folder). The layer's log,
// clearskies.log, is written next to the ini.
//
// The game is started with exec: this process becomes the game. Inside an AppImage that matters, because the
// AppImage stays mounted (with the layer in it) only while this process runs.
#include <SDL3/SDL.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../third_party/stb/stb_truetype.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include "cemu_presets.h"

#define LAYER_NAME "VK_LAYER_CLEARSKIES_savage_wind"
#define APP_TITLE "Clear Skies"
#define APP_VERSION "0.2.1"
#ifdef _WIN32
#define LAYER_FILE "clearskies_layer.dll"
#define GAME_WORD "savagewind.exe"
#else
#define LAYER_FILE "libclearskies_layer.so"
#define GAME_WORD "the Savage Wind AppImage"
#endif
#define PATH_CAP 4096

static char g_data[PATH_CAP];  // the layer, its manifest, the default clearskies.ini, fonts, LICENSES.txt (ends in /)
static char g_ini[PATH_CAP];
static char g_game[PATH_CAP];
static char g_preset[64] = "serfrost";
static char **g_game_args;  // after --
static int g_game_nargs;

static int exists(const char *p) {
    SDL_PathInfo i;
    return p && p[0] && SDL_GetPathInfo(p, &i) && i.type == SDL_PATHTYPE_FILE;
}
#ifndef _WIN32
static int dir_exists(const char *p) {
    SDL_PathInfo i;
    return p && p[0] && SDL_GetPathInfo(p, &i) && i.type == SDL_PATHTYPE_DIRECTORY;
}
#endif

// ------------------------------------------------------------------ clearskies.ini

// One key in one section; comments after ; or # are left out. Empty when missing.
static void ini_get(const char *sec, const char *key, char *out, size_t cap) {
    out[0] = 0;
    size_t n;
    char *f = SDL_LoadFile(g_ini, &n);
    if (!f) return;
    char cur[128] = "";
    for (char *line = f, *next; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        char *e = line + strlen(line);
        while (e > line && (e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
        while (*line == ' ' || *line == '\t') line++;
        if (*line == '[') {
            char *end = strchr(line, ']');
            if (end) { *end = 0; SDL_strlcpy(cur, line + 1, sizeof cur); }
            continue;
        }
        if (*line == ';' || *line == '#' || SDL_strcasecmp(cur, sec)) continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = line, *v = eq + 1;
        for (char *t = eq; t > k && (t[-1] == ' ' || t[-1] == '\t'); ) *--t = 0;
        if (SDL_strcasecmp(k, key)) continue;
        char *c = strpbrk(v, ";#");
        if (c) *c = 0;
        while (*v == ' ' || *v == '\t') v++;
        for (char *t = v + strlen(v); t > v && (t[-1] == ' ' || t[-1] == '\t'); ) *--t = 0;
        SDL_strlcpy(out, v, cap);
        break;
    }
    SDL_free(f);
}

// Sets one key, keeping every other line (and the file's CRLF line endings).
static int ini_set(const char *sec, const char *key, const char *val) {
    size_t n = 0;
    char *f = SDL_LoadFile(g_ini, &n);
    size_t cap = n + strlen(sec) + strlen(key) + strlen(val) + 64;
    char *out = SDL_malloc(cap), cur[128] = "";
    if (!out) { SDL_free(f); return 0; }
    size_t o = 0;
    int done = 0;
#define EMIT(...) (o += (size_t)SDL_snprintf(out + o, cap - o, __VA_ARGS__))
    for (char *line = f, *next; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        size_t len = strlen(line);
        if (len && line[len - 1] == '\r') line[--len] = 0;
        const char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '[') {
            if (!done && !SDL_strcasecmp(cur, sec)) { EMIT("%s = %s\r\n", key, val); done = 1; }
            const char *end = strchr(s, ']');
            size_t m = end ? (size_t)(end - s - 1) : 0;
            if (m >= sizeof cur) m = sizeof cur - 1;
            memcpy(cur, s + 1, m);
            cur[m] = 0;
        } else if (!done && !SDL_strcasecmp(cur, sec) && *s != ';' && *s != '#') {
            const char *eq = strchr(s, '=');
            if (eq) {
                const char *ke = eq;
                while (ke > s && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
                if ((size_t)(ke - s) == strlen(key) && !SDL_strncasecmp(s, key, (size_t)(ke - s))) {
                    EMIT("%s = %s\r\n", key, val);
                    done = 1;
                    continue;
                }
            }
        }
        EMIT("%s\r\n", line);
    }
    if (!done) {
        if (SDL_strcasecmp(cur, sec)) EMIT("%s[%s]\r\n", o ? "\r\n" : "", sec);
        EMIT("%s = %s\r\n", key, val);
    }
#undef EMIT
    SDL_free(f);
    int ok = SDL_SaveFile(g_ini, out, o);
    SDL_free(out);
    return ok;
}

static void load_ini(void) {
    char v[PATH_CAP];
    ini_get("launcher", "game", v, sizeof v);
    if (v[0]) SDL_strlcpy(g_game, v, sizeof g_game);
    ini_get("clarity", "preset", v, sizeof v);
    if (v[0]) SDL_strlcpy(g_preset, v, sizeof g_preset);
}

static void save_ini(void) {
    ini_set("clarity", "preset", g_preset);
    ini_set("launcher", "game", g_game);
}

// ------------------------------------------------------------------ where things are

static void find_files(void) {
    const char *base = SDL_GetBasePath();  // (the program's folder, ending in /)
    if (!base) base = "./";
    char p[PATH_CAP];
    SDL_snprintf(p, sizeof p, "%s../share/clearskies/VkLayer_clearskies.json", base);  // (inside the AppImage)
    if (exists(p)) SDL_snprintf(g_data, sizeof g_data, "%s../share/clearskies/", base);
    else SDL_strlcpy(g_data, base, sizeof g_data);

    // A portable folder keeps its settings next to the program; an AppImage can't, so they go to ~/.config.
    SDL_snprintf(p, sizeof p, "%sclearskies.ini", base);
    int portable = exists(p) && !SDL_getenv("APPIMAGE");
#ifndef _WIN32
    portable = portable && access(p, W_OK) == 0;
#else
    portable = 1;
#endif
    if (portable) { SDL_strlcpy(g_ini, p, sizeof g_ini); return; }
    const char *xdg = SDL_getenv("XDG_CONFIG_HOME"), *home = SDL_getenv("HOME");
    char dir[PATH_CAP];
    if (xdg && xdg[0] == '/') SDL_snprintf(dir, sizeof dir, "%s/ClearSkies", xdg);
    else SDL_snprintf(dir, sizeof dir, "%s/.config/ClearSkies", home ? home : ".");
    SDL_CreateDirectory(dir);
    SDL_snprintf(g_ini, sizeof g_ini, "%s/clearskies.ini", dir);
    if (!exists(g_ini)) {  // the first run: start from the shipped clearskies.ini, with its list of presets
        SDL_snprintf(p, sizeof p, "%sclearskies.ini", g_data);
        size_t n;
        void *d = SDL_LoadFile(p, &n);
        if (d) { SDL_SaveFile(g_ini, d, n); SDL_free(d); }
    }
}

// ------------------------------------------------------------------ presets

// Row 0 is Off, then Cemu's presets in Cemu's order.
static const char *row_id(int row) { return row <= 0 ? "off" : k_presets[row - 1].id; }
static const char *row_title(int row) { return row <= 0 ? "Off (the game's own look)" : k_presets[row - 1].title; }
#define NROWS (CLARITY_NPRESETS + 1)

static int preset_row(const char *id) {
    if (!SDL_strcasecmp(id, "off") || !SDL_strcasecmp(id, "disabled") || !SDL_strcasecmp(id, "none")) return 0;
    if (!SDL_strcasecmp(id, "recommended")) id = "serfrost";
    if (!SDL_strcasecmp(id, "dim")) id = "serfrost_dim";
    if (!SDL_strcasecmp(id, "bright")) id = "serfrost_bright";
    for (int i = 0; i < CLARITY_NPRESETS; i++)
        if (!SDL_strcasecmp(id, k_presets[i].id)) return i + 1;
    return -1;
}

// ------------------------------------------------------------------ messages

static SDL_Window *g_win;

static void tell(SDL_MessageBoxFlags kind, const char *msg) {
#ifndef _WIN32
    if (!g_win && isatty(2)) { fprintf(stderr, "%s: %s\n", APP_TITLE, msg); return; }  // (started from a terminal)
#endif
    if (!SDL_ShowSimpleMessageBox(kind, APP_TITLE, msg, g_win)) fprintf(stderr, "%s: %s\n", APP_TITLE, msg);
}

#ifndef _WIN32
static int ask(SDL_MessageBoxFlags kind, const char *msg, const char *yes, const char *no) {
    const SDL_MessageBoxButtonData b[] = {
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, no},
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, yes},
    };
    SDL_MessageBoxData d = {kind, g_win, APP_TITLE, msg, 2, b, NULL};
    int r = 0;
    if (!SDL_ShowMessageBox(&d, &r)) { fprintf(stderr, "%s: %s\n", APP_TITLE, msg); return 1; }
    return r == 1;
}
#endif

// ------------------------------------------------------------------ starting the game

static void prepend_env(const char *name, const char *value, const char *sep) {
    const char *old = SDL_getenv(name);
    if (old && strstr(old, value)) return;
    char buf[8192];
    if (old && old[0]) SDL_snprintf(buf, sizeof buf, "%s%s%s", value, sep, old);
    else SDL_strlcpy(buf, value, sizeof buf);
    SDL_setenv_unsafe(name, buf, 1);
}

// The environment that turns the layer on for the game (and only the game: it's this process's own).
static int layer_env(void) {
    char layer[PATH_CAP], manifest[PATH_CAP];
    SDL_snprintf(layer, sizeof layer, "%s" LAYER_FILE, g_data);
    SDL_snprintf(manifest, sizeof manifest, "%sVkLayer_clearskies.json", g_data);
    if (!exists(layer) || !exists(manifest)) {
        tell(SDL_MESSAGEBOX_ERROR, LAYER_FILE " or VkLayer_clearskies.json is missing. Download Clear Skies again.");
        return 0;
    }
    char dir[PATH_CAP];
    SDL_strlcpy(dir, g_data, sizeof dir);
    size_t n = strlen(dir);
    if (n > 1 && (dir[n - 1] == '/' || dir[n - 1] == '\\')) dir[n - 1] = 0;
#ifdef _WIN32
    const char *sep = ";";
#else
    const char *sep = ":";
#endif
    prepend_env("VK_ADD_LAYER_PATH", dir, sep);  // (Vulkan loader 1.3.234 and newer)
    prepend_env("VK_LAYER_PATH", dir, sep);      // (older loaders)
    prepend_env("VK_INSTANCE_LAYERS", LAYER_NAME, sep);
    SDL_setenv_unsafe("CLEARSKIES_INI", g_ini, 1);
    // Clear Skies' own AppImage variables would otherwise reach the game (Savage Wind's AppImage sets its own)
    SDL_unsetenv_unsafe("APPDIR");
    SDL_unsetenv_unsafe("APPIMAGE");
    SDL_unsetenv_unsafe("ARGV0");
    SDL_unsetenv_unsafe("OWD");
    return 1;
}

static void run(char **argv) {  // never returns when it works
#ifdef _WIN32
    (void)argv;
    tell(SDL_MESSAGEBOX_INFORMATION, "This is a preview of the Linux launcher: on Windows, use ClearSkies.exe.");
#else
    if (g_win) { SDL_DestroyWindow(g_win); g_win = NULL; }
    SDL_Quit();
    execvp(argv[0], argv);
    char msg[PATH_CAP + 128];
    SDL_snprintf(msg, sizeof msg, "Couldn't start %s (%s).", argv[0], strerror(errno));
    tell(SDL_MESSAGEBOX_ERROR, msg);
    exit(1);
#endif
}

static int start_game(void) {
    if (!exists(g_game)) {
        tell(SDL_MESSAGEBOX_WARNING, "Choose " GAME_WORD " first (Browse).");
        return 0;
    }
#ifndef _WIN32
    if (access(g_game, X_OK) != 0) {
        if (!ask(SDL_MESSAGEBOX_INFORMATION,
                 "This Savage Wind AppImage isn't allowed to run as a program yet (downloaded files usually aren't).\n\n"
                 "Allow it now?", "Allow and play", "Cancel"))
            return 0;
        struct stat st;
        if (stat(g_game, &st) != 0 || chmod(g_game, st.st_mode | S_IXUSR | S_IXGRP | S_IXOTH) != 0) {
            tell(SDL_MESSAGEBOX_ERROR, "Couldn't allow it to run. Right-click it, open Properties, and tick "
                                       "\"Allow executing file as program\" (or run chmod +x on it).");
            return 0;
        }
    }
    // Savage Wind opens its own setup screen until the game files are copied (extracted/romfs in its data folder)
    const char *root = SDL_getenv("SAVAGEWIND_DATA_ROOT"), *xdg = SDL_getenv("XDG_DATA_HOME"), *home = SDL_getenv("HOME");
    char romfs[PATH_CAP];
    if (root && root[0]) SDL_snprintf(romfs, sizeof romfs, "%s/extracted/romfs", root);
    else if (xdg && xdg[0] == '/') SDL_snprintf(romfs, sizeof romfs, "%s/savagewind/extracted/romfs", xdg);
    else SDL_snprintf(romfs, sizeof romfs, "%s/.local/share/savagewind/extracted/romfs", home ? home : "");
    if (!dir_exists(romfs) &&
        !ask(SDL_MESSAGEBOX_INFORMATION,
             "This Savage Wind hasn't finished setting up its game files yet, so it will open its own setup screen "
             "first. Clarity applies once the game itself is running.\n\nStart anyway?", "Start", "Cancel"))
        return 0;
#endif
    if (!layer_env()) return 0;
    char dir[PATH_CAP];
    SDL_strlcpy(dir, g_game, sizeof dir);
    char *s = strrchr(dir, '/');
#ifndef _WIN32
    if (s) { *s = 0; if (chdir(dir) != 0) { /* (the game finds its files without it) */ } }
#else
    (void)s;
#endif
    char **argv = SDL_calloc((size_t)g_game_nargs + 2, sizeof *argv);
    argv[0] = g_game;
    for (int i = 0; i < g_game_nargs; i++) argv[i + 1] = g_game_args[i];
    run(argv);
    SDL_free(argv);
    return 0;
}

// ------------------------------------------------------------------ drawing: a little software canvas

typedef uint32_t Color;  // 0xRRGGBB
#define RGB(r, g, b) ((Color)(((r) << 16) | ((g) << 8) | (b)))
#define C_SKY_TOP RGB(0x2b, 0x7f, 0xd4)
#define C_SKY_LOW RGB(0xa8, 0xd8, 0xf6)
#define C_SUN RGB(0xff, 0xf1, 0xc2)
#define C_SUN_GLOW RGB(0xd8, 0xec, 0xf6)
#define C_CLOUD RGB(0xf7, 0xfb, 0xff)
#define C_BG RGB(0x10, 0x16, 0x1e)
#define C_PANEL RGB(0x19, 0x22, 0x2d)
#define C_HOVER RGB(0x23, 0x2e, 0x3b)
#define C_BORDER RGB(0x2c, 0x3a, 0x48)
#define C_TEXT RGB(0xe6, 0xee, 0xf5)
#define C_MUTED RGB(0x8a, 0x9d, 0xb0)
#define C_FAINT RGB(0x5f, 0x72, 0x85)
#define C_ACCENT RGB(0x4f, 0xb0, 0xf0)
#define C_ACCENT_HOT RGB(0x74, 0xc4, 0xf7)
#define C_ACCENT_TEXT RGB(0x05, 0x16, 0x26)
#define C_WHITE RGB(0xff, 0xff, 0xff)

static uint32_t *g_px;  // ARGB, g_pw x g_ph
static int g_pw, g_ph;
static float g_k = 1;  // pixels per layout unit
static int g_clip_y0, g_clip_y1;

static Color mix(Color a, Color b, float t) {
    int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255, br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
    return RGB((int)(ar + (br - ar) * t + 0.5f), (int)(ag + (bg - ag) * t + 0.5f), (int)(ab + (bb - ab) * t + 0.5f));
}

static void blend(int x, int y, Color c, float a) {
    if (x < 0 || y < g_clip_y0 || x >= g_pw || y >= g_clip_y1 || a <= 0) return;
    uint32_t *p = &g_px[y * g_pw + x];
    *p = 0xff000000u | (a >= 1 ? c : mix(*p & 0xffffff, c, a));
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

// Shapes take layout units and are drawn anti-aliased from their signed distance.
static void fill(float x, float y, float w, float h, Color c) {
    int x0 = (int)floorf(x * g_k), y0 = (int)floorf(y * g_k), x1 = (int)ceilf((x + w) * g_k), y1 = (int)ceilf((y + h) * g_k);
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++) blend(px, py, c, 1);
}

static void round_rect(float x, float y, float w, float h, float r, Color c) {
    float X = x * g_k, Y = y * g_k, W = w * g_k, H = h * g_k, R = r * g_k;
    float cx = X + W / 2, cy = Y + H / 2, hx = W / 2 - R, hy = H / 2 - R;
    for (int py = (int)floorf(Y); py < (int)ceilf(Y + H); py++)
        for (int px = (int)floorf(X); px < (int)ceilf(X + W); px++) {
            float qx = fabsf(px + 0.5f - cx) - hx, qy = fabsf(py + 0.5f - cy) - hy;
            float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
            float d = sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - R;
            blend(px, py, c, clampf(0.5f - d, 0, 1));
        }
}

static void framed(float x, float y, float w, float h, float r, Color fillc, Color edge) {
    round_rect(x, y, w, h, r, edge);
    float e = 1 / g_k;  // one pixel
    round_rect(x + e, y + e, w - 2 * e, h - 2 * e, r - e, fillc);
}

static void ellipse(float cx, float cy, float rx, float ry, Color c) {
    float CX = cx * g_k, CY = cy * g_k, RX = rx * g_k, RY = ry * g_k, m = fminf(RX, RY);
    for (int py = (int)floorf(CY - RY); py < (int)ceilf(CY + RY); py++)
        for (int px = (int)floorf(CX - RX); px < (int)ceilf(CX + RX); px++) {
            float dx = (px + 0.5f - CX) / RX, dy = (py + 0.5f - CY) / RY;
            blend(px, py, c, clampf(0.5f - (sqrtf(dx * dx + dy * dy) - 1) * m, 0, 1));
        }
}

static void line(float ax, float ay, float bx, float by, float width, Color c) {  // a round-capped stroke
    float AX = ax * g_k, AY = ay * g_k, BX = bx * g_k, BY = by * g_k, r = width * g_k / 2;
    int x0 = (int)floorf(fminf(AX, BX) - r - 1), x1 = (int)ceilf(fmaxf(AX, BX) + r + 1);
    int y0 = (int)floorf(fminf(AY, BY) - r - 1), y1 = (int)ceilf(fmaxf(AY, BY) + r + 1);
    float vx = BX - AX, vy = BY - AY, vv = vx * vx + vy * vy;
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++) {
            float wx = px + 0.5f - AX, wy = py + 0.5f - AY, t = vv > 0 ? clampf((wx * vx + wy * vy) / vv, 0, 1) : 0;
            float dx = wx - vx * t, dy = wy - vy * t;
            blend(px, py, c, clampf(0.5f - (sqrtf(dx * dx + dy * dy) - r), 0, 1));
        }
}

static void cloud(float x, float y, float s, Color c) {  // a soft little cumulus, s = its size
    ellipse(x, y, s, s * 0.6f, c);
    ellipse(x + s * 0.9f, y - s * 0.3f, s * 0.9f, s * 0.8f, c);
    ellipse(x + s * 1.9f, y, s * 0.9f, s * 0.6f, c);
    fill(x, y, s * 1.9f, s * 0.6f, c);
}

// ------------------------------------------------------------------ text (Fira Sans through stb_truetype)

typedef struct { stbtt_fontinfo info; unsigned char *data; } Font;
typedef struct { Font *font; float px; float scale; float ascent, descent, gap; } Face;
static Font g_medium, g_semibold;
enum { F_TITLE, F_SUB, F_BODY, F_SMALL, F_LABEL, F_BUTTON, F_COUNT };
static Face g_face[F_COUNT];

typedef struct { int face, cp, w, h, xoff, yoff; float adv; unsigned char *bmp; } Glyph;
static Glyph *g_glyphs;
static int g_nglyphs, g_capglyphs;

static int load_font(Font *f, const char *name) {
    const char *base = SDL_GetBasePath();
    char p[PATH_CAP];
    const char *dirs[] = {"%sfonts/%s", "%s../../third_party/fonts/FiraSans/%s", "%s../third_party/fonts/FiraSans/%s"};
    for (int i = 0; i < 3; i++) {
        SDL_snprintf(p, sizeof p, dirs[i], i ? (base ? base : "./") : g_data, name);
        size_t n;
        if ((f->data = SDL_LoadFile(p, &n)) && stbtt_InitFont(&f->info, f->data, stbtt_GetFontOffsetForIndex(f->data, 0)))
            return 1;
        SDL_free(f->data);
        f->data = NULL;
    }
    return 0;
}

static void set_faces(void) {
    static const struct { Font *f; float pt; } spec[F_COUNT] = {
        [F_TITLE] = {&g_semibold, 28}, [F_SUB] = {&g_medium, 11},  [F_BODY] = {&g_medium, 10},
        [F_SMALL] = {&g_medium, 9},    [F_LABEL] = {&g_semibold, 8}, [F_BUTTON] = {&g_semibold, 10},
    };
    for (int i = 0; i < F_COUNT; i++) {
        Face *fc = &g_face[i];
        fc->font = spec[i].f;
        fc->px = spec[i].pt * 96 / 72 * g_k;  // (the Windows launcher's sizes, in points at 96 dpi)
        fc->scale = stbtt_ScaleForMappingEmToPixels(&fc->font->info, fc->px);
        int a, d, g;
        stbtt_GetFontVMetrics(&fc->font->info, &a, &d, &g);
        fc->ascent = a * fc->scale, fc->descent = d * fc->scale, fc->gap = g * fc->scale;
    }
    for (int i = 0; i < g_nglyphs; i++) free(g_glyphs[i].bmp);  // (stb_truetype allocates with malloc)
    g_nglyphs = 0;
}

static Glyph *glyph(int face, int cp) {
    for (int i = 0; i < g_nglyphs; i++)
        if (g_glyphs[i].face == face && g_glyphs[i].cp == cp) return &g_glyphs[i];
    if (g_nglyphs == g_capglyphs) {
        g_capglyphs = g_capglyphs ? g_capglyphs * 2 : 256;
        g_glyphs = SDL_realloc(g_glyphs, (size_t)g_capglyphs * sizeof *g_glyphs);
    }
    Face *fc = &g_face[face];
    Glyph *gl = &g_glyphs[g_nglyphs++];
    memset(gl, 0, sizeof *gl);
    gl->face = face, gl->cp = cp;
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&fc->font->info, cp, &adv, &lsb);
    gl->adv = adv * fc->scale;
    gl->bmp = stbtt_GetCodepointBitmap(&fc->font->info, fc->scale, fc->scale, cp, &gl->w, &gl->h, &gl->xoff, &gl->yoff);
    return gl;
}

static int utf8(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    int c = *p++, n = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
    if (n) c &= 0x3f >> n;
    while (n-- && (*p & 0xc0) == 0x80) c = (c << 6) | (*p++ & 0x3f);
    *s = (const char *)p;
    return c;
}

static float text_width(int face, const char *s, size_t len) {  // in pixels
    float w = 0;
    const char *e = s + len;
    int prev = 0;
    while (s < e && *s) {
        int c = utf8(&s);
        if (prev) w += stbtt_GetCodepointKernAdvance(&g_face[face].font->info, prev, c) * g_face[face].scale;
        w += glyph(face, c)->adv;
        prev = c;
    }
    return w;
}

static void draw_run(int face, const char *s, size_t len, float px, float baseline, Color c) {  // pixels
    const char *e = s + len;
    int prev = 0;
    while (s < e && *s) {
        int cp = utf8(&s);
        if (prev) px += stbtt_GetCodepointKernAdvance(&g_face[face].font->info, prev, cp) * g_face[face].scale;
        Glyph *gl = glyph(face, cp);
        int x0 = (int)floorf(px + 0.5f) + gl->xoff, y0 = (int)floorf(baseline + 0.5f) + gl->yoff;
        for (int y = 0; y < gl->h; y++)
            for (int x = 0; x < gl->w; x++) {
                unsigned char a = gl->bmp[y * gl->w + x];
                if (a) blend(x0 + x, y0 + y, c, a / 255.0f);
            }
        px += gl->adv;
        prev = cp;
    }
}

enum { T_LEFT = 0, T_CENTER = 1, T_RIGHT = 2, T_VCENTER = 4, T_ELLIPSIS = 8, T_ELLIPSIS_START = 16, T_WRAP = 32 };

// Text in a box given in layout units, like DrawText.
static void text(const char *s, float x, float y, float w, float h, int face, Color c, int flags) {
    Face *fc = &g_face[face];
    float X = x * g_k, Y = y * g_k, W = w * g_k, H = h * g_k, lh = fc->ascent - fc->descent + fc->gap;
    if (flags & T_WRAP) {  // word wrap, top-aligned
        float base = Y + fc->ascent;
        const char *p = s;
        while (*p) {
            const char *end = p, *fit = NULL;
            for (;;) {
                const char *sp = strchr(end, ' ');
                const char *cand = sp ? sp : end + strlen(end);
                if (text_width(face, p, (size_t)(cand - p)) > W && fit) break;
                fit = cand;
                if (!sp) break;
                end = sp + 1;
            }
            draw_run(face, p, (size_t)(fit - p), X, base, c);
            p = *fit ? fit + 1 : fit;
            base += lh;
        }
        return;
    }
    char buf[PATH_CAP];
    SDL_strlcpy(buf, s, sizeof buf);
    float tw = text_width(face, buf, strlen(buf));
    if (tw > W && (flags & (T_ELLIPSIS | T_ELLIPSIS_START))) {
        const char *dots = "\xE2\x80\xA6";  // …
        size_t n = strlen(buf);
        if (flags & T_ELLIPSIS_START) {  // keep the end (a path's file name)
            const char *t = buf;
            char tmp[PATH_CAP];
            while (*t) {
                t++;
                while ((*t & 0xc0) == 0x80) t++;
                SDL_snprintf(tmp, sizeof tmp, "%s%s", dots, t);
                if (text_width(face, tmp, strlen(tmp)) <= W) break;
            }
            SDL_strlcpy(buf, tmp, sizeof buf);
        } else {
            while (n > 0) {
                do n--; while (n > 0 && (buf[n] & 0xc0) == 0x80);
                buf[n] = 0;
                char tmp[PATH_CAP];
                SDL_snprintf(tmp, sizeof tmp, "%s%s", buf, dots);
                if (text_width(face, tmp, strlen(tmp)) <= W) { SDL_strlcpy(buf, tmp, sizeof buf); break; }
            }
        }
        tw = text_width(face, buf, strlen(buf));
    }
    float px = (flags & T_CENTER) ? X + (W - tw) / 2 : (flags & T_RIGHT) ? X + W - tw : X;
    float base = (flags & T_VCENTER) ? Y + (H - (fc->ascent - fc->descent)) / 2 + fc->ascent : Y + fc->ascent;
    draw_run(face, buf, strlen(buf), px, base, c);
}

// ------------------------------------------------------------------ the window

// Layout, in the Windows launcher's 96-dpi units
#define W_CLIENT 560
#define H_CLIENT 470
#define H_HEADER 132
#define PAD 28
#define ROW_H 26

typedef struct { float x, y, w, h; } Box;
enum { H_NONE, H_COMBO, H_PATH, H_BROWSE, H_EDIT, H_PLAY, H_LICENSES, H_LIST };
static const Box k_box[] = {
    [H_COMBO] = {PAD, H_HEADER + 44, W_CLIENT - 2 * PAD, 32},
    [H_PATH] = {PAD, H_HEADER + 144, W_CLIENT - 2 * PAD - 108, 36},
    [H_BROWSE] = {W_CLIENT - PAD - 96, H_HEADER + 144, 96, 36},
    [H_EDIT] = {PAD, H_HEADER + 204, 140, 44},
    [H_PLAY] = {W_CLIENT - PAD - 160, H_HEADER + 204, 160, 44},
    [H_LICENSES] = {W_CLIENT - PAD - 140, H_CLIENT - 62 + 10, 140, 24},
};

static int g_row, g_hot, g_pressed, g_open, g_list_top, g_list_hot = -1;
static int g_list_rows;

static Box list_box(void) {
    Box c = k_box[H_COMBO];
    g_list_rows = (int)((H_CLIENT - 10 - (c.y + c.h + 2)) / ROW_H);
    if (g_list_rows > NROWS) g_list_rows = NROWS;
    return (Box){c.x, c.y + c.h + 2, c.w, (float)(g_list_rows * ROW_H + 2)};
}

static int inside(Box b, float x, float y) { return x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h; }

static int hit(float x, float y, int *list_row) {
    *list_row = -1;
    if (g_open) {
        Box l = list_box();
        if (inside(l, x, y)) {
            int r = g_list_top + (int)((y - l.y - 1) / ROW_H);
            *list_row = r < NROWS ? r : NROWS - 1;
            return H_LIST;
        }
    }
    for (int i = H_COMBO; i <= H_LICENSES; i++)
        if (inside(k_box[i], x, y)) return i;
    return H_NONE;
}

static void button(int id, const char *label) {
    Box b = k_box[id];
    int primary = id == H_PLAY, hot = g_hot == id && !g_open, pressed = hot && g_pressed == id;
    Color f = primary ? (pressed ? C_ACCENT : hot ? C_ACCENT_HOT : C_ACCENT) : (pressed ? C_BG : hot ? C_HOVER : C_PANEL);
    framed(b.x, b.y, b.w, b.h, 10, f, primary ? f : (hot ? C_ACCENT : C_BORDER));
    text(label, b.x, b.y + (pressed ? 1 : 0), b.w, b.h, F_BUTTON, primary ? C_ACCENT_TEXT : C_TEXT, T_CENTER | T_VCENTER);
}

static void paint(void) {
    g_clip_y0 = 0, g_clip_y1 = g_ph;
    for (int i = 0; i < g_pw * g_ph; i++) g_px[i] = 0xff000000u | C_BG;

    // Sky: the gradient with the sun's glow fading into it, then the sun and two clouds
    int hh = (int)ceilf(H_HEADER * g_k);
    float sx = (W_CLIENT - 92) * g_k, sy = 50 * g_k, glow = 80 * g_k;
    Color warm = mix(C_SUN, C_SUN_GLOW, 0.5f);
    for (int y = 0; y < hh && y < g_ph; y++) {
        Color base = mix(C_SKY_TOP, C_SKY_LOW, (float)y / hh);
        for (int x = 0; x < g_pw; x++) {
            float dx = x + 0.5f - sx, dy = y + 0.5f - sy, d2 = dx * dx + dy * dy, t = 0;
            if (d2 < glow * glow) { float f = 1 - d2 / (glow * glow); t = f * f * 0.7f; }
            g_px[y * g_pw + x] = 0xff000000u | mix(base, warm, t);
        }
    }
    g_clip_y1 = (int)(H_HEADER * g_k);
    ellipse(W_CLIENT - 92, 50, 26, 26, C_SUN);
    cloud(W_CLIENT - 190, 96, 22, mix(C_CLOUD, C_SKY_LOW, 0.25f));
    cloud(W_CLIENT - 120, 110, 30, C_CLOUD);
    g_clip_y1 = g_ph;
    text(APP_TITLE, PAD, 26, 320, 48, F_TITLE, C_WHITE, T_LEFT);
    text("Cemu's Clarity presets for Savage Wind", PAD + 2, 76, 360, 24, F_SUB, RGB(0xf2, 0xf8, 0xfd), T_LEFT);
    fill(0, H_HEADER, W_CLIENT, 1 / g_k, C_BORDER);

    // Look
    float y = H_HEADER + 22;
    text("LOOK", PAD, y, 200, 18, F_LABEL, C_MUTED, T_LEFT);
    Box c = k_box[H_COMBO];
    int chot = g_hot == H_COMBO || g_open;
    framed(c.x, c.y, c.w, c.h, 4, chot ? C_HOVER : C_PANEL, chot ? C_ACCENT : C_BORDER);
    text(row_title(g_row), c.x + 10, c.y, c.w - 44, c.h, F_BODY, C_TEXT, T_VCENTER | T_ELLIPSIS);
    float ax = c.x + c.w - 18, ay = c.y + c.h / 2;
    line(ax - 4, ay - 2, ax, ay + 2, 1.4f, C_MUTED);
    line(ax, ay + 2, ax + 4, ay - 2, 1.4f, C_MUTED);
    char desc[256];
    if (g_row <= 0) SDL_strlcpy(desc, "The game's own tone mapping, unchanged.", sizeof desc);
    else SDL_snprintf(desc, sizeof desc, "Cemu preset %d  \xC2\xB7  clearskies.ini: preset = %s", k_presets[g_row - 1].cemu_number, row_id(g_row));
    text(desc, PAD, y + 62, W_CLIENT - 2 * PAD, 20, F_SMALL, C_FAINT, T_ELLIPSIS);

    // Savage Wind
    y += 98;
    text("SAVAGE WIND", PAD, y, 200, 18, F_LABEL, C_MUTED, T_LEFT);
    Box p = k_box[H_PATH];
    int phot = g_hot == H_PATH && !g_open;
    framed(p.x, p.y, p.w, p.h, 8, C_PANEL, phot ? C_ACCENT : C_BORDER);
    if (g_game[0]) text(g_game, p.x + 12, p.y, p.w - 24, p.h, F_BODY, C_TEXT, T_VCENTER | T_ELLIPSIS_START);
    else text("Where is " GAME_WORD "?", p.x + 12, p.y, p.w - 24, p.h, F_BODY, C_FAINT, T_VCENTER | T_ELLIPSIS);
    button(H_BROWSE, "Browse");
    button(H_EDIT, "Edit settings");
    button(H_PLAY, "Play");

    // Footer
    float fy = H_CLIENT - 62;
    fill(0, fy, W_CLIENT, 1 / g_k, C_BORDER);
    text("A mod for Savage Wind. Not made by or affiliated with the Savage Wind team, Cemu or Nintendo.", PAD, fy + 12,
         W_CLIENT - 2 * PAD - 140, 40, F_SMALL, C_FAINT, T_WRAP);
    Box l = k_box[H_LICENSES];
    text("Licenses & credits", l.x, l.y, l.w, l.h, F_SMALL, g_hot == H_LICENSES && !g_open ? C_ACCENT_HOT : C_ACCENT,
         T_RIGHT | T_VCENTER);

    // The open preset list, over everything else
    if (g_open) {
        Box b = list_box();
        framed(b.x, b.y, b.w, b.h, 4, C_PANEL, C_BORDER);
        for (int i = 0; i < g_list_rows; i++) {
            int r = g_list_top + i;
            float ry = b.y + 1 + i * ROW_H;
            int sel = r == g_row, hot = r == g_list_hot;
            if (sel || hot) fill(b.x + 1, ry, b.w - 2, ROW_H, sel ? C_ACCENT : C_HOVER);
            text(row_title(r), b.x + 10, ry, b.w - 20, ROW_H, F_BODY, sel ? C_ACCENT_TEXT : C_TEXT, T_VCENTER | T_ELLIPSIS);
        }
        if (NROWS > g_list_rows) {  // where the visible rows are in the whole list
            float th = b.h - 8, kh = th * g_list_rows / NROWS, ky = b.y + 4 + (th - kh) * g_list_top / (NROWS - g_list_rows);
            round_rect(b.x + b.w - 7, ky, 3, kh, 1.5f, C_FAINT);
        }
    }
}

static void list_show(int row) {  // scrolls the open list so row is visible
    if (row < g_list_top) g_list_top = row;
    if (row >= g_list_top + g_list_rows) g_list_top = row - g_list_rows + 1;
    if (g_list_top > NROWS - g_list_rows) g_list_top = NROWS - g_list_rows;
    if (g_list_top < 0) g_list_top = 0;
}

static void open_list(void) {
    g_open = 1;
    list_box();
    g_list_top = g_row - g_list_rows / 2;
    list_show(g_row);
    g_list_hot = -1;
}

static void set_row(int r) {
    if (r < 0) r = 0;
    if (r >= NROWS) r = NROWS - 1;
    g_row = r;
    SDL_strlcpy(g_preset, row_id(r), sizeof g_preset);
}

// ------------------------------------------------------------------ Browse

static Uint32 g_ev_picked;

static void SDLCALL picked(void *ud, const char *const *files, int filter) {
    (void)ud; (void)filter;
    SDL_Event e;
    SDL_zero(e);
    e.type = g_ev_picked;
    if (!files) {  // no file dialog on this system
        e.user.code = 1;
        e.user.data1 = SDL_strdup(SDL_GetError());
    } else if (files[0]) {
        e.user.data1 = SDL_strdup(files[0]);
    } else return;  // (cancelled)
    SDL_PushEvent(&e);
}

static void browse(void) {
    static const SDL_DialogFileFilter filters[] = {
#ifdef _WIN32
        {"Savage Wind (savagewind.exe)", "exe"},
#else
        {"Savage Wind AppImage", "AppImage;appimage"},
#endif
        {"All files", "*"},
    };
    char dir[PATH_CAP] = "";
    if (g_game[0]) {
        SDL_strlcpy(dir, g_game, sizeof dir);
        char *s = strrchr(dir, '/');
        if (s) s[1] = 0;
    }
    SDL_ShowOpenFileDialog(picked, NULL, g_win, filters, 2, dir[0] ? dir : NULL, false);
}

static void set_game(const char *path) {
    SDL_strlcpy(g_game, path, sizeof g_game);
    save_ini();
}

static void show_licenses(void) {
    char p[PATH_CAP], url[PATH_CAP + 16];
    SDL_snprintf(p, sizeof p, "%sLICENSES.txt", g_data);
    SDL_snprintf(url, sizeof url, "file://%s", p);
    if (exists(p) && SDL_OpenURL(url)) return;
    tell(SDL_MESSAGEBOX_INFORMATION,
         "Clear Skies " APP_VERSION ", a mod for Savage Wind.\n\n"
         "Clear Skies' code: MIT License.\n"
         "Clarity shader and presets: from Cemu's graphic packs (cemu-project/cemu_graphic_packs), CC0 1.0. Clarity "
         "GFX by Jamie and Kiri with Crementif; presets by Serfrost, Bruz, t3nka, CellHunter, The Complaining Gamer, "
         "Visk, Silentverge, Godot, Hexaae, MaranBr, YeMaoWuXin and Cobalt.\n"
         "Linux support by Danny Rashid. SDL3 (zlib), stb_truetype (public domain), Fira Sans (OFL 1.1).\n"
         "Savage Wind Recomp (MIT) is not included: Clear Skies works with your own copy.\n\n"
         "Not made by or affiliated with the Savage Wind team, the Cemu team or Nintendo. The Legend of Zelda: Breath "
         "of the Wild is \xC2\xA9 Nintendo.");
}

static void edit_settings(void) {
    save_ini();
    char url[PATH_CAP + 16];
    SDL_snprintf(url, sizeof url, "file://%s", g_ini);
    if (!SDL_OpenURL(url)) {
        char msg[PATH_CAP + 64];
        SDL_snprintf(msg, sizeof msg, "Open this file in a text editor:\n%s", g_ini);
        tell(SDL_MESSAGEBOX_INFORMATION, msg);
    }
}

static void play(void) {
    save_ini();
    start_game();
}

static void click(int id, int list_row) {
    if (g_open) {
        if (id == H_LIST && list_row >= 0) set_row(list_row);
        g_open = 0;
        return;
    }
    switch (id) {
    case H_COMBO: open_list(); break;
    case H_PATH:
    case H_BROWSE: browse(); break;
    case H_EDIT: edit_settings(); break;
    case H_PLAY: play(); break;
    case H_LICENSES: show_licenses(); break;
    }
}

// ------------------------------------------------------------------ main

static void set_scale(SDL_Window *w) {
    int pw = W_CLIENT, ph = H_CLIENT;
    if (w) SDL_GetWindowSizeInPixels(w, &pw, &ph);
    float k = pw / (float)W_CLIENT;
    if (!g_px || pw != g_pw || ph != g_ph || k != g_k) {
        g_pw = pw, g_ph = ph, g_k = k;
        SDL_free(g_px);
        g_px = SDL_malloc((size_t)pw * ph * 4);
        set_faces();
    }
}

static int save_png(const char *path) {
    SDL_Surface *s = SDL_CreateSurfaceFrom(g_pw, g_ph, SDL_PIXELFORMAT_ARGB8888, g_px, g_pw * 4);
    int ok = s && SDL_SavePNG(s, path);
    SDL_DestroySurface(s);
    return ok;
}

static void usage(void) {
    printf("Clear Skies %s, a mod for Savage Wind: Cemu's Clarity presets.\n\n"
           "  clearskies                      the window\n"
           "  clearskies --play               start with the saved settings\n"
           "  clearskies --preset <name>      choose a preset (saved); --list shows them\n"
           "  clearskies --game <path>        choose the Savage Wind AppImage (saved)\n"
           "  clearskies ... --no-play        only save those choices\n"
           "  clearskies ... -- <args>        everything after -- goes to the game\n"
           "  clearskies <command...>         run a command with the layer on (Steam: clearskies %%command%%)\n\n"
           "Settings: %s\n", APP_VERSION, g_ini);
}

int main(int argc, char **argv) {
    find_files();
    load_ini();

    int play_now = 0, smoke = 0, open_now = 0;
    const char *shot = NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--")) { g_game_args = argv + i + 1; g_game_nargs = argc - i - 1; break; }
        else if (!strcmp(a, "--play")) play_now = 1;
        else if (!strcmp(a, "--no-play")) play_now = -1;  // (only save --preset / --game)
        else if (!strcmp(a, "--list")) {
            printf("%-20s %s\n", "off", row_title(0));
            for (int r = 1; r < NROWS; r++) printf("%-20s %s\n", row_id(r), row_title(r));
            return 0;
        } else if ((!strcmp(a, "--preset") || !strcmp(a, "--game")) && i + 1 < argc) {
            const char *v = argv[++i];
            if (a[2] == 'p') {
                if (preset_row(v) < 0) {
                    char msg[256];
                    SDL_snprintf(msg, sizeof msg, "Unknown preset \"%s\". --list shows the names.", v);
                    tell(SDL_MESSAGEBOX_WARNING, msg);
                    return 2;
                }
                SDL_strlcpy(g_preset, v, sizeof g_preset);
            } else {
#ifndef _WIN32
                char full[PATH_CAP];
                const char *owd = SDL_getenv("OWD");  // (the folder the AppImage was started from)
                if (v[0] != '/') { SDL_snprintf(full, sizeof full, "%s/%s", owd ? owd : ".", v); v = full; }
                char real[PATH_MAX];
                if (realpath(v, real)) v = real;
#endif
                SDL_strlcpy(g_game, v, sizeof g_game);
            }
            save_ini();
        } else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
        else if (!strcmp(a, "--screenshot") && i + 1 < argc) shot = argv[++i];  // (tests: draw once, save a PNG)
        else if (!strcmp(a, "--open-list")) open_now = 1;                       // (tests: with the list open)
        else if (!strcmp(a, "--smoke")) smoke = 1;                              // (tests: open the window, then close)
        else if (a[0] == '-') {
            fprintf(stderr, "%s: unknown option %s (--help lists them)\n", APP_TITLE, a);
            return 2;
        } else {  // Steam's %command%: the game's own command line, run with the layer on
            if (!layer_env()) return 1;
            run(argv + i);
            return 1;
        }
    }
    int r0 = preset_row(g_preset);
    set_row(r0 < 0 ? preset_row("serfrost") : r0);
    if (play_now) {
        save_ini();
        if (play_now < 0) { printf("preset: %s\ngame: %s\nsettings: %s\n", g_preset, g_game, g_ini); return 0; }
        return start_game() ? 0 : 1;
    }

    if (!load_font(&g_medium, "FiraSans-Medium.ttf") || !load_font(&g_semibold, "FiraSans-SemiBold.ttf")) {
        tell(SDL_MESSAGEBOX_ERROR, "Clear Skies' fonts are missing. Download Clear Skies again.");
        return 1;
    }
    if (shot) {  // no window needed: the picture is drawn in memory
        g_k = 0;
        set_scale(NULL);
        if (open_now) open_list();
        paint();
        if (!save_png(shot)) { fprintf(stderr, "screenshot: %s\n", SDL_GetError()); return 1; }
        return 0;
    }

    SDL_SetHint(SDL_HINT_APP_ID, "clearskies");
    SDL_SetAppMetadata(APP_TITLE, APP_VERSION, "io.github.devall96.ClearSkies");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "%s: no display (%s). Use --play, or run it from your desktop.\n", APP_TITLE, SDL_GetError());
        return 1;
    }
    g_ev_picked = SDL_RegisterEvents(1);
    float ui = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (ui <= 0) ui = 1;
    g_win = SDL_CreateWindow(APP_TITLE, (int)(W_CLIENT * ui), (int)(H_CLIENT * ui), SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    SDL_Renderer *ren = g_win ? SDL_CreateRenderer(g_win, NULL) : NULL;
    if (!ren) {
        fprintf(stderr, "%s: %s\n", APP_TITLE, SDL_GetError());
        return 1;
    }
    char icon[PATH_CAP];
    SDL_snprintf(icon, sizeof icon, "%sclearskies.png", g_data);
    SDL_Surface *ic = SDL_LoadPNG(icon);
    if (ic) { SDL_SetWindowIcon(g_win, ic); SDL_DestroySurface(ic); }
    SDL_Cursor *hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER), *arrow = SDL_GetDefaultCursor();
    SDL_Texture *tex = NULL;
    int tw = 0, th = 0, dirty = 1, shown = 0;
    if (open_now) open_list();

    for (;;) {
        if (dirty) {
            set_scale(g_win);
            if (!tex || tw != g_pw || th != g_ph) {
                SDL_DestroyTexture(tex);
                tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, g_pw, g_ph);
                tw = g_pw, th = g_ph;
            }
            paint();
            SDL_UpdateTexture(tex, NULL, g_px, g_pw * 4);
            SDL_RenderTexture(ren, tex, NULL, NULL);
            SDL_RenderPresent(ren);
            dirty = 0;
            if (!shown) { SDL_ShowWindow(g_win); shown = 1; }
            if (smoke) { printf("window ok: %dx%d pixels, %s\n", g_pw, g_ph, SDL_GetCurrentVideoDriver()); break; }
        }
        SDL_Event e;
        if (!SDL_WaitEvent(&e)) continue;
        float density = SDL_GetWindowPixelDensity(g_win);
        switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            goto done;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_EXPOSED:
            dirty = 1;
            break;
        case SDL_EVENT_MOUSE_MOTION: {
            float x = e.motion.x * density / g_k, y = e.motion.y * density / g_k;
            int lr, h = hit(x, y, &lr);
            if (h != g_hot || lr != g_list_hot) { g_hot = h, g_list_hot = lr; dirty = 1; }
            SDL_SetCursor(h != H_NONE ? hand : arrow);
            break;
        }
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            if (g_hot || g_list_hot >= 0) { g_hot = H_NONE, g_list_hot = -1; dirty = 1; }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                int lr;
                g_pressed = hit(e.button.x * density / g_k, e.button.y * density / g_k, &lr);
                if (g_open && g_pressed != H_LIST && g_pressed != H_COMBO) { g_open = 0, g_pressed = H_NONE; }
                dirty = 1;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                int lr, h = hit(e.button.x * density / g_k, e.button.y * density / g_k, &lr);
                int was = g_pressed;
                g_pressed = H_NONE;
                dirty = 1;
                if (h == was && h != H_NONE) click(h, lr);
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (g_open) {
                g_list_top -= (e.wheel.y > 0 ? 1 : e.wheel.y < 0 ? -1 : 0) * 2;
                if (g_list_top > NROWS - g_list_rows) g_list_top = NROWS - g_list_rows;
                if (g_list_top < 0) g_list_top = 0;
                dirty = 1;
            }
            break;
        case SDL_EVENT_KEY_DOWN:
            switch (e.key.key) {
            case SDLK_ESCAPE:
                if (g_open) g_open = 0;
                else goto done;
                break;
            case SDLK_UP:
            case SDLK_DOWN:
                set_row(g_row + (e.key.key == SDLK_UP ? -1 : 1));
                if (g_open) list_show(g_row);
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
            case SDLK_SPACE:
                if (g_open) g_open = 0;
                else play();
                break;
            }
            dirty = 1;
            break;
        case SDL_EVENT_DROP_FILE:  // a file dropped on the window: the game
            if (e.drop.data) set_game(e.drop.data);
            dirty = 1;
            break;
        default:
            if (e.type == g_ev_picked) {
                if (e.user.code) {
                    char msg[1024];
                    SDL_snprintf(msg, sizeof msg, "No file chooser is available here (%s).\n\nDrag " GAME_WORD
                                 " onto this window instead, or start Clear Skies with --game <path>.",
                                 e.user.data1 ? (char *)e.user.data1 : "?");
                    tell(SDL_MESSAGEBOX_WARNING, msg);
                } else set_game(e.user.data1);
                SDL_free(e.user.data1);
                dirty = 1;
            }
        }
    }
done:
    save_ini();
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(g_win);
    SDL_Quit();
    return 0;
}
