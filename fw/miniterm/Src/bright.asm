;
;--------------------------------------------------------------------------
; File: BRIGHT.ASM
; Jan Damborsky
; Created: 17.8.2015
; Last modified: 18.8.2015
;
; Automatic control of LCD brightness.
;
; variables occupy RAM space 0x30-0x3f
;
; Changelog:
; 
;--------------------------------------------------------------------------

 INCLUDE "p16f690.inc"
 INCLUDE "./include/bright.inc"

; extern definitions

; LOCAL CONSTANTS DEFINITION

; definition of local state machines states
cBright_Begin		EQU 0

;--------------------------------------------------------------------------
; MACRO definitions
; ----
;--------------------------------------------------------------------------
BRIGHT_CODE	code	0x0300
;--------------------------------------------------------------------------
; state machine
sBright_Ctrl
 GLOBAL sBright_Ctrl

	movlw	0x03
	movwf	PCLATH
	movf	vBright_Ctrl,w	; beware of PCL overflow !
	addwf	PCL,f

; main application
	goto	lBright_Begin			; 0
	goto	lBright_Measure			; 1
	goto	lBright_SetBacklight	; 2

; state machine switch
lBright_ControlDecE
 	decf	vBright_Ctrl,w
	goto	lBright_ControlCtrlE
lBright_ControlIncE
 	incf	vBright_Ctrl,w
lBright_ControlCtrlE
 	movwf	vBright_Ctrl
lBright_ControlE

	return
;--------------------------------------------------------------------------
;--------------------------------------------------------------------------
; MAIN APPLICATION
;----
; initialize brightness measuring
; and starts AD conversion
lBright_Begin
	movlw	cBRIGHT_NUM_AVE
	movwf	vBright_Cnt

	clrf	vBright_AccL
	clrf	vBright_AccH

	bsf		ADCON0,GO
	goto	lBright_ControlIncE	; lBright_Measure
;----
; wait until AD conversion is done
lBright_Measure
	btfsc	ADCON0,GO
	goto	lBright_ControlE

; add conversion result to accumulator
; only higher 8 bits of result used for now
	movf	ADRESH,w
	addwf	vBright_AccL,f

	btfsc	STATUS,C
	incf	vBright_AccH,f

	decfsz	vBright_Cnt,f
	goto	lBright_Measure1
	goto	lBright_ControlIncE ; lBright_SetBacklight

lBright_Measure1
	bsf		ADCON0,GO
	goto	lBright_ControlE
;----
; adjusts LCD backlight according to measured value
lBright_SetBacklight
; calculate average value
; divide by 256 - that is discard lower byte
	movf	vBright_AccH,w

; set PWM directly from calculated value
	movwf	CCPR1L

; start new control cycle
	movlw	cBright_Begin
	goto	lBright_ControlCtrlE ; lBright_Begin
; Subroutines
;----
; initialize the module
sBright_Ini
 GLOBAL	sBright_Ini

	movlw	cBright_Begin
	movwf	vBright_Ctrl

	return
;----
	END
;----
