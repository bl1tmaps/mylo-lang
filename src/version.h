#ifndef MYLO_VERSION_H
#define MYLO_VERSION_H

// The version of the language, its tools and Mylo IDE. Release builds may pass
// -DMYLO_VERSION_SUFFIX="..." (for example a commit hash) to tell builds apart.
#define MYLO_VERSION "0.5.0"

#ifndef MYLO_VERSION_SUFFIX
#define MYLO_VERSION_SUFFIX ""
#endif

#define MYLO_VERSION_FULL MYLO_VERSION MYLO_VERSION_SUFFIX

#endif
