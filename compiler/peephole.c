/* =========================================================================
 * CST-405  ·  Project 2  ·  Minimalist Language Compiler
 * FILE: peephole.c   —   Phase 6b — peephole optimization of MIPS assembly
 * -------------------------------------------------------------------------
 * codegen.c writes the assembly for a whole program into a temporary file,
 * splits it into lines and hands the array to peepholeOptimize().  The pass
 * sweeps the array repeatedly, applying the rules below, until a sweep
 * changes nothing (a fixed point — the same structure as the TAC optimizer).
 *
 *   RULE                 PATTERN                         BECOMES
 *   self move            move $t1, $t1                   (deleted)
 *   store then load      sw $t1, A   / lw $t2, A         sw $t1, A / move $t2, $t1
 *                        sw $t1, A   / lw $t1, A         sw $t1, A
 *   load then store      lw $t1, A   / sw $t1, A         lw $t1, A
 *   jump to next         j L         / L:                L:
 *   jump threading       j L1  ...  L1: j L2             j L2
 *   dead write           li $v0, 0   ... li $v0, 10      li $v0, 10
 *                        (a register written and then overwritten, or never
 *                         read again before the function returns or exits)
 *
 * WHY THESE ARE SAFE
 *   The window rules (first four) only look at ADJACENT instructions, with
 *   nothing but comments between them — a label between two instructions
 *   means control can arrive from elsewhere, so the pattern does not apply.
 *
 *   The dead-write rule scans FORWARD from the write along the only path
 *   execution can take (fall-through), and gives up — keeping the
 *   instruction — the moment it meets anything it cannot reason about: a
 *   branch, a jump, a call, a directive, or an instruction it does not
 *   recognise.  It concludes "dead" only when it sees the register
 *   overwritten before being read, or reaches `jr $ra` / the exit syscall
 *   (the $t and $a registers are caller-saved, so nobody reads them after a
 *   return; only $v0, the return value, survives `jr`).
 *
 *   Instructions with side effects are never deleted: stores, syscalls,
 *   jumps, and `div` (SPIM's div traps on a zero divisor; deleting it would
 *   change the program's behaviour).
 * ========================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "peephole.h"

/* ---------------------------------------------------------------------------
 * PARSING ONE LINE OF ASSEMBLY
 * -------------------------------------------------------------------------*/
typedef enum { K_BLANK, K_LABEL, K_DIRECTIVE, K_INSTR } LineKind;

typedef struct {
    LineKind kind;
    char op[16];          /* mnemonic, or the label name for K_LABEL        */
    char arg[3][64];      /* operands, trimmed                              */
    int  nargs;
} Ins;

/* Copy `s` without its trailing "# comment" (ignoring '#' inside quotes). */
static void stripComment(const char* s, char* out, size_t n) {
    int inQuote = 0;
    size_t i = 0;
    for (; s[i] && i < n - 1; i++) {
        if (s[i] == '"') inQuote = !inQuote;
        if (s[i] == '#' && !inQuote) break;
        out[i] = s[i];
    }
    out[i] = '\0';
}

static void trim(char* s) {
    char* p = s;
    while (isspace((unsigned char)*p)) p++;
    memmove(s, p, strlen(p) + 1);
    size_t len = strlen(s);
    while (len && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
}

static void parseLine(const char* line, Ins* ins) {
    char buf[256];
    memset(ins, 0, sizeof *ins);
    stripComment(line, buf, sizeof buf);
    trim(buf);

    if (!buf[0])        { ins->kind = K_BLANK;     return; }
    if (buf[0] == '.')  { ins->kind = K_DIRECTIVE; return; }

    size_t len = strlen(buf);
    char* colon = strchr(buf, ':');
    if (colon) {
        /* "name:" alone is a label; "name: .word 0" is data — opaque to us */
        if (colon == buf + len - 1) {
            ins->kind = K_LABEL;
            *colon = '\0';
            snprintf(ins->op, sizeof ins->op, "%.15s", buf);
            /* labels can be longer than op[]; keep the full name in arg[0] */
            snprintf(ins->arg[0], sizeof ins->arg[0], "%.63s", buf);
        } else {
            ins->kind = K_DIRECTIVE;
        }
        return;
    }

    ins->kind = K_INSTR;
    char* p = buf;
    size_t k = 0;
    while (*p && !isspace((unsigned char)*p) && k < sizeof ins->op - 1) ins->op[k++] = *p++;
    ins->op[k] = '\0';

    /* operands: comma-separated */
    while (*p && ins->nargs < 3) {
        while (isspace((unsigned char)*p) || *p == ',') p++;
        if (!*p) break;
        k = 0;
        while (*p && *p != ',' && k < sizeof ins->arg[0] - 1) ins->arg[ins->nargs][k++] = *p++;
        ins->arg[ins->nargs][k] = '\0';
        trim(ins->arg[ins->nargs]);
        ins->nargs++;
    }
}

/* ---------------------------------------------------------------------------
 * WHAT AN INSTRUCTION READS AND WRITES
 * -------------------------------------------------------------------------*/
static int opIn(const char* op, const char* const* set) {
    for (int i = 0; set[i]; i++) if (strcmp(op, set[i]) == 0) return 1;
    return 0;
}

static const char* const THREE_REG[] = { "add", "addu", "sub", "subu", "mul", "div",
    "and", "or", "xor", "nor", "slt", "sgt", "sle", "sge", "seq", "sne", NULL };
static const char* const REG_IMM[]   = { "addi", "addiu", "andi", "ori", "xori", "slti", NULL };
static const char* const BRANCHES[]  = { "b", "beq", "bne", "beqz", "bnez", "blt", "bgt",
    "ble", "bge", "bltz", "bgtz", "blez", "bgez", NULL };

/* Does operand `a` mention register `r`, either directly or as the base of
 * a memory operand like 8($sp)? */
static int mentions(const char* a, const char* r) {
    if (strcmp(a, r) == 0) return 1;
    const char* open = strchr(a, '(');
    if (open) {
        char base[32];
        size_t k = 0;
        for (const char* q = open + 1; *q && *q != ')' && k < sizeof base - 1; q++) base[k++] = *q;
        base[k] = '\0';
        return strcmp(base, r) == 0;
    }
    return 0;
}

/* Is the instruction one we model precisely?  Anything else is treated as
 * "might read every register", which stops every optimization around it. */
static int isKnown(const Ins* in) {
    const char* op = in->op;
    return !strcmp(op, "li") || !strcmp(op, "la") || !strcmp(op, "move") ||
           !strcmp(op, "lw") || !strcmp(op, "sw") || !strcmp(op, "syscall") ||
           !strcmp(op, "jr") || !strcmp(op, "j")  || !strcmp(op, "jal") ||
           opIn(op, THREE_REG) || opIn(op, REG_IMM) || opIn(op, BRANCHES);
}

/* The register this instruction writes, or NULL. */
static const char* writtenReg(const Ins* in) {
    const char* op = in->op;
    if (in->nargs < 1) return NULL;
    if (!strcmp(op, "li") || !strcmp(op, "la") || !strcmp(op, "move") ||
        !strcmp(op, "lw") || opIn(op, THREE_REG) || opIn(op, REG_IMM))
        return in->arg[0];
    return NULL;
}

static int readsReg(const Ins* in, const char* r) {
    const char* op = in->op;
    if (!isKnown(in)) return 1;                          /* be conservative */
    if (!strcmp(op, "li")) return 0;
    if (!strcmp(op, "la") || !strcmp(op, "lw") || !strcmp(op, "move"))
        return in->nargs > 1 && mentions(in->arg[1], r);
    if (!strcmp(op, "sw"))
        return mentions(in->arg[0], r) || (in->nargs > 1 && mentions(in->arg[1], r));
    if (opIn(op, THREE_REG) || opIn(op, REG_IMM)) {
        for (int i = 1; i < in->nargs; i++) if (mentions(in->arg[i], r)) return 1;
        return 0;
    }
    if (!strcmp(op, "syscall"))                          /* service in $v0, args in $a0-$a3 */
        return !strcmp(r, "$v0") || !strncmp(r, "$a", 2);
    if (!strcmp(op, "jal")) return !strncmp(r, "$a", 2); /* arguments to the callee */
    for (int i = 0; i < in->nargs; i++) if (mentions(in->arg[i], r)) return 1;
    return 0;                                            /* jr, j, branches */
}

static int isControl(const Ins* in) {
    return !strcmp(in->op, "j") || !strcmp(in->op, "jr") || !strcmp(in->op, "jal") ||
           opIn(in->op, BRANCHES);
}

/* Registers whose dead writes we are allowed to delete. */
static int isScratchReg(const char* r) {
    return (r[0] == '$' && r[1] == 't' && isdigit((unsigned char)r[2]) && !r[3]) ||
           !strcmp(r, "$v0") ||
           (r[0] == '$' && r[1] == 'a' && r[2] >= '0' && r[2] <= '3' && !r[3]);
}

/* ---------------------------------------------------------------------------
 * HELPERS OVER THE LINE ARRAY
 * -------------------------------------------------------------------------*/

/* Index of the next K_INSTR after i, skipping only blank/comment lines.
 * Returns -1 if a label or directive comes first (the window is broken). */
static int nextAdjacentInstr(char** lines, int n, int i, Ins* out) {
    for (int j = i + 1; j < n; j++) {
        if (!lines[j]) continue;
        parseLine(lines[j], out);
        if (out->kind == K_BLANK) continue;
        return out->kind == K_INSTR ? j : -1;
    }
    return -1;
}

/* Is lines[j] the `syscall` that exits the program (preceded by li $v0, 10)? */
static int isExitSyscall(char** lines, int j) {
    Ins in;
    parseLine(lines[j], &in);
    if (in.kind != K_INSTR || strcmp(in.op, "syscall") != 0) return 0;
    for (int k = j - 1; k >= 0; k--) {
        if (!lines[k]) continue;
        Ins p;
        parseLine(lines[k], &p);
        if (p.kind == K_BLANK) continue;
        return p.kind == K_INSTR && !strcmp(p.op, "li") && p.nargs == 2 &&
               !strcmp(p.arg[0], "$v0") && !strcmp(p.arg[1], "10");
    }
    return 0;
}

/* Is the value written to `r` by the instruction at i never read?  See the
 * header comment for the reasoning; every uncertain case answers "no". */
static int isDeadWrite(char** lines, int n, int i, const char* r) {
    for (int j = i + 1; j < n; j++) {
        if (!lines[j]) continue;
        Ins in;
        parseLine(lines[j], &in);
        if (in.kind == K_BLANK || in.kind == K_LABEL) continue;   /* fall through */
        if (in.kind == K_DIRECTIVE) return 0;
        if (isExitSyscall(lines, j)) return strcmp(r, "$v0") != 0;
        if (readsReg(&in, r)) return 0;
        if (isControl(&in)) {
            if (!strcmp(in.op, "jr")) return strcmp(r, "$v0") != 0;  /* $v0 = return value */
            return 0;                                                /* branch/jump/call  */
        }
        const char* w = writtenReg(&in);
        if (w && !strcmp(w, r)) return 1;                           /* overwritten unread */
    }
    return 0;   /* ran off the end: keep it */
}

/* Index of the line holding label `name:`, or -1. */
static int findLabelLine(char** lines, int n, const char* name) {
    for (int j = 0; j < n; j++) {
        if (!lines[j]) continue;
        Ins in;
        parseLine(lines[j], &in);
        if (in.kind == K_LABEL && !strcmp(in.arg[0], name)) return j;
    }
    return -1;
}

static void replaceLine(char** lines, int i, const char* text) {
    free(lines[i]);
    lines[i] = strdup(text);
}

static void deleteLine(char** lines, int i) {
    free(lines[i]);
    lines[i] = NULL;
}

/* ---------------------------------------------------------------------------
 * ONE SWEEP — returns how many changes it made
 * -------------------------------------------------------------------------*/
static int sweep(char** lines, int n, PeepStats* st) {
    int changes = 0;

    for (int i = 0; i < n; i++) {
        if (!lines[i]) continue;
        Ins a;
        parseLine(lines[i], &a);
        if (a.kind != K_INSTR) continue;

        /* move $tX, $tX does nothing */
        if (!strcmp(a.op, "move") && a.nargs == 2 && !strcmp(a.arg[0], a.arg[1])) {
            deleteLine(lines, i); st->selfMove++; changes++;
            continue;
        }

        Ins b;
        int j = nextAdjacentInstr(lines, n, i, &b);

        /* sw R, A ; lw R2, A  — the value is still in R */
        if (j >= 0 && !strcmp(a.op, "sw") && !strcmp(b.op, "lw") &&
            a.nargs == 2 && b.nargs == 2 && !strcmp(a.arg[1], b.arg[1])) {
            if (!strcmp(a.arg[0], b.arg[0])) {
                deleteLine(lines, j);
            } else {
                char text[512];
                snprintf(text, sizeof text,
                         "    move %s, %s          # peephole: was lw %s, %s",
                         b.arg[0], a.arg[0], b.arg[0], b.arg[1]);
                replaceLine(lines, j, text);
            }
            st->storeLoad++; changes++;
            continue;
        }

        /* lw R, A ; sw R, A  — memory already holds that value */
        if (j >= 0 && !strcmp(a.op, "lw") && !strcmp(b.op, "sw") &&
            a.nargs == 2 && b.nargs == 2 &&
            !strcmp(a.arg[0], b.arg[0]) && !strcmp(a.arg[1], b.arg[1])) {
            deleteLine(lines, j); st->loadStore++; changes++;
            continue;
        }

        if (!strcmp(a.op, "j") && a.nargs == 1) {
            /* j L ; L:  — the jump lands where we would fall anyway */
            int isNext = 0;
            for (int k = i + 1; k < n; k++) {
                if (!lines[k]) continue;
                Ins l;
                parseLine(lines[k], &l);
                if (l.kind == K_BLANK) continue;
                if (l.kind != K_LABEL) break;
                if (!strcmp(l.arg[0], a.arg[0])) { isNext = 1; break; }
            }
            if (isNext) { deleteLine(lines, i); st->jumpToNext++; changes++; continue; }

            /* j L1 ... L1: j L2  — go straight to L2 */
            int lbl = findLabelLine(lines, n, a.arg[0]);
            if (lbl >= 0) {
                for (int k = lbl + 1; k < n; k++) {
                    if (!lines[k]) continue;
                    Ins t;
                    parseLine(lines[k], &t);
                    if (t.kind == K_BLANK || t.kind == K_LABEL) continue;
                    if (t.kind == K_INSTR && !strcmp(t.op, "j") && t.nargs == 1 &&
                        strcmp(t.arg[0], a.arg[0]) != 0) {
                        char text[512];
                        snprintf(text, sizeof text,
                                 "    j    %s          # peephole: threaded past %s",
                                 t.arg[0], a.arg[0]);
                        replaceLine(lines, i, text);
                        st->jumpThread++; changes++;
                    }
                    break;
                }
            }
            continue;
        }

        /* a value nobody reads — but never delete div (it can trap) */
        const char* w = writtenReg(&a);
        if (w && isScratchReg(w) && strcmp(a.op, "div") != 0 &&
            isDeadWrite(lines, n, i, w)) {
            deleteLine(lines, i); st->deadWrite++; changes++;
            continue;
        }
    }
    return changes;
}

/* ---------------------------------------------------------------------------
 * PUBLIC ENTRY POINTS
 * -------------------------------------------------------------------------*/
int countInstructions(char** lines, int n) {
    int c = 0;
    for (int i = 0; i < n; i++) {
        if (!lines[i]) continue;
        Ins in;
        parseLine(lines[i], &in);
        if (in.kind == K_INSTR) c++;
    }
    return c;
}

int peepholeOptimize(char** lines, int n, PeepStats* st) {
    memset(st, 0, sizeof *st);
    st->linesBefore = countInstructions(lines, n);

    int changes;
    do {
        changes = sweep(lines, n, st);
        st->passes++;
    } while (changes > 0 && st->passes < 20);

    /* compact: squeeze out the deleted (NULL) entries */
    int m = 0;
    for (int i = 0; i < n; i++) if (lines[i]) lines[m++] = lines[i];

    st->linesAfter = countInstructions(lines, m);
    return m;
}
