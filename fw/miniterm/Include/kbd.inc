;
;--------------------------------------------------------------------------
; File: KBD.INC
; Author: Jan Damborsky
; Created: 17.6.2015
; Last modified: 17.6.2015
; Changelog:
;
; 
;
; variables occupy RAM space 0x40-0x4f
;
;--------------------------------------------------------------------------
;
; global variables
vKbd_Ctrl		EQU 0x40 ; state machine control register
vKbd_IncCurrSt	EQU 0x41 ; current state of incremental switch state machine
vKbd_IncNewSt	EQU 0x42 ; new state of incremental switch state machine
vKbd_NumSteps	EQU 0x43 ; counts number of steps of incremental switch

; keyboard port
PORT_KBD	EQU PORTA
bKBD_CHA	EQU 0
bKBD_CHB	EQU 1
bKBD_S1		EQU 3

cKBD_CH_MSK	EQU	(1 << bKBD_CHA) | (1 << bKBD_CHB)