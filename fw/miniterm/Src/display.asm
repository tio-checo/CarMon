;
;--------------------------------------------------------------------------
; File: DISPLAY.ASM
; Jan Damborsky
; Created: 19.5.2015
; Last modified: 18.8.2015
;
; - populates display content according to data received from master.
;
; variables occupy RAM space 0x50-0x6f
;
; Changelog:
; 
;--------------------------------------------------------------------------

 INCLUDE "p16f690.inc"
 INCLUDE "./include/display.inc"

; extern definitions

; LOCAL CONSTANTS DEFINITION
;DISP_SIMUL		EQU 1 ; simulator mode
; - configured via 'Project->Build Options'

; definition of local state machines states
cDisp_Begin		EQU 0
cDisp_Wait5sec	EQU 7
cDisp_Idle		EQU 11
cDisp_WriteFB1	EQU 12
;--------------------------------------------------------------------------
; MACRO definitions
; ----
;--------------------------------------------------------------------------
DISP_CODE	code	0x0500
;--------------------------------------------------------------------------
; state machine
sDisp_Ctrl
 GLOBAL sDisp_Ctrl

	movlw	0x05
	movwf	PCLATH
	movf	vDisp_Ctrl,w	; beware of PCL overflow !
	addwf	PCL,f

; main application
	goto	lDisp_Begin		; 0

; display init
	goto	lDisp_Init1		; 1
	goto	lDisp_Init2		; 2
	goto	lDisp_Init3		; 3
	goto	lDisp_Init4		; 4
	goto	lDisp_WelcomeString ; 5
	goto	lDisp_SetAddr	; 6

	goto	lDisp_Wait5sec	; 7
	goto	lDisp_ReturnHome ; 8
	goto	lDisp_DemoString ; 9
	goto	lDisp_SetAddr	; 10
	goto	lDisp_Idle		; 11

; transfer framebuffer to LCD
	goto	lDisp_WriteFB1	; 12
	goto	lDisp_ReturnHome ; 13
	goto	lDisp_WriteFB2	; 14
	goto	lDisp_SetAddr	; 15

; state machine switch
lDisp_ControlDecE
 	decf	vDisp_Ctrl,w
	goto	lDisp_ControlCtrlE
lDisp_ControlIncE
 	incf	vDisp_Ctrl,w
lDisp_ControlCtrlE
 	movwf	vDisp_Ctrl
lDisp_ControlE

	return
;--------------------------------------------------------------------------
;----
sDisp_GetInitString
	addwf	PCL,f
	retlw	' '
	retlw	'C'
	retlw	'a'
	retlw	'r'
	retlw	'M'
	retlw	'o'
	retlw	'n'
	retlw	' '
	retlw	'2'
	retlw	'0'
	retlw	'1'
	retlw	'5'
	retlw	'('
	retlw	'c'
	retlw	')'
	retlw	0
;----
sDisp_GetDemoString
	addwf	PCL,f
	retlw	' '
	retlw	'6'
	retlw	'8'
	retlw	'0'
	retlw	0xb2
	retlw	' '
	retlw	'5'
	retlw	'7'
	retlw	'0'
	retlw	0xb2
	retlw	' '
	retlw	'1'
	retlw	'0'
	retlw	'2'
	retlw	'%'
	retlw	0
;----
; initialize display module
sDisp_Ini
 GLOBAL	sDisp_Ini

	movlw	cDisp_Begin
	movwf	vDisp_Ctrl

	clrf	vDisp_Cmd

	return
;----
; writes instruction to display
; assumes that BUSY flag is inactive
;
sDisp_WriteInst
	movwf	vDisp_Tmp

	swapf	vDisp_Tmp,w
	andlw	0x0f
	movwf	PORT_DISP

	bsf		PORT_DISP,bDISP_E	; E _/-\_
	bcf		PORT_DISP,bDISP_E

	movf	vDisp_Tmp,w
	andlw	0x0f
	movwf	PORT_DISP

	bsf		PORT_DISP,bDISP_E
	bcf		PORT_DISP,bDISP_E

	return	
;----
; writes data to display
; assumes that BUSY flag is inactive
;
sDisp_WriteData
	movwf	vDisp_Tmp

	swapf	vDisp_Tmp,w
	andlw	0x0f
	iorlw	1 << bDISP_RS		; RS _/-
	movwf	PORT_DISP

	bsf		PORT_DISP,bDISP_E	; E _/-\_
	bcf		PORT_DISP,bDISP_E

	movf	vDisp_Tmp,w
	andlw	0x0f
	iorlw	1 << bDISP_RS		; RS --
	movwf	PORT_DISP

	bsf		PORT_DISP,bDISP_E
	bcf		PORT_DISP,bDISP_E

	bcf		PORT_DISP,bDISP_RS	; RS -\_

	return
;----
; reads 'busy' flag and address
;
sDisp_ReadStatus
	bsf		PORT_DISP,bDISP_RW	; RW _/-

	movlw	0x0f
	bsf		STATUS,RP0
	iorwf	PORT_DISP,f
	bcf		STATUS,RP0

	bsf		PORT_DISP,bDISP_E	; E _/-

	nop
 IF DISP_SIMUL == 1
	movlw	0
 ELSE
	swapf	PORT_DISP,w
 ENDIF
	andlw	0xf0
	movwf	vDisp_Tmp
	
	bcf		PORT_DISP,bDISP_E	; E -\_/-
	bsf		PORT_DISP,bDISP_E

	nop
 IF DISP_SIMUL == 1
	movlw	0
 ELSE
	movf	PORT_DISP,w
 ENDIF
	andlw	0x0f
	iorwf	vDisp_Tmp,f

	bcf		PORT_DISP,bDISP_E	; E  -\_
	bcf		PORT_DISP,bDISP_RW	; RW -\_

	movlw	0xf0
	bsf		STATUS,RP0
	andwf	PORT_DISP,f
	bcf		STATUS,RP0

	return	
;----
; Bin -> ASCII conversion
sDisp_Bin2ASCII
	andlw	0x0f
	addlw	0x06

	movwf	vDisp_Tmp

	movlw	0x30-0x06
	btfsc	STATUS,DC
	movlw	0x41-0x06-0x0A

	addwf	vDisp_Tmp,w

	return
;----
;--------------------------------------------------------------------------
; MAIN APPLICATION
;----
;
lDisp_Begin
 IF DISP_SIMUL == 1
	movlw	2
 ELSE
	movlw	250
 ENDIF

	movwf	vDisp_Timer

	goto	lDisp_ControlIncE	; lDisp_Init1
;----
; Initialization after power up
;----
; wait 25 ms
lDisp_Init1
	decfsz	vDisp_Timer,f
	goto	lDisp_ControlE

	goto	lDisp_ControlIncE	; lDisp_Init2

;----
lDisp_Init2
	call	sDisp_ReadStatus	; wait until display is ready
	btfsc	vDisp_Tmp,bDISP_BSY
	goto	lDisp_ControlE

; 'Function Set' instruction
;  DL = 0 - 4bit data mode
;  N = 0  - 1 line display mode
;  F = 0  - 5x8 dots format display mode
	movlw	0x20
	call	sDisp_WriteInst

	goto	lDisp_ControlIncE	; lDisp_Init3
;----
lDisp_Init3
	call	sDisp_ReadStatus	; wait until display is ready
	btfsc	vDisp_Tmp,bDISP_BSY
	goto	lDisp_ControlE

; 'Entry mode set' instruction
; I/D = 1 - increment DDRAM address
; S = 0   - do not shift display
	movlw	0x06
	call	sDisp_WriteInst

	goto	lDisp_ControlIncE	; lDisp_Init4
;----
lDisp_Init4
	call	sDisp_ReadStatus	; wait until display is ready
	btfsc	vDisp_Tmp,bDISP_BSY
	goto	lDisp_ControlE

; 'Display ON/OFF control' instruction
; D = 1 - display On
; C = 0 - cursor Off
; B = 0 - cursor blinking Off
	movlw	0x0c
	call	sDisp_WriteInst

	movlw	0
	movwf	vDisp_StrIndex

	goto	lDisp_ControlIncE	; lDisp_WelcomeString
;----
lDisp_WelcomeString
	call	sDisp_ReadStatus	; wait until display is ready
	btfsc	vDisp_Tmp,bDISP_BSY
	goto	lDisp_ControlE

; DDRAM address space is not contiguous, need account for that
	movf	vDisp_StrIndex,w
	call	sDisp_GetInitString

; string is null-terminated
; using 'iorlw' to test for zero, since it sets Z status flag
	iorlw	0
	btfsc	STATUS,Z
	goto	lDisp_WelcomeString1

	call	sDisp_WriteData

; move to the next character
	incf	vDisp_StrIndex,f

	movf	vDisp_StrIndex,w
	xorlw	cDISP_LCD_FB_HALF
	btfss	STATUS,Z			; adjust DDRAM address
	goto	lDisp_ControlE

	goto	lDisp_ControlIncE	; lDisp_SetAddr

lDisp_WelcomeString1
	movlw	200
	movwf	vDisp_Timer
	clrf	vDisp_Tmp

	movlw	cDisp_WriteFB1
	goto	lDisp_ControlCtrlE	; lDisp_WriteFB1
;----
; wait for ~5 seconds
lDisp_Wait5sec
	decfsz	vDisp_Tmp,f
	goto	lDisp_ControlE

	decfsz	vDisp_Timer,f
	goto	lDisp_ControlE

	goto	lDisp_ControlIncE	; lDisp_ReturnHome
;----
; Return cursor to Home position
lDisp_ReturnHome
	call	sDisp_ReadStatus	; wait until display is ready
	btfsc	vDisp_Tmp,bDISP_BSY
	goto	lDisp_ControlE

; 'Return Home' instruction
	movlw	0x02
	call	sDisp_WriteInst

	movlw	0
	movwf	vDisp_StrIndex

	goto	lDisp_ControlIncE
;----
lDisp_DemoString
	call	sDisp_ReadStatus	; wait until display is ready
	btfsc	vDisp_Tmp,bDISP_BSY
	goto	lDisp_ControlE

	movf	vDisp_StrIndex,w
	call	sDisp_GetDemoString

; string is null-terminated
; using 'iorlw' to test for zero, since it sets Z status flag
	iorlw	0
	btfsc	STATUS,Z
	goto	lDisp_DemoString1

	call	sDisp_WriteData

; move to the next character
	incf	vDisp_StrIndex,f

	movf	vDisp_StrIndex,w
	xorlw	cDISP_LCD_FB_HALF
	btfss	STATUS,Z			; adjust DDRAM address
	goto	lDisp_ControlE

	goto	lDisp_ControlIncE	; lDisp_SetAddr

lDisp_DemoString1
	movlw	cDisp_Idle
	goto	lDisp_ControlCtrlE	; lDisp_Idle
;----
lDisp_Idle
	goto	lDisp_ControlE
;----
; wait until master provides new data
lDisp_WriteFB1
	btfss	vDisp_Cmd,bDISP_CMD_FB_WRITE
	goto	lDisp_ControlE

	goto	lDisp_ControlIncE	; lDisp_ReturnHome
;----
; frame buffer populated with new data from master,
; transfer it to the LCD
lDisp_WriteFB2
	call	sDisp_ReadStatus	; wait until display is ready
	btfsc	vDisp_Tmp,bDISP_BSY
	goto	lDisp_ControlE

; get next character from frame buffer
	movlw	vDisp_FB
	addwf	vDisp_StrIndex,w
	movwf	FSR
	movf	INDF,w

	call	sDisp_WriteData		; write data to the display

; move to the next character
	incf	vDisp_StrIndex,f

; if last character has been written, we are done
	movf	vDisp_StrIndex,w
	xorlw	cDISP_FB_SIZE

	btfsc	STATUS,Z			; we are not done yet
	goto	lDisp_WriteFB21

; DDRAM address space is not contiguous, need account for that
	movf	vDisp_StrIndex,w
	xorlw	cDISP_LCD_FB_HALF
	btfss	STATUS,Z			; adjust DDRAM address
	goto	lDisp_ControlE

	goto	lDisp_ControlIncE	; lDisp_SetAddr

; we are done, wait for another packet from master
lDisp_WriteFB21
	bcf		vDisp_Cmd,bDISP_CMD_FB_WRITE

	movlw	cDisp_WriteFB1
	goto	lDisp_ControlCtrlE	; lDisp_WriteFB1
;----
; set DDRAM address
; if vDisp_StrIndex == 0, then set to 0, otherwise to 40
;
lDisp_SetAddr
	call	sDisp_ReadStatus	; wait until display is ready
	btfsc	vDisp_Tmp,bDISP_BSY
	goto	lDisp_ControlE

	movf	vDisp_StrIndex,w
	iorlw	0

	btfss	STATUS,Z
	movlw	40

; 'Set DDRAM address' instruction
	iorlw	0x80
	call	sDisp_WriteInst

	goto	lDisp_ControlDecE	; return to previous instruction
;----
	END
;----
