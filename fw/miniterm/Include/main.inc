;------------------------------------------------------------------------
; File: MAIN.INC
;
; Created: 9.6.2015
; Last modified: 9.6.2015
;
; Changelog:
;
;------------------------------------------------------------------------

; constants

; variables
vWStore			EQU	0x70	; W register stored during interrupt
vSTATUSStore	EQU	0x71	; STATUS stored during interrupt
vPCLATHStore	EQU	0x72	; PCLATH stored during interrupt
vStsReg			EQU	0x73	; device status
