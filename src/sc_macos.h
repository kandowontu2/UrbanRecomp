#pragma once
#include <stdbool.h>
/* Finder launches have no writable working directory. Bundled Mac builds use
 * Application Support for settings/saves and Resources for shipped assets. */
bool ScMacPreparePaths(int argc,char **argv);
