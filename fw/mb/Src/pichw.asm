;--------------------------------------------------------------------------
; File: PICHW.ASM
; Author: Jan Damborsky
; Created: 1.3.2016
; Last modified: 12.3.2016
; Changelog:
;
;--------------------------------------------------------------------------

	include "p16f690.inc"
	include "./include/main.inc"
	include	"./include/pichw.inc"
	include	"./include/pwr.inc"

;------------------------------------------------------
;---- I/O initialization
MIODef	MACRO port,value,direction
	movlw	value
	movwf	port
	movlw	direction
	bsf		STATUS,RP0
	movwf	port-PORTA+TRISA
	bcf		STATUS,RP0
	ENDM
;----
;mPicIni	MACRO
PICHW_CODE	code 0x0100
	global	lPicIni11
lPicIni11

; set frequency of Internal oscillator
	movlw	OSCCON_INI
	bsf		STATUS,RP0
	movwf	OSCCON
	bcf		STATUS,RP0

	clrf	vStsReg

;	btfss	RCON,NOT_TO	; was not reset caused by WDOG
;	bsf	vStsReg,cWDT_OUT

; interrupt initialization

	movlw	INTCON_INI
	movwf	INTCON		; disable all interrupts

	clrf	PIR1
	clrf	PIR2

	bsf		STATUS,RP0
	clrf	PIE1
	clrf	PIE2
	bcf		STATUS,RP0

; enable interrupts from peripherals
;	bsf		INTCON,PEIE
;	bsf		INTCON,GIE

; initialize TMR0 - driven by Fosc/4
	clrf	TMR0

	movlw	OPTION_INI
	bsf		STATUS,RP0
	movwf	OPTION_REG
	bcf		STATUS,RP0

; PORTx & TRISx initialization
	MIODef  PORTA,ValueA,DirA
	MIODef  PORTB,ValueB,DirB
	MIODef  PORTC,ValueC,DirC

;
; configure analog/digital pins as digital ones (except of AN2),
; since they are analog after POR
;
	bsf		STATUS,RP1
	movlw	ANSEL_INI
	movwf	ANSEL
	clrf	ANSELH
	bcf		STATUS,RP1

; configure weak pull-ups & interrupt-on-change for PORTA
	bsf		STATUS,RP0
	movlw	WPUA_INI
	movwf	WPUA
	movlw	IOCA_INI
	movwf	IOCA
	bcf		STATUS,RP0

; configure weak pull-ups & interrupt-on-change for PORTB
	bsf		STATUS,RP1
	movlw	WPUB_INI
	movwf	WPUB
	movlw	IOCB_INI
	movwf	IOCB
	bcf		STATUS,RP1

;	movlw	T1CON_INI	; T=400ns for Fosc=20MHz
;	movwf	T1CON

; initialize SSP module to operate in I2C 7-bit slave mode
	bsf		STATUS,RP0
	movlw	SSPADD_INI
	movwf	SSPADD
	bcf		STATUS,RP0

;
; enable SSP module in SSPMSK mode before changing the mode
; to the desired I2C mode to workaround following issue:
; In any of the I2C modes, the SSP module will fail to recognize
; the first Start bit received after a transition from module
; disable to module enable.
;
	movlw	b'00111001'
	movwf	SSPCON

	movlw	SSPCON_INI
	movwf	SSPCON

; initialize AD converter - measures environment brightness
; used to control LCD backlight
; ADCON1 first, since ADCON0 enables ADC

	bsf		STATUS,RP0
	movlw	ADCON1_INI
	movwf	ADCON1
	bcf		STATUS,RP0

	movlw	ADCON0_INI
	movwf	ADCON0

	return
;	ENDM
;---------------------------------------------------------------
mPpgms	MACRO
	ENDM
;---------------------------------------------------------------
	end