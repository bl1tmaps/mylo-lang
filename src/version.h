#ifndef MYLO_VERSION_H
#define MYLO_VERSION_H

// The version of the language, its tools and Mylo IDE. Builds may pass
// -DMYLO_VERSION_SUFFIX="..." to add text after the number.
#define MYLO_VERSION "0.5.0"

#ifndef MYLO_VERSION_SUFFIX
#define MYLO_VERSION_SUFFIX ""
#endif

// CMake generates mylo_git_version.h on each build from `git describe`, giving the newest
// tag and commit, e.g. " (0.4.0-22-g793005a)". Builds without git leave it out.
#ifdef MYLO_HAVE_GIT_VERSION
#include "mylo_git_version.h"
#endif
#ifndef MYLO_GIT_SUFFIX
#define MYLO_GIT_SUFFIX ""
#endif

#define MYLO_VERSION_FULL MYLO_VERSION MYLO_VERSION_SUFFIX MYLO_GIT_SUFFIX

#endif
