;--------------------------------------------------------------------------
; File: PICHW.ASM
; Author: Jan Damborsky
; Created: 1.3.2016
; Last modified: 12.3.2016
; Changelog:
;
;--------------------------------------------------------------------------

; 1) definice registru po RESETU
; 2) inicializace po zapnuti
; 3) obsluha preruseni
;---
; Unused pins configured as inputs if weak pull-ups are available;
; outputs otherwise.
;
;---
ValueA	EQU	b'00100000'	; PA
;			  ||||||||--------- RA0 - debug pin (ICSPDAT)
;			  |||||||---------- RA1 - NC (ICSPCLK)
;			  ||||||----------- RA2 - CAN Rx
;			  |||||------------ RA3 - /MCLR
;			  ||||------------- RA4 - NC
;			  |||-------------- RA5 - +5V ON
;
ValueB	EQU	b'00000000'	; PB
;			  ||||||||--------- NA
;			  |||||||---------- NA
;			  ||||||----------- NA
;			  |||||------------ NA
;			  ||||------------- RB4 - I2C SDA
;			  |||-------------- RB5 - NC
;			  ||--------------- RB6 - I2C SCL
;			  |---------------- RB7 - +12V ON
;
ValueC	EQU	b'00000000'	; PC
;			  ||||||||--------- RC0 - NC
;			  |||||||---------- RC1 - NC
;			  ||||||----------- RC2 - NC
;			  |||||------------ RC3 - NC
;			  ||||------------- RC4 - NC
;			  |||-------------- RC5 - NC
;			  ||--------------- RC6 - NC
;			  |---------------- RC7 - NC
;
;----
; 1 - input, 0 - output
;----
DirA	EQU	b'00011110'	; PA
;			  ||||||||--------- RA0 - debug pin (ISPDAT)
;			  |||||||---------- RA1 - NC (ISPCLK)
;			  ||||||----------- RA2 - CAN Rx
;			  |||||------------ RA3 - /MCLR
;			  ||||------------- RA4 - NC
;			  |||-------------- RA5 - +5V ON
;
DirB	EQU	b'01111111'	; PB
;			  ||||||||--------- NA
;			  |||||||---------- NA
;			  ||||||----------- NA
;			  |||||------------ NA
;			  ||||------------- RB4 - I2C SDA (must be IN per datasheet)
;			  |||-------------- RB5 - NC
;			  ||--------------- RB6 - I2C SCL (must be IN per datasheet)
;			  |---------------- RB7 - +12V ON
;
DirC	EQU	b'01000000'	; PC
;			  ||||||||--------- RC0 - NC
;			  |||||||---------- RC1 - NC
;			  ||||||----------- RC2 - NC
;			  |||||------------ RC3 - NC
;			  ||||------------- RC4 - NC
;			  |||-------------- RC5 - NC
;			  ||--------------- RC6 - NC
;			  |---------------- RC7 - NC
;
;---- inicializacni hodnoty ridicich registru
OSCCON_INI	EQU b'00010000'	; OSCCON - oscillator control register
;		    	  ||||||||--------- SCS - clock source defined by CONFIG register
;			      |||||||---------- LTS - LFINTOSC stable bit (RO)
;			      ||||||----------- HTS - HFINTOSC stable bit (RO)
;			      |||||------------ OSTS - Oscillator start-up time-out status (RO)
;		    	  ||||------------- IRCF2:0 - Internal oscillator freq set to 125kHz
;			      |---------------- NA

INTCON_INI	EQU b'00000000'	; INTCON - interrupt control register
;		    	  ||||||||--------- RABIF - RB Change port flag
;			      |||||||---------- INTF  - INT0 flag
;			      ||||||----------- T0IF  - TMR0 overflow flag
;			      |||||------------ RABIE - RB Change port disabled
;		    	  ||||------------- INTE  - INT0 disabled
;			      |||-------------- T0IE  - TMR0 interrupt disabled
;			      ||--------------- PEIE  - peripheral interrupts disabled
;			      |---------------- GIE   - all interrupts disabled

OPTION_INI	EQU b'00001000'	; OPTION register
;		    	  ||||||||--------- PS2:PS0 - prescaler - WDT 1:1, TIMER0 1:2 
;			      |||||------------ PSA     - prescaler assigned to WDT
;			      ||||------------- T0SE    - TMR0 source edge select
;			      |||-------------- T0CS    - TMR0 driven by Fosc/4
;		    	  ||--------------- INTEDG  - INT pin interrupts on falling edge
;			      |---------------- /RABPU  - pull-ups on PORTA & PORTB enabled

WPUA_INI	EQU b'00110111'	; WPUA register - PORTA weak pull-ups
;				  ||||||||--------- RA0 - NC (ICSPDAT)
;				  |||||||---------- RA1 - NC (ICSPCLK)
;				  ||||||----------- RA2 - CAN Rx
;				  |||||------------ RA3 - /MCLR (weak pull-up not available)
;				  ||||------------- RA4 - NC
;				  |||-------------- RA5 - +5V ON

IOCA_INI	EQU b'00000000'	; IOCA register - PORTA interrupt-on-change
;				  ||||||||--------- RA0 - NC - no
;				  |||||||---------- RA1 - NC - no
;				  ||||||----------- RA2 - CAN Rx - no
;				  |||||------------ RA3 - /MCLR - no
;				  ||||------------- RA4 - NC - no
;				  |||-------------- RA5 - +5V ON

WPUB_INI	EQU b'11110000'	; WPUB register - PORTB weak pull-ups
;				  ||||||||--------- NA
;				  |||||||---------- NA
;				  ||||||----------- NA
;				  |||||------------ NA
;				  ||||------------- RB4 - I2C SDA
;				  |||-------------- RB5 - NC
;				  ||--------------- RB6 - I2C SCL
;				  |---------------- RB7 - +12V ON
;
IOCB_INI	EQU b'00000000'	; IOCB register - PORTB interrupt-on-change
;				  ||||||||--------- NA
;				  |||||||---------- NA
;				  ||||||----------- NA
;				  |||||------------ NA
;				  ||||------------- RB4 - I2C SDA - no
;				  |||-------------- RB5 - NC - no
;				  ||--------------- RB6 - I2C SCL - no
;				  |---------------- RB7 - +12V ON - no
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
SSPADD_INI	EQU (0x32 << 1)	; I2C address of this device
;---

T2CON_INI	EQU	b'00000000'
;		    	  ||||||||--------- T2CKPS1:0 - 1:1 prescaler
;			      ||||||----------- TMR2ON    - TMR2 OFF
;			      |||||------------ TOUTPS3:0 - 1:1 postscaler
;			      |---------------- unused

;---
T1CON_INI	EQU b'00010000'	; inicializace TIMER1
;				  ||||||||--------- TMR1ON		- TMR1 OFF
;				  |||||||---------- TMR1CS		- clock Fosc/4
;				  ||||||----------- T1SYNC		- ignored
;				  |||||------------ T1OSCEN		- oscilator OFF
;				  ||||------------- T1CKPS1:0	- PS 1:2
;				  ||--------------- unimplemented
;---
ANSEL_INI	EQU b'00000000'	; ANSEL init value (1 for analog pin)
;			      ||||||||--------- AN0/RA0 - NC (ICSPDAT)
;			      |||||||---------- AN1/RA1 - NC (ICSPCLK)
;			      ||||||----------- AN2/RA2 - CAN Rx
;			      |||||------------ AN3/RA4 - NC
;			      ||||------------- AN4/RC0 - NC
;			      |||-------------- AN5/RC1 - NC
;			      ||--------------- AN6/RC2 - NC
;			      |---------------- AN7/RC3 - NC
;---
ADCON0_INI	EQU b'00001000'	; ADCON0 init value
;				  ||||||||--------- ADON	- ADC disabled
;				  |||||||---------- GO/DONE	- AD conversion not started
;				  ||||||----------- CHS3:0	- AN2 measured
;				  ||--------------- VCFG	- Vref connected to Vdd
;				  |---------------- ADFM	- conversion result left justified
;---
ADCON1_INI	EQU b'01100000'	; ADCON1 init value
;				  ||||||||--------- bit3:0	- not implemented
;				  ||||------------- ADCS2:0	- ADC clock set to Fosc/64
;				  |---------------- bit		- not implemented
;---
FOSC		EQU 125		; Fosc in kHz
;TAD			EQU	16		; Tad in useconds
TMR0_CLK	EQU	32		; TMR0clk in us
MAIN_LOOP_TIME	EQU	4000	; period of main loop in us
TMR0_PRELOAD	EQU (262 - MAIN_LOOP_TIME / TMR0_CLK)
cLED_TICKS		EQU (1000000 / (MAIN_LOOP_TIME * 2))

;
PORT_LED	EQU PORTA	; LED port
bLED		EQU 0		; LED pin
PORT_BL		EQU PORTB	; bootloader select pin
bBL			EQU 4
;------------------------------------------------------