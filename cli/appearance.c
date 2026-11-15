#define _XOPEN_SOURCE_EXTENDED 1

#include "vex.h"
#include "cli/appearance.h"

#include <stdlib.h>
#include <string.h>

#include "ui/surface/terminal_canvas.h"
#include "format/terminal_style.h"

;;DEFINITION
/** The app's chosen look and its rule drawing. The choice is a preset NAME
 * (persisted in Settings, applied by the host through this module); the glyphs
 * and the geometry belong to the toolkit. The module keeps one resolved
 * TerminalAppearance in static state so it lives with whichever view copy is
 * active — the host resolves Primeagen_setAppearance next to the draw pointers,
 * so a reload adopts the same look without touching the terminal. */
;;OVERVIEW
/** ========================================================================
 * MODULE: PrimeagenAppearance (cli/appearance.c)
 * ============================================================================
 * STATE (file-local):
 *   static TerminalAppearance s_current   resolved preset (lazy default)
 *   static bool s_loaded                  false until first use
 *   static char s_name[]                  the chosen preset name
 *   static u32 *s_cells; bytes s_cap  rule scratch (cold growth)
 * FUNCTION REGISTRY:
 *   - fn PrimeagenAppearance_use(const char *presetName)
 *   - const TerminalAppearance *PrimeagenAppearance_current()
 *   - const char *PrimeagenAppearance_name()
 *   - bool PrimeagenAppearance_drawRule(WINDOW *win, int row, int left, int width)
 * CONTRACT: cold owner thread; unknown/null names adopt the toolkit default; the
 * rule blit is one pass and clips to `width`; no file, network or model access.
 * ============================================================================
 */

#define PRIMEAGEN_APPEARANCE_NAME_MAX 32

static TerminalAppearance s_current;
static bool s_loaded = false;
static char s_name[PRIMEAGEN_APPEARANCE_NAME_MAX] = "ascii-striped";
static u32 *s_cells = nullptr;
static bytes s_cap = 0;
static u16 *s_styles = nullptr;

const TerminalAppearance *PrimeagenAppearance_current() {
    if (!s_loaded) {
        s_current = TerminalAppearance_0();
        s_loaded = true;
    }
    return &s_current;
}

const char *PrimeagenAppearance_name() { return s_name; }

fn PrimeagenAppearance_use(const char *presetName) {
    const char *wanted = (presetName != nullptr && presetName[0] != '\0') ? presetName : "ascii-striped";
    s_current = TerminalAppearance_named(wanted);
    bytes n = strlen(wanted);
    if (n >= sizeof s_name)
        n = sizeof s_name - 1;
    memcpy(s_name, wanted, n);
    s_name[n] = '\0';
    s_loaded = true;
}

bool PrimeagenAppearance_drawRule(WINDOW *win, int row, int left, int width) {
    if (win == nullptr || width <= 0 || row < 0 || left < 0)
        return false;
    if ((bytes) width > s_cap) {
        u32 *cells = (u32*) realloc(s_cells, (bytes) width * sizeof(u32));
        if (cells == nullptr)
            return false;
        s_cells = cells;
        u16 *styles = (u16*) realloc(s_styles, (bytes) width * sizeof(u16));
        if (styles == nullptr)
            return false;
        s_styles = styles;
        s_cap = (bytes) width;
    }
    TerminalCanvas canvas = TerminalCanvas(s_cells, s_styles, (bytes) width, 1);
    if (!TerminalCanvas_isValid(&canvas))
        return false;
    const TerminalAppearance *look = PrimeagenAppearance_current();
    TerminalAppearance_drawRule(look, &canvas, 0, TerminalPen_1(TERMINAL_STYLE_DIM));

    // One blit: wide-cell aware, dimmed, so a box-drawing or ASCII glyph both land.
    for (int x = 0; x < width; ++x) {
        u32 cp = TerminalCanvas_glyphAt(&canvas, (bytes) x, 0);
        if (cp == 0)
            continue;   // wide-glyph continuation cell: the wide cell already spans it
        wchar_t text[2] = { (wchar_t) cp, L'\0' };
        cchar_t glyph;
        if (setcchar(&glyph, text, A_DIM, 0, nullptr) == OK)
            (discard) mvadd_wch(row, left + x, &glyph);
        else
            (discard) mvaddch(row, left + x, (chtype) ' ' | A_DIM);
    }
    return true;
}
