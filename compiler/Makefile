# ============================================================================
# CST-405 mini compiler — build rules
# ----------------------------------------------------------------------------
#   make            build ./minicompiler
#   make test       compile every tests/*.cm, run it under SPIM, and compare
#                   with tests/expected/ (PASS/FAIL per test)
#   make unittest   unit tests for the TAC optimizer and the peephole pass
#   make check      both of the above
#   make clean      remove everything that `make` produced
#
# The two generated files are worth knowing about:
#   lex.yy.c        written by flex from scanner.l
#   parser.tab.c/.h written by bison from parser.y
# Never edit those two — edit the .l and .y files and rebuild.
# ============================================================================
CC     = gcc
LEX    = flex
YACC   = bison
CFLAGS = -g -Wall

TARGET = minicompiler
OBJS   = lex.yy.o parser.tab.o main.o ast.o symtab.o semantic.o codegen.o tac.o peephole.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# The scanner needs the token numbers that bison assigns, so parser.tab.h
# must exist before flex output is compiled.
lex.yy.c: scanner.l parser.tab.h
	$(LEX) scanner.l

parser.tab.c parser.tab.h: parser.y
	$(YACC) -d parser.y

lex.yy.o: lex.yy.c
	$(CC) $(CFLAGS) -c lex.yy.c

parser.tab.o: parser.tab.c
	$(CC) $(CFLAGS) -c parser.tab.c

main.o:     main.c ast.h semantic.h codegen.h tac.h trace.h
	$(CC) $(CFLAGS) -c main.c
ast.o:      ast.c ast.h
	$(CC) $(CFLAGS) -c ast.c
symtab.o:   symtab.c symtab.h trace.h
	$(CC) $(CFLAGS) -c symtab.c
semantic.o: semantic.c semantic.h ast.h symtab.h trace.h
	$(CC) $(CFLAGS) -c semantic.c
codegen.o:  codegen.c codegen.h peephole.h ast.h symtab.h tac.h trace.h
	$(CC) $(CFLAGS) -c codegen.c
tac.o:      tac.c tac.h ast.h trace.h
	$(CC) $(CFLAGS) -c tac.c
peephole.o: peephole.c peephole.h
	$(CC) $(CFLAGS) -c peephole.c

# ----------------------------------------------------------------------------
# Compile every tests/*.cm file and run the result in SPIM.  Each test file
# states its expected output in a header comment; compare and you have a
# regression suite.
# ----------------------------------------------------------------------------
test: $(TARGET)
	@echo "Regression tests (tests/*.cm against tests/expected/)"
	@bash tests/run_tests.sh

# ----------------------------------------------------------------------------
# Unit tests for the two optimizers, on hand-built input the starter
# language cannot produce yet (jumps, labels, spill/reload traffic).
# ----------------------------------------------------------------------------
UNIT = tests/unit/test_optimizer tests/unit/test_peephole

tests/unit/test_optimizer: tests/unit/test_optimizer.c tac.o
	$(CC) $(CFLAGS) -o $@ $^
tests/unit/test_peephole: tests/unit/test_peephole.c peephole.o
	$(CC) $(CFLAGS) -o $@ $^

unittest: $(UNIT)
	@status=0; for t in $(UNIT); do ./$$t || status=1; echo; done; exit $$status

# Everything: unit tests, then the regression suite.
check: unittest test

clean:
	rm -f $(TARGET) $(OBJS) lex.yy.c parser.tab.c parser.tab.h parser.output
	rm -f tests/*.s tests/*.tac *.s *.tac $(UNIT)

.PHONY: all clean test unittest check
