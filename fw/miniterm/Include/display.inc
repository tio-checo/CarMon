;
;--------------------------------------------------------------------------
; File: DISPLAY.INC
; Author: Jan Damborsky
; Created: 19.5.2015
; Last modified: 29.10.2023
; Changelog:
;  - v0.2
;    - added support for 16x2 alphanumeric LCD with Hitachi HD44780U controller
;
; Controls display according to data received from master.
;
; variables occupy RAM space 0x50-0x78
;
;--------------------------------------------------------------------------
;
; Display type
; 0 - 1x16
; 1 - 2x16 Hitachi HD44780 controller
DISP_CONTROLLER    EQU 1
; global variables
vDisp_Ctrl		EQU 0x50 ; state machine control register
vDisp_Cmd		EQU 0x51 ; Command register
vDisp_Tmp		EQU 0x52 ; temporary register
vDisp_Timer		EQU 0x53 ; timer
vDisp_StrIndex		EQU 0x54 ; string index

 IF DISP_CONTROLLER == 0
vDisp_FB		EQU 0x58 ; 16-char frame buffer (0x58 - 0x67)
vDisp_Backlight		EQU 0x68 ; display backlight
cDISP_FB_SIZE		EQU 0x10 ; frame buffer size
 ELSE
vDisp_FB		EQU 0x58 ; 32-char frame buffer (0x58 - 0x77)
vDisp_Backlight		EQU 0x78 ; display backlight
cDISP_FB_SIZE		EQU 0x20 ; frame buffer size
 ENDIF

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
 IF DISP_CONTROLLER == 0
cDISP_LCD_FB_HALF	EQU 8
 ELSE
cDISP_LCD_FB_HALF	EQU 16
 ENDIF
;----