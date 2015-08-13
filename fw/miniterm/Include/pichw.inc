;--------------------------------------------------------------------------
; File: PICHW.ASM
; Author: Jan Damborsky
; Created: 19.5.2015
; Last modified: 12.8.2015
; Changelog:
;
;--------------------------------------------------------------------------

; 1) definice registru po RESETU
; 2) inicializace po zapnuti
; 3) obsluha preruseni
;---
; obsah portu po RESETU
;---
ValueA	EQU	b'00000000'	; PA
;			  ||||||||--------- RA0 - Rotary switch CHA
;			  |||||||---------- RA1 - Rotary switch CHB
;			  ||||||----------- RA2 - AN2 (environment brightness)
;			  |||||------------ RA3 - Rotary switch S1
;			  ||||------------- RA4 - NC
;			  |||-------------- RA5 - NC
;
ValueB	EQU	b'00000000'	; PB
;			  ||||||||--------- NA
;			  |||||||---------- NA
;			  ||||||----------- NA
;			  |||||------------ NA
;			  ||||------------- RB4 - I2C SDA
;			  |||-------------- RB5 - NC
;			  ||--------------- RB6 - I2C SCL
;			  |---------------- RB7 - debug pin
;
ValueC	EQU	b'00000000'	; PC
;			  ||||||||--------- RC0 - LCD DB4
;			  |||||||---------- RC1 - LCD DB5
;			  ||||||----------- RC2 - LCD DB6
;			  |||||------------ RC3 - LCD DB7
;			  ||||------------- RC4 - LCD RS
;			  |||-------------- RC5 - PWM (LCD backlight)
;			  ||--------------- RC6 - LCD R/W
;			  |---------------- RC7 - LCD E
;
;----
; 1 - input, 0 - output
;----
DirA	EQU	b'00001011'	; PA
;			  ||||||||--------- RA0 - Rotary switch CHA
;			  |||||||---------- RA1 - Rotary switch CHB
;			  ||||||----------- RA2 - AN2 (environment brightness)
;			  |||||------------ RA3 - Rotary switch S1
;			  ||||------------- RA4 - NC
;			  |||-------------- RA5 - NC
;
DirB	EQU	b'01011111'	; PB
;			  ||||||||--------- NA
;			  |||||||---------- NA
;			  ||||||----------- NA
;			  |||||------------ NA
;			  ||||------------- RB4 - I2C SDA (must be IN per datasheet)
;			  |||-------------- RB5 - NC
;			  ||--------------- RB6 - I2C SCL (must be IN per datasheet)
;			  |---------------- RB7 - debug pin
;
DirC	EQU	b'00000000'	; PC
;			  ||||||||--------- RC0 - LCD DB4
;			  |||||||---------- RC1 - LCD DB5
;			  ||||||----------- RC2 - LCD DB6
;			  |||||------------ RC3 - LCD DB7
;			  ||||------------- RC4 - LCD RS
;			  |||-------------- RC5 - PWM (LCD backlight)
;			  ||--------------- RC6 - LCD R/W
;			  |---------------- RC7 - LCD E
;
;---- inicializacni hodnoty ridicich registru
INTCON_INI	EQU b'00000000'	; INTCON - interrupt control register
;		    	  ||||||||--------- RABIF - RB Change port flag
;			      |||||||---------- INTF  - INT0 flag
;			      ||||||----------- T0IF  - TMR0 overflow flag
;			      |||||------------ RABIE - RB Change port disabled
;		    	  ||||------------- INTE  - INT0 disabled
;			      |||-------------- T0IE  - TMR0 interrupt disabled
;			      ||--------------- PEIE  - peripheral interrupts disabled
;			      |---------------- GIE   - all interrupts disabled

OPTION_INI	EQU b'01001000'	; OPTION register
;		    	  ||||||||--------- PS2:PS0 - prescaler - WDT 1:1, TIMER0 1:2 
;			      |||||------------ PSA     - prescaler assigned to WDT
;			      ||||------------- T0SE    - TMR0 source edge select
;			      |||-------------- T0CS    - TMR0 driven by Fosc/4
;		    	  ||--------------- INTEDG  - INT pin interrupts on rising edge
;			      |---------------- /RABPU  - pull-ups on PORTA & PORTB enabled

WPUA_INI	EQU b'00110111'	; WPUA register - PORTA weak pull-ups
;				  ||||||||--------- RA0 - Rotary switch CHA
;				  |||||||---------- RA1 - Rotary switch CHB
;				  ||||||----------- RA2 - AN2 (environment brightness)
;				  |||||------------ RA3 - Rotary switch S1 - external pull-up
;				  ||||------------- RA4 - NC
;				  |||-------------- RA5 - NC

IOCA_INI	EQU b'00000011'	; IOCA register - PORTA interrupt-on-change
;				  ||||||||--------- RA0 - Rotary switch CHA - yes
;				  |||||||---------- RA1 - Rotary switch CHB - yes
;				  ||||||----------- RA2 - AN2 (environment brightness) - no
;				  |||||------------ RA3 - Rotary switch S1 - no
;				  ||||------------- RA4 - NC - no
;				  |||-------------- RA5 - NC - no

WPUB_INI	EQU b'11110000'	; WPUB register - PORTB weak pull-ups
;				  ||||||||--------- NA
;				  |||||||---------- NA
;				  ||||||----------- NA
;				  |||||------------ NA
;				  ||||------------- RB4 - I2C SDA
;				  |||-------------- RB5 - NC
;				  ||--------------- RB6 - I2C SCL
;				  |---------------- RB7 - debug pin
;
IOCB_INI	EQU b'00000000'	; IOCB register - PORTB interrupt-on-change
;				  ||||||||--------- NA
;				  |||||||---------- NA
;				  ||||||----------- NA
;				  |||||------------ NA
;				  ||||------------- RB4 - I2C SDA - no
;				  |||-------------- RB5 - NC - no
;				  ||--------------- RB6 - I2C SCL - no
;				  |---------------- RB7 - debug pin - no
;
;---
; SSP module operates in I2C bus slave mode
;
SSPCON_INI	EQU b'00110110'
;				  ||||||||--------- SSPM3:0 - I2C slave mode, 7-bit address
;				  ||||------------- CKP     - Enable clock
;				  |||-------------- SSPEN   - Enable I2C mode
;				  ||--------------- SSPOV   - clear SSPOV
;				  |---------------- WCOL    - NA
;
SSPADD_INI	EQU (0x31 << 1)	; I2C address of this device
;---
; PWM controlling LCD brightness
PR2_INI		EQU	0xff
CCPR1L_INI	EQU	0x80
CCP1CON_INI	EQU	b'00001100'
;		    	  ||||||||--------- CCP1M3:0 - PWM mode
;			      ||||------------- DC1B1:0  - two LSBs of the PWM duty cycle
;		    	  ||--------------- P1M1:0   - PWM single output, P1A modulated

T2CON_INI	EQU	b'00000100'
;		    	  ||||||||--------- T2CKPS1:0 - 1:1 prescaler
;			      ||||||----------- TMR2ON    - TMR2 on
;			      |||||------------ TOUTPS3:0 - 1:1 postscaler
;			      |---------------- unused

;---
T1CON_INI EQU	b'00010001'	; inicializace TIMER1
;				  ||||||||--------- TMR1ON - TMR1 ON
;				  |||||||---------- TMR1CS - clock Fosc/4
;				  ||||||----------- T1SYNC - ignored
;				  |||||------------ T1OSCEN - oscilator OFF
;				  ||||------------- T1CKPS1:0  - PS 1:2
;				  ||--------------- unimplemented
;---
FOSC		EQU 4		; Fosc in MHz
TMR0_CLK	EQU	1		; TMR0clk in us
MAIN_LOOP_TIME	EQU	100	; period of main loop in us
TMR0_PRELOAD	EQU (262 - MAIN_LOOP_TIME)

;
PORT_DEBUG	EQU PORTB	; debug port
bDEBUG		EQU 7		; debug pin
;------------------------------------------------------