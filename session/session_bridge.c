#include "vex.h"
#include "session/session_bridge.h"
#include "personality/owo.h"
#include "net/json.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

;;DEFINITION
/* SessionBridge borrows two host-created nonblocking pipes and consumes API
 * Haven's actual EngineProvider/Harness contracts. The host owns OpenCode login,
 * model selection, child cleanup and its dedicated remote session. A turn is an
 * injected Harness job, not a new connector or replacement agent engine. Writes
 * and reads advance incrementally on the terminal owner thread; no blocking wait
 * or provider API runs here. Cold admission bounds each frame and escapes JSON
 * through Vexspoke's writer. A malformed/failed/timed-out wire is poisoned rather
 * than risking attributing a late reply to another turn.
 *
 * A turn can pause for a human. The host emits a bounded approval frame when the
 * remote policy requires consent; the bridge records it as a pending question,
 * keeps the turn open, and suspends its local deadline because the wait belongs
 * to the terminal, not the model. SessionBridge_decide queues exactly one
 * once/always/reject reply and restarts the remote watchdog. The bridge never
 * decides by itself and never auto-approves: only an explicit terminal decision
 * leaves here, and the durable project rule stays OpenCode's.
 *
 * A turn is also a stream, not one lump. The host emits a bounded {note:...} line
 * as each tool acts, and the bridge surfaces it through SessionBridge_takeNote so
 * the terminal can append it immediately. Exactly one frame is consumed per poll,
 * so a burst of notes stays ordered and none is dropped. Free cancels
 * admission before the host releases pipes; remote cancellation is best effort,
 * not proof of a killed server operation. Derived buffers/identity have no raw
 * setters per the Conflict Triage Law and Single Class Per File Law (Java Law). */
;;OVERVIEW
/* ============================================================================
 * CLASS: SessionBridge (session/session_bridge)
 * STRUCT FIELDS (Mirroring session/session_bridge.h):
 *   Harness harness : API Haven owner of admitted single in-flight job.
 *   i32 readFd : borrowed nonblocking host response pipe; -1 unbound.
 *   i32 writeFd : borrowed nonblocking host request pipe; -1 unbound.
 *   bool owo : selects canonical Owo_instructions for subsequent turns.
 *   u32 jobId : current Haven job; zero means idle.
 *   u64 startedMs : monotonic clock of the last observed progress.
 *   char request[SESSION_REQUEST_CAP] : bounded outbound JSON frame queue.
 *   bytes requestLength : queued frame bytes, including newline.
 *   bytes requestOffset : bytes already accepted by the pipe.
 *   char response[SESSION_RESPONSE_CAP] : inbound newline JSON frame.
 *   bytes responseLength : accumulated bytes; preserves partial frames.
 *   char text[SESSION_RESPONSE_CAP] : copied answer or safe generic error.
 *   bool failed : permanent poisoned-wire latch until host relaunch.
 *   bool awaiting : an approval frame is displayed and unanswered.
 *   u32 approvalSerial : increments per approval frame; display identity.
 *   char approvalId[SESSION_APPROVAL_ID_CAP] : remote permission identity.
 *   char approvalLabel[SESSION_APPROVAL_LABEL_CAP] : bounded display text.
 *   char note[SESSION_NOTE_CAP] : newest streamed activity line.
 *   bool notePending : a note is waiting for the terminal to take it.
 *   char decision[8] : queued reply spelling; empty when nothing is queued.
 * FUNCTION REGISTRY:
 *   SessionBridge SessionBridge_0() : unbound.
 *   SessionBridge SessionBridge_2(i32 readFd, i32 writeFd) : borrowed pipes.
 *   bool SessionBridge_submit(SessionBridge *, const char *) : cold admission.
 *   i32 SessionBridge_poll(SessionBridge *) : bounded IO; one frame per call.
 *   fn SessionBridge_free(SessionBridge *) : cancel borrowed job, never close fd.
 *   fn SessionBridge_setOwo(SessionBridge *, bool), bool SessionBridge_isOwo(const SessionBridge *)
 *   bool SessionBridge_isBusy(const SessionBridge *), const char *SessionBridge_getText(const SessionBridge *)
 *   bool SessionBridge_isAwaiting(const SessionBridge *)
 *   const char *SessionBridge_getApprovalLabel(const SessionBridge *)
 *   u32 SessionBridge_getApprovalSerial(const SessionBridge *)
 *   const char *SessionBridge_takeNote(SessionBridge *) : borrow one note, clear it.
 *   bool SessionBridge_decide(SessionBridge *, const char *) : queue once/always/reject.
 *   bool SessionBridge_setTimeout(SessionBridge *, u64), u64 SessionBridge_getTimeout(const SessionBridge *) : validated cold current watchdog, 1..120000ms.
 *   bool SessionBridge_toString(const SessionBridge *, char *, bytes, bool *) : bounded value.
 *   bool SessionBridge_toStringStruct(const SessionBridge *, char *, bytes, bool *) : bounded redacted fields.
 * Private static helpers:
 *   u64 nowMs() : monotonic clock, zero on clock failure.
 *   bool queueText(SessionBridge *, const char *, const char *) : one-field frame.
 *   bool spawn(any , const EngineProviderSlot *, const char *, bytes, u64, any *) : queue JSON.
 *   HarnessStatus handleFrame(SessionBridge *, JsonDoc *) : classify one reply frame.
 *   HarnessStatus pollJob(any , any , u64) : progress pipes, one frame per call.
 *   fn cancel(any , any ) : clear pending IO, poison wire.
 * CONTRACT: trusted live pipes, valid owner address, serialized calls only.
 * Null/default safe, busy rejection preserves current turn; no arbitrary pointer
 * validation. Prompt is printable ASCII/newline/tab, response bytes are escaped
 * by the display. Local helper emits exactly {ok:boolean,text:string},
 * {ask:{id,label}} or {note:string}; decisions emit {decision:string}. Duplicate
 * keys, embedded NUL, trailing junk in one frame and excess fields reject. Error
 * texts are generic and never contain provider diagnostics/tokens. Notes are
 * single bounded display lines, never parsed as instructions. No history storage
 * or secrets.
 * ============================================================================ */
;;INTENTION("derived wire identity/buffers mutate only through admission/decide/poll/free; projections redact transcript buffers and summarize the borrowed Haven driver (which has no bounded projection), preserving privacy under the Conflict Triage Law")

;;GETTER
static inline Harness *SessionBridge_harness(SessionBridge *self) { return &(*self).harness; }
;;GETTER
static inline const Harness *SessionBridge_harnessConst(const SessionBridge *self) { return &(*self).harness; }
;;GETTER
static inline i32 SessionBridge_readFd(const SessionBridge *self) { return (*self).readFd; }
;;GETTER
static inline i32 SessionBridge_writeFd(const SessionBridge *self) { return (*self).writeFd; }
;;GETTER
static inline u32 SessionBridge_jobId(const SessionBridge *self) { return (*self).jobId; }
;;GETTER
static inline u32 *SessionBridge_jobIdPtr(SessionBridge *self) { return &(*self).jobId; }
;;SETTER
static inline fn SessionBridge_setJobId(SessionBridge *self, u32 jobId) { (*self).jobId = jobId; }
;;GETTER
static inline u64 SessionBridge_startedMs(const SessionBridge *self) { return (*self).startedMs; }
;;SETTER
static inline fn SessionBridge_setStartedMs(SessionBridge *self, u64 startedMs) { (*self).startedMs = startedMs; }
;;GETTER
static inline char *SessionBridge_request(SessionBridge *self) { return (*self).request; }
;;GETTER
static inline bytes SessionBridge_requestLength(const SessionBridge *self) { return (*self).requestLength; }
;;SETTER
static inline fn SessionBridge_setRequestLength(SessionBridge *self, bytes requestLength) { (*self).requestLength = requestLength; }
;;GETTER
static inline bytes SessionBridge_requestOffset(const SessionBridge *self) { return (*self).requestOffset; }
;;SETTER
static inline fn SessionBridge_setRequestOffset(SessionBridge *self, bytes requestOffset) { (*self).requestOffset = requestOffset; }
;;GETTER
static inline char *SessionBridge_response(SessionBridge *self) { return (*self).response; }
;;GETTER
static inline bytes SessionBridge_responseLength(const SessionBridge *self) { return (*self).responseLength; }
;;SETTER
static inline fn SessionBridge_setResponseLength(SessionBridge *self, bytes responseLength) { (*self).responseLength = responseLength; }
;;GETTER
static inline char *SessionBridge_textMut(SessionBridge *self) { return (*self).text; }
;;GETTER
static inline bool SessionBridge_failed(const SessionBridge *self) { return (*self).failed; }
;;SETTER
static inline fn SessionBridge_setFailed(SessionBridge *self, bool failed) { (*self).failed = failed; }
;;SETTER
static inline fn SessionBridge_setAwaiting(SessionBridge *self, bool awaiting) { (*self).awaiting = awaiting; }
;;SETTER
static inline fn SessionBridge_setApprovalSerial(SessionBridge *self, u32 approvalSerial) { (*self).approvalSerial = approvalSerial; }
;;GETTER
static inline char *SessionBridge_approvalId(SessionBridge *self) { return (*self).approvalId; }
;;GETTER
static inline char *SessionBridge_approvalLabelMut(SessionBridge *self) { return (*self).approvalLabel; }
;;GETTER
static inline char *SessionBridge_note(SessionBridge *self) { return (*self).note; }
;;GETTER
static inline bool SessionBridge_notePending(const SessionBridge *self) { return (*self).notePending; }
;;SETTER
static inline fn SessionBridge_setNotePending(SessionBridge *self, bool notePending) { (*self).notePending = notePending; }
;;GETTER
static inline char *SessionBridge_decision(SessionBridge *self) { return (*self).decision; }
static u64 nowMs() {
    struct timespec stamp;
    if (clock_gettime(CLOCK_MONOTONIC, &stamp) != 0)
        return 0;
    return (u64) stamp.tv_sec * 1000u + (u64) stamp.tv_nsec / 1000000u;
}
;;WHAT("SessionBridge")
static bool queueText(SessionBridge *self, const char *key, const char *value) {
    char *request = SessionBridge_request(self);
    JsonWriter writer = JsonWriter(request, sizeof(((SessionBridge) {0}).request) - 1);
    Json_beginObject(&writer);
    Json_addKey(&writer, key);
    Json_stringVal(&writer, value);
    Json_endObject(&writer);
    if (!JsonWriter_ok(&writer))
        return false;
    bytes len = JsonWriter_used(&writer);
    request[len++] = '\n';
    SessionBridge_setRequestLength(self, len);
    SessionBridge_setRequestOffset(self, 0);
    return true;
}
;;WHAT("SessionBridge")
static bool spawn(any context, const EngineProviderSlot *engine, const char *prompt,
                  bytes length, u64 timeoutMs, any *outHandle) {
    (discard) engine;
    (discard) length;
    (discard) timeoutMs;
    SessionBridge *self = context;
    char *request = SessionBridge_request(self);
    JsonWriter writer = JsonWriter(request, sizeof(((SessionBridge) {0}).request) - 1);
    Json_beginObject(&writer);
    Json_addKey(&writer, "prompt");
    Json_stringVal(&writer, prompt);
    Json_addKey(&writer, "instructions");
    Json_stringVal(&writer, Owo_instructions(SessionBridge_isOwo(self)));
    Json_endObject(&writer);
    if (!JsonWriter_ok(&writer))
        return false;
    bytes len = JsonWriter_used(&writer);
    request[len++] = '\n';
    SessionBridge_setRequestLength(self, len);
    SessionBridge_setRequestOffset(self, 0);
    SessionBridge_setResponseLength(self, 0);
    u64 started = nowMs();
    SessionBridge_setStartedMs(self, started);
    SessionBridge_textMut(self)[0] = '\0';
    SessionBridge_setAwaiting(self, false);
    SessionBridge_setNotePending(self, false);
    SessionBridge_note(self)[0] = '\0';
    SessionBridge_decision(self)[0] = '\0';
    *outHandle = self;
    return started != 0;
}
;;WHAT("SessionBridge")
static HarnessStatus handleFrame(SessionBridge *self, JsonDoc *doc) {
    JsonRef root = Json_root(doc);
    if (Json_type(doc, root) != JSON_OBJECT)
        return HARNESS_STATUS_TIMEOUT;
    JsonRef ask = Json_member(doc, root, "ask");
    JsonRef note = Json_member(doc, root, "note");
    if (Json_count(doc, root) == 1 && Json_type(doc, ask) == JSON_OBJECT) {
        JsonRef identity = Json_member(doc, ask, "id");
        JsonRef label = Json_member(doc, ask, "label");
        if (Json_count(doc, ask) != 2 || Json_type(doc, identity) != JSON_STRING ||
            Json_type(doc, label) != JSON_STRING)
            return HARNESS_STATUS_TIMEOUT;
        u32 idLength, labelLength;
        const char *idValue = Json_string(doc, identity, &idLength);
        const char *labelValue = Json_string(doc, label, &labelLength);
        JsonStringView id = Json_getString(doc, identity);
        JsonStringView display = Json_getString(doc, label);
        if (!idValue || idLength == 0 || memchr(idValue, '\0', idLength) || !labelValue ||
            memchr(labelValue, '\0', labelLength) || id.data == nullptr ||
            id.length >= sizeof(((SessionBridge) {0}).approvalId) || display.data == nullptr ||
            display.length >= sizeof(((SessionBridge) {0}).approvalLabel))
            return HARNESS_STATUS_TIMEOUT;
        char *approvalId = SessionBridge_approvalId(self);
        char *approvalLabel = SessionBridge_approvalLabelMut(self);
        memcpy(approvalId, id.data, id.length);
        approvalId[id.length] = '\0';
        memcpy(approvalLabel, display.data, display.length);
        approvalLabel[display.length] = '\0';
        SessionBridge_setAwaiting(self, true);
        SessionBridge_setApprovalSerial(self, SessionBridge_getApprovalSerial(self) + 1);
        return HARNESS_STATUS_RUNNING;
    }
    if (Json_count(doc, root) == 1 && Json_type(doc, note) == JSON_STRING) {
        /* A decoded NUL byte would truncate the display line; reject it instead. */
        u32 noteLength;
        const char *noteValue = Json_string(doc, note, &noteLength);
        JsonStringView noteText = Json_getString(doc, note);
        if (!noteValue || noteLength == 0 || memchr(noteValue, '\0', noteLength) ||
            noteText.data == nullptr || noteText.length >= sizeof(((SessionBridge) {0}).note))
            return HARNESS_STATUS_TIMEOUT;
        char *noteBuf = SessionBridge_note(self);
        memcpy(noteBuf, noteText.data, noteText.length);
        noteBuf[noteText.length] = '\0';
        SessionBridge_setNotePending(self, true);
        return HARNESS_STATUS_RUNNING;
    }
    JsonRef success = Json_member(doc, root, "ok");
    JsonRef text = Json_member(doc, root, "text");
    if (Json_count(doc, root) != 2 ||
        (Json_type(doc, success) != JSON_TRUE && Json_type(doc, success) != JSON_FALSE) ||
        Json_type(doc, text) != JSON_STRING)
        return HARNESS_STATUS_TIMEOUT;
    u32 textLength;
    const char *value = Json_string(doc, text, &textLength);
    JsonStringView textValue = Json_getString(doc, text);
    if (!value || memchr(value, '\0', textLength) || !Json_bool(doc, success) ||
        textValue.data == nullptr || textValue.length >= sizeof(((SessionBridge) {0}).text))
        return HARNESS_STATUS_TIMEOUT;
    char *textBuf = SessionBridge_textMut(self);
    memcpy(textBuf, textValue.data, textValue.length);
    textBuf[textValue.length] = '\0';
    return HARNESS_STATUS_DONE;
}
;;WHAT("SessionBridge")
static HarnessStatus pollJob(any context, any handle, u64 timeoutMs) {
    (discard) handle;
    SessionBridge *self = context;
    if (SessionBridge_failed(self))
        goto fail;
    bytes reqOffset = SessionBridge_requestOffset(self);
    bytes reqLen = SessionBridge_requestLength(self);
    if (reqOffset < reqLen) {
        ssize_t sent = write(SessionBridge_writeFd(self), SessionBridge_request(self) + reqOffset,
                             reqLen - reqOffset);
        if (sent > 0)
            SessionBridge_setRequestOffset(self, reqOffset + (bytes) sent);
        else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            goto fail;
    }
    /* Exactly one frame per call keeps a burst of streamed notes ordered. A frame
     * already buffered is progress we hold, so it is consumed before the watchdog
     * is consulted; the watchdog only measures silence with nothing in hand. */
    char *response = SessionBridge_response(self);
    bytes respLen = SessionBridge_responseLength(self);
    char *newline = memchr(response, '\n', respLen);
    if (newline == nullptr) {
        /* The human wait is not the model's budget: while an approval is displayed
         * the watchdog is suspended and SessionBridge_decide restarts it on
         * submission. Otherwise it measures silence since the last frame, not the
         * turn's total duration, so tool-using turns may run for minutes. */
        if (!SessionBridge_isAwaiting(self)) {
            u64 now = nowMs();
            u64 started = SessionBridge_startedMs(self);
            if (!now || now < started || now - started >= timeoutMs)
                goto fail;
        }
        if (respLen >= sizeof(((SessionBridge) {0}).response) - 1)
            goto fail;
        ssize_t n = read(SessionBridge_readFd(self), response + respLen,
                         sizeof(((SessionBridge) {0}).response) - 1 - respLen);
        if (n == 0)
            goto fail;
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                return HARNESS_STATUS_RUNNING;
            goto fail;
        }
        if (memchr(response + respLen, '\0', (bytes) n))
            goto fail;
        respLen += (bytes) n;
        SessionBridge_setResponseLength(self, respLen);
        response[respLen] = '\0';
        newline = memchr(response, '\n', respLen);
        if (newline == nullptr)
            return HARNESS_STATUS_RUNNING;
    }
    bytes frame = (bytes) (newline - response);
    *newline = '\0';
    JsonDoc doc;
    JsonNode nodes[8];
    char scratch[SESSION_RESPONSE_CAP];
    if (!Json_parse(&doc, nodes, 8, scratch, sizeof(scratch), response))
        goto fail;
    HarnessStatus status = handleFrame(self, &doc);
    bytes remaining = respLen - frame - 1;
    memmove(response, newline + 1, remaining);
    SessionBridge_setResponseLength(self, remaining);
    if (status == HARNESS_STATUS_TIMEOUT)
        goto fail;
    /* Any frame is progress: the watchdog measures remote silence, not total
     * duration, so a i64 multi-tool turn is not killed for taking minutes. */
    u64 started = nowMs();
    SessionBridge_setStartedMs(self, started);
    if (!started)
        goto fail;
    return status;
fail:
    SessionBridge_setFailed(self, true);
    SessionBridge_setAwaiting(self, false);
    SessionBridge_setNotePending(self, false);
    strcpy(SessionBridge_textMut(self), "OpenCode turn failed or timed out. Exit and relaunch; remote cancellation is best effort.");
    return HARNESS_STATUS_TIMEOUT;
}
;;WHAT("SessionBridge")
static fn cancel(any context, any handle) {
    (discard) handle;
    SessionBridge *self = context;
    SessionBridge_setFailed(self, true);
    SessionBridge_setAwaiting(self, false);
    SessionBridge_setNotePending(self, false);
    SessionBridge_setRequestLength(self, 0);
}
SessionBridge SessionBridge_0() {
    SessionBridge self = {0};
    self.harness = Harness_0();
    Harness_setTimeout(&self.harness, SESSION_TIMEOUT_MS_DEFAULT);
    self.readFd = -1;
    self.writeFd = -1;
    return self;
}
SessionBridge SessionBridge_2(i32 readFd, i32 writeFd) {
    SessionBridge self = SessionBridge_0();
    self.readFd = readFd;
    self.writeFd = writeFd;
    Harness_setEngine(&self.harness, EngineProvider_get(EngineProvider_shared(), "opencode"));
    return self;
}
bool SessionBridge_submit(SessionBridge *self, const char *prompt) {
    if (!self || !prompt || !*prompt || SessionBridge_readFd(self) < 0 || SessionBridge_writeFd(self) < 0 ||
        SessionBridge_failed(self) || SessionBridge_jobId(self) || SessionBridge_isAwaiting(self) ||
        strnlen(prompt, SESSION_REQUEST_CAP) >= SESSION_REQUEST_CAP / 2) {
        THROW("session turn rejected: empty, oversized, busy or unbound wire");
        return false;
    }
    for (const u8 *p = (const u8*) prompt; *p; ++p)
        if ((*p < ' ' && *p != '\n' && *p != '\t') || *p > '~') {
            THROW("session prompt must use printable ASCII, newline or tab");
            return false;
        }
    HarnessDriverTable table = {spawn, pollJob, cancel};
    Harness *harness = SessionBridge_harness(self);
    Harness_setDriver(harness, self, table);
    bool ok = Harness_run(harness, prompt, strlen(prompt), 0, SessionBridge_jobIdPtr(self));
    if (!ok)
        THROW("session request exceeds wire capacity");
    return ok;
}
i32 SessionBridge_poll(SessionBridge *self) {
    u32 jobId = self ? SessionBridge_jobId(self) : 0;
    if (!self || !jobId)
        return 0;
    HarnessStatus status = Harness_poll(SessionBridge_harness(self), jobId);
    if (status == HARNESS_STATUS_RUNNING)
        return 0;
    SessionBridge_setJobId(self, 0);
    SessionBridge_setAwaiting(self, false);
    return status == HARNESS_STATUS_DONE ? 1 : -1;
}
fn SessionBridge_free(SessionBridge *self) {
    if (!self)
        return;
    u32 jobId = SessionBridge_jobId(self);
    if (jobId)
        Harness_cancel(SessionBridge_harness(self), jobId);
    SessionBridge_setJobId(self, 0);
    SessionBridge_setAwaiting(self, false);
}
;;SETTER
fn SessionBridge_setOwo(SessionBridge *self, bool enabled) {
    if (self)
        (*self).owo = enabled;
}
;;GETTER
bool SessionBridge_isOwo(const SessionBridge *self) { return self && (*self).owo; }
bool SessionBridge_isBusy(const SessionBridge *self) { return self && SessionBridge_jobId(self); }
;;GETTER
bool SessionBridge_isAwaiting(const SessionBridge *self) { return self && (*self).awaiting; }
;;GETTER
const char *SessionBridge_getApprovalLabel(const SessionBridge *self) { return self ? (*self).approvalLabel : ""; }
;;GETTER
u32 SessionBridge_getApprovalSerial(const SessionBridge *self) { return self ? (*self).approvalSerial : 0; }
const char *SessionBridge_takeNote(SessionBridge *self) {
    if (self == nullptr || !SessionBridge_notePending(self))
        return nullptr;
    SessionBridge_setNotePending(self, false);
    return SessionBridge_note(self);
}
bool SessionBridge_decide(SessionBridge *self, const char *decision) {
    if (!self || !decision || !SessionBridge_jobId(self) || SessionBridge_failed(self) || !SessionBridge_isAwaiting(self) ||
        SessionBridge_requestOffset(self) != SessionBridge_requestLength(self)) {
        THROW("session decision rejected: idle, not awaiting, unflushed or null");
        return false;
    }
    if (strcmp(decision, "once") != 0 && strcmp(decision, "always") != 0 && strcmp(decision, "reject") != 0) {
        THROW("session decision must be once, always or reject");
        return false;
    }
    if (!queueText(self, "decision", decision)) {
        THROW("session decision exceeds wire capacity");
        return false;
    }
    strcpy(SessionBridge_decision(self), decision);
    SessionBridge_setAwaiting(self, false);
    u64 started = nowMs();
    SessionBridge_setStartedMs(self, started);
    return started != 0;
}
bool SessionBridge_setTimeout(SessionBridge *self, u64 timeoutMs) {
    if (!self || !timeoutMs || timeoutMs > SESSION_TIMEOUT_MS_MAX || SessionBridge_jobId(self)) {
        THROW("session timeout rejected: null, busy or out of safety range");
        return false;
    }
    Harness_setTimeout(SessionBridge_harness(self), timeoutMs);
    return true;
}
u64 SessionBridge_getTimeout(const SessionBridge *self) {
    return self ? Harness_getTimeout(SessionBridge_harnessConst(self)) : 0;
}
;;GETTER
const char *SessionBridge_getText(const SessionBridge *self) { return self ? (*self).text : ""; }
bool SessionBridge_toString(const SessionBridge *self, char *dest, bytes cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || !cap)
        return false;
    i32 n = snprintf(dest, cap, "%s", !self ? "nullptr" : SessionBridge_isAwaiting(self) ? "OpenCode approval pending"
                     : SessionBridge_isBusy(self) ? "OpenCode pending" : "OpenCode idle");
    bool cut = n < 0 || (bytes) n >= cap;
    if (outTruncated)
        *outTruncated = cut;
    return !cut;
}
bool SessionBridge_toStringStruct(const SessionBridge *self, char *dest, bytes cap, bool *outTruncated) {
    if (!self)
        return SessionBridge_toString(self, dest, cap, outTruncated);
    if (outTruncated)
        *outTruncated = false;
    if (!dest || !cap)
        return false;
    i32 n = snprintf(dest, cap, "{harness:borrowed-driver,readFd:%d,writeFd:%d,owo:%d,jobId:%u,startedMs:%llu,request:redacted,requestLength:%zu,requestOffset:%zu,response:redacted,responseLength:%zu,text:redacted,failed:%d,awaiting:%d,approvalSerial:%u,approvalId:redacted,approvalLabel:redacted,note:redacted,notePending:%d,decision:redacted}",
                     SessionBridge_readFd(self), SessionBridge_writeFd(self), SessionBridge_isOwo(self), SessionBridge_jobId(self),
                     (u64) SessionBridge_startedMs(self), SessionBridge_requestLength(self),
                     SessionBridge_requestOffset(self), SessionBridge_responseLength(self), SessionBridge_failed(self),
                     SessionBridge_isAwaiting(self), SessionBridge_getApprovalSerial(self), SessionBridge_notePending(self));
    bool cut = n < 0 || (bytes) n >= cap;
    if (outTruncated)
        *outTruncated = cut;
    return !cut;
}
