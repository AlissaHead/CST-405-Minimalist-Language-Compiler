/* =========================================================================
 * CST-405  ·  Project 2  ·  Minimalist Language Compiler
 * FILE: trace.h   —   Shared tracing helper
 * -------------------------------------------------------------------------
 * THE PIPELINE, AND WHERE THIS FILE SITS IN IT
 *   scanner -> parser -> ast -> semantic -> tac -> codegen
 *
 * CREDITS
 *   From the CST-405 course starter code.
 * ========================================================================= */

#ifndef TRACE_H
#define TRACE_H
#include <stdio.h>
#include <stdarg.h>

/* ----------------------------------------------------------------------------
 * TRACING
 * The compiler narrates what it is doing, because watching a phase run is the
 * fastest way to understand it.  Once the narration stops being useful, pass
 * -q on the command line and every trace() call goes quiet.  Real error
 * messages use fprintf(stderr, ...) and are never suppressed.
 * --------------------------------------------------------------------------*/
extern int quiet;

static inline void trace(const char* fmt, ...) {
    if (quiet) return;
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

#endif
