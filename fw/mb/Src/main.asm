;------------------------------------------------------------------------
; File: MAIN.ASM
; Main Application
; Device: PIC16F690
; Jan Damborsky
; version: v0.1
;
; Created: 1.3.2016
; Last modified: 9.3.2016
; Changelog:
;
; memory usage
; ============
;
;  module name       CODE        RAM
; -------------------------------------
;  MAIN          0x000-0x0ff  0x70-0x73
;  PICHW         0x100-0x1ff
;  MCOM          0x200-0x2ff  0x20-0x2f
;  PWR           0x300-0x3ff  0x30-0x3f
;
;------------------------------------------------------------------------

	__config _FOSC_INTRCIO & _WDT_OFF & _BOR_ON

;----
 INCLUDE "p16f690.inc"
;----
 INCLUDE "./include/pichw.inc"

 INCLUDE "./include/main.inc"
;---
;------------------------------------------------------
;------------------------------------------------------
CODE_SEG_RST	code	0x00
;------------------------------------------------------
	goto	lAppInit

;------------------------------------------------------
CODE_SEG_INT	code	0x04
;------------------------------------------------------
; INTERRUPT HANDLER
;
; saved registers: W, STATUS, PCLATH
;----
; save PCLATH, STATUS, W registers
	movwf	vWStore
	movf	STATUS,w
	movwf	vSTATUSStore
	movf	PCLATH,w
	movwf	vPCLATHStore

; ...

; restore PCLATH, STATUS, W registers
lIntRestore
	movf	vPCLATHStore,w
	movwf	PCLATH
	movf	vSTATUSStore,w
	movwf	STATUS
	swapf	vWStore,f
	swapf	vWStore,w
	retfie
;----
;------------------------------------------------------
; MAIN loop
;------------------------------------------------------
;
lAppInit
; initialize local variables
	clrf	STATUS	; select data BANK0

 EXTERN lPicIni11, sMcom_Ini, sPwr_Ini
 
	call	lPicIni11
	call	sMcom_Ini
	call	sPwr_Ini

	movlw	cLED_TICKS
	movwf	vLedTimer

lMainLoop
; wait until TMR0 expires
	btfss	INTCON, T0IF
	goto	lMainLoop

; re-load TMR0
	movlw	TMR0_PRELOAD
	movwf	TMR0
	bcf		INTCON, T0IF

; toggle LED pin if appropriate
	decfsz	vLedTimer,f
	goto	lMainLoop1

	movlw	(1 << bLED)
	xorwf	PORT_LED,f

	movlw	cLED_TICKS
	movwf	vLedTimer

lMainLoop1

; State machines
 EXTERN sMcom_Ctrl, sPwr_Ctrl
	call	sMcom_Ctrl
	call	sPwr_Ctrl

	goto	lMainLoop

;----
;-----------------------------------------------------
	END
;-----------------------------------------------------
