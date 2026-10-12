/* =========================================================================
 * CST-405  ·  TOPIC 2  ·  Compiler for a Starter Language
 * FILE: scanner.l   —   Phase 1 — Lexical analysis
 * -------------------------------------------------------------------------
 * THE PIPELINE, AND WHERE THIS FILE SITS IN IT
 *   scanner -> parser -> ast -> semantic -> tac -> codegen
 *   ^^^^^^^  this file
 *
 * WHAT IS NEW IN TOPIC 2
 *   • The starter token set: `int`, `print`, identifiers, numbers,
 *   • `+`, `=`, `;`, `(`, `)`, and both comment forms
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
/* PHASE 1 — LEXICAL ANALYSIS
 *
 * The scanner turns a stream of CHARACTERS into a stream of TOKENS.
 * It is the only part of the compiler that ever sees whitespace or comments.
 *
 *      int  sum  =  a + 10 ;
 *      └─┬┘ └─┬┘ │  │ │ └┬┘ │
 *       INT  ID  '=' ID '+' NUM ';'
 *
 * Flex turns the rules below into a finite automaton.  Two rules decide
 * everything you need to know about how it behaves:
 *
 *   LONGEST MATCH   "<=" wins over "<" because it matches more characters.
 *                   This is why "<=" must exist as a rule at all, and why
 *                   its position in the file does not matter.
 *   FIRST MATCH     when two rules match the SAME length, the one written
 *                   first wins.  This is why the keyword rules sit above
 *                   the identifier rule: without that order, "int" would
 *                   come back as an ID.
 */
#include <stdio.h>
#include <stdlib.h>
#include "parser.tab.h"  /* Token definitions from Bison */

/* POSITION TRACKING — what makes an error message point at an exact spot.
 *
 * The parser usually notices a mistake one token LATE: a missing ';' is only
 * detected when the NEXT statement's first token shows up, often on the next
 * line.  So the scanner keeps two positions the parser can ask for:
 *
 *   tokLine/tokCol          where the token just scanned STARTS
 *                           (the one the parser is complaining about)
 *   prevEndLine/prevEndCol  the spot just PAST the token before it
 *                           (where a missing ';' or ')' belonged)
 *
 * Whitespace and comments never count as tokens, so they are skipped. */
int tokLine = 1, tokCol = 1;
int prevEndLine = 1, prevEndCol = 1;
static int curLine = 1, curCol = 1;             /* scan position            */
static int lastEndLine = 1, lastEndCol = 1;     /* end of newest real token */
static int matchLine = 1, matchCol = 1;         /* start of the CURRENT match,
                                                 * comments included */

/* Lexical errors do not stop the scan (we keep going so that every bad
 * character in the file is reported at once), but main.c checks this count
 * and refuses to certify the program if it is not zero. */
int lexErrors = 0;

static void trackPosition(const char* text, int len) {
    matchLine = curLine;  matchCol = curCol;
    int skip = text[0] == ' ' || text[0] == '\t' || text[0] == '\n' ||
               text[0] == '\r' || (text[0] == '/' && len > 1);
    if (!skip) {
        prevEndLine = lastEndLine;  prevEndCol = lastEndCol;
        tokLine = curLine;          tokCol = curCol;
    }
    for (int i = 0; i < len; i++) {
        if (text[i] == '\n') { curLine++; curCol = 1; }
        else                 { curCol++; }
    }
    if (!skip) { lastEndLine = curLine; lastEndCol = curCol; }
}
#define YY_USER_ACTION trackPosition(yytext, yyleng);
%}

/* Flex options to suppress warnings for unused functions */
%option nounput
%option noinput
%option yylineno

%%

"int"           { return INT; }
"print"         { return PRINT; }

[a-zA-Z_][a-zA-Z0-9_]* {
    yylval.str = strdup(yytext);
    return ID;
}

[0-9]+          {
    yylval.num = atoi(yytext);
    return NUM;
}


"+"             { return '+'; }
"-"             { return '-'; }
"*"             { return '*'; }

"="             { return '='; }

";"             { return ';'; }
"("             { return '('; }
")"             { return ')'; }

"//".*          { /* Skip single-line comments */ }
"/*"([^*]|\*+[^*/])*\*+"/"  { /* Skip multi-line comments */ }

    /* UNTERMINATED COMMENT.  This pattern is the closed-comment pattern
     * without its final "/", so it can match everything up to end of file.
     * For a comment that IS closed, the rule above matches one character
     * more and wins by LONGEST MATCH; this rule can only win when there is
     * no closing star-slash at all.  It swallows the rest of the file, so
     * the comment's contents are not reported as a cascade of bogus tokens. */
"/*"([^*]|\*+[^*/])*\**     { lexErrors++;
                              fprintf(stderr, "Lexical Error at line %d, column %d: "
                                      "unterminated comment (missing the closing */)\n",
                                      matchLine, matchCol); }

[ \t\r\n]       { }

.               { lexErrors++;
                  fprintf(stderr, "Lexical Error at line %d, column %d: unknown character '%c'\n",
                         tokLine, tokCol, *yytext); }

%%

int yywrap() {
    /* End of file counts as a position too: an error there (a missing ';'
     * after the very last statement) must point just past the last token. */
    prevEndLine = lastEndLine;  prevEndCol = lastEndCol;
    tokLine = curLine;          tokCol = curCol;
    return 1;
}
