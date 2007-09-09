# Makefile using z80asm
# Copyright 2007  Bas Wijnen <wijnen@debian.org>
#
# This file is part of z80asm.
#
# Z80asm is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# Z80asm is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

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
