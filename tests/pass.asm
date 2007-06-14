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
