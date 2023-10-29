;------------------------------------------------------------------------
; File: MAIN.INC
;
; Created: 9.6.2015
; Last modified: 27.10.2023
;
; Changelog:
;
;------------------------------------------------------------------------

; constants

; variables
vWStore		EQU	0x7a	; W register stored during interrupt
vSTATUSStore	EQU	0x7b	; STATUS stored during interrupt
vPCLATHStore	EQU	0x7c	; PCLATH stored during interrupt
vStsReg		EQU	0x7d	; device status
