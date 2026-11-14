#include "tui/tui.h"
#include "tui/markdown.h"
#include "tui/utf8.h"
#include "annotation/annotation.h"
#include "exception/throw.h"
#include "net/json.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

;;DEFINITION
/**
 * Tui owns the cold, flat display state of a terminal interface: copied transcript
 * entries, a growable ASCII command line, live terminal geometry and a viewport
 * offset measured from the bottom. It owns neither an agent nor a terminal screen.
 * Ncurses consumes its row projections; later agent events can append text without
 * printing into terminal scrollback. The UI thread excludes mutation/read overlap.
 *
 * Input and entries grow geometrically from caller-selected capacities. Rejection
 * preserves initialized text/count and ownership; partial constructors free their
 * buffers. Transcript bytes are made terminal-safe at admission, so rendering
 * never executes escape sequences. Wrapping is derived from current terminal-cell
 * width, not a guessed fixed screen size. Following the tail and reading older
 * output use Unicode scalar cell widths after optional cold Markdown removal; parallel
 * host-owned cell flags carry formatting and speaker styles, never ANSI strings.
 * output are distinct states: appends preserve a scrolled viewport's old content.
 *
 * This preview owns ordinary display buffers, not an ecosystem allocator or
 * persistent history backend. Shallow copying a live Tui is outside the contract.
 * No JSON parser is duplicated: cold structure projection uses Vexspoke's actual
 * Json_writeString for the quoted input field. UTF-8 transcript display preserves
 * scalars and uses the host's stable locale/wcwidth. Full grapheme/ZWJ shaping,
 * Unicode input editing and provider
 * integration are separate future work, not inferred from the ncurses frontend.
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Tui (tui/tui.c)
 * ============================================================================
 * STRUCT FIELDS (Mirroring tui/tui.h):
 * ----------------------------------------------------------------------------
 *   Tui {
 *     TuiEntry *entries;       // owned flat directory of copied display entries
 *     bytes count;            // initialized entries, never a rejection ceiling
 *     bytes capacity;         // current directory allocation; grows cold
 *     char *input;             // owned NUL-terminated printable-ASCII input
 *     bytes inputLength;      // initialized input bytes, excluding terminator
 *     bytes inputCapacity;    // current input allocation, including terminator
 *     bytes scroll;           // wrapped display rows above the bottom viewport
 *     int rows;                // current terminal rows; zero valid before attach
 *     int columns;             // current terminal columns; zero before attach
 *     bool dirty;              // repaint demand; ncurses clears after success
 *   }
 *
 * SLOT RECORD (behaviorless TuiEntry, owned only by Tui):
 * ----------------------------------------------------------------------------
 *   TuiEntry {
 *     char *text;              // owned terminal-safe UTF-8/newline bytes
 *     u8 *styles;         // owned per-visible-byte flags plus terminator
 *     bytes length;           // byte extent excluding terminator
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - Tui Tui_0(void)
 *   - Tui Tui_1(bytes inputCapacity)
 *   - Tui Tui_2(bytes inputCapacity, bytes entryCapacity)
 *   - Tui(...) / Tui_zero()
 *
 * Core operations:
 *   - bool Tui_add(Tui *self, const char *text, bytes length)
 *   - bool Tui_addStyled(Tui *self, const char *text, bytes length,
 *                        u8 baseStyle, bool markdown)
 *     Sanitize then format cold; failed admission publishes no entry.
 *   - bool Tui_entriesReserve(Tui *self, bytes capacity)
 *   - bool Tui_inputReserve(Tui *self, bytes capacity)
 *   - bool Tui_inputAddChar(Tui *self, int character)
 *   - bool Tui_inputBackspace(Tui *self)
 *   - void Tui_inputClear(Tui *self)
 *   - void Tui_clear(Tui *self)
 *   - bool Tui_popLast(Tui *self)
 *     Remove the newest entry; empty is ordinary no-change, not rejection.
 *
 * Setters:
 *   - bool Tui_setInput(Tui *self, const char *text, bytes length)
 *   - bool Tui_setSize(Tui *self, int rows, int columns)
 *   - void Tui_setScroll(Tui *self, bytes rowsFromBottom)
 *   - void Tui_scrollBy(Tui *self, int64_t delta)
 *   - void Tui_setDirty(Tui *self, bool dirty)
 *
 * Getters:
 *   - void Tui_getSize(const Tui *self, int *outRows, int *outColumns)
 *   - bool Tui_isReady(const Tui *self) / Tui_isDirty(const Tui *self)
 *   - bytes Tui_getCount / getCapacity / getInputLength / getInputCapacity /
 *            getScroll / getViewportHeight / getViewportWidth /
 *            getVisualRowCount / getFirstVisibleRow(const Tui *self)
 *   - bytes Tui_getRowCapacity(const Tui *self): max entry byte length plus NUL
 *   - const char *Tui_getInput(const Tui *self)
 *
 * Projections:
 *   - bool Tui_copyRow(const Tui *self, bytes rowIndex,
 *                     char *dest, bytes cap, bool *outTruncated)
 *   - bool Tui_copyStyledRow(const Tui *self, bytes rowIndex, char *dest,
 *                           u8 *styles, bytes cap, bool *outTruncated)
 *     Visible wrapping with parallel flags; optional styles output.
 *   - bool Tui_toString(const Tui *self, char *dest, bytes cap,
 *                       bool *outTruncated)
 *   - bool Tui_toStringStruct(const Tui *self, char *dest, bytes cap,
 *                             bool *outTruncated)
 *
 * Lifecycle:
 *   - void Tui_free(Tui *self)
 *
 * Private static helpers:
 *   - reject(reason) / growth(current, needed, elementSize, dest)
 *   - rowEnd(text,start,end,width): whole UTF-8 scalars wrapped by terminal cells
 *   - format(dest, cap, outTruncated, pattern, ...)
 *
 * CONSTRUCTION / FAILURE / OWNERSHIP CONTRACT:
 * ----------------------------------------------------------------------------
 *   Invalid capacities or allocation failure return an all-zero unready value.
 *   Invalid input, overflow and truncated projections reject loudly once.
 *   Setters reject rather than partially apply invalid input; scroll clamps to
 *   current content. Getters return safe defaults on nullptr. Backspace on an
 *   empty input returns false as an ordinary no-change condition, not rejection.
 *   Row projections do not escape directory pointers. getInput is borrowed only
 *   until the next input mutation/free. Clear releases entries but retains
 *   backing capacity; free releases all state and is repeatable. No worker,
 *   per-frame reflection, generic pointer validation or concurrency is promised.
 * ============================================================================
 */

;;INTENTION("derived count/capacity/backing/inputLength change only through admitted operations; raw setters would forge ownership per the Conflict Triage Law + Single Class Per File Law (Java Law)")

/* Report one cold rejection; no error text is supplied by model output here. */
static bool reject(const char *reason) {
    THROW("tui: %s", reason);
    return false;
}

/* Select geometric cold growth without wrapping capacity or allocation bytes. */
static bool growth(bytes current, bytes needed, bytes elementSize, bytes *dest) {
    if (needed > PTRDIFF_MAX / elementSize)
        return reject("capacity overflow");
    bytes capacity = current == 0 ? 1 : current;
    while (capacity < needed) {
        if (capacity > (bytes) PTRDIFF_MAX / elementSize / 2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }
    *dest = capacity;
    return true;
}

/* Construct an empty owner or reclaim every partially allocated buffer. */
Tui Tui_2(bytes inputCapacity, bytes entryCapacity) {
    Tui self = {0};
    if (inputCapacity == 0 || entryCapacity == 0 || inputCapacity > PTRDIFF_MAX || entryCapacity > PTRDIFF_MAX / sizeof(TuiEntry)) {
        reject("invalid initial capacities");
        return self;
    }
    self.input = malloc(inputCapacity);
    self.entries = calloc(entryCapacity, sizeof(TuiEntry));
    if (self.input == nullptr || self.entries == nullptr) {
        Tui_free(&self);
        reject("construction allocation failed");
        return self;
    }
    self.input[0] = '\0';
    self.inputCapacity = inputCapacity;
    self.capacity = entryCapacity;
    self.dirty = true;
    return self;
}

Tui Tui_0(void) {
    return Tui_2(TUI_INPUT_CAPACITY_DEFAULT, TUI_ENTRY_CAPACITY_DEFAULT);
}

Tui Tui_1(bytes inputCapacity) {
    return Tui_2(inputCapacity, TUI_ENTRY_CAPACITY_DEFAULT);
}

/* Grow backing without moving or freeing any entry's copied text. */
bool Tui_entriesReserve(Tui *self, bytes capacity) {
    if (!Tui_isReady(self))
        return reject("unready entry owner");
    if (capacity <= (*self).capacity)
        return true;
    bytes next;
    if (!growth((*self).capacity, capacity, sizeof(TuiEntry), &next))
        return false;
    TuiEntry *entries = realloc((*self).entries, next * sizeof(TuiEntry));
    if (entries == nullptr)
        return reject("entry growth allocation failed");
    (*self).entries = entries;
    (*self).capacity = next;
    return true;
}

/* Reserve more input room; caller must not retain a prior borrowed input view. */
bool Tui_inputReserve(Tui *self, bytes capacity) {
    if (!Tui_isReady(self))
        return reject("unready input owner");
    if (capacity <= (*self).inputCapacity)
        return true;
    bytes next;
    if (!growth((*self).inputCapacity, capacity, sizeof(char), &next))
        return false;
    char *input = realloc((*self).input, next);
    if (input == nullptr)
        return reject("input growth allocation failed");
    (*self).input = input;
    (*self).inputCapacity = next;
    return true;
}

/* Preserve valid Unicode scalars; escape malformed bytes and terminal controls. */
bool Tui_add(Tui *self, const char *text, bytes length) {
    return Tui_addStyled(self, text, length, 0, false);
}

bool Tui_addStyled(Tui *self, const char *text, bytes length, u8 baseStyle, bool markdown) {
    if (baseStyle & ~TEXT_STYLE_MASK)
        return reject("invalid display style");
    if (!Tui_isReady(self) || text == nullptr || length > (bytes) PTRDIFF_MAX / 4 - 1 || (*self).count == SIZE_MAX)
        return reject("invalid transcript span");
    bytes previousRows = Tui_getVisualRowCount(self);
    char *copy = malloc(length * 4 + 1);
    if (copy == nullptr)
        return reject("transcript allocation failed");
    static const char HEX[] = "0123456789abcdef";
    bytes used = 0;
    for (bytes i = 0; i < length;) {
        unsigned char c = (unsigned char) text[i];
        u32 scalar;
        bytes bytes = TuiUtf8_decode(text + i, length - i, &scalar);
        if (bytes != 0 && TuiUtf8_safe(scalar)) {
            memcpy(copy + used, text + i, bytes);
            used += bytes;
            i += bytes;
        } else {
            copy[used++] = '\\';
            copy[used++] = 'x';
            copy[used++] = HEX[c >> 4];
            copy[used++] = HEX[c & 15];
            ++i;
        }
    }
    copy[used] = '\0';
    u8 *styles = malloc(used + 1);
    char *rendered = markdown ? malloc(used + 1) : nullptr;
    if (styles == nullptr || (markdown && rendered == nullptr)) {
        free(styles);
        free(rendered);
        free(copy);
        return reject("style allocation failed");
    }
    if (markdown) {
        bytes visible = 0;
        if (!Markdown_render(copy, used, rendered, styles, used + 1, &visible, nullptr)) {
            free(styles);
            free(rendered);
            free(copy);
            return false;
        }
        free(copy);
        copy = rendered;
        used = visible;
        for (bytes i = 0; i < used; ++i)
            styles[i] |= baseStyle;
    } else
        memset(styles, baseStyle, used);
    styles[used] = 0;
    if (!Tui_entriesReserve(self, (*self).count + 1)) {
        free(copy);
        free(styles);
        return false;
    }
    TuiEntry *entry = &(*self).entries[(*self).count];
    (*entry).text = copy;
    (*entry).styles = styles;
    (*entry).length = used;
    ++(*self).count;
    if ((*self).scroll != 0) {
        bytes added = Tui_getVisualRowCount(self) - previousRows;
        (*self).scroll = added > SIZE_MAX - (*self).scroll ? SIZE_MAX : (*self).scroll + added;
    }
    Tui_setScroll(self, (*self).scroll);
    (*self).dirty = true;
    return true;
}

/* Copy validated ASCII before growth, so self/interior input aliases stay safe. */
bool Tui_setInput(Tui *self, const char *text, bytes length) {
    if (!Tui_isReady(self) || text == nullptr || length >= PTRDIFF_MAX)
        return reject("invalid input span");
    for (bytes i = 0; i < length; ++i)
        if ((unsigned char) text[i] < ' ' || (unsigned char) text[i] > '~')
            return reject("input editing is printable ASCII only");
    char *copy = malloc(length + 1);
    if (copy == nullptr)
        return reject("input copy allocation failed");
    memcpy(copy, text, length);
    copy[length] = '\0';
    if (!Tui_inputReserve(self, length + 1)) {
        free(copy);
        return false;
    }
    memcpy((*self).input, copy, length + 1);
    free(copy);
    (*self).inputLength = length;
    (*self).dirty = true;
    return true;
}

bool Tui_inputAddChar(Tui *self, int character) {
    if (!Tui_isReady(self) || character < ' ' || character > '~' || (*self).inputLength >= PTRDIFF_MAX - 1)
        return reject("invalid input character");
    if (!Tui_inputReserve(self, (*self).inputLength + 2))
        return false;
    (*self).input[(*self).inputLength++] = (char) character;
    (*self).input[(*self).inputLength] = '\0';
    (*self).dirty = true;
    return true;
}

bool Tui_inputBackspace(Tui *self) {
    if (!Tui_isReady(self))
        return reject("unready input owner");
    if ((*self).inputLength == 0)
        return false;
    (*self).input[--(*self).inputLength] = '\0';
    (*self).dirty = true;
    return true;
}

void Tui_inputClear(Tui *self) {
    if (self == nullptr || (*self).input == nullptr)
        return;
    (*self).input[0] = '\0';
    (*self).inputLength = 0;
    (*self).dirty = true;
}

/* Reclaim copied entries, retain growable backing, and return to tail following. */
void Tui_clear(Tui *self) {
    if (self == nullptr)
        return;
    for (bytes i = 0; i < (*self).count; ++i) {
        TuiEntry *entry = &(*self).entries[i];
        free((*entry).text);
        free((*entry).styles);
        *entry = (TuiEntry) {0};
    }
    (*self).count = 0;
    (*self).scroll = 0;
    (*self).dirty = true;
}

/* Drop only the newest entry: the caller owns the "it was the trailing one"
 * invariant. Empty is ordinary no-change, never a rejection. */
bool Tui_popLast(Tui *self) {
    if (!Tui_isReady(self))
        return reject("unready transcript owner");
    if ((*self).count == 0)
        return false;
    TuiEntry *entry = &(*self).entries[(*self).count - 1];
    free((*entry).text);
    free((*entry).styles);
    *entry = (TuiEntry) {0};
    --(*self).count;
    Tui_setScroll(self, (*self).scroll);
    (*self).dirty = true;
    return true;
}

void Tui_free(Tui *self) {
    if (self == nullptr)
        return;
    Tui_clear(self);
    free((*self).entries);
    free((*self).input);
    *self = (Tui) {0};
}

bool Tui_setSize(Tui *self, int rows, int columns) {
    if (self == nullptr || rows < 0 || columns < 0)
        return reject("invalid terminal geometry");
    (*self).rows = rows;
    (*self).columns = columns;
    Tui_setScroll(self, (*self).scroll);
    (*self).dirty = true;
    return true;
}

void Tui_getSize(const Tui *self, int *outRows, int *outColumns) {
    if (outRows != nullptr)
        *outRows = self == nullptr ? 0 : (*self).rows;
    if (outColumns != nullptr)
        *outColumns = self == nullptr ? 0 : (*self).columns;
}

/* Scroll is clamped to actual current wrapped content, not transcript entries. */
void Tui_setScroll(Tui *self, bytes rowsFromBottom) {
    if (self == nullptr)
        return;
    bytes total = Tui_getVisualRowCount(self);
    bytes height = Tui_getViewportHeight(self);
    bytes maximum = total > height ? total - height : 0;
    (*self).scroll = rowsFromBottom < maximum ? rowsFromBottom : maximum;
    (*self).dirty = true;
}

void Tui_scrollBy(Tui *self, int64_t delta) {
    if (self == nullptr)
        return;
    bytes current = (*self).scroll;
    if (delta >= 0) {
        u64 magnitude = (u64) delta;
        bytes next = magnitude > SIZE_MAX - current ? SIZE_MAX : current + (bytes) magnitude;
        Tui_setScroll(self, next);
    } else {
        u64 magnitude = (u64) (-(delta + 1)) + 1;
        Tui_setScroll(self, magnitude > current ? 0 : current - (bytes) magnitude);
    }
}

void Tui_setDirty(Tui *self, bool dirty) {
    if (self != nullptr)
        (*self).dirty = dirty;
}

bool Tui_isReady(const Tui *self) { return self != nullptr && (*self).input != nullptr && (*self).entries != nullptr; }
bool Tui_isDirty(const Tui *self) { return self != nullptr && (*self).dirty; }
bytes Tui_getCount(const Tui *self) { return self == nullptr ? 0 : (*self).count; }
bytes Tui_getCapacity(const Tui *self) { return self == nullptr ? 0 : (*self).capacity; }
const char *Tui_getInput(const Tui *self) { return self == nullptr ? nullptr : (*self).input; }
bytes Tui_getInputLength(const Tui *self) { return self == nullptr ? 0 : (*self).inputLength; }
bytes Tui_getInputCapacity(const Tui *self) { return self == nullptr ? 0 : (*self).inputCapacity; }
bytes Tui_getScroll(const Tui *self) { return self == nullptr ? 0 : (*self).scroll; }
bytes Tui_getViewportHeight(const Tui *self) { return self == nullptr || (*self).rows < TUI_MIN_ROWS ? 0 : (bytes) ((*self).rows - TUI_CHROME_ROWS); }
bytes Tui_getViewportWidth(const Tui *self) { return self == nullptr || (*self).columns < TUI_MIN_COLUMNS ? 0 : (bytes) ((*self).columns - 2); }

bytes Tui_getRowCapacity(const Tui *self) {
    bytes capacity = 1;
    if (self == nullptr)
        return capacity;
    for (bytes i = 0; i < (*self).count; ++i) {
        const TuiEntry *entry = &(*self).entries[i];
        if ((*entry).length + 1 > capacity)
            capacity = (*entry).length + 1;
    }
    return capacity;
}

static bytes rowEnd(const char *text, bytes start, bytes end, bytes width) {
    bytes at = start, cells = 0;
    while (at < end) {
        u32 scalar;
        bytes consumed = TuiUtf8_decode(text + at, end - at, &scalar);
        if (consumed == 0)
            break; // cold admission guarantees valid display bytes
        bytes nextCells = TuiUtf8_width(scalar);
        if (nextCells > width - cells && at != start)
            break;
        cells += nextCells;
        at += consumed;
    }
    return at;
}

/* Measure logical lines and wrap chunks without allocating a flattened copy. */
bytes Tui_getVisualRowCount(const Tui *self) {
    bytes width = Tui_getViewportWidth(self);
    if (self == nullptr || width == 0)
        return 0;
    bytes total = 0;
    for (bytes i = 0; i < (*self).count; ++i) {
        const TuiEntry *entry = &(*self).entries[i];
        bytes start = 0;
        for (bytes at = 0; at <= (*entry).length; ++at)
            if (at == (*entry).length || (*entry).text[at] == '\n') {
                bytes next = start;
                do {
                    if (total == SIZE_MAX)
                        return SIZE_MAX;
                    ++total;
                    next = rowEnd((*entry).text, next, at, width);
                } while (next < at);
                start = at + 1;
            }
    }
    return total;
}

bytes Tui_getFirstVisibleRow(const Tui *self) {
    bytes total = Tui_getVisualRowCount(self);
    bytes height = Tui_getViewportHeight(self);
    if (self == nullptr || total <= height)
        return 0;
    bytes bottom = total - height;
    return (*self).scroll > bottom ? 0 : bottom - (*self).scroll;
}

/* Copy one full visual row or flag the partial buffer; absence leaves empty text. */
bool Tui_copyRow(const Tui *self, bytes rowIndex, char *dest, bytes cap, bool *outTruncated) {
    return Tui_copyStyledRow(self, rowIndex, dest, nullptr, cap, outTruncated);
}

bool Tui_copyStyledRow(const Tui *self, bytes rowIndex, char *dest, u8 *styles, bytes cap, bool *outTruncated) {
    if (outTruncated != nullptr)
        *outTruncated = false;
    if (dest == nullptr || cap == 0) {
        if (outTruncated != nullptr)
            *outTruncated = true;
        return reject("missing row destination");
    }
    dest[0] = '\0';
    if (styles != nullptr)
        styles[0] = 0;
    bytes width = Tui_getViewportWidth(self);
    if (self == nullptr || width == 0)
        return reject("row unavailable");
    for (bytes i = 0; i < (*self).count; ++i) {
        const TuiEntry *entry = &(*self).entries[i];
        bytes start = 0;
        for (bytes at = 0; at <= (*entry).length; ++at)
            if (at == (*entry).length || (*entry).text[at] == '\n') {
                bytes next = start;
                do {
                    bytes end = rowEnd((*entry).text, next, at, width);
                    if (rowIndex != 0) {
                        --rowIndex;
                        next = end;
                        continue;
                    }
                    bytes count = end - next;
                    bytes copied = 0;
                    while (copied < count) {
                        u32 scalar;
                        bytes bytes = TuiUtf8_decode((*entry).text + next + copied, count - copied, &scalar);
                        if (bytes > cap - 1 - copied)
                            break;
                        copied += bytes;
                    }
                    memcpy(dest, (*entry).text + next, copied);
                    dest[copied] = '\0';
                    if (styles != nullptr) {
                        memcpy(styles, (*entry).styles + next, copied);
                        styles[copied] = 0;
                    }
                    if (copied != count) {
                        if (outTruncated != nullptr)
                            *outTruncated = true;
                        return reject("row projection truncated");
                    }
                    return true;
                } while (next < at);
                start = at + 1;
            }
    }
    return reject("row index out of range");
}

/* Common bounded cold formatter; callers never project an unvalidated row. */
static bool format(char *dest, bytes cap, bool *outTruncated, const char *pattern, ...) {
    if (outTruncated != nullptr)
        *outTruncated = false;
    if (dest == nullptr || cap == 0) {
        if (outTruncated != nullptr)
            *outTruncated = true;
        return reject("missing projection destination");
    }
    va_list args;
    va_start(args, pattern);
    int length = vsnprintf(dest, cap, pattern, args);
    va_end(args);
    if (length < 0 || (bytes) length >= cap) {
        if (outTruncated != nullptr)
            *outTruncated = true;
        return reject("projection truncated");
    }
    return true;
}

bool Tui_toString(const Tui *self, char *dest, bytes cap, bool *outTruncated) {
    if (self == nullptr)
        return format(dest, cap, outTruncated, "nullptr");
    return format(dest, cap, outTruncated, "tui: %zu entries, %zu input bytes, %zu rows above tail", (*self).count, (*self).inputLength, (*self).scroll);
}

/* Declaration-order field dump, with real Vexspoke JSON string escaping. */
bool Tui_toStringStruct(const Tui *self, char *dest, bytes cap, bool *outTruncated) {
    if (self == nullptr)
        return format(dest, cap, outTruncated, "nullptr");
    if (!format(dest, cap, outTruncated, "Tui{entries=%p,count=%zu,capacity=%zu,input=", (void*) (*self).entries, (*self).count, (*self).capacity))
        return false;
    bytes used = strlen(dest);
    int64_t length = Json_writeString(dest + used, cap - used, (*self).input == nullptr ? "" : (*self).input);
    if (length < 0) {
        dest[used] = '\0';
        if (outTruncated != nullptr)
            *outTruncated = true;
        return reject("input projection truncated");
    }
    used += (bytes) length;
    return format(dest + used, cap - used, outTruncated, ",inputLength=%zu,inputCapacity=%zu,scroll=%zu,rows=%d,columns=%d,dirty=%s}", (*self).inputLength, (*self).inputCapacity, (*self).scroll, (*self).rows, (*self).columns, (*self).dirty ? "true" : "false");
}
