/* =========================================================================
 * Unit tests for the TAC optimizer (tac.c, Phase 5)        `make unittest`
 * -------------------------------------------------------------------------
 * The starter language has no if/while, so no .cm program can produce a
 * jump — yet the IR already defines GOTO / IF_FALSE / IF_TRUE / LABEL, and
 * the flow optimizations (branch simplification, jump threading, jump-to-
 * next removal, unreachable code removal) must be correct before a later
 * topic starts emitting them.  These tests build TAC lists by hand, run the
 * real optimizeTAC(), and compare the result line by line.
 *
 * They also pin down the value optimizations and the safety rules: stores
 * to globals are kept, division by zero is not folded, and a cycle of jumps
 * does not hang the optimizer.
 * ========================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../tac.h"

int quiet = 1;            /* trace.h: silence the optimizer's narration */

static int failures = 0, cases = 0;

/* Shorthand for appending one instruction to the unoptimized list. */
static void E(TACOp op, char* a1, char* a2, char* res) { appendTAC(createTAC(op, a1, a2, res)); }

/* Optimize whatever has been appended since initTAC() and compare the
 * formatted listing with `expect` (NULL-terminated). */
static void check(const char* name, const char* expect[]) {
    optimizeTAC();
    TACList* l = getOptimizedTAC();

    char got[64][128];
    int g = 0;
    for (TACInstr* c = l->head; c && g < 64; c = c->next)
        formatTAC(c, got[g++], sizeof got[0]);

    int e = 0, ok = 1;
    while (expect[e]) e++;
    if (e != g) ok = 0;
    for (int i = 0; ok && i < e; i++)
        if (strcmp(expect[i], got[i]) != 0) ok = 0;

    cases++;
    if (ok) { printf("  PASS  %s\n", name); return; }
    failures++;
    printf("  FAIL  %s\n        expected:\n", name);
    for (int i = 0; i < e; i++) printf("          %s\n", expect[i]);
    printf("        got:\n");
    for (int i = 0; i < g; i++) printf("          %s\n", got[i]);
}

#define LINES(...) (const char*[]){ __VA_ARGS__, NULL }

int main(void) {
    printf("TAC optimizer\n");

    /* ---------------- value optimizations ------------------------------ */
    initTAC();
    E(TAC_FUNC_BEGIN, NULL, NULL, "main");
    E(TAC_DECL, "int", NULL, "x");
    E(TAC_MUL, "2", "3", "t0");
    E(TAC_ASSIGN, "t0", NULL, "x");
    E(TAC_ADD, "x", "4", "t1");
    E(TAC_PRINT, "t1", NULL, NULL);
    E(TAC_RETURN, "0", NULL, NULL);
    E(TAC_FUNC_END, NULL, NULL, "main");
    check("folding + propagation + dead code + dead store",
          LINES("FUNC_BEGIN main", "DECL int x", "PRINT 10", "RETURN 0", "FUNC_END main"));

    initTAC();
    E(TAC_FUNC_BEGIN, NULL, NULL, "f");
    E(TAC_PARAM, "int", NULL, "p");          /* p is unknown at compile time */
    E(TAC_ADD, "p", "0", "t0");
    E(TAC_MUL, "t0", "1", "t1");
    E(TAC_MUL, "0", "t1", "t2");
    E(TAC_ADD, "t1", "t2", "t3");
    E(TAC_PRINT, "t3", NULL, NULL);
    E(TAC_RETURN, "0", NULL, NULL);
    E(TAC_FUNC_END, NULL, NULL, "f");
    check("algebraic identities on an unknown value (p+0, p*1, 0*p)",
          LINES("FUNC_BEGIN f", "PARAM int p", "PRINT p", "RETURN 0", "FUNC_END f"));

    /* ---------------- flow optimizations ------------------------------- */
    initTAC();
    E(TAC_FUNC_BEGIN, NULL, NULL, "main");
    E(TAC_IF_FALSE, "0", "L1", NULL);        /* condition false: always jumps */
    E(TAC_PRINT, "1", NULL, NULL);
    E(TAC_LABEL, NULL, NULL, "L1");
    E(TAC_PRINT, "2", NULL, NULL);
    E(TAC_RETURN, "0", NULL, NULL);
    E(TAC_FUNC_END, NULL, NULL, "main");
    check("branch always taken -> GOTO; skipped code unreachable; GOTO to next removed",
          LINES("FUNC_BEGIN main", "L1:", "PRINT 2", "RETURN 0", "FUNC_END main"));

    initTAC();
    E(TAC_FUNC_BEGIN, NULL, NULL, "main");
    E(TAC_IF_TRUE, "0", "L1", NULL);         /* condition false: never jumps */
    E(TAC_PRINT, "1", NULL, NULL);
    E(TAC_LABEL, NULL, NULL, "L1");
    E(TAC_PRINT, "2", NULL, NULL);
    E(TAC_RETURN, "0", NULL, NULL);
    E(TAC_FUNC_END, NULL, NULL, "main");
    check("branch never taken -> removed",
          LINES("FUNC_BEGIN main", "PRINT 1", "L1:", "PRINT 2", "RETURN 0", "FUNC_END main"));

    initTAC();
    E(TAC_FUNC_BEGIN, NULL, NULL, "main");
    E(TAC_DECL, "int", NULL, "c");
    E(TAC_ASSIGN, "0", NULL, "c");
    E(TAC_IF_FALSE, "c", "L1", NULL);        /* c is known to be 0 here */
    E(TAC_PRINT, "1", NULL, NULL);
    E(TAC_LABEL, NULL, NULL, "L1");
    E(TAC_PRINT, "2", NULL, NULL);
    E(TAC_RETURN, "0", NULL, NULL);
    E(TAC_FUNC_END, NULL, NULL, "main");
    check("constant propagated into a condition, then the branch simplified",
          /* c = 0 is kept: the function has a label, so DSE does not apply */
          LINES("FUNC_BEGIN main", "DECL int c", "c = 0", "L1:", "PRINT 2",
                "RETURN 0", "FUNC_END main"));

    initTAC();
    E(TAC_FUNC_BEGIN, NULL, NULL, "f");
    E(TAC_PARAM, "int", NULL, "p");
    E(TAC_IF_TRUE, "p", "L1", NULL);
    E(TAC_PRINT, "1", NULL, NULL);
    E(TAC_RETURN, "0", NULL, NULL);
    E(TAC_LABEL, NULL, NULL, "L1");
    E(TAC_GOTO, "L2", NULL, NULL);           /* a jump to a jump */
    E(TAC_PRINT, "99", NULL, NULL);          /* unreachable */
    E(TAC_LABEL, NULL, NULL, "L2");
    E(TAC_PRINT, "2", NULL, NULL);
    E(TAC_RETURN, "0", NULL, NULL);
    E(TAC_FUNC_END, NULL, NULL, "f");
    check("jump threading: IF_TRUE p GOTO L1 / L1: GOTO L2  ->  IF_TRUE p GOTO L2",
          LINES("FUNC_BEGIN f", "PARAM int p", "IF_TRUE p GOTO L2", "PRINT 1",
                "RETURN 0", "L1:", "L2:", "PRINT 2", "RETURN 0", "FUNC_END f"));

    initTAC();
    E(TAC_FUNC_BEGIN, NULL, NULL, "main");
    E(TAC_PRINT, "1", NULL, NULL);
    E(TAC_RETURN, "0", NULL, NULL);
    E(TAC_PRINT, "2", NULL, NULL);           /* after RETURN: unreachable */
    E(TAC_DECL, "int", NULL, "z");           /* ...but a declaration is kept */
    E(TAC_FUNC_END, NULL, NULL, "main");
    check("code after RETURN removed, declarations kept",
          LINES("FUNC_BEGIN main", "PRINT 1", "RETURN 0", "DECL int z", "FUNC_END main"));

    /* ---------------- safety rules ------------------------------------- */
    initTAC();
    E(TAC_DECL, "int", NULL, "g");           /* file scope: a global */
    E(TAC_FUNC_BEGIN, NULL, NULL, "main");
    E(TAC_ASSIGN, "5", NULL, "g");
    E(TAC_RETURN, "0", NULL, NULL);
    E(TAC_FUNC_END, NULL, NULL, "main");
    check("a store to a global is never removed as dead",
          LINES("DECL int g", "FUNC_BEGIN main", "g = 5", "RETURN 0", "FUNC_END main"));

    initTAC();
    E(TAC_FUNC_BEGIN, NULL, NULL, "main");
    E(TAC_DIV, "1", "0", "t0");
    E(TAC_PRINT, "t0", NULL, NULL);
    E(TAC_RETURN, "0", NULL, NULL);
    E(TAC_FUNC_END, NULL, NULL, "main");
    check("division by zero is not folded (it must trap at run time)",
          LINES("FUNC_BEGIN main", "t0 = 1 / 0", "PRINT t0", "RETURN 0", "FUNC_END main"));

    initTAC();
    E(TAC_FUNC_BEGIN, NULL, NULL, "main");
    E(TAC_LABEL, NULL, NULL, "L1");
    E(TAC_GOTO, "L2", NULL, NULL);
    E(TAC_LABEL, NULL, NULL, "L2");
    E(TAC_GOTO, "L1", NULL, NULL);
    E(TAC_FUNC_END, NULL, NULL, "main");
    check("a cycle of jumps terminates and is still an infinite loop",
          LINES("FUNC_BEGIN main", "L1:", "L2:", "GOTO L1", "FUNC_END main"));

    printf("\n  %d case(s), %d failure(s)\n", cases, failures);
    return failures ? 1 : 0;
}
