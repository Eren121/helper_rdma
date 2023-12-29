#pragma once

#include <errno.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>

#ifndef HENSURE
#define HENSURE(x) if(!(x)) \
    { const int line = __LINE__; const char *file = __FILE__; \
      fprintf(stderr, "'" #x "' failed [%s:%d].\n", file, line); exit(EXIT_FAILURE); }
#endif

/**
 * If `x` is false, then exit the program with EXIT_FAILURE and print the errno string.
 */
#ifndef HENSURE_ERRNO
#define HENSURE_ERRNO(x) do { if(!(x)) \
    { const int line = __LINE__; const char *file = __FILE__; \
      fprintf(stderr, "'" #x "' failed. errno=\"%s\" [%s:%d]\n", strerror(errno), file, line); exit(EXIT_FAILURE); } \
    } while(0)
#endif

#ifndef FATAL_ERROR
#define FATAL_ERROR(...) fprintf(stderr, "%s:%d: ", __FILE__, __LINE__); helper_rdma::fatal_error(__VA_ARGS__)
#endif

namespace helper_rdma
{

/**
 * Exit the program with the error code EXIT_FAILURE.
 * Print the `info` message and the errno error string.
 */
static inline
void fatal_errno(const char* info)
{
    fprintf(stderr, "%s:\n%s\n", info, strerror(errno));
    exit(EXIT_FAILURE);
}

static inline
void fatal_error(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    puts("");


    //exit(EXIT_FAILURE);

    // Force segfault
    *reinterpret_cast<char*>(0) = 0;
}

}