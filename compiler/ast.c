/* =========================================================================
 * CST-405  ·  Project 2  ·  Minimalist Language Compiler
 * FILE: ast.c   —   Phase 2 — Syntax analysis (the tree it builds)
 * -------------------------------------------------------------------------
 * THE PIPELINE, AND WHERE THIS FILE SITS IN IT
 *   scanner -> parser -> ast -> semantic -> tac -> codegen
 *                        ^^^  this file
 *
 * WHAT THIS FILE CONTRIBUTES
 *   • One constructor per node kind, plus a tree printer
 *
 * CREDITS
 *   AST constructors and printAST: Alissa Head.
 *   (The node layout in ast.h comes from the CST-405 course starter code.)
 * ========================================================================= */

/* AST IMPLEMENTATION
 * Functions to create and manipulate Abstract Syntax Tree nodes
 * The AST is built during parsing and used for all subsequent phases
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

/* External line number from scanner */
extern int yylineno;

/* Create a number literal node */
ASTNode* createNum(int value) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = NODE_NUM;
    node->lineno = yylineno;
    node->data.num = value;  /* Store the integer value */
    return node;
}

/* Create a variable reference node */
ASTNode* createVar(char* name) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = NODE_VAR;
    node->lineno = yylineno;
    node->data.name = strdup(name);  /* Copy the variable name */
    return node;
}

/* Create a binary operation node (e.g. x + y) */
ASTNode* createBinOp(char op, ASTNode* left, ASTNode* right) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = NODE_BINOP;
    node->lineno = yylineno;
    node->data.binop.op = op;
    node->data.binop.left = left;
    node->data.binop.right = right;
    return node;
}

/* Create a declaration node (e.g. int x) */
ASTNode* createDecl(char* type, char* name) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = NODE_DECL;
    node->lineno = yylineno;
    node->data.decl.varType = strdup(type);  /* both come from short-lived   */
    node->data.decl.name    = strdup(name);  /* buffers (a literal / yytext) */
    return node;
}

/* Create an assignment node (e.g. x = 10) */
ASTNode* createAssign(char* var, ASTNode* value) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = NODE_ASSIGN;
    node->lineno = yylineno;
    node->data.assign.var   = strdup(var);   /* scanner's buffer is reused   */
    node->data.assign.value = value;
    return node;
}

/* Create a print statement node (e.g. print(x)) */
ASTNode* createPrint(ASTNode* expr) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = NODE_PRINT;
    node->lineno = yylineno;
    node->data.expr = expr;
    return node;
}

/* Create a statement list node, linking stmt1 (the list so far, since the
 * grammar is left recursive) to stmt2 (the newly parsed statement). */
ASTNode* createStmtList(ASTNode* stmt1, ASTNode* stmt2) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = NODE_STMT_LIST;
    node->lineno = yylineno;
    node->data.stmtlist.stmt = stmt1;
    node->data.stmtlist.next = stmt2;
    return node;
}

/* Text form of a packed operator code — just the character today, but kept
 * as a function because Topic 3 packs multi-character operators (==, <=, ...)
 * into the same char slot and this is the one place that has to know how. */
const char* opText(char op) {
    static char buf[2];
    buf[0] = op;
    buf[1] = '\0';
    return buf;
}

/* Display the AST structure (for debugging and education) */
void printAST(ASTNode* node, int level) {
    if (!node) return;
    for (int i = 0; i < level; i++) printf("  ");

    switch (node->type) {
        case NODE_NUM:
            printf("NUM %d\n", node->data.num);
            break;
        case NODE_VAR:
            printf("VAR %s\n", node->data.name);
            break;
        case NODE_BINOP:
            printf("BINOP '%s'\n", opText(node->data.binop.op));
            printAST(node->data.binop.left, level + 1);
            printAST(node->data.binop.right, level + 1);
            break;
        case NODE_DECL:
            printf("DECL %s %s\n", node->data.decl.varType, node->data.decl.name);
            break;
        case NODE_ASSIGN:
            printf("ASSIGN %s\n", node->data.assign.var);
            printAST(node->data.assign.value, level + 1);
            break;
        case NODE_PRINT:
            printf("PRINT\n");
            printAST(node->data.expr, level + 1);
            break;
        case NODE_STMT_LIST:
            /* A list of statements is a sequence, not a nesting: print both
             * halves at the SAME level instead of indenting deeper. */
            printAST(node->data.stmtlist.stmt, level);
            printAST(node->data.stmtlist.next, level);
            break;
        default:
            printf("UNKNOWN NODE TYPE %d\n", node->type);
            break;
    }
}
