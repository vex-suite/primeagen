#ifndef PRIMEAGEN_TOOLS_WRITE_H
#define PRIMEAGEN_TOOLS_WRITE_H

#include "vex.h"
#include "tools/fs.h"
bool ToolWrite_execute(int root, const char *path, const char *text, bytes length);
#endif
