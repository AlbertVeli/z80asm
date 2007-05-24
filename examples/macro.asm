; usage of macros

	include "msx-bios.asm"

; Cartridge programs need to start at 0x4000
	org 0x4000

; header to make this a cartridge
	db "AB"
	dw start

; macro definition
print:	macro string
	ld hl, string
; this label (and .finish) must be local to prevent duplicate definitions
; when the macro is used more than once.
.start:	ld a, (hl)
	and a
	jr z, .finish
	call CHPUT
	inc hl
	jr .start
.finish:endm

; Call the macros
start:	print text
	print text2
; Stop doing things
	di
loop:	halt
	jr loop

; The text strings
text:	db "This is some text\r\n", 0
text2:	db "This is some more text\r\n", 0

; Fill up the rest of the page
	ds 0x8000 - $
