; Hello world.  Load as a cartridge in openmsx.
	include 'msx-bios.asm'	; symbolic names for bios calls
	; header for a rom
	org 0x4000
	db "AB"
	dw start
text:	db "This is the source which generated this program:\r\n"
	bininclude 'hello.asm'
	db 0
start:	ld a, 80
	ld (0xf3ae), a	; width 80
	xor a
	call CHGMOD	; screen 0
	ld hl, text
loop:	ld a, (hl)
	and a	; set the z flag if A is 0
	jr z,stop	; and return in that case
	call CHPUT
	inc hl
	jr loop
stop:	jr stop
	ds 0x8000 - $	; fill up the rest of the page
