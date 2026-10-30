#define _CRT_SECURE_NO_WARNINGS
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

char *read_file(const char *fn) {
  FILE *f = fopen(fn, "rb");
  if (!f) return NULL;

  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *b = (char *)malloc(len + 1);
  if (len > 0) fread(b, 1, len, f);
  b[len] = '\0';
  fclose(f);
  return b;
}

// Wrapper for stdout
void setTerminalColor(enum MyloColor fg, enum MyloColor bg) {
  fsetTerminalColor(stdout, fg, bg);
}

// Wrapper for stdout
void resetTerminal() {
  fresetTerminal(stdout);
}

// Actual Implementation
void fsetTerminalColor(FILE* f, enum MyloColor fg, enum MyloColor bg) {
#ifdef ENABLE_TERMINAL_COLOURS
  fprintf(f, "\033[%d;%dm", (int)fg, (int)bg);
#endif
}

void fresetTerminal(FILE* f) {
#ifdef ENABLE_TERMINAL_COLOURS
  fprintf(f, "\033[0m");
#endif
}

void get_executable_dir(char* out_path, int size, const char* argv0) {
    strncpy(out_path, argv0, size);
    char* last_slash = strrchr(out_path, '/');
    char* last_backslash = strrchr(out_path, '\\');
    char* cut = (last_slash > last_backslash) ? last_slash : last_backslash;

    if (cut) {
        *cut = '\0';
    } else {
        strcpy(out_path, ".");
    }
}

char* find_file_recursive(const char* base_path, const char* target_file) {
    static char result_path[2048]; // Static buffer avoids deep malloc tracking during recursion

#ifdef _WIN32
    char search_path[MAX_PATH];
    snprintf(search_path, MAX_PATH, "%s\\*", base_path);
    WIN32_FIND_DATA fFD;
    HANDLE hFind = FindFirstFileA(search_path, &fFD);
    if (hFind == INVALID_HANDLE_VALUE) return NULL;

    do {
        if (strcmp(fFD.cFileName, ".") == 0 || strcmp(fFD.cFileName, "..") == 0) continue;

        char current_path[MAX_PATH];
        snprintf(current_path, MAX_PATH, "%s\\%s", base_path, fFD.cFileName);

        if (fFD.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            char* found = find_file_recursive(current_path, target_file);
            if (found) { FindClose(hFind); return found; }
        } else {
            if (strcmp(fFD.cFileName, target_file) == 0) {
                strncpy(result_path, current_path, 2048);
                FindClose(hFind);
                return result_path;
            }
        }
    } while (FindNextFileA(hFind, &fFD));
    FindClose(hFind);
#else
    DIR* d = opendir(base_path);
    if (!d) return NULL;

    struct dirent* dir;
    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;

        char current_path[2048];
        snprintf(current_path, 2048, "%s/%s", base_path, dir->d_name);

        struct stat st;
        if (stat(current_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                char* found = find_file_recursive(current_path, target_file);
                if (found) { closedir(d); return found; }
            } else {
                if (strcmp(dir->d_name, target_file) == 0) {
                    strncpy(result_path, current_path, 2048);
                    closedir(d);
                    return result_path;
                }
            }
        }
    }
    closedir(d);
#endif
    return NULL;
}