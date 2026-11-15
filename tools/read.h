#ifndef PRIMEAGEN_TOOLS_READ_H
#define PRIMEAGEN_TOOLS_READ_H

#include "vex.h"
#include "tools/fs.h"
bool ToolRead_execute(i32 root, const char *path, bytes cap, char **dest, bytes *outLength);
#endif
