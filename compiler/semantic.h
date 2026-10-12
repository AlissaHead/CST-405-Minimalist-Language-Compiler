/* =========================================================================
 * CST-405  ·  Project 2  ·  Minimalist Language Compiler
 * FILE: semantic.h   —   Phase 3 — Semantic analysis
 * -------------------------------------------------------------------------
 * THE PIPELINE, AND WHERE THIS FILE SITS IN IT
 *   scanner -> parser -> ast -> semantic -> tac -> codegen
 *                               ^^^^^^^^  this file
 *
 * CREDITS
 *   Interface from the CST-405 course starter code.
 * ========================================================================= */

#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "ast.h"

/* SEMANTIC ANALYSIS - WITH FUNCTION SUPPORT
 * This phase checks the semantic correctness of the program
 * Ensures variables are declared before use, no redeclarations, etc.
 * Now supports functions, scopes, parameters, and control flow
 */

/* SEMANTIC ERROR TRACKING */
typedef struct {
    int errorCount;           /* Number of semantic errors found */
    int warningCount;         /* Number of warnings issued */
} SemanticInfo;

/* SEMANTIC ANALYSIS FUNCTIONS */
void initSemantic();                     /* Initialize semantic analyzer */
int performSemanticAnalysis(ASTNode* root);  /* Run semantic checks on AST, returns 0 if successful */
void printSemanticSummary();             /* Print summary of semantic analysis results */

#endif
