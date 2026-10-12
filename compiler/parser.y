/* =========================================================================
 * CST-405  ·  TOPIC 2  ·  Compiler for a Starter Language
 * FILE: parser.y   —   Phase 2 — Syntax analysis
 * -------------------------------------------------------------------------
 * THE PIPELINE, AND WHERE THIS FILE SITS IN IT
 *   scanner -> parser -> ast -> semantic -> tac -> codegen
 *              ^^^^^^  this file
 *
 * WHAT IS NEW IN TOPIC 2
 *   • The starter grammar: a program is a list of statements
 *   • Declaration, assignment, addition, and print
 *   • Error productions that name the mistake instead of just saying "syntax error"
 *
 * WHAT COMES NEXT
 *   Topic 3 adds functions, arrays and the rest of arithmetic — and with them, real activation records.
 *
 * YOUR TASK
 *   This is Project 2: the first compiler you build end to end.  Sections
 *   marked  TODO (Topic 2)  are yours.  Everything else — the headers, the
 *   scanner, the driver, the register allocator — is given, because the
 *   point of this project is the six PHASES, not the plumbing between them.
 * ========================================================================= */

%{
/* PHASE 2 — SYNTAX ANALYSIS
 *
 * The parser answers one question: do these tokens form a legal program?
 * Bison builds an LALR(1) bottom-up parser from the grammar below.  It reads
 * tokens left to right, shifts them onto a stack, and REDUCES whenever the
 * top of the stack matches the right-hand side of a rule.
 *
 * Answering "yes" is not enough, though — the rest of the compiler needs the
 * program's STRUCTURE.  So each rule carries a semantic action in { } that
 * builds one node of the abstract syntax tree.  $1, $2, ... are the values of
 * the symbols on the right-hand side; $$ is the value this rule hands back.
 *
 *      expr '+' expr   { $$ = createBinOp('+', $1, $3); }
 *       │        │                              │   └── right operand
 *       │        └── $3                         └────── left operand
 *       └── $1
 *
 * By the time yyparse() returns, the tree has been built bottom-up beneath us
 * and `root` points at the whole program.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

/* External declarations for lexer interface */
extern int yylex();
extern int yyparse();
extern FILE* yyin;
extern int yylineno;  /* Line number from scanner */

void yyerror(const char* s);
ASTNode* root = NULL;
static int syntaxErrors = 0;   /* error productions recover, so count errors ourselves */

/* ---- SYNTAX ERROR REPORTING (helpers are defined after the grammar) ------
 * Bison detects a mistake and calls yyerror() with its own wording.  We do
 * not print that immediately: we HOLD it, because an error production may be
 * able to say something far more useful.  Whichever way it goes, exactly one
 * message is printed per mistake, with the line, the column, the source line
 * and a caret under the spot:
 *
 *   diagnose()          an error production explains it: print OUR message at
 *                       the spot the missing piece belonged
 *   flushSyntaxError()  nobody could explain it: print Bison's message at the
 *                       token it complained about
 *
 * Positions come from scanner.l (see trackPosition there).                   */
#include <stdarg.h>
void flushSyntaxError(void);
static void diagnose(const char* fmt, ...);
static int  endsStatement(int tok);

/* Use inside an error production for a missing ';'.  Only claim the error if
 * the token we stopped at plausibly STARTS the next statement — otherwise the
 * real mistake is something else and Bison's own message is the honest one. */
#define MISSING_SEMI(...) \
    do { if (endsStatement(yychar)) { diagnose(__VA_ARGS__); yyerrok; } \
         else flushSyntaxError(); } while (0)
%}

/* SEMANTIC VALUES UNION */
%union {
    int num;
    char* str;
    struct ASTNode* node;
}

/* Ask Bison to list what it EXPECTED, e.g.
 *     syntax error, unexpected ')', expecting number or identifier
 * instead of a bare "syntax error".  The quoted names below are what it prints. */
%define parse.error verbose

/* TOKEN DECLARATIONS */
%token <num> NUM "number"
%token <str> ID "identifier"
%token INT "'int'" PRINT "'print'"

/* NON-TERMINAL TYPES */
/* Every non-terminal that yields an AST node must be declared here.
 * Add to this line as you add rules:
 *   %type <node> program stmt_list stmt decl assign expr print_stmt
 * (bison rejects a %type for a non-terminal that has no rules yet,
 *  which is why only `program` is listed to begin with) */
%type <node> program stmt_list stmt decl assign expr print_stmt

/* OPERATOR PRECEDENCE AND ASSOCIATIVITY
 * Listed from lowest to highest precedence.
 *
 * DANGLING-ELSE RESOLUTION:
 *   The grammar has an ambiguity: in  if (c) if (c) s  else s
 *   the "else" can bind to either "if".  The standard resolution
 *   is that else binds to the nearest (innermost) if.
 *
 *   We express this by giving the if-without-else rule a lower
 *   precedence (%prec LOWER_THAN_ELSE) than the ELSE token, so
 *   when bison sees ELSE it shifts (attaches it to the inner if)
 *   rather than reducing (closing the outer if first).
 */
%left '+' '-'
%left '*' '/'

%%

/* --------------------------------------------------------------------
 * TODO (Topic 2) — THE GRAMMAR
 * Everything above this line is given: the token declarations, the
 * precedence table, and the union that lets a rule hand back an ASTNode*.
 * Below it, write the rules.  Each rule needs a semantic action in { }
 * that builds one AST node and assigns it to $$.
 *
 * THE GRAMMAR YOU ARE IMPLEMENTING
 *
 *     program     ->  stmt_list
 *     stmt_list   ->  stmt  |  stmt_list stmt
 *     stmt        ->  decl  |  assign  |  print_stmt
 *     decl        ->  'int' ID ';'
 *     assign      ->  ID '=' expr ';'
 *     expr        ->  NUM  |  ID  |  expr '+' expr
 *     print_stmt  ->  'print' '(' expr ')' ';'
 *
 * THE ACTIONS YOU NEED
 *
 *     program    : stmt_list          { root = $1; }
 *     stmt_list  : stmt_list stmt     { $$ = createStmtList($1, $2); }
 *     decl       : INT ID ';'         { $$ = createDecl("int", $2); free($2); }
 *     assign     : ID '=' expr ';'    { $$ = createAssign($1, $3); free($1); }
 *     expr       : expr '+' expr      { $$ = createBinOp('+', $1, $3); }
 *     print_stmt : PRINT '(' expr ')' ';'  { $$ = createPrint($3); }
 *
 * TWO THINGS THAT WILL BITE YOU
 *
 *   free($2) on an ID.  The scanner strdup'd the identifier text; the AST
 *   constructor strdup's it again.  Whoever received it from the scanner
 *   has to free it, and that is this rule.  Skip it and the compiler
 *   leaks — small here, embarrassing under valgrind in Topic 6.
 *
 *   `root` is a global declared in the prologue above.  The `program` rule
 *   is the ONLY place that assigns it.  If root stays NULL after a
 *   successful parse, that assignment is missing and every later phase
 *   will silently do nothing.
 *
 * GOING FURTHER (worth doing, and worth talking about in your video)
 *
 *   Add ERROR PRODUCTIONS so that a missing semicolon produces
 *     "line 4: missing ';' after assignment to x"
 *   instead of "syntax error".  The shape is:
 *     | ID '=' expr error  { report it; $$ = NULL; yyerrok; }
 *   Error messages are most of what people judge a compiler by.
 * -------------------------------------------------------------------- */

/* -- PROGRAM ------------------------------------------------------------
 * The ONLY rule that assigns the global `root`. */
program:
    stmt_list           { if (syntaxErrors > 0) YYABORT;   /* recovered, but still a failed parse */
                          root = $1; $$ = $1; }
    ;

/* -- STATEMENT LIST -----------------------------------------------------
 * Left recursive: $1 is the list built so far, $2 is the new statement.
 * A statement that failed to parse (error production below) comes back as
 * NULL, so we skip it instead of putting a hole in the tree. */
stmt_list:
    stmt                { $$ = $1; }
    | stmt_list stmt    { $$ = ($2 == NULL) ? $1 : ($1 == NULL) ? $2
                                            : createStmtList($1, $2); }
    ;

stmt:
    decl                { $$ = $1; }
    | assign            { $$ = $1; }
    | print_stmt        { $$ = $1; }
    /* PANIC-MODE RECOVERY: when no more specific rule fits, skip ahead to the
     * next ';' and carry on.  Without this the parser would give up at the
     * first mistake and every later error would go unreported. */
    | error ';'         { flushSyntaxError(); $$ = NULL; yyerrok; }
    ;

/* -- DECLARATIONS -------------------------------------------------------
 *   int x;         BEGINNER: one node, a DECL
 *   int x = 5;     a DECL followed by an ASSIGN, glued into a small list
 *                  (the same tree you would get from  int x;  x = 5;) */
decl:
    INT ID ';'
        { $$ = createDecl("int", $2); free($2); }
    | INT ID '=' expr ';'
        { $$ = createStmtList(createDecl("int", $2), createAssign($2, $4));
          free($2); }
    /* error productions: name the mistake instead of "syntax error" */
    | INT ID error
        { MISSING_SEMI("missing ';' after the declaration of '%s'", $2);
          free($2); $$ = NULL; }
    | INT ID '=' expr error
        { MISSING_SEMI("missing ';' after the initial value of '%s'", $2);
          free($2); $$ = NULL; }
    ;

/* -- ASSIGNMENT ---------------------------------------------------------
 *   x = 5;   x = y + 2;   x = (a + b) - 1; */
assign:
    ID '=' expr ';'
        { $$ = createAssign($1, $3); free($1); }
    | ID '=' expr error
        { MISSING_SEMI("missing ';' after the assignment to '%s'", $1);
          free($1); $$ = NULL; }
    ;

/* -- EXPRESSIONS --------------------------------------------------------
 *   NUM, ID              leaves of the tree
 *   expr '+' expr        the starter operator; left associative
 *   expr '-' expr        same shape, different operator char
 *   expr '*' expr        binds tighter than + and - (see the %left table)
 *   '(' expr ')'         no node of its own: parentheses only steer the
 *                        parse, so we pass the inner tree straight up
 *
 * NOTE: '-' and '*' also need a rule in scanner.l  ("-" { return '-'; })
 *       before the scanner will hand them to the parser. */
expr:
    NUM                 { $$ = createNum($1); }
    | ID                { $$ = createVar($1); free($1); }
    | expr '+' expr     { $$ = createBinOp('+', $1, $3); }
    | expr '-' expr     { $$ = createBinOp('-', $1, $3); }
    | expr '*' expr     { $$ = createBinOp('*', $1, $3); }
    | '(' expr ')'      { $$ = $2; }
    /* an operator with nothing after it:  y = 5 + ;  */
    | expr '+' error    { diagnose("expected a value after '+'");  yyerrok; $$ = $1; }
    | expr '-' error    { diagnose("expected a value after '-'");  yyerrok; $$ = $1; }
    | expr '*' error    { diagnose("expected a value after '*'");  yyerrok; $$ = $1; }
    ;

/* -- PRINT --------------------------------------------------------------
 *   print(x);   print(a + b); */
print_stmt:
    PRINT '(' expr ')' ';'
        { $$ = createPrint($3); }
    | PRINT '(' expr ')' error
        { MISSING_SEMI("missing ';' after the print statement");
          $$ = NULL; }
    | PRINT '(' expr error ';'
        { diagnose("missing ')' to close the '(' after print");
          yyerrok; $$ = NULL; }
    ;

%%

/* ==========================================================================
 * ERROR REPORTING — the helpers declared in the prologue
 * ========================================================================*/

/* The message Bison produced, held until we know whether an error production
 * can explain it better.  `pending*` is where Bison noticed the problem (the
 * token it stopped at); `gap*` is the spot just past the token BEFORE that,
 * which is where a missing ';' or ')' belonged. */
static int  pendingActive = 0;
static char pendingMsg[256];
static int  pendingLine, pendingCol;
static int  gapLine, gapCol;

extern int tokLine, tokCol, prevEndLine, prevEndCol;   /* scanner.l */

/* Echo the offending source line with a caret under the column.  The scanner
 * has already buffered the file, so it is safe to borrow yyin for a moment as
 * long as the read position is put back afterwards. */
static void showSource(int line, int col) {
    if (!yyin) return;
    long resume = ftell(yyin);
    if (resume < 0) return;                       /* not a seekable file */
    fseek(yyin, 0, SEEK_SET);

    char text[512];
    for (int n = 1; fgets(text, sizeof text, yyin); n++) {
        if (n != line) continue;
        text[strcspn(text, "\r\n")] = '\0';
        fprintf(stderr, "  %4d | %s\n", line, text);
        fprintf(stderr, "       | ");
        for (int i = 0; i < col - 1; i++)
            fputc(i < (int)strlen(text) && text[i] == '\t' ? '\t' : ' ', stderr);
        fprintf(stderr, "^\n");
        break;
    }
    fseek(yyin, resume, SEEK_SET);
}

static void printError(int line, int col, const char* msg) {
    fprintf(stderr, "Syntax Error at line %d, column %d: %s\n", line, col, msg);
    showSource(line, col);
}

/* Print Bison's own message, if nobody has claimed it. */
void flushSyntaxError(void) {
    if (!pendingActive) return;
    pendingActive = 0;
    printError(pendingLine, pendingCol, pendingMsg);
}

/* An error production explains the pending error: print OUR message, at the
 * spot where the missing piece belonged, and drop Bison's. */
static void diagnose(const char* fmt, ...) {
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    pendingActive = 0;
    printError(gapLine, gapCol, msg);
}

/* Could this token be the FIRST token of a new statement? */
static int endsStatement(int tok) {
    return tok == INT || tok == PRINT || tok == ID || tok == YYEOF;
}

/* Called by Bison the moment it detects a mistake. */
void yyerror(const char* s) {
    flushSyntaxError();                 /* an earlier one nobody explained */
    syntaxErrors++;
    pendingActive = 1;
    snprintf(pendingMsg, sizeof pendingMsg, "%s", s);
    pendingLine = tokLine;   pendingCol = tokCol;
    gapLine = prevEndLine;   gapCol = prevEndCol;
}
