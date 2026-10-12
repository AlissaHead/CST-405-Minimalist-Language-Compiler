/* =========================================================================
 * CST-405  ·  Project 2  ·  Minimalist Language Compiler
 * FILE: peephole.h   —   Phase 6b — peephole optimization of MIPS assembly
 * -------------------------------------------------------------------------
 * PIPELINE
 *   scanner -> parser -> ast -> semantic -> tac -> optimizer -> codegen
 *                                                                 |
 *                                                     peephole <--+  (this file)
 *
 * INPUT : the MIPS assembly codegen.c produced, one line per array element
 * OUTPUT: the same array, rewritten in place, with redundant instructions
 *         removed; the number of lines that remain is returned
 *
 * A peephole optimizer slides a small "window" over the final instructions
 * and replaces patterns it recognises with something shorter.  It knows
 * nothing about the source program — only about MIPS — which is why it can
 * clean up waste that the TAC optimizer cannot see: the register cache's
 * spill/reload traffic, moves a register onto itself, values loaded into
 * $v0 that are overwritten before anyone reads them, and so on.
 * ========================================================================= */
#ifndef PEEPHOLE_H
#define PEEPHOLE_H

/* What the peephole pass did, reported by main.c next to the TAC numbers. */
typedef struct {
    int linesBefore;     /* instruction lines before the pass                */
    int linesAfter;      /* ... and after                                    */
    int passes;          /* sweeps until nothing changed                     */
    int selfMove;        /* move $tX, $tX                       -> (deleted) */
    int storeLoad;       /* sw $tX, A ; lw $tY, A  -> sw $tX, A ; move $tY,$tX */
    int loadStore;       /* lw $tX, A ; sw $tX, A  -> lw $tX, A              */
    int deadWrite;       /* register written, then overwritten / never read  */
    int jumpToNext;      /* j L ; L:                            -> L:        */
    int jumpThread;      /* j L1 ... L1: j L2                   -> j L2      */
} PeepStats;

/* Optimize `n` lines of assembly held in `lines` (each one malloc'd, no
 * trailing newline).  Removed lines are freed; replaced lines are freed and
 * re-allocated.  Returns the new number of lines; `st` receives the counts. */
int peepholeOptimize(char** lines, int n, PeepStats* st);

/* Count real instructions (not labels, directives, comments or blanks). */
int countInstructions(char** lines, int n);

#endif
