#include "vex.h"
#include "cli/update.h"
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

;;DEFINITION
/* Local developer update admission reads a trusted builder's SHA256 descriptor,
 * then delegates the complete candidate transaction to Hotcwap. It never builds,
 * downloads, grants permissions or contacts a provider. Host has paused actions
 * before entry; success/failure resolves synchronously before it resumes. */
;;OVERVIEW
/* ============================================================================
 * MODULE: Primeagen local update adapter (cli/update.c)
 * OWNED STATE: none; stack descriptor and temporary read descriptor only.
 * FUNCTION REGISTRY:
 *   bool Primeagen_update(HotModule *hot, const char *payload,
 *                         const char *checksum, u32 *loaded)
 *       Parse exact SHA256 reference, call Hot_updateCold for cli_view.dylib.
 * CONTRACT: cold owner-thread; caller excludes module users. Local build inputs
 * and parent directories trusted/stable. No auth/signature or hostile-tree claim.
 * Refuse symlink/nonregular/malformed checksum before module admission; running
 * code/model remain unchanged on rejection. Output count zero on failure.
 * ============================================================================ */
bool Primeagen_update(HotModule *hot, const char *payload, const char *checksum, u32 *loaded) {
    if (loaded != nullptr)
        *loaded = 0;
    if (hot == nullptr || payload == nullptr || checksum == nullptr)
        return false;
    i32 fd = open(checksum, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    struct stat info;
    u8 text[66];
    bytes used = 0;
    bool ok = fd >= 0 && fstat(fd, &info) == 0 && S_ISREG(info.st_mode);
    while (ok && used < sizeof(text)) {
        ssize_t n = read(fd, text + used, sizeof(text) - used);
        if (n < 0)
            ok = false;
        else if (n == 0)
            break;
        else
            used += (bytes) n;
    }
    if (fd >= 0 && close(fd) != 0)
        ok = false;
    ok = ok && (used == 64 || (used == 65 && text[64] == '\n'));
    HotSectionDigest digest = {.filename = "cli_view.dylib"};
    for (bytes i = 0; ok && i < 64; ++i) {
        u8 c = text[i];
        i32 value = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (value < 0)
            ok = false;
        else if (i % 2 == 0)
            digest.sha256[i / 2] = (u8) (value * 16);
        else
            digest.sha256[i / 2] |= (u8) value;
    }
    if (!ok) {
        THROW("primeagen update: invalid/missing local build checksum");
        return false;
    }
    return Hot_updateCold(hot, payload, &digest, 1, loaded) == HOT_OK;
}
