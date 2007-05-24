# Makefile using z80asm
# The output of the assembler can be parsed by vim or emacs.

all: pass

%: %.asm %.correct-err %.correct-bin ../z80asm Makefile
	../z80asm -I ../headers $< -o $@.bin 2> $@.err
	diff $@.correct-bin $@.bin
	diff $@.correct-err $@.err
	rm $@.bin $@.err

clean:
	rm -f *-actual.err *.bin

.PHONY: clean all
