; this org not strictly	necessary
	org   $4000

; load an existing rom,	16384 bytes in size, which was originally assembled at $4000
	incbin 'disk.rom'

; seek back to absolute	position 16 in the output file
	seek  $10
; since	the rom	is loaded at addres $4000, code	at $10 should be referred to as	$4010
	org   $4010

; the following	instructions will overwrite the	code at	$10
	jp    diskio
	jp    dskchg
	jp    getdpb
	jp    choice
	jp    dskfmt
	jp    drvoff

	seek  $3410
	org   $7410

diskio:	
dskchg:	
getdpb:	
choice:	
dskfmt:	
drvoff:
	add   a,2
	add   2
	sub   a,2
	sub   2

	ei
	ret

; The result of	this will be a new, patched binary rom file that is still exactly 16384	bytes in size.
