;
;--------------------------------------------------------------------------
; File: PWR.ASM
; Author: Jan Damborsky
; Created: 9.3.2016
; Last modified: 15.3.2016
;
; Power management.
;
; variables occupy RAM space 0x30-0x3f
;
; Changelog:
; 
;--------------------------------------------------------------------------

 INCLUDE "p16f690.inc"
 INCLUDE "./include/pwr.inc"

; extern definitions

; LOCAL CONSTANTS DEFINITION
;PWR_SIMUL		EQU 1 ; simulator mode
; - configured via 'Project->Build Options'

; definition of local state machines states
cPwr_MonitorBl	EQU 0
;--------------------------------------------------------------------------
; MACRO definitions
;----
; 5V On
mPwr5VOn	MACRO
	bcf		cPWR_PORT5V,bPWR_5V
	ENDM
;----
; 5V Off
mPwr5VOff	MACRO
	bsf		cPWR_PORT5V,bPWR_5V
	ENDM
;----
; 12V On
mPwr12VOn	MACRO
	bsf		cPWR_PORT12V,bPWR_12V
	ENDM
;----
; 12V Off
mPwr12VOff	MACRO
	bcf		cPWR_PORT12V,bPWR_12V
	ENDM
;----
; skip following instruction if 5V is On
mPwrSkipIf5VIsOn MACRO
	btfsc	cPWR_PORT5V,bPWR_5V
	ENDM
;----
; skip following instruction if 12V is On
mPwrSkipIf12VIsOn MACRO
	btfss	cPWR_PORT12V,bPWR_12V
	ENDM
;----
; skip following instruction if 5V is Off
mPwrSkipIf5VIsOff MACRO
	btfss	cPWR_PORT5V,bPWR_5V
	ENDM
;----
; skip following instruction if 12V is Off
mPwrSkipIf12VIsOff MACRO
	btfsc	cPWR_PORT12V,bPWR_12V
	ENDM
;--------------------------------------------------------------------------
PWR_CODE	code	0x0300
;--------------------------------------------------------------------------
; state machine
sPwr_Ctrl
 GLOBAL sPwr_Ctrl

	movlw	0x03
	movwf	PCLATH
	movf	vPwr_Ctrl,w	; beware of PCL overflow !
	addwf	PCL,f

; test if PIC32MX is operating in bootloader mode
	goto	lPwr_MonitorBl	; 0

; monitors CAN bus
	goto	lPwr_MonitorCan	; 1
	goto	lPwr_Wait4CanActive ; 2

; state machine switch
lPwr_ControlDecE
 	decf	vPwr_Ctrl,w
	goto	lPwr_ControlCtrlE
lPwr_ControlIncE
 	incf	vPwr_Ctrl,w
lPwr_ControlCtrlE
 	movwf	vPwr_Ctrl
lPwr_ControlE

	return
;--------------------------------------------------------------------------
;----
; initialize the module
sPwr_Ini
 GLOBAL	sPwr_Ini

; initialize state machine
	movlw	cPwr_MonitorBl
	movwf	vPwr_Ctrl

; initialize timers
	movlw	cPWR_LOOPS_PER_SEC
	movwf	vPwr_SecTimer

	movlw	high cPWR_5VTOUT
	movwf	vPwr_5vTimerH
	movlw	low cPWR_5VTOUT
	movwf	vPwr_5vTimerL

	movlw	high cPWR_12VTOUT
	movwf	vPwr_12vTimerH
	movlw	low cPWR_12VTOUT
	movwf	vPwr_12vTimerL

; turn the powers on
	mPwr5VOn
	mPwr12VOn

	return
;----
;--------------------------------------------------------------------------
; MAIN APPLICATION
;----
;
; Never turn the powers off if PIC32MX module is operating
; in bootloader mode.
;
lPwr_MonitorBl
	btfss	PORT_BL,bBL
	goto	lPwr_ControlE
	goto	lPwr_ControlIncE ; lPwr_MonitorCan
;----
; Monitor CAN traffic and reset timers if there is an activity
; detected on CAN bus.
; Output of Rx Can PHY is connected to INT pin.
;
lPwr_MonitorCan
	btfss	INTCON,INTF
	goto	lPwr_MonitorCanIdle

; CAN bus is active, reset timers
; and make sure both powers are ON
	bcf		INTCON,INTF

	movlw	cPWR_LOOPS_PER_SEC
	movwf	vPwr_SecTimer

	movlw	high cPWR_5VTOUT
	movwf	vPwr_5vTimerH
	movlw	low cPWR_5VTOUT
	movwf	vPwr_5vTimerL

	movlw	high cPWR_12VTOUT
	movwf	vPwr_12vTimerH
	movlw	low cPWR_12VTOUT
	movwf	vPwr_12vTimerL

	mPwr5VOn
	mPwr12VOn

	goto	lPwr_ControlE

; No activity detected on CAN since last time,
; decrement timers.
lPwr_MonitorCanIdle
	decfsz	vPwr_SecTimer,f
	goto	lPwr_ControlE

	movlw	cPWR_LOOPS_PER_SEC
	movwf	vPwr_SecTimer

; Do not decrement the timer if power is already Off.
	mPwrSkipIf12VIsOn
	goto	lPwr_Monitor5VTmr

	decf	vPwr_12vTimerL,f
	movf	vPwr_12vTimerL,w
	xorlw	0xff
	btfss	STATUS,Z
	goto	lPwr_Monitor12VTmrExp

	decf	vPwr_12vTimerH,f

; If timer expired, turn 12V power Off.
lPwr_Monitor12VTmrExp
	movf	vPwr_12vTimerH,f
	btfss	STATUS,Z
	goto	lPwr_Monitor5VTmr

	movf	vPwr_12vTimerL,f
	btfss	STATUS,Z
	goto	lPwr_Monitor5VTmr

	mPwr12VOff

; Handle 5V timer.
lPwr_Monitor5VTmr
	mPwrSkipIf5VIsOn
	goto	lPwr_MonitorCheckIdle

	decf	vPwr_5vTimerL,f
	movf	vPwr_5vTimerL,w
	xorlw	0xff
	btfss	STATUS,Z
	goto	lPwr_Monitor5VTmrExp

	decf	vPwr_5vTimerH,f

; If timer expired, turn 5V power Off.
lPwr_Monitor5VTmrExp
	movf	vPwr_5vTimerH,f
	btfss	STATUS,Z
	goto	lPwr_ControlE

	movf	vPwr_5vTimerL,f
	btfss	STATUS,Z
	goto	lPwr_ControlE

	mPwr5VOff

; If both powers are turned off, there is no need
; to deal with timers.
; Instead wait for activity on CAN bus.
lPwr_MonitorCheckIdle
	mPwrSkipIf12VIsOff
	goto	lPwr_ControlE
	mPwrSkipIf5VIsOff
	goto	lPwr_ControlE

	goto	lPwr_ControlIncE ; lPwr_Wait4CanActive
;----
; Both 12V as well as 5V are now turned off.
; If there is an activity detected on CAN bus
; turn both power on and switch to monitoring mode.
;
lPwr_Wait4CanActive
	btfss	INTCON,INTF
	goto	lPwr_ControlE

	mPwr5VOn
	mPwr12VOn

	goto	lPwr_ControlDecE ; lPwr_MonitorCan
;----
	END
;----
