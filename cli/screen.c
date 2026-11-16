#define _XOPEN_SOURCE_EXTENDED 1

#include "vex.h"
#include "cli/screen.h"
#include "cli/appearance.h"

#include <stdlib.h>
#include <string.h>

#include "format/terminal_style.h"
#include "format/terminal_color.h"
#include "ui/panel/terminal_frame.h"
#include "ui/list/terminal_list_view.h"
#include "ui/text/terminal_text_view.h"
#include "ui/field/terminal_input_line.h"
#include "ui/text/terminal_status_bar.h"

;;DEFINITION
/** Primeagen's frame as a RENDERER: the whole screen is composed into an owned
 * cell grid with the Determinant toolkit and handed to ncurses in one blit.
 * Two layouts mirror `_notes/_self/tui.txt` — HOME (title rail with the menu, the
 * app's picture in the panel beside it) and CHAT (left rail, centre chat, right
 * people). The frame draws its OWN border and corners, so it reads as a TUI
 * inside the window rather than borrowing the window's edges.
 *
 * FOCUS: exactly one panel owns the keyboard. Its selected row is HIGHLIGHTED
 * (reverse video); a panel that does not own the keyboard still marks its
 * selected row, but only with a COLOUR change — never a highlight. */
;;OVERVIEW
/** ========================================================================
 * CLASS: PrimeagenScreen (cli/screen.c)
 * ============================================================================
 * STRUCT FIELDS (Mirroring cli/screen.h):
 *   u32 *cells       codepoint plane
 *   u16 *styles      attribute plane
 *   u32 *fg, *bg     0xRRGGBBAA planes
 *   bytes rows, columns, capacity
 * PRIVATE HELPERS:
 *   putText / putCentered(canvas, ..., pen)   bounded draws
 *   lookAccent/lookMuted/lookText/lookRule/lookError(appearance)  named pens
 *   selectionPen(appearance, focused)  HIGHLIGHT when focused, COLOUR when not
 *   reshuffleBannerNoise()             fresh decor scatter, one per render
 *   renderTooSmall(root, look)         centred, word-wrapped resolution notice
 *   drawBorder(root, look)             the frame's own box (real corners)
 *   pairFor(rgb)                       packed colour -> ncurses pair (cached)
 *   navItems(panel, &count)            the rail's rows per panel
 * FUNCTION REGISTRY (exports):
 *   - PrimeagenScreen_init / _bind / _free / _isBound / _canvas
 *   - PrimeagenScreen_setBanner / _banner / _hasBanner
 *   - PrimeagenScreen_renderHome(self, title, menu, count, selection, banner)
 *   - PrimeagenScreen_renderChat(self, state)
 *   - PrimeagenScreen_present(self, win)
 * CONTRACT: null/too-small screens show the notice; the blit is one pass and
 * allocates nothing per cell. Cold owner thread only.
 * ============================================================================
 */

;;GETTER
static inline u32 *PrimeagenScreen_cells(const PrimeagenScreen *self) { return (*self).cells; }
;;SETTER
static inline fn PrimeagenScreen_setCells(PrimeagenScreen *self, u32 *cells) { (*self).cells = cells; }
;;GETTER
static inline u16 *PrimeagenScreen_styles(const PrimeagenScreen *self) { return (*self).styles; }
;;SETTER
static inline fn PrimeagenScreen_setStyles(PrimeagenScreen *self, u16 *styles) { (*self).styles = styles; }
;;GETTER
static inline u32 *PrimeagenScreen_fg(const PrimeagenScreen *self) { return (*self).fg; }
;;SETTER
static inline fn PrimeagenScreen_setFg(PrimeagenScreen *self, u32 *fg) { (*self).fg = fg; }
;;GETTER
static inline u32 *PrimeagenScreen_bg(const PrimeagenScreen *self) { return (*self).bg; }
;;SETTER
static inline fn PrimeagenScreen_setBg(PrimeagenScreen *self, u32 *bg) { (*self).bg = bg; }
;;GETTER
static inline bytes PrimeagenScreen_rows(const PrimeagenScreen *self) { return (*self).rows; }
;;SETTER
static inline fn PrimeagenScreen_setRows(PrimeagenScreen *self, bytes rows) { (*self).rows = rows; }
;;GETTER
static inline bytes PrimeagenScreen_columns(const PrimeagenScreen *self) { return (*self).columns; }
;;SETTER
static inline fn PrimeagenScreen_setColumns(PrimeagenScreen *self, bytes columns) { (*self).columns = columns; }
;;GETTER
static inline bytes PrimeagenScreen_capacity(const PrimeagenScreen *self) { return (*self).capacity; }
;;SETTER
static inline fn PrimeagenScreen_setCapacity(PrimeagenScreen *self, bytes capacity) { (*self).capacity = capacity; }

;;GETTER
static inline u32 TerminalAppearance_colorAccent(const TerminalAppearance *self) { return (*self).colorAccent; }
;;GETTER
static inline u32 TerminalAppearance_colorMuted(const TerminalAppearance *self) { return (*self).colorMuted; }
;;GETTER
static inline u32 TerminalAppearance_colorText(const TerminalAppearance *self) { return (*self).colorText; }
;;GETTER
static inline u32 TerminalAppearance_colorError(const TerminalAppearance *self) { return (*self).colorError; }
;;GETTER
static inline const TerminalRule *TerminalAppearance_rule(const TerminalAppearance *self) { return &(*self).rule; }

;;GETTER
static inline Tui *PrimeagenScreenState_tui(const PrimeagenScreenState *self) { return (*self).tui; }
;;GETTER
static inline i32 PrimeagenScreenState_panel(const PrimeagenScreenState *self) { return (*self).panel; }
;;GETTER
static inline i32 PrimeagenScreenState_selection(const PrimeagenScreenState *self) { return (*self).selection; }
;;GETTER
static inline i32 PrimeagenScreenState_focusPanel(const PrimeagenScreenState *self) { return (*self).focusPanel; }
;;GETTER
static inline bool PrimeagenScreenState_failed(const PrimeagenScreenState *self) { return (*self).failed; }
;;GETTER
static inline bool PrimeagenScreenState_busy(const PrimeagenScreenState *self) { return (*self).busy; }
;;GETTER
static inline bool PrimeagenScreenState_sessionMode(const PrimeagenScreenState *self) { return (*self).sessionMode; }

fn PrimeagenScreen_init(PrimeagenScreen *self) {
    if (self != nullptr)
        memset(self, 0, sizeof(*self));
}

bool PrimeagenScreen_isBound(const PrimeagenScreen *self) {
    return self != nullptr && PrimeagenScreen_cells(self) != nullptr && PrimeagenScreen_rows(self) > 0 && PrimeagenScreen_columns(self) > 0;
}

bool PrimeagenScreen_bind(PrimeagenScreen *self, bytes rows, bytes columns) {
    if (self == nullptr || rows == 0 || columns == 0 || rows > SIZE_MAX / columns)
        return false;
    bytes want = rows * columns;
    if (want > PrimeagenScreen_capacity(self)) {
        u32 *cells = (u32*) realloc(PrimeagenScreen_cells(self), want * sizeof(u32));
        if (cells == nullptr) return false;
        PrimeagenScreen_setCells(self, cells);
        u16 *styles = (u16*) realloc(PrimeagenScreen_styles(self), want);
        if (styles == nullptr) return false;
        PrimeagenScreen_setStyles(self, styles);
        u32 *fg = (u32*) realloc(PrimeagenScreen_fg(self), want * sizeof(u32));
        if (fg == nullptr) return false;
        PrimeagenScreen_setFg(self, fg);
        u32 *bg = (u32*) realloc(PrimeagenScreen_bg(self), want * sizeof(u32));
        if (bg == nullptr) return false;
        PrimeagenScreen_setBg(self, bg);
        PrimeagenScreen_setCapacity(self, want);
    }
    PrimeagenScreen_setRows(self, rows);
    PrimeagenScreen_setColumns(self, columns);
    return true;
}

fn PrimeagenScreen_free(PrimeagenScreen *self) {
    if (self == nullptr)
        return;
    free(PrimeagenScreen_cells(self));
    free(PrimeagenScreen_styles(self));
    free(PrimeagenScreen_fg(self));
    free(PrimeagenScreen_bg(self));
    memset(self, 0, sizeof(*self));
}

// The app's BANNER: borrowed pixels kept as a toolkit image. White paper is
// inverted to blank and the app's palette scatters the ink.
static TerminalImage s_banner;
static bool s_hasBanner = false;
static u32 s_noiseSeed = 1;
// The banner's "paper" is NOT blank: it is a gray field of random glyphs — the
// app's decor. Primeagen owns the alphabet and the gray; the toolkit ships no art.
#define PRIMEAGEN_SCREEN_NOISE_ALPHABET \
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789%@#$&*+=/<>"
#define PRIMEAGEN_SCREEN_NOISE_COLOR TERMINAL_RGB(0x70, 0x70, 0x86)
// How much of the band the decor covers. The field fills the band edge to
// edge; this only decides how solid it looks.
#define PRIMEAGEN_SCREEN_NOISE_DENSITY 80

// Re-scatter the banner's paper texture. Called on EVERY render, so a revalidate (which
// re-renders) draws a fresh random field while a fixed seed stays reproducible.
static fn reshuffleBannerNoise() {
    if (!s_hasBanner)
        return;
    s_noiseSeed = s_noiseSeed * 1103515245u + 12345u;
    TerminalImage_setNoiseSeed(&s_banner, s_noiseSeed);
}

// Which tone is the picture's "paper"? Sample it on a coarse grid: a BRIGHT
// average means a light background (so invert — the paper falls to blank and the
// ink becomes glyphs), a DARK average is already oriented that way. This is why
// one banner can be white-on-black and another black-on-white with no per-image
// setting: the paper is whatever covers the frame, and it becomes the decor.
static bool paperIsBright(const u8 *rgba, u32 width, u32 height) {
    TerminalImage probe = TerminalImage((u8*) (pointer) rgba, (bytes) width, (bytes) height);
    u64 sum = 0;
    bytes count = 0;
    for (bytes ly = 0; ly < 16; ++ly) {
        for (bytes lx = 0; lx < 16; ++lx) {
            TerminalImageSample sample = TerminalImage_sampleCell(&probe, lx, ly, 16, 16);
            if (!sample.valid || sample.alpha == 0)
                continue;
            sum += TerminalImage_luminance(sample.red, sample.green, sample.blue);
            ++count;
        }
    }
    return count == 0 ? true : (sum / count) > 128;
}

fn PrimeagenScreen_setBanner(const u8 *rgba, u32 width, u32 height) {
    if (rgba == nullptr || width == 0 || height == 0) {
        s_hasBanner = false;
        s_banner = TerminalImage_0();
        return;
    }
    s_banner = TerminalImage((u8*) (pointer) rgba, (bytes) width, (bytes) height);
    TerminalImage_setInvert(&s_banner, paperIsBright(rgba, width, height));
    TerminalImageColorRamp ramp = TerminalImage_rampPurple();
    (discard) TerminalImage_setColorRamp(&s_banner, ramp.data, ramp.count);
    // Which letter reads light and which reads dark is randomized per cell, not
    // luminance-ordered — the palette's whole range (≈30% up to near-white).
    TerminalImage_setPaletteScatter(&s_banner, (u8) ramp.count);
    TerminalImage_setJitter(&s_banner, 20, 11);
    TerminalImage_setStyle(&s_banner, 0);
    (discard) TerminalImage_setNoise(&s_banner, PRIMEAGEN_SCREEN_NOISE_ALPHABET, PRIMEAGEN_SCREEN_NOISE_COLOR);
    TerminalImage_setNoiseStyle(&s_banner, TERMINAL_STYLE_DIM);
    TerminalImage_setNoiseDensity(&s_banner, PRIMEAGEN_SCREEN_NOISE_DENSITY);
    s_hasBanner = true;
}

const TerminalImage *PrimeagenScreen_banner() { return s_hasBanner ? &s_banner : nullptr; }
bool PrimeagenScreen_hasBanner() { return s_hasBanner; }

TerminalCanvas PrimeagenScreen_canvas(PrimeagenScreen *self) {
    if (!PrimeagenScreen_isBound(self))
        return TerminalCanvas_0();
    // All FOUR planes: the colour planes are what make a "colour change" (an
    // unfocused panel's selection) distinguishable from a highlight.
    return TerminalCanvas_6(PrimeagenScreen_cells(self), PrimeagenScreen_styles(self),
                            PrimeagenScreen_fg(self), PrimeagenScreen_bg(self),
                            PrimeagenScreen_columns(self), PrimeagenScreen_rows(self));
}

PrimeagenScreenEvent PrimeagenScreen_classify(i32 key) {
    switch (key) {
        case KEY_RESIZE: return PRIMEAGEN_SCREEN_EVENT_RESIZE;
        case '1':        return PRIMEAGEN_SCREEN_EVENT_HOME;
        case '2':        return PRIMEAGEN_SCREEN_EVENT_CHAT;
        case 'f':        return PRIMEAGEN_SCREEN_EVENT_FOCUS;
        case 'q':
        case 'Q':        return PRIMEAGEN_SCREEN_EVENT_QUIT;
        default:         return PRIMEAGEN_SCREEN_EVENT_NONE;
    }
}

bool PrimeagenScreen_revalidate(PrimeagenScreen *self, i32 rows, i32 columns) {
    if (rows <= 0 || columns <= 0)
        return false;
    if (!PrimeagenScreen_bind(self, (bytes) rows, (bytes) columns))
        return false;
    return (bytes) rows >= PRIMEAGEN_SCREEN_MIN_ROWS
        && (bytes) columns >= PRIMEAGEN_SCREEN_MIN_COLUMNS;
}

// --- pens -------------------------------------------------------------------
static TerminalPen lookAccent(const TerminalAppearance *look) {
    return TerminalPen_2(TERMINAL_STYLE_ACCENT | TERMINAL_STYLE_BOLD, TerminalAppearance_colorAccent(look));
}
static TerminalPen lookMuted(const TerminalAppearance *look) {
    return TerminalPen_2(TERMINAL_STYLE_MUTED, TerminalAppearance_colorMuted(look));
}
static TerminalPen lookText(const TerminalAppearance *look) {
    return TerminalPen_2(TERMINAL_STYLE_NONE, TerminalAppearance_colorText(look));
}
static TerminalPen lookRule(const TerminalAppearance *look) {
    return TerminalPen_2(TERMINAL_STYLE_DIM, TerminalAppearance_colorMuted(look));
}
static TerminalPen lookError(const TerminalAppearance *look) {
    return TerminalPen_2(TERMINAL_STYLE_ERROR | TERMINAL_STYLE_BOLD, TerminalAppearance_colorError(look));
}
// The focused panel HIGHLIGHTS its selection; an unfocused panel only recolours it.
static TerminalPen selectionPen(const TerminalAppearance *look, bool focused) {
    if (focused)
        return TerminalPen_2(TERMINAL_STYLE_REVERSE | TERMINAL_STYLE_ACCENT, TerminalAppearance_colorAccent(look));
    return TerminalPen_2(TERMINAL_STYLE_ACCENT, TerminalAppearance_colorAccent(look));
}

static fn putText(TerminalCanvas *canvas, bytes lx, bytes ly, const char *text, TerminalPen pen) {
    if (text != nullptr)
        TerminalCanvas_text(canvas, lx, ly, text, strlen(text), pen);
}

static fn putCentered(TerminalCanvas *canvas, bytes ly, const char *text, TerminalPen pen) {
    if (text == nullptr)
        return;
    bytes width = TerminalCanvas_getWidth(canvas);
    bytes length = strlen(text);
    bytes lx = length < width ? (width - length) / 2 : 0;
    TerminalCanvas_text(canvas, lx, ly, text, length, pen);
}

// Too small to resolve: one centred notice, re-centred every render so it stays
// anchored while the window is dragged. Word-wrapped, never split mid-word.
static fn renderTooSmall(TerminalCanvas *root, const TerminalAppearance *look) {
    TerminalCanvas_fill(root, ' ', TerminalPen_1(TERMINAL_STYLE_NONE));
    bytes width = TerminalCanvas_getWidth(root);
    bytes height = TerminalCanvas_getHeight(root);
    if (width == 0 || height == 0)
        return;
    const char *text = PRIMEAGEN_SCREEN_TOO_SMALL;
    bytes length = strlen(text);
    enum { MAX_LINES = 8 };
    bytes lineStart[MAX_LINES];
    bytes lineLength[MAX_LINES];
    bytes lines = 0;
    bytes at = 0;
    while (at < length && lines < MAX_LINES) {
        bytes start = at;
        bytes lastSpace = 0;
        bytes used = 0;
        while (at < length && used < width) {
            if (text[at] == ' ')
                lastSpace = at;
            ++at;
            ++used;
        }
        bytes end = at;
        if (end < length && lastSpace > start) {
            end = lastSpace;
            at = lastSpace + 1;
        }
        lineStart[lines] = start;
        lineLength[lines] = end - start;
        ++lines;
    }
    bytes first = lines < height ? (height - lines) / 2 : 0;
    for (bytes i = 0; i < lines && first + i < height; ++i) {
        bytes span = lineLength[i];
        bytes lx = span < width ? (width - span) / 2 : 0;
        TerminalCanvas_text(root, lx, first + i, text + lineStart[i], span, lookError(look));
    }
}

// The frame's OWN box, with real corners, so the picture reads as a terminal
// instrument set inside the window rather than content pressed against it.
static fn drawBorder(TerminalCanvas *root, const TerminalAppearance *look) {
    TerminalRule_drawBox(TerminalAppearance_rule(look), root, lookRule(look));
}

fn PrimeagenScreen_renderHome(PrimeagenScreen *self, const char *title,
                                const char *const *menu, bytes menuCount, bytes menuSelection,
                                const TerminalImage *banner) {
    if (!PrimeagenScreen_isBound(self))
        return;
    TerminalCanvas root = PrimeagenScreen_canvas(self);
    TerminalCanvas_fill(&root, ' ', TerminalPen_1(TERMINAL_STYLE_NONE));
    const TerminalAppearance *look = PrimeagenAppearance_current();
    if (TerminalCanvas_getWidth(&root) < PRIMEAGEN_SCREEN_MIN_COLUMNS
        || TerminalCanvas_getHeight(&root) < PRIMEAGEN_SCREEN_MIN_ROWS) {
        renderTooSmall(&root, look);
        return;
    }
    drawBorder(&root, look);
    TerminalCanvas inner = TerminalCanvas_sub(&root, 1, 1,
                                              TerminalCanvas_getWidth(&root) - 2,
                                              TerminalCanvas_getHeight(&root) - 2);
    if (!TerminalCanvas_isValid(&inner))
        return;

    // The tui.txt basics: a title rail with the menu stacked down it, the app's
    // picture filling the panel beside it.
    bytes columns = TerminalCanvas_getWidth(&inner);
    bytes rows = TerminalCanvas_getHeight(&inner);
    bytes left = PRIMEAGEN_SCREEN_LEFT_DEFAULT < columns / 3 ? PRIMEAGEN_SCREEN_LEFT_DEFAULT : columns / 3;
    TerminalCanvas rail = TerminalCanvas_sub(&inner, 0, 0, left, rows);
    TerminalCanvas panel = TerminalCanvas_sub(&inner, left + 1, 0, columns - left - 1, rows);
    putCentered(&rail, 0, title, lookAccent(look));
    TerminalRule_drawCut(TerminalAppearance_rule(look), &rail, 1, 1, lookRule(look));
    bytes row = 3;
    for (bytes i = 0; i < menuCount && row + 2 < TerminalCanvas_getHeight(&rail); ++i, row += 2) {
        TerminalPen pen = (i == menuSelection) ? selectionPen(look, true) : lookText(look);
        putCentered(&rail, row, menu[i], pen);
        TerminalRule_drawCut(TerminalAppearance_rule(look), &rail, row + 1, 1, lookRule(look));
    }
    TerminalCanvas_vline(&inner, left, '|', lookRule(look));

    // Zoom-fit into the panel: one uniform scale (cell-aspect corrected), centred,
    // so the picture is never squashed. The decor is re-scattered first, so every
    // revalidate draws a fresh random field behind it.
    if (banner != nullptr && TerminalImage_isValid(banner)) {
        reshuffleBannerNoise();
        TerminalImage_renderContain(banner, &panel);
    }
}

// The rail's rows per panel. Static sample content; the app owns real data.
static fn navItems(i32 panel, const char *const **outItems, bytes *outCount) {
    static const char *const people[] = {"Sola (@solasol)", "Muse Writer (@muse...)", "Vex (@vexgraph)"};
    static const char *const servers[] = {"Ecosystem", "Other Server", "Another Server"};
    static const char *const channels[] = {"# general", "# whatever-this", "# hotcwaphsdf"};
    switch (panel) {
        case 1: *outItems = servers; *outCount = 3; break;
        case 2: *outItems = channels; *outCount = 3; break;
        default: *outItems = people; *outCount = 3; break;
    }
}

bool PrimeagenScreen_renderChat(PrimeagenScreen *self, const PrimeagenScreenState *state) {
    Tui *tui = state ? PrimeagenScreenState_tui(state) : nullptr;
    if (!PrimeagenScreen_isBound(self) || state == nullptr || tui == nullptr)
        return false;
    TerminalCanvas root = PrimeagenScreen_canvas(self);
    TerminalCanvas_fill(&root, ' ', TerminalPen_1(TERMINAL_STYLE_NONE));
    const TerminalAppearance *look = PrimeagenAppearance_current();
    bytes screenCols = PrimeagenScreen_columns(self);
    bytes screenRows = PrimeagenScreen_rows(self);
    if (screenCols < PRIMEAGEN_SCREEN_MIN_COLUMNS || screenRows < PRIMEAGEN_SCREEN_MIN_ROWS) {
        renderTooSmall(&root, look);
        return true;
    }
    drawBorder(&root, look);
    TerminalCanvas inner = TerminalCanvas_sub(&root, 1, 1, screenCols - 2, screenRows - 2);
    if (!TerminalCanvas_isValid(&inner))
        return false;

    bytes columns = TerminalCanvas_getWidth(&inner);
    bytes rows = TerminalCanvas_getHeight(&inner);
    bytes left = PRIMEAGEN_SCREEN_LEFT_DEFAULT < columns / 3 ? PRIMEAGEN_SCREEN_LEFT_DEFAULT : columns / 3;
    bytes right = PRIMEAGEN_SCREEN_RIGHT_DEFAULT < columns / 4 ? PRIMEAGEN_SCREEN_RIGHT_DEFAULT : columns / 4;
    if (left + right + 4 >= columns)
        right = 0;

    // Left rail: the focused list. Selection highlights only when it owns focus.
    TerminalCanvas rail = TerminalCanvas_sub(&inner, 0, 0, left, rows);
    putCentered(&rail, 0, "Primeagen", lookAccent(look));
    TerminalRule_drawCut(TerminalAppearance_rule(look), &rail, 1, 1, lookRule(look));
    putText(&rail, 0, 2, "[<-] Back", lookMuted(look));
    const char *const *items = nullptr;
    bytes itemCount = 0;
    navItems(PrimeagenScreenState_panel(state), &items, &itemCount);
    i32 selection = PrimeagenScreenState_selection(state);
    i32 focusPanel = PrimeagenScreenState_focusPanel(state);
    for (bytes i = 0; i < itemCount && i + 4 < TerminalCanvas_getHeight(&rail); ++i) {
        bool selected = (i32) i == selection;
        TerminalPen pen = selected ? selectionPen(look, focusPanel == 0) : lookText(look);
        if (selected)
            putText(&rail, 0, i + 4, focusPanel == 0 ? "> " : "* ", pen);
        putText(&rail, 2, i + 4, items[i], pen);
    }
    if (TerminalCanvas_getHeight(&rail) > 0)
        putText(&rail, 0, TerminalCanvas_getHeight(&rail) - 1, "[+] Add", lookMuted(look));
    TerminalCanvas_vline(&inner, left, '|', lookRule(look));

    // Centre: channel title, transcript, palette, input, status.
    bytes centreWidth = right > 0 ? columns - left - right - 2 : columns - left - 1;
    TerminalCanvas centre = TerminalCanvas_sub(&inner, left + 1, 0, centreWidth, rows);
    putText(&centre, 1, 0, "Ecosystem > #general", lookText(look));
    TerminalRule_drawCut(TerminalAppearance_rule(look), &centre, 1, 1, lookRule(look));

    bytes statusRow = rows > 0 ? rows - 1 : 0;
    bytes inputRow = rows > 1 ? rows - 2 : 0;
    bytes sepInput = inputRow > 0 ? inputRow - 1 : 0;
    bytes chatHeight = sepInput > 2 ? sepInput - 2 : 0;
    TerminalCanvas chat = TerminalCanvas_sub(&centre, 0, 2, centreWidth, chatHeight);
    if (TerminalCanvas_isValid(&chat)) {
        bytes first = Tui_getFirstVisibleRow(tui);
        bytes total = Tui_getVisualRowCount(tui);
        bytes height = TerminalCanvas_getHeight(&chat);
        char line[512];
        u8 styles[512];
        for (bytes i = 0; i < height && first + i < total; ++i) {
            if (!Tui_copyStyledRow(tui, first + i, line, styles, sizeof line, nullptr))
                break;
            TerminalPen pen = lookText(look);
            if (styles[0] & TEXT_STYLE_USER) pen.style |= TERMINAL_STYLE_BOLD;
            if (styles[0] & TEXT_STYLE_ASSISTANT) pen.style |= TERMINAL_STYLE_ACCENT;
            if (styles[0] & TEXT_STYLE_ERROR) pen.style |= TERMINAL_STYLE_ERROR;
            putText(&chat, 0, i, line, pen);
        }
    }
    TerminalRule_drawCut(TerminalAppearance_rule(look), &centre, sepInput, 1, lookRule(look));
    putText(&centre, 0, inputRow, "> ", lookAccent(look));
    putText(&centre, 2, inputRow, Tui_getInput(tui), lookText(look));
    const char *status = PrimeagenScreenState_failed(state) ? "session closed | Ctrl-Q and relaunch"
                       : PrimeagenScreenState_busy(state) ? "working | Ctrl-Q cancels locally"
                       : PrimeagenScreenState_sessionMode(state) ? "OpenCode chat | inspect auto | edit/shell ask"
                       : "disconnected | /connect uses your login";
    putText(&centre, 0, statusRow, status, lookMuted(look));

    // Right rail: the people list — same focus rule as the left rail.
    if (right > 0) {
        TerminalCanvas people = TerminalCanvas_sub(&inner, columns - right, 0, right, rows);
        TerminalCanvas_vline(&inner, columns - right - 1, '|', lookRule(look));
        putCentered(&people, 0, "Users", lookText(look));
        TerminalRule_drawCut(TerminalAppearance_rule(look), &people, 1, 1, lookRule(look));
        for (bytes i = 0; i < itemCount && i + 2 < TerminalCanvas_getHeight(&people); ++i) {
            bool selected = (i32) i == selection;
            TerminalPen pen = selected ? selectionPen(look, focusPanel == 2) : lookMuted(look);
            putText(&people, 1, i + 2, items[i], pen);
        }
    }
    return true;
}

// Map a packed 0xRRGGBBAA foreground onto a ncurses colour pair, quantised to
// the terminal's palette and cached. TERMINAL_COLOR_DEFAULT keeps pair 0 (the
// host default). This is what makes a colour change distinguishable from
// reverse-video highlighting.
static i16 pairFor(u32 rgb) {
    static u32 keys[512];
    static i16 pairs[512];
    static i32 count = 0;
    if (rgb == TERMINAL_COLOR_DEFAULT || !has_colors())
        return 0;
    for (i32 i = 0; i < count; ++i)
        if (keys[i] == rgb)
            return pairs[i];
    if (count >= 511 || COLORS < 8)
        return 0;
    i32 colour;
    if (COLORS >= 256) {
        i32 r = TERMINAL_COLOR_RED(rgb) * 5 / 255;
        i32 g = TERMINAL_COLOR_GREEN(rgb) * 5 / 255;
        i32 b = TERMINAL_COLOR_BLUE(rgb) * 5 / 255;
        colour = 16 + 36 * r + 6 * g + b;
    } else {
        colour = (TERMINAL_COLOR_RED(rgb) > 127 ? 1 : 0)
               | (TERMINAL_COLOR_GREEN(rgb) > 127 ? 2 : 0)
               | (TERMINAL_COLOR_BLUE(rgb) > 127 ? 4 : 0);
    }
    i16 pair = (i16) (count + 1);
    if (init_pair(pair, (i16) colour, -1) != OK)
        return 0;
    keys[count] = rgb;
    pairs[count] = pair;
    ++count;
    return pair;
}

bool PrimeagenScreen_present(const PrimeagenScreen *self, WINDOW *win) {
    if (!PrimeagenScreen_isBound(self) || win == nullptr)
        return false;
    bytes rows = PrimeagenScreen_rows(self);
    bytes columns = PrimeagenScreen_columns(self);
    const u32 *cells = PrimeagenScreen_cells(self);
    const u16 *styles = PrimeagenScreen_styles(self);
    const u32 *fg = PrimeagenScreen_fg(self);
    for (bytes y = 0; y < rows; ++y) {
        for (bytes x = 0; x < columns; ++x) {
            bytes index = y * columns + x;
            u32 cp = cells[index];
            if (cp == 0)
                continue;
            u16 style = styles[index];
            attr_t attr = A_NORMAL;
            if (style & TERMINAL_STYLE_DIM) attr |= A_DIM;
            if (style & TERMINAL_STYLE_BOLD) attr |= A_BOLD;
            if (style & TERMINAL_STYLE_REVERSE) attr |= A_REVERSE;
            if (style & TERMINAL_STYLE_MUTED) attr |= A_DIM;
            if (style & TERMINAL_STYLE_ACCENT) attr |= A_BOLD;
            i16 pair = pairFor(fg[index]);
            wchar_t text[2] = { (wchar_t) cp, L'\0' };
            cchar_t glyph;
            if (setcchar(&glyph, text, attr, pair, nullptr) != OK)
                continue;
            (discard) mvadd_wch((i32) y, (i32) x, &glyph);
        }
    }
    return refresh() != ERR;
}
