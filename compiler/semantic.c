/* =========================================================================
 * CST-405  ·  Project 2  ·  Minimalist Language Compiler
 * FILE: semantic.c   —   Phase 3 — Semantic analysis
 * -------------------------------------------------------------------------
 * THE PIPELINE, AND WHERE THIS FILE SITS IN IT
 *   scanner -> parser -> ast -> semantic -> tac -> codegen
 *                               ^^^^^^^^  this file
 *
 * WHAT THIS FILE CONTRIBUTES
 *   • Undeclared variables and duplicate declarations, reported with line numbers
 *
 * CREDITS
 *   Semantic checks: Alissa Head.
 * ========================================================================= */

/* SEMANTIC ANALYSIS IMPLEMENTATION - WITH FUNCTION SUPPORT
 * Performs semantic checks on the Abstract Syntax Tree
 * Now supports functions, scopes, parameters, control flow
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "semantic.h"
#include "symtab.h"
#include "trace.h"

#define MAX_FUNCTIONS 100
#define MAX_PARAMS 20
#define MAX_SCOPE_DEPTH 10


/* Scope for variables */
typedef struct {
    char* names[MAX_VARS];
    int count;
} Scope;

/* Global semantic information */
static SemanticInfo semInfo;
static Scope scopes[MAX_SCOPE_DEPTH];
static int scopeDepth = 0;

/* Initialize semantic analyzer */
void initSemantic() {
    semInfo.errorCount = 0;
    semInfo.warningCount = 0;
    scopeDepth = 0;

    trace("SEMANTIC ANALYZER: initialized\n\n");
}

/* Scope management */
static void enterScope() {
    if (scopeDepth >= MAX_SCOPE_DEPTH) {
        fprintf(stderr, "SEMANTIC ERROR: Maximum scope depth exceeded\n");
        semInfo.errorCount++;
        return;
    }
    scopes[scopeDepth].count = 0;
    scopeDepth++;
}

static void exitScope() {
    if (scopeDepth > 0) {
        /* Free variable names in this scope */
        for (int i = 0; i < scopes[scopeDepth - 1].count; i++) {
            free(scopes[scopeDepth - 1].names[i]);
        }
        scopeDepth--;
    }
}

/* Print current semantic scopes for debugging */
static void printSemanticScopes() {
    trace("\n┌─────────────────────────────────────────────────────────┐\n");
    trace("│ SEMANTIC SCOPE STACK (Depth: %d)                        \n", scopeDepth);
    trace("├─────────────────────────────────────────────────────────┤\n");

    if (scopeDepth == 0) {
        trace("│ (no active scopes)                                      │\n");
    } else {
        for (int depth = 0; depth < scopeDepth; depth++) {
            if (depth == 0) {
                trace("│ Scope[%d] GLOBAL (%d variables)                        \n", depth, scopes[depth].count);
            } else {
                trace("│ Scope[%d] LOCAL (%d variables)                         \n", depth, scopes[depth].count);
            }

            if (scopes[depth].count > 0) {
                trace("│   Variables: ");
                for (int i = 0; i < scopes[depth].count; i++) {
                    trace("%s", scopes[depth].names[i]);
                    if (i < scopes[depth].count - 1) trace(", ");
                }
                trace("\n");
            } else {
                trace("│   (empty)\n");
            }
        }
    }
    trace("└─────────────────────────────────────────────────────────┘\n\n");
}

/* Add variable to current scope */
/* RESERVED IDENTIFIERS
 * The intermediate-code generator invents names of its own: temporaries
 * t0, t1, ... and labels L0, L1, ...  If a user variable had one of those
 * shapes the back end could not tell them apart, so the language reserves
 * that namespace.  Reporting it here — in the semantic analyzer, with a line
 * number — is far kinder than a mysterious wrong answer at run time. */
static int isReservedName(const char* name) {
    if (!name || !name[0]) return 0;
    if (strncmp(name, "__sw", 4) == 0) return 1;          /* switch temporaries */
    if (name[0] != 't' && name[0] != 'L') return 0;
    if (!name[1]) return 0;
    for (int i = 1; name[i]; i++)
        if (name[i] < '0' || name[i] > '9') return 0;
    return 1;
}

static void reportReserved(const char* name, int lineno) {
    fprintf(stderr, "\n╔════════════════════════════════════════════════════════════╗\n");
    fprintf(stderr, "║ SEMANTIC ERROR - Reserved Identifier                      ║\n");
    fprintf(stderr, "╚════════════════════════════════════════════════════════════╝\n");
    fprintf(stderr, "  📍 Location: Line %d\n", lineno);
    fprintf(stderr, "  ❌ Error: '%s' is reserved for the compiler's own use\n", name);
    fprintf(stderr, "  📖 Note: names of the form t0, t1, ... are three-address-code\n");
    fprintf(stderr, "           temporaries and L0, L1, ... are generated labels.\n");
    fprintf(stderr, "  💡 Suggestion: rename the variable, for example '%s_' or 'total'\n\n", name);
}

static int addVarToScope(char* name) {
    if (scopeDepth == 0) {
        fprintf(stderr, "SEMANTIC ERROR: No scope to add variable to\n");
        return -1;
    }

    Scope* currentScope = &scopes[scopeDepth - 1];

    /* Check if already declared in current scope */
    for (int i = 0; i < currentScope->count; i++) {
        if (strcmp(currentScope->names[i], name) == 0) {
            return -1;  /* Already declared in this scope */
        }
    }

    /* Add to current scope */
    if (currentScope->count >= MAX_VARS) {
        fprintf(stderr, "SEMANTIC ERROR: Too many variables in scope\n");
        return -1;
    }

    currentScope->names[currentScope->count] = strdup(name);
    currentScope->count++;
    return 0;
}

/* Check if variable is declared in any visible scope */
static int isVarDeclaredInScope(char* name) {
    /* Search from innermost to outermost scope */
    for (int depth = scopeDepth - 1; depth >= 0; depth--) {
        for (int i = 0; i < scopes[depth].count; i++) {
            if (strcmp(scopes[depth].names[i], name) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

/* Forward declaration: checkStmt and checkStmtList are mutually recursive,
 * which is exactly what you want when the thing being checked is a tree. */
static void checkStmtList(ASTNode* node);

/* Report an undeclared variable, with a line number and a fix. */
static void reportUndeclared(const char* name, int lineno) {
    fprintf(stderr, "\n╔════════════════════════════════════════════════════════════╗\n");
    fprintf(stderr, "║ SEMANTIC ERROR - Undeclared Variable                      ║\n");
    fprintf(stderr, "╚════════════════════════════════════════════════════════════╝\n");
    fprintf(stderr, "  📍 Location: Line %d\n", lineno);
    fprintf(stderr, "  ❌ Error: Variable '%s' is used before being declared\n", name);
    fprintf(stderr, "  💡 Suggestion: Add a declaration before using this variable:\n");
    fprintf(stderr, "     → int %s;  (add this before line %d)\n\n", name, lineno);
    semInfo.errorCount++;
}

/* Report a variable declared twice in the same scope. */
static void reportRedeclared(const char* name, int lineno) {
    fprintf(stderr, "\n╔════════════════════════════════════════════════════════════╗\n");
    fprintf(stderr, "║ SEMANTIC ERROR - Duplicate Declaration                    ║\n");
    fprintf(stderr, "╚════════════════════════════════════════════════════════════╝\n");
    fprintf(stderr, "  📍 Location: Line %d\n", lineno);
    fprintf(stderr, "  ❌ Error: Variable '%s' is already declared in this scope\n", name);
    fprintf(stderr, "  💡 Suggestion: remove the duplicate, or rename one of the two\n\n");
    semInfo.errorCount++;
}

/* Check expression for semantic correctness */
static void checkExpr(ASTNode* node) {
    if (!node) return;

    switch (node->type) {
        case NODE_NUM:
            /* A literal always means what it says — nothing to check. */
            break;

        case NODE_VAR:
            /* The name must be declared and VISIBLE from here: the scope
             * stack is searched innermost-first, so an outer name still
             * resolves and a shadowed one correctly does not. */
            if (!isVarDeclaredInScope(node->data.name)) {
                reportUndeclared(node->data.name, node->lineno);
            } else {
                trace("  ✓ Use of '%s' resolves (line %d)\n", node->data.name, node->lineno);
            }
            break;

        case NODE_BINOP:
            /* Nothing to check about the operator itself; recurse into both
             * operands so an error deep in a+b+c is still reported. */
            checkExpr(node->data.binop.left);
            checkExpr(node->data.binop.right);
            break;

        default:
            break;
    }
}

/* Check statement */
static void checkStmt(ASTNode* node) {
    if (!node) return;

    switch (node->type) {
        case NODE_DECL: {
            char* name = node->data.decl.name;

            /* Names shaped like t0 or L3 are reserved for the compiler's own
             * temporaries and labels (isReservedName, above) — catch that
             * here, with a line number, instead of letting it corrupt TAC. */
            if (isReservedName(name)) {
                reportReserved(name, node->lineno);
                semInfo.errorCount++;
                /* Still enter the name in scope.  The program will not be
                 * compiled either way, but without this every later use of
                 * the name would ALSO be reported as "undeclared" — a
                 * cascade of errors that all have the same single cause. */
                addVarToScope(name);
                break;
            }

            /* The name must NOT already be declared in THIS scope.  (An outer
             * scope with the same name is legal shadowing, not an error —
             * that is exactly why addVarToScope only searches the current
             * scope, while isVarDeclaredInScope below searches every scope.) */
            if (addVarToScope(name) == -1) {
                reportRedeclared(name, node->lineno);
            } else {
                trace("  ✓ Variable '%s' declared (line %d)\n", name, node->lineno);
            }
            break;
        }

        case NODE_ASSIGN:
            /* The target must already be declared.  Order matters here: for
             *     int x;  x = x + 1;
             * checking the right-hand side must happen with x already in
             * scope, which it is — the DECL above was a separate, earlier
             * statement, so this check only has to confirm the target. */
            if (!isVarDeclaredInScope(node->data.assign.var)) {
                reportUndeclared(node->data.assign.var, node->lineno);
            } else {
                trace("  ✓ Assignment to '%s' is valid (line %d)\n",
                      node->data.assign.var, node->lineno);
            }
            checkExpr(node->data.assign.value);
            break;

        case NODE_PRINT:
            checkExpr(node->data.expr);
            break;

        case NODE_STMT_LIST:
            checkStmtList(node);
            break;

        default:
            break;
    }
}

/* Check statement list */
static void checkStmtList(ASTNode* node) {
    if (!node) return;

    if (node->type == NODE_STMT_LIST) {
        checkStmt(node->data.stmtlist.stmt);
        checkStmtList(node->data.stmtlist.next);
    } else {
        checkStmt(node);
    }
}

int performSemanticAnalysis(ASTNode* root) {
    if (!root) {
        fprintf(stderr, "SEMANTIC ERROR: No AST to analyze\n");
        return -1;
    }

    trace("Running semantic analysis with function support...\n\n");

    /* Enter global scope */
    enterScope();
    trace("Entered global scope\n");
    printSemanticScopes();

    /* The starter language has no functions, so one walk over the statement
     * list is the whole analysis.  Topic 3 replaces this with two passes. */
    checkStmtList(root);

    trace("\nFinished walking the AST — final state of the scope stack:\n");
    printSemanticScopes();

    /* Exit global scope */
    exitScope();

    return semInfo.errorCount > 0 ? -1 : 0;
}

/* Print semantic analysis summary */
void printSemanticSummary() {
    trace("═══════════════════════════════════════════\n");
    trace("SEMANTIC ANALYSIS SUMMARY\n");
    trace("═══════════════════════════════════════════\n");
    trace("Errors found:       %d\n", semInfo.errorCount);
    trace("Warnings found:     %d\n", semInfo.warningCount);
    trace("\n");

    if (semInfo.errorCount == 0) {
        trace("✓ Semantic analysis passed - program is semantically correct!\n");
    } else {
        trace("✗ Semantic analysis failed - fix errors before proceeding\n");
    }
    trace("═══════════════════════════════════════════\n\n");
}
