;
;--------------------------------------------------------------------------
; File: MCOM.INC
; Author: Jan Damborsky
; Created: 9.3.2016
; Last modified: 17.8.2016
; Changelog:
;
; Power management.
;
; variables occupy RAM space 0x30-0x3f
;
;--------------------------------------------------------------------------
;
;
 INCLUDE "./include/pichw.inc"

; global variables
vPwr_Ctrl		EQU 0x30 ; state machine control register
vPwr_SecTimer	EQU 0x31 ; senconds timer
vPwr_5vTimerH	EQU 0x32 ; 5V timer - high
vPwr_5vTimerL	EQU 0x33 ; 5V timer - low
vPwr_12vTimerH	EQU 0x34 ; 12V timer - high
vPwr_12vTimerL	EQU 0x35 ; 12V timer - low

; constants
; number of loops per one second
cPWR_LOOPS_PER_SEC	EQU (1000000 / MAIN_LOOP_TIME)
; timeouts in seconds
cPWR_5VTOUT		EQU	2*60*60	; 2 hours
cPWR_12VTOUT	EQU	10		; 10 seconds

; 5V control signal
cPWR_PORT5V		EQU PORTA
bPWR_5V			EQU 5
; 12V control signal
cPWR_PORT12V	EQU PORTB
bPWR_12V		EQU 7
