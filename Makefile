# ga4444 -- exact Gale's game (4x4 FOUR in a row) ground truth
#
#   make            build ./gt4444
#   make selftest   board + encoding + primitive checks, no solving
#   make export     self-checks, known-answer check, then the COMPLETE export
#   make clean
#
# The whole 16-cell game solves in well under a minute on one core, so the
# export here is complete: every legal position at every ply.

CC      ?= cc
CFLAGS  ?= -std=c11 -O3 -Wall -Wextra
LDFLAGS ?=

gt4444: gt4444.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

.PHONY: selftest export clean
selftest: gt4444
	./gt4444 selftest

PLYS ?= 16
export: gt4444
	@mkdir -p data
	./gt4444 export --plys $(PLYS) --out data

clean:
	rm -f gt4444
