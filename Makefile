# Makefile for the MSX z80 assembler by shevek
# Copyright (C) 2002  Bas Wijnen
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
# See the file named COPYING for more details.
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
LDFLAGS = -O5 -Wall
CFLAGS = -O5 -Wall -Wwrite-strings -Wcast-qual -Wcast-align -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls -Wnested-externs -Winline -Werror -pedantic-errors -ansi -Wshadow -g -W

all:z80asm

z80asm:z80asm.c Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f a.bin a.out core *~ \#* z80asm

dist: clean
	rm -rf /tmp/msxz80asm
	tar cf - -C .. msxz80asm | tar xf - -C /tmp
	find /tmp/msxz80asm -name CVS | xargs rm -rf
	tar cvzf ../msxz80asm-`date +%Y%m%d`00.tar.gz -C /tmp msxz80asm
