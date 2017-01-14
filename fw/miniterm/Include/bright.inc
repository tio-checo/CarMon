;
;--------------------------------------------------------------------------
; File: BRIGHT.INC
; Author: Jan Damborsky
; Created: 17.8.2015
; Last modified: 11.1.2017
;
; Automatic control of LCD brightness.
;
; variables occupy RAM space 0x30-0x4f
;
; Changelog:
;
;--------------------------------------------------------------------------
;
; global variables
vBright_Ctrl		EQU 0x30	; state machine control register
vBright_AccL		EQU	0x31	; accumulator used for calculating average value
vBright_AccH		EQU	0x32
vBright_Cnt			EQU 0x33	; average counter

cBRIGHT_NUM_AVE		EQU 0		; number of average cycles (zero maps to 256)