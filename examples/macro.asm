; usage of macros

	include "msx-bios.asm"

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
	jp z, .finish
	call CHPUT
	inc hl
	jp .start
.finish:endm

; Cartridge programs need to start at 0x4000
	org 0x4000
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
