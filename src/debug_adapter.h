#ifndef MYLO_DEBUG_ADAPTER_H
#define MYLO_DEBUG_ADAPTER_H

#include "vm.h"

// Initialize and start the DAP loop
// The loop handles stdin/stdout communication with VS Code
void start_debug_adapter(VM* vm, const char* filename, const char* source_content);

#endif
