; pass.asm - test program which should pass compilation (with warnings)
; Copyright 2002-2007  Bas Wijnen
;
; This file is part of z80asm.
;
; Z80asm is free software; you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation; either version 3 of the License, or
; (at your option) any later version.
;
; Z80asm is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program.  If not, see <http://www.gnu.org/licenses/>.

	; Some constructs which should compile properly

	ld a, (0xc0)
	ld a, (0xc0) + 3	; this is the same as ld a, 0xc3
	ld b, (0xc0)		; warning: expression in parentheses
	ld a, -3
	ld c, 0xc003		; warning: 8-bit value truncated
	ld bc, 0xc003
	ld de, 0x12345		; warning: 16-bit value truncated
	ld bc, -0x8000
	ld de, -0x8001		; warning: 16-bit value truncated
	ld b, -0x81		; warning: 8-bit value truncated
	ld a, -0x80
	halt
	add hl, de
	ex af, af'
	jr z, $ + 4
	defb 0, 4, 5, label - $
label:
	jr label + 2
	db ';("', "'"
