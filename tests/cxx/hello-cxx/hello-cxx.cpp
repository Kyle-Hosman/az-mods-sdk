// SPDX-License-Identifier: GPL-3.0-only
//
// hello-cxx: the SDK's C++ test mod. A page under Mod Menu > Community Mods > Installed >
// Hello C++ with each deck's BPM, built with g++ and linked against the deck's own
// libstdc++.so.6. CI builds it, validates it and runs it under the test harness, so a C++
// mod is known to build, load and pass the same checks as a C one.
//
// What is C++ about it, and the rules that keep that safe inside EP147:
//   - standard library templates (std::array, std::clamp-style helpers, std::snprintf),
//     with no heap allocation on the busy paths;
//   - no global object with a constructor that does work: every global is constant- or
//     zero-initialized, and setup happens in init();
//   - no exception leaves the mod: init() catches everything and returns non-zero;
//   - the descriptor and the entry point are plain C, behind extern "C" (azmod-mod.h).
#include <array>
#include <cstdio>
#include <ctime>
#include <exception>
#include <string>

#include "azmod-mod.h"

#define MOD_ID "hello-cxx"

namespace
{

constexpr int kMaxDecks = 4;
constexpr long kMinRedrawMs = 250;      // at most 4 repaints a second

const az_core_v1 *C;

// One deck as last shown: loaded, and the BPM x 100 (0 = unknown).
struct DeckRow {
    int loaded = 0;
    long bpm_centi = 0;

    bool update(int now_loaded, double bpm)
    {
        if (!(bpm > 0.0 && bpm < 1000.0)) bpm = 0.0;   // unknown or not a number
        long centi = static_cast<long>(bpm * 100.0 + 0.5);
        bool changed = now_loaded != loaded || centi != bpm_centi;
        loaded = now_loaded;
        bpm_centi = centi;
        return changed;
    }
};

std::array<DeckRow, kMaxDecks> rows{};
int shown_decks = -1;
int dirty = 0;
long last_redraw_ms = -kMinRedrawMs;

template <typename T>
T clamp(T v, T lo, T hi)
{
    return v < lo ? lo : v > hi ? hi
                                : v;
}

long now_ms()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<long>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

int deck_count()
{
    int n = C->deck_count();
    return n < 0 ? 0 : clamp(n, 0, kMaxDecks);     // unknown: show nothing
}

// Draw s, shortened from the end (whole UTF-8 characters) until it fits in max_w pixels: the
// core cuts drawing off at the area's edge but does not shorten text.
void draw_fitted(az_draw *d, int x, int y, const char *s, int px, uint32_t color, int max_w)
{
    std::array<char, 64> buf{};
    std::snprintf(buf.data(), buf.size(), "%s", s);
    size_t len = std::char_traits<char>::length(buf.data());
    while (len > 0 && C->text_width(buf.data(), px) > max_w) {
        do len--;
        while (len > 0 && (static_cast<unsigned char>(buf[len]) & 0xC0) == 0x80);
        buf[len] = '\0';
    }
    if (len > 0) C->draw_text(d, x, y, buf.data(), px, color);
}

void on_draw(az_draw *d, az_rect area, void *)
{
    const uint32_t text = C->color(AZ_COL_TEXT);
    const uint32_t dim = C->color(AZ_COL_TEXT_DIM);
    const int row = C->row_height(d);
    const int px = clamp(row / 2, 8, 96);
    const int pad = row / 4;
    int y = area.y;

    C->draw_header(d, "Hello C++");
    y += row;
    for (int deck = 0; deck < deck_count() && y + row <= area.y + area.h; deck++) {
        const DeckRow &r = rows[static_cast<size_t>(deck)];
        char line[64];
        if (!r.loaded)
            std::snprintf(line, sizeof line, "Deck %d   no track", deck + 1);
        else if (r.bpm_centi == 0)
            std::snprintf(line, sizeof line, "Deck %d   --- BPM", deck + 1);
        else
            std::snprintf(line, sizeof line, "Deck %d   %ld.%02ld BPM", deck + 1,
                          r.bpm_centi / 100, r.bpm_centi % 100);
        draw_fitted(d, area.x + pad, y + pad, line, px, r.loaded ? text : dim, area.w - 2 * pad);
        y += row;
    }
}

int on_input(const az_event *ev, void *)
{
    return ev->kind == AZ_EV_KNOB || ev->kind == AZ_EV_CLICK;   // BACK and taps: the core
}

const az_screen_v1 SCREEN = { sizeof(az_screen_v1), on_draw, on_input, nullptr, nullptr };

// Periodic at a varying rate: repaints are paced by the monotonic clock, not by ticks.
void tick()
{
    int n = deck_count();
    if (n != shown_decks) {
        shown_decks = n;
        dirty = 1;
    }
    for (int deck = 0; deck < n; deck++) {
        int loaded = C->deck_loaded(deck) == 1;
        if (rows[static_cast<size_t>(deck)].update(loaded, loaded ? C->deck_bpm(deck) : 0.0))
            dirty = 1;
    }
    long now = now_ms();
    if (dirty && now - last_redraw_ms >= kMinRedrawMs) {
        dirty = 0;
        last_redraw_ms = now;
        C->request_redraw();
    }
}

int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;
    try {
        if (!AZ_HAS(C, deck_count) || !AZ_HAS(C, request_redraw) || !AZ_HAS(C, text_width))
            return 2;
        if (C->register_screen(MOD_ID, &SCREEN) != 0) return 3;
        C->log(MOD_ID, "loaded on %s", C->fw_tag());
        return 0;
    } catch (const std::exception &) {
        return 4;                       // an exception never leaves the mod
    } catch (...) {
        return 4;
    }
}

const char *const FW_TAGS[] = { "any", nullptr };

const az_mod_v1 MOD = { AZ_MOD_ABI, sizeof(az_mod_v1), MOD_ID, FW_TAGS, init, tick, nullptr };

}  // namespace

// The one exported symbol (declared with AZ_MOD_EXPORT, extern "C", in azmod-mod.h).
const az_mod_v1 *az_mod_v1_entry(void)
{
    return &MOD;
}
