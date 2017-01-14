;
;--------------------------------------------------------------------------
; File: DISPLAY.INC
; Author: Jan Damborsky
; Created: 19.5.2015
; Last modified: 11.1.2017
; Changelog:
;
; Controls display according to data received from master.
;
; variables occupy RAM space 0x50-0x6f
;
;--------------------------------------------------------------------------
;

; global variables
vDisp_Ctrl		EQU 0x50 ; state machine control register
vDisp_Cmd		EQU 0x51 ; Command register
vDisp_Tmp		EQU 0x52 ; temporary register
vDisp_Timer		EQU 0x53 ; timer
vDisp_StrIndex	EQU 0x54 ; string index
vDisp_FB		EQU 0x58 ; 16-char frame buffer (0x58 - 0x67)
vDisp_Backlight	EQU 0x68 ; display backlight

cDISP_FB_SIZE	EQU 0x10 ; frame buffer size

; Command register bits
; If set to 1, frame buffer is transferred to LCD.
; Cleared after operation is done.
bDISP_CMD_FB_WRITE	EQU 0

; display port
PORT_DISP	EQU PORTC
bDISP_RS	EQU 4
bDISP_RW	EQU 6
bDISP_E		EQU 7

; Display status bits
bDISP_BSY	EQU 7		; 1 if display is currently busy

; LCD framebuffer is not contiguous - it is divided to two halves
cDISP_LCD_FB_HALF	EQU 0x08