/* Native shared seam owner: nullable outputs preserve state, stdin/read boundary,
 * direct public header clients and failure recovery. Real disposable owner paths
 * supplied by runner; no caller-address validation or racing filesystem promise. */

#include "vex.h"
#include "tools/fs.h"
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
i32 main(i32 argc, char **argv) {
    assert(argc == 2);
    i32 root = open(argv[1], O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(root >= 0);
    i32 parent = -1;
    char name[256] = "keep";
    assert(!ToolFs_parent(root, nullptr, &parent, name, sizeof(name)));
    assert(parent == -1 && strcmp(name, "keep") == 0);
    assert(!ToolFs_parent(root, "a", nullptr, name, sizeof(name)));
    assert(!ToolFs_parent(root, "a", &parent, nullptr, sizeof(name)));
    assert(!ToolFs_parent(root, "../a", &parent, name, sizeof(name)));
    assert(ToolFs_parent(root, "a", &parent, name, sizeof(name)));
    assert(strcmp(name, "a") == 0 && parent >= 0);
    close(parent);
    char *text = nullptr;
    bytes length = 99;
    assert(!ToolFs_read(root, "a", 16, nullptr, &length));
    assert(!ToolFs_read(root, "a", 16, &text, nullptr));
    assert(!ToolFs_read(root, "a", 0, &text, &length));
    assert(!ToolFs_read(root, "a", TOOL_BYTES_MAX + 1, &text, &length));
    assert(text == nullptr && length == 99);
    assert(ToolFs_publish(root, "a", "abc", 3, true));
    assert(!ToolFs_publish(root, "a", "wrong", 5, true));
    assert(!ToolFs_publish(root, "a", nullptr, 0, false));
    assert(ToolFs_read(root, "a", 3, &text, &length));
    assert(length == 3 && strcmp(text, "abc") == 0);
    free(text); text = nullptr; length = 99;
    assert(!ToolFs_read(root, "a", 2, &text, &length));
    assert(text == nullptr && length == 99);
    assert(ToolFs_publish(root, "a", "xyz", 3, false));
    assert(ToolFs_read(root, "a", 3, &text, &length));
    assert(strcmp(text, "xyz") == 0);
    free(text); text = nullptr;
    assert(!ToolFs_input(0, &text, &length));
    assert(ToolFs_input(3, &text, &length));
    assert(length == 3 && strcmp(text, "inp") == 0);
    free(text);
    close(root);
}
