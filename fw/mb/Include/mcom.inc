;
;--------------------------------------------------------------------------
; File: MCOM.INC
; Author: Jan Damborsky
; Created: 1.3.2016
; Last modified: 1.3.2016
; Changelog:
;
; Handles communication with master connected via I2C bus.
;
; variables occupy RAM space 0x20-0x2f
;
;--------------------------------------------------------------------------
;
; I2C packet received from master has following format
;
; ---------------
; | Addr | Data |
; ---------------
;

; global variables
vMcom_Ctrl		EQU 0x20 ; state machine control register
vMcom_Sts		EQU 0x21 ; status register
vMcom_Ptr		EQU 0x22 ; index in data buffer

bMCOM_STS_RECVDATA	EQU	0 ; set if data are received
bMCOM_STS_STOP		EQU 1 ; stop condition detected