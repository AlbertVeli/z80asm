# Makefile using z80asm
# The output of the assembler can be parsed by vim or emacs.

all: hello.rom macro.rom

%.rom: %.asm
	z80asm $< -o $@
