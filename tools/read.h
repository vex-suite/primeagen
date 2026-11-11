#ifndef PRIMEAGEN_TOOLS_READ_H
#define PRIMEAGEN_TOOLS_READ_H
#include "tools/fs.h"
bool ToolRead_execute(int root, const char *path, bytes cap, char **dest, bytes *outLength);
#endif
