;
;--------------------------------------------------------------------------
; File: KBD.ASM
; Jan Damborsky
; Created: 17.6.2015
; Last modified: 17.8.2015
;
; Handles keyboard.
;
; variables occupy RAM space 0x40-0x4f
;
; Changelog:
; 
;--------------------------------------------------------------------------

 INCLUDE "p16f690.inc"
 INCLUDE "./include/kbd.inc"

; extern definitions

; LOCAL CONSTANTS DEFINITION

; definition of local state machines states
cKbd_Begin		EQU 0

;--------------------------------------------------------------------------
; MACRO definitions
; ----
mKBD_READ_INCSW	MACRO
	movf	PORT_KBD,w
	andlw	cKBD_CH_MSK
	bcf		INTCON,RABIF
	ENDM
;--------------------------------------------------------------------------
KBD_CODE	code	0x0400
;--------------------------------------------------------------------------
; state machine
sKbd_Ctrl
 GLOBAL sKbd_Ctrl

	movlw	0x04
	movwf	PCLATH
	movf	vKbd_Ctrl,w	; beware of PCL overflow !
	addwf	PCL,f

; main application
	goto	lKbd_Begin		; 0
	goto	lKbd_IncSw		; 1

; state machine switch
lKbd_ControlDecE
 	decf	vKbd_Ctrl,w
	goto	lKbd_ControlCtrlE
lKbd_ControlIncE
 	incf	vKbd_Ctrl,w
lKbd_ControlCtrlE
 	movwf	vKbd_Ctrl
lKbd_ControlE

	return
;--------------------------------------------------------------------------
;--------------------------------------------------------------------------
; MAIN APPLICATION
;----
;
lKbd_Begin
	goto	lKbd_ControlIncE	; lKbd_IncSw
;----
lKbd_IncSw
; wait until state of incremental switch changes
	btfss	INTCON,RABIF
	goto	lKbd_ControlE

; read new state
	mKBD_READ_INCSW
	movwf	vKbd_IncNewSt

; process state transition and determine if total number of steps
; should be incremented or decremented
	bcf		STATUS,C
	rlf		vKbd_IncCurrSt,f
	rlf		vKbd_IncCurrSt,w
	iorwf	vKbd_IncNewSt,w

	call	sKbd_IncCalcStep

	xorlw	1
	btfsc	STATUS,Z
	goto	lKbd_Increment

	xorlw	-1^1
	btfsc	STATUS,Z
	goto	lKbd_Decrement

; no change, just save new state
lKbd_SaveNewState
	movf	vKbd_IncNewSt,w
	movwf	vKbd_IncCurrSt
	goto	lKbd_ControlE

; increment number of steps, but do not overflow
lKbd_Increment
	incf	vKbd_NumSteps,f
	btfsc	STATUS,Z
	decf	vKbd_NumSteps,f

	goto	lKbdAdjustPWM

lKbd_Decrement
	movf	vKbd_NumSteps,f
	btfss	STATUS,Z
	decf	vKbd_NumSteps,f

; adjust display brightness accordingly
lKbdAdjustPWM
	movf	vKbd_NumSteps,w
;	movwf	CCPR1L

	goto	lKbd_SaveNewState
;----
; Subroutines
;----
; incremental switch state machine
sKbd_IncCalcStep
	andlw	0x0f
	addwf	PCL,f
	retlw	0	; 0000 - 0 -> 0: no change
	retlw	1	; 0001 - 0 -> 1: +1
	retlw	-1	; 0010 - 0 -> 2: -1
	retlw	0	; 0011 - 0 -> 3: invalid
	retlw	-1	; 0100 - 1 -> 0: -1
	retlw	0	; 0101 - 1 -> 1: no change
	retlw	0	; 0110 - 1 -> 2: invalid
	retlw	1	; 0111 - 1 -> 3: +1
	retlw	1	; 1000 - 2 -> 0: +1
	retlw	0	; 1001 - 2 -> 1: invalid
	retlw	0	; 1010 - 2 -> 2: no change
	retlw	-1	; 1011 - 2 -> 3: -1
	retlw	0	; 1100 - 3 -> 0: invalid
	retlw	-1	; 1101 - 3 -> 1: -1
	retlw	1	; 1110 - 3 -> 2: +1
	retlw	0	; 1111 - 3 -> 3: no change
;----
; initialize keyboard module
sKbd_Ini
 GLOBAL	sKbd_Ini

	movlw	cKbd_Begin
	movwf	vKbd_Ctrl

; initialize incremental switch state machine
	mKBD_READ_INCSW
	movwf	vKbd_IncCurrSt

	movlw	0x80
	movwf	vKbd_NumSteps

	return
;----
	END
;----
