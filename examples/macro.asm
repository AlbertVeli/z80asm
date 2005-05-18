; usage of macros

	include "msx-bios.asm"
	db "AB"
	dw start
	org 0x4000
print:	macro string
	ld hl, string
; this label (and .finish) must be local to prevent duplicate definitions
.start:
	ld a, (hl)
	and a
	jp z, .finish
	call CHPUT
	inc hl
	jp .start
.finish:
	endm

start:	print text
	print text2
loop:	halt
	jr loop
text:	db "This is some text\r\n", 0
text2:	db "This is some more text\r\n", 0
