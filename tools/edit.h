#ifndef PRIMEAGEN_TOOLS_EDIT_H
#define PRIMEAGEN_TOOLS_EDIT_H

#include "vex.h"
#include "tools/fs.h"
bool ToolEdit_execute(int root, const char *path, const char *oldText, const char *newText, bytes cap);
#endif
