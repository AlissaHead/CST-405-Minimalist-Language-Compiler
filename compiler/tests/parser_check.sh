#!/bin/bash
set -u
# Work on a copy so the project folder stays clean
HERE="$(cd "$(dirname "$0")/.." && pwd)"
rm -rf ~/t2test && mkdir -p ~/t2test && cp -r "$HERE" ~/t2test/compiler
cd ~/t2test/compiler

echo "=== 1. bison strict check (warnings + conflicts) ==="
bison -Wall -Wcounterexamples -d -o /tmp/p.tab.c parser.y
echo "bison exit code: $?"

echo
echo "=== 2. throwaway harness (NOT part of your project) ==="
mkdir -p ~/t2test/h && cd ~/t2test/h
cp ../compiler/parser.y ../compiler/scanner.l ../compiler/ast.h .
# scanner: add - and * so the new rules can actually fire
sed -i 's|^"+"             { return .+.; }|"+" { return (int)0x2b; }\n"-" { return (int)0x2d; }\n"*" { return (int)0x2a; }|' scanner.l
grep -n 'return (int)' scanner.l

cat > stubs.c <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
extern int yylineno;
static ASTNode* mk(NodeType t){ASTNode*n=calloc(1,sizeof*n);n->type=t;n->lineno=yylineno;return n;}
ASTNode* createNum(int v){ASTNode*n=mk(NODE_NUM);n->data.num=v;return n;}
ASTNode* createVar(char*s){ASTNode*n=mk(NODE_VAR);n->data.name=strdup(s);return n;}
ASTNode* createBinOp(char op,ASTNode*l,ASTNode*r){ASTNode*n=mk(NODE_BINOP);n->data.binop.op=op;n->data.binop.left=l;n->data.binop.right=r;return n;}
ASTNode* createDecl(char*t,char*s){ASTNode*n=mk(NODE_DECL);n->data.decl.varType=strdup(t);n->data.decl.name=strdup(s);return n;}
ASTNode* createAssign(char*v,ASTNode*e){ASTNode*n=mk(NODE_ASSIGN);n->data.assign.var=strdup(v);n->data.assign.value=e;return n;}
ASTNode* createPrint(ASTNode*e){ASTNode*n=mk(NODE_PRINT);n->data.expr=e;return n;}
ASTNode* createStmtList(ASTNode*a,ASTNode*b){ASTNode*n=mk(NODE_STMT_LIST);n->data.stmtlist.stmt=a;n->data.stmtlist.next=b;return n;}
const char* opText(char c){static char b[2];b[0]=c;return b;}
void printAST(ASTNode*n,int lv){
  if(!n)return; for(int i=0;i<lv;i++)printf("  ");
  switch(n->type){
   case NODE_NUM:printf("NUM %d\n",n->data.num);break;
   case NODE_VAR:printf("VAR %s\n",n->data.name);break;
   case NODE_BINOP:printf("BINOP %c\n",n->data.binop.op);printAST(n->data.binop.left,lv+1);printAST(n->data.binop.right,lv+1);break;
   case NODE_DECL:printf("DECL %s %s\n",n->data.decl.varType,n->data.decl.name);break;
   case NODE_ASSIGN:printf("ASSIGN %s\n",n->data.assign.var);printAST(n->data.assign.value,lv+1);break;
   case NODE_PRINT:printf("PRINT\n");printAST(n->data.expr,lv+1);break;
   case NODE_STMT_LIST:printAST(n->data.stmtlist.stmt,lv);printAST(n->data.stmtlist.next,lv);break;
  }
}
extern int yyparse(void); extern FILE*yyin; extern ASTNode*root;
int main(int argc,char**argv){
  yyin=fopen(argv[1],"r"); if(!yyin){perror("open");return 2;}
  int rc=yyparse();
  printf("yyparse returned %d, root=%s\n",rc,root?"set":"NULL");
  printAST(root,0); return rc;
}
EOF

bison -d parser.y && flex scanner.l && \
gcc -Wall -Wno-unused-function -o ptest parser.tab.c lex.yy.c stubs.c
echo "harness build exit code: $?"

run() { echo; echo "----- $1 -----"; printf '%s\n' "$2" > case.cm; echo "$2"; echo "  >>>"; ./ptest case.cm; echo "  (exit $?)"; }

run "declaration + assignment + print" 'int x;
x = 5;
print(x);'

run "int x = 5;  (declaration with initializer)" 'int x = 5;
print(x);'

run "left associativity  a + b + c" 'int a; int b; int c; int r;
r = a + b + c;'

run "precedence  1 + 2 * 3" 'int r;
r = 1 + 2 * 3;'

run "parentheses  (1 + 2) * 3" 'int r;
r = (1 + 2) * 3;'

run "subtraction is left assoc  9 - 3 - 2" 'int r;
r = 9 - 3 - 2;'

run "tests/t2_05 missing semicolon (assign)" "$(cat ../compiler/tests/t2_05_errors_syntax.cm)"

run "missing semicolon after declaration" 'int x
int y;
y = 1;'

run "missing semicolon after print" 'int x;
print(x)
x = 2;'
