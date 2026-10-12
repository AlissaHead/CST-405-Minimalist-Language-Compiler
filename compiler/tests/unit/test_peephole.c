/* =========================================================================
 * Unit tests for the peephole optimizer (peephole.c)      `make unittest`
 * -------------------------------------------------------------------------
 * The starter language folds almost every program down to constants, so a
 * real .cm file only ever gives the peephole pass one or two chances to act.
 * These tests feed it hand-written MIPS instead, one case per rule, plus the
 * cases where it must NOT change anything — those matter just as much,
 * because a peephole rule that fires where it should not silently breaks
 * correct programs.
 *
 * Each case lists the input lines and the lines expected back.  Comments are
 * ignored and whitespace is collapsed when comparing.
 * ========================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../../peephole.h"

static int failures = 0, cases = 0;

/* Strip the comment and collapse runs of whitespace, for comparison. */
static void normalize(const char* in, char* out, size_t n) {
    size_t k = 0;
    int space = 0;
    for (const char* p = in; *p && *p != '#' && k < n - 1; p++) {
        if (isspace((unsigned char)*p)) { space = 1; continue; }
        if (space && k > 0) out[k++] = ' ';
        space = 0;
        out[k++] = *p;
    }
    out[k] = '\0';
}

/* Run the optimizer on `in` and compare with `expect` (both NULL-terminated). */
static void check(const char* name, const char* in[], const char* expect[]) {
    char* lines[64];
    int n = 0;
    while (in[n]) { lines[n] = strdup(in[n]); n++; }

    PeepStats st;
    int m = peepholeOptimize(lines, n, &st);

    /* gather the non-blank results */
    char got[64][128];
    int g = 0;
    for (int i = 0; i < m; i++) {
        normalize(lines[i], got[g], sizeof got[g]);
        if (got[g][0]) g++;
        free(lines[i]);
    }

    int e = 0, ok = 1;
    while (expect[e]) e++;
    if (e != g) ok = 0;
    for (int i = 0; ok && i < e; i++) {
        char want[128];
        normalize(expect[i], want, sizeof want);
        if (strcmp(want, got[i]) != 0) ok = 0;
    }

    cases++;
    if (ok) { printf("  PASS  %s\n", name); return; }
    failures++;
    printf("  FAIL  %s\n        expected:\n", name);
    for (int i = 0; i < e; i++) printf("          %s\n", expect[i]);
    printf("        got:\n");
    for (int i = 0; i < g; i++) printf("          %s\n", got[i]);
}

#define LINES(...) (const char*[]){ __VA_ARGS__, NULL }
#define NONE       (const char*[]){ NULL }

int main(void) {
    printf("peephole optimizer\n");

    /* ---------------- rules that SHOULD fire ---------------------------- */
    check("self move is deleted",
          LINES("move $t1, $t1", "sw $t1, 0($sp)"),
          LINES("sw $t1, 0($sp)"));

    check("store then load of the same slot, same register: load deleted",
          LINES("sw $t0, 4($sp)", "lw $t0, 4($sp)", "sw $t0, 8($sp)"),
          LINES("sw $t0, 4($sp)", "sw $t0, 8($sp)"));

    check("store then load into another register: load becomes move",
          LINES("sw $t0, 4($sp)", "lw $t3, 4($sp)", "sw $t3, 8($sp)"),
          LINES("sw $t0, 4($sp)", "move $t3, $t0", "sw $t3, 8($sp)"));

    check("load then store back to the same slot: store deleted",
          LINES("lw $t2, g_x", "sw $t2, g_x", "sw $t2, g_y"),
          LINES("lw $t2, g_x", "sw $t2, g_y"));

    check("jump to the very next label is deleted",
          LINES("j L1", "L1:", "sw $t0, 0($sp)"),
          LINES("L1:", "sw $t0, 0($sp)"));

    check("jump to a jump is threaded to the final target",
          LINES("j L1", "sw $t0, 0($sp)", "L1:", "j L2", "L2:", "jr $ra"),
          LINES("j L2", "sw $t0, 0($sp)", "L1:", "L2:", "jr $ra"));

    check("$v0 overwritten before the exit syscall: first write is dead",
          LINES("li $v0, 0", "main__epilogue:", "lw $ra, 20($sp)",
                "addi $sp, $sp, 24", "li $v0, 10", "syscall"),
          LINES("main__epilogue:", "lw $ra, 20($sp)",
                "addi $sp, $sp, 24", "li $v0, 10", "syscall"));

    check("register overwritten before it is read: first write is dead",
          LINES("li $t0, 1", "li $t0, 2", "move $a0, $t0", "li $v0, 1", "syscall"),
          LINES("li $t0, 2", "move $a0, $t0", "li $v0, 1", "syscall"));

    check("$t register is dead after jr $ra (caller-saved)",
          LINES("li $t4, 9", "jr $ra"),
          LINES("jr $ra"));

    check("deleting one dead write exposes another (fixed point)",
          LINES("li $t0, 5", "move $t1, $t0", "li $t1, 7", "move $a0, $t1",
                "li $v0, 1", "syscall", "jr $ra"),
          LINES("li $t1, 7", "move $a0, $t1", "li $v0, 1", "syscall", "jr $ra"));

    /* ---------------- cases that must NOT change ------------------------ */
    check("$v0 is the return value: kept before jr $ra",
          LINES("li $v0, 5", "jr $ra"),
          LINES("li $v0, 5", "jr $ra"));

    check("a label between store and load breaks the window",
          LINES("sw $t0, 0($sp)", "L3:", "lw $t0, 0($sp)", "move $a0, $t0",
                "li $v0, 1", "syscall"),
          LINES("sw $t0, 0($sp)", "L3:", "lw $t0, 0($sp)", "move $a0, $t0",
                "li $v0, 1", "syscall"));

    check("a value read by a branch is live",
          LINES("li $t0, 1", "beq $t0, $zero, L1", "L1:", "jr $ra"),
          LINES("li $t0, 1", "beq $t0, $zero, L1", "L1:", "jr $ra"));

    check("scan stops at a jump: value kept (could be read at the target)",
          LINES("li $t0, 1", "j L5", "L5:", "move $a0, $t0", "li $v0, 1", "syscall"),
          LINES("li $t0, 1", "L5:", "move $a0, $t0", "li $v0, 1", "syscall"));

    check("div is never deleted (it can trap on a zero divisor)",
          LINES("div $t2, $t0, $t1", "li $t2, 0", "jr $ra"),
          LINES("div $t2, $t0, $t1", "jr $ra"));

    check("argument registers are read by syscall",
          LINES("la $a0, __nl", "li $v0, 4", "syscall"),
          LINES("la $a0, __nl", "li $v0, 4", "syscall"));

    check("stores are never deleted as dead",
          LINES("sw $t0, g_x", "li $v0, 10", "syscall"),
          LINES("sw $t0, g_x", "li $v0, 10", "syscall"));

    check("a cycle of jumps terminates and is still an infinite loop",
          LINES("L1:", "j L2", "L2:", "j L1"),
          LINES("L1:", "L2:", "j L1"));          /* only the jump-to-next goes */

    printf("\n  %d case(s), %d failure(s)\n", cases, failures);
    return failures ? 1 : 0;
}
