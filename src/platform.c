#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#endif

#ifdef _WIN32
double monotonic_seconds(void) {
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
}

int cpu_count(void) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors > 0 ? (int)info.dwNumberOfProcessors : 1;
}

static int make_directory(const char *path) {
    return _mkdir(path);
}
#else
double monotonic_seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

int cpu_count(void) {
    const long count = sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? (int)count : 1;
}

static int make_directory(const char *path) {
    return mkdir(path, 0777);
}
#endif

static int is_directory(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && (info.st_mode & S_IFMT) == S_IFDIR;
}

int make_directories(const char *path) {
    const size_t length = strlen(path);
    if (length == 0) return 0;
    char *prefix = malloc(length + 1);
    if (!prefix) return -1;
    memcpy(prefix, path, length + 1);
    // On cree chaque niveau tour a tour : "a", puis "a/b", puis "a/b/c".
    for (size_t index = 1; index <= length; ++index) {
        const char c = prefix[index];
        if (c != '/' && c != '\\' && c != '\0') continue;
        if (prefix[index - 1] == ':') continue;  // "C:" n'est pas un dossier
        prefix[index] = '\0';
        if (make_directory(prefix) != 0 && errno != EEXIST) {
            free(prefix);
            return -1;
        }
        prefix[index] = c;
    }
    const int status = is_directory(path) ? 0 : -1;
    free(prefix);
    return status;
}
