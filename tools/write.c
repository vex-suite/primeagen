#include "vex.h"
#include "tools/write.h"
;;DEFINITION
/* Replace one existing file only after complete new bytes have staged. The host
 * approves this exact action; the operation is not crash-durable or a sandbox. */
;;OVERVIEW
/* ============================================================================
 * MODULE: write tool (tools/write.c)
 * OWNED STATE: none; root/text borrowed through the synchronous operation.
 * FUNCTION REGISTRY: bool ToolWrite_execute(i32 root,const char *path,
 *                                         const char *text,bytes length)
 * CONTRACT: existing regular/single-link target, canonical no-symlink path.
 * Common ToolFs stages/replaces; failure preserves target before publication.
 * Successful replacement changes inode identity, preserves permission bits,
 * not timestamps/xattrs/ACLs/ownership metadata. No implicit parent creation.
 * ============================================================================ */
bool ToolWrite_execute(i32 root, const char *path, const char *text, bytes length) {
    return ToolFs_publish(root, path, text, length, false);
}
