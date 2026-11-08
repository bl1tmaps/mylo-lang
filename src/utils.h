#ifndef MYLO_UTILS_H
#define MYLO_UTILS_H

// This turns on Terminal Colours
#define ENABLE_TERMINAL_COLOURS
#include <stdio.h>

#ifdef _WIN32
#include <stdlib.h>
#ifndef realpath
#define realpath(N, R) _fullpath((R), (N), 4096)
#endif
#endif

// Read file to char array
char *read_file(const char *fn);

// ANSI Color Codes
enum MyloColor {
  MyloFgRed = 31,
  MyloFgGreen = 32,
  MyloFgYellow = 33,
  MyloFgBlue = 34,
  MyloFgMagenta = 35,
  MyloFgCyan = 36,
  MyloFgWhite = 37,
  MyloFgDefault = 39,
  MyloBgColorRed = 41,
  MyloBgColorGreen = 42,
  MyloBgColorYellow = 43,
  MyloBgColorBlue = 44,
  MyloBgColorMagenta = 45,
  MyloBgColorCyan = 46,
  MyloBgColorWhite = 47,
  MyloBgColorDefault = 49
};

extern char mylo_exe_dir[1024]; // NEW: Expose the global variable to all files

// Original (Defaults to stdout)
void setTerminalColor(enum MyloColor fg, enum MyloColor bg);
void resetTerminal();

// New (Allows stderr)
void fsetTerminalColor(FILE* f, enum MyloColor fg, enum MyloColor bg);
void fresetTerminal(FILE* f);

// --- Path and Module Utilities ---
void get_executable_dir(char* out_path, int size, const char* argv0);
char* find_file_recursive(const char* base_path, const char* target_file);

#endif