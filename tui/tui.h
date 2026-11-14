#ifndef PRIMEAGEN_TUI_H
#define PRIMEAGEN_TUI_H

#include <stddef.h>
#include <stdint.h>
#include "tui/text_style.h"

/* Named cold allocation defaults, not permanent input/history ceilings. */
#define TUI_INPUT_CAPACITY_DEFAULT 32u
#define TUI_ENTRY_CAPACITY_DEFAULT 16u
#define TUI_CHROME_ROWS 6
#define TUI_MIN_COLUMNS 20
#define TUI_MIN_ROWS (TUI_CHROME_ROWS + 1)

/* SLOT RECORD: owned display bytes; no behavior or independently escaping row. */
typedef struct TuiEntry {
    char *text;
    u8 *styles;
    bytes length;
} TuiEntry;

/* Owner-thread-only display state. Not the agent, provider or tool dispatcher.
 * Initialized instances own their buffers; never shallow-copy a live instance.
 * Derived storage/count/inputLength mutate through operations only. Ordinary
 * live-object pointers are required; no arbitrary-address/type validation exists. */
typedef struct Tui {
    TuiEntry *entries;
    bytes count;
    bytes capacity;
    char *input;
    bytes inputLength;
    bytes inputCapacity;
    bytes scroll;
    int rows;
    int columns;
    bool dirty;
} Tui;

Tui Tui_0();
Tui Tui_1(bytes inputCapacity);
Tui Tui_2(bytes inputCapacity, bytes entryCapacity);
#define TUI_ARITY_(_0, _1, _2, N, ...) N
#define TUI_ARITY(...) TUI_ARITY_(0 __VA_OPT__(,) __VA_ARGS__, 2, 1, 0)
#define TUI_JOIN_(a, b) a##b
#define TUI_JOIN(a, b) TUI_JOIN_(a, b)
#define Tui(...) TUI_JOIN(Tui_, TUI_ARITY(__VA_ARGS__))(__VA_ARGS__)
#define Tui_zero() Tui_0()

/* Add copies/sanitizes a live span: safe UTF-8/newlines survive; malformed bytes,
 * terminal controls and bidi overrides render as escapes. Wrap uses wcwidth under
 * stable host LC_CTYPE, never splits a scalar; no full grapheme shaping. Input
 * editing remains ASCII. Growth is cold and fallible. No row or input
 * pointer escapes a mutation except the explicitly borrowed getInput view. */
bool Tui_add(Tui *self, const char *text, bytes length);
/* Sanitize first, then optional Markdown; baseStyle contains host flags only.
 * Admission preserves existing entries on allocation/parser failure. */
bool Tui_addStyled(Tui *self, const char *text, bytes length, u8 baseStyle, bool markdown);
bool Tui_entriesReserve(Tui *self, bytes capacity);
bool Tui_inputReserve(Tui *self, bytes capacity);
bool Tui_setInput(Tui *self, const char *text, bytes length);
bool Tui_inputAddChar(Tui *self, int character);
bool Tui_inputBackspace(Tui *self);
fn Tui_inputClear(Tui *self);
fn Tui_clear(Tui *self);
/* Remove the newest entry and its buffers; false when the transcript is empty
 * (an ordinary no-change condition, not a rejection). Used by the live
 * "working..." placeholder, which is always the trailing entry while shown. */
bool Tui_popLast(Tui *self);
fn Tui_free(Tui *self);

bool Tui_setSize(Tui *self, int rows, int columns);
fn Tui_getSize(const Tui *self, int *outRows, int *outColumns);
fn Tui_setScroll(Tui *self, bytes rowsFromBottom);
fn Tui_scrollBy(Tui *self, int64_t delta);
fn Tui_setDirty(Tui *self, bool dirty);

bool Tui_isReady(const Tui *self);
bool Tui_isDirty(const Tui *self);
bytes Tui_getCount(const Tui *self);
bytes Tui_getCapacity(const Tui *self);
const char *Tui_getInput(const Tui *self);
bytes Tui_getInputLength(const Tui *self);
bytes Tui_getInputCapacity(const Tui *self);
bytes Tui_getScroll(const Tui *self);
bytes Tui_getViewportHeight(const Tui *self);
bytes Tui_getViewportWidth(const Tui *self);
/* Sufficient byte capacity for any current row, including combining sequences. */
bytes Tui_getRowCapacity(const Tui *self);
bytes Tui_getVisualRowCount(const Tui *self);
bytes Tui_getFirstVisibleRow(const Tui *self);
bool Tui_copyRow(const Tui *self, bytes rowIndex, char *dest, bytes cap, bool *outTruncated);
/* styles parallels copied bytes and uses the same cap; nullptr omits flags. */
bool Tui_copyStyledRow(const Tui *self, bytes rowIndex, char *dest, u8 *styles, bytes cap, bool *outTruncated);

bool Tui_toString(const Tui *self, char *dest, bytes cap, bool *outTruncated);
bool Tui_toStringStruct(const Tui *self, char *dest, bytes cap, bool *outTruncated);

#endif
