;
;--------------------------------------------------------------------------
; File: MCOM.ASM
; Author: Jan Damborsky
; Created: 1.3.2016
; Last modified: 9.3.2016
;
; Handles communication with master connected via I2C bus.
;
; variables occupy RAM space 0x20-0x2f
;
; Changelog:
; 
;--------------------------------------------------------------------------

 INCLUDE "p16f690.inc"
  INCLUDE "./include/mcom.inc"

; extern definitions

; LOCAL CONSTANTS DEFINITION
;MCOM_SIMUL		EQU 1 ; simulator mode
; - configured via 'Project->Build Options'

; definition of local state machines states
cMcom_Wait4Start	EQU 0
;--------------------------------------------------------------------------
; MACRO definitions
;----
; reads received character from SSPBUF
; and clears PIR1[SSPIF]
;
mMcomGetChar	MACRO
	movf	SSPBUF,w
	bcf		PIR1,SSPIF
	ENDM
; ----
;--------------------------------------------------------------------------
MCOM_CODE	code	0x0200
;--------------------------------------------------------------------------
; state machine
sMcom_Ctrl
 GLOBAL sMcom_Ctrl

	movlw	0x02
	movwf	PCLATH
	movf	vMcom_Ctrl,w	; beware of PCL overflow !
	addwf	PCL,f

; receives packet from master device
	goto	lMcom_Wait4Start	; 0
	goto	lMcom_RecvByte		; 1
	goto	lMcom_RecvAddr		; 2
	goto	lMcom_RecvByte		; 3
	goto	lMcom_RecvCmd		; 4
	goto	lMcom_RecvByte		; 5
	goto	lMcom_RecvData		; 6
	goto	lMcom_Wait4Stop		; 7

; state machine switch
lMcom_ControlDecE
 	decf	vMcom_Ctrl,w
	goto	lMcom_ControlCtrlE
lMcom_ControlIncE
 	incf	vMcom_Ctrl,w
lMcom_ControlCtrlE
 	movwf	vMcom_Ctrl
lMcom_ControlE

	return
;--------------------------------------------------------------------------
;----
; initialize the module
sMcom_Ini
 GLOBAL	sMcom_Ini

	movlw	cMcom_Wait4Start
	movwf	vMcom_Ctrl

	return
;----
;--------------------------------------------------------------------------
; MAIN APPLICATION
;----
;
; wait for I2C start condition
lMcom_Wait4Start
	bsf		STATUS,RP0
	btfss	SSPSTAT,S
	goto	lMcom_Wait4Start1

	bcf		STATUS,RP0
	goto	lMcom_ControlIncE	; lMcom_RecvByte

lMcom_Wait4Start1
	bcf		STATUS,RP0
	goto	lMcom_ControlE
;----
; wait until byte is received
; terminate receive process if stop condition is detected
lMcom_RecvByte
	bsf		STATUS,RP0
	btfsc	SSPSTAT,P
	goto	lMcom_RecvByteStop	; stop condition detected

;
; Check for PIR1[SSPIF] bit set to determine
; if byte has been received, since we realized
; that checking just for SSPSTAT[BF] may lead
; to reading SSPBUF multiple times.
; PIR1[SSPIF] must be clear by software once
; SSPBUF is read
;
	bcf		STATUS,RP0
	btfsc	PIR1,SSPIF
	goto	lMcom_ControlIncE	; byte received

	goto	lMcom_ControlE

lMcom_RecvByteStop
; stop condition detected, re-start receiving process
; unless we are receiving data in which case we
; set appropriate flag and move to the next state
	bcf		STATUS,RP0

	movlw	cMcom_Wait4Start
	btfss	vMcom_Sts,bMCOM_STS_RECVDATA
	goto	lMcom_ControlCtrlE	; lMcom_Wait4Start

	bsf		vMcom_Sts,bMCOM_STS_STOP
	goto	lMcom_ControlIncE
;----
; I2C address received - discarded for now
lMcom_RecvAddr
	mMcomGetChar

	goto	lMcom_ControlIncE	; lMcom_RecvByte
;----
; command received - discarded for now
lMcom_RecvCmd
	mMcomGetChar

; the only command supported for now is writing to frame buffer
;	movlw	vDisp_FB
	movwf	vMcom_Ptr

; data are to be received
	bsf		vMcom_Sts,bMCOM_STS_RECVDATA

	goto	lMcom_ControlIncE	; lMcom_RecvByte
;----
; receive data payload
lMcom_RecvData
; if stop condition has been detected, terminate receive process
	btfss	vMcom_Sts,bMCOM_STS_STOP
	goto	lMcom_RecvData1

	bcf		vMcom_Sts,bMCOM_STS_STOP
	goto	lMcom_RecvData2

lMcom_RecvData1
	movf	vMcom_Ptr,w
	movwf	FSR

	mMcomGetChar
	movwf	INDF

	incf	vMcom_Ptr,f

;	movlw	vDisp_FB + 0x10
	xorwf	vMcom_Ptr,w

	btfss	STATUS,Z
	goto	lMcom_ControlDecE	; lMcom_RecvByte

lMcom_RecvData2
; no more data are to be received
	bcf		vMcom_Sts,bMCOM_STS_RECVDATA

; let display module refresh LCD
;	bsf		vDisp_Cmd,bDISP_CMD_FB_WRITE

	goto	lMcom_ControlIncE	; lMcom_Wait4Stop
;----
lMcom_Wait4Stop
	bsf		STATUS,RP0
	btfss	SSPSTAT,P
	goto	lMcom_Wait4Start1

	bcf		STATUS,RP0

	movlw	cMcom_Wait4Start
	goto	lMcom_ControlCtrlE	; lMcom_Wait4Start
;----
	END
;----
