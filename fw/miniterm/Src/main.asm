;------------------------------------------------------------------------
; Build with MPLAB X IDE v5.30
; which is the last version which supports MPASM assembler.
;
; File: MAIN.ASM
; Main Application
; Device: PIC16F690
; Jan Damborsky
; version: v0.2
;
; Created: 19.5.2015
; Last modified: 27.10.2023
; Changelog:
;  - v0.2
;    - added support for 16x2 alphanumeric LCD with Hitachi HD44780U controller
;
; memory usage
; ============
;
;  module name       CODE        RAM
; -------------------------------------
;  MAIN          0x000-0x0ff  0x7a-0x7d
;  PICHW         0x100-0x1ff
;  MCOM          0x200-0x2ff  0x20-0x2f
;  BRIGHTNESS    0x300-0x3ff  0x30-0x3f
;  KBD           0x400-0x4ff  0x40-0x4f
;  DISPLAY       0x500-0x5ff  0x50-0x78
;
;------------------------------------------------------------------------

	__config _FOSC_INTRCIO & _WDT_OFF & _BOR_ON

;----
 INCLUDE "p16f690.inc"
;----
 INCLUDE "../include/pichw.inc"
 INCLUDE "../include/display.inc"
 
 INCLUDE "../include/main.inc"
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

 EXTERN lPicIni11, sMcom_Ini, sBright_Ini, sDisp_Ini, sKbd_Ini
 
	call	lPicIni11
	call	sMcom_Ini
	call	sBright_Ini
	call	sDisp_Ini
	call	sKbd_Ini

lMainLoop
; wait until TMR0 expires
	btfss	INTCON, T0IF
	goto	lMainLoop

; re-load TMR0
	movlw	TMR0_PRELOAD
	movwf	TMR0
	bcf	INTCON, T0IF

; toggle debug pin
	movlw	(1 << bDEBUG)
	xorwf	PORT_DEBUG,f

; State machines
 EXTERN sMcom_Ctrl, sBright_Ctrl, sKbd_Ctrl, sDisp_Ctrl
	call	sMcom_Ctrl
	call	sBright_Ctrl
	call	sKbd_Ctrl
	call	sDisp_Ctrl

	goto	lMainLoop

;----
;-----------------------------------------------------
	END
;-----------------------------------------------------
