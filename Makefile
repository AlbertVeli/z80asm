# Makefile for the Z80 assembler by shevek
# Copyright (C) 2002,2004  Bas Wijnen
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

CC = gcc
LDFLAGS = -O2 -Wall
CFLAGS = -O2 -Wall -Wwrite-strings -Wcast-qual -Wcast-align -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls -Wnested-externs -Winline -pedantic -ansi -Wshadow -g -W -Ignulib

all:z80asm

z80asm:z80asm.c Makefile gnulib/getopt.o gnulib/getopt1.o
	$(CC) $(CFLAGS) $(LDFLAGS) $< gnulib/getopt.o gnulib/getopt1.o -o $@

gnulib/%.o:gnulib/%.c gnulib/getopt.h Makefile
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	for i in . gnulib ; do \
		rm -f $$i/a.bin $$i/a.out $$i/core $$i/*~ $$i/\#* $$i/*.o ; \
	done
	rm -f z80asm z80asm.exe

dist: clean
	rm -rf /tmp/z80asm
	tar cf - -C .. z80asm | tar xf - -C /tmp
	find /tmp/z80asm -name CVS | xargs rm -rf
	tar cvzf ../z80asm-`date +%Y%m%d`00.tar.gz -C /tmp z80asm
