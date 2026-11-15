#ifndef PRIMEAGEN_TOOLS_LIST_H
#define PRIMEAGEN_TOOLS_LIST_H

#include "vex.h"
#include "tools/fs.h"
bool ToolList_execute(i32 root, const char *path, bytes cap, char **dest, bytes *outLength);
#endif
