// Copyright (c) 2002-2010,  Microchip Technology Inc.
//
// Microchip licenses this software to you solely for use with Microchip
// products.  The software is owned by Microchip and its licensors, and
// is protected under applicable copyright laws.  All rights reserved.
//
// SOFTWARE IS PROVIDED "AS IS."  MICROCHIP EXPRESSLY DISCLAIMS ANY
// WARRANTY OF ANY KIND, WHETHER EXPRESS OR IMPLIED, INCLUDING BUT
// NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.  IN NO EVENT SHALL
// MICROCHIP BE LIABLE FOR ANY INCIDENTAL, SPECIAL, INDIRECT OR
// CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA, HARM TO YOUR
// EQUIPMENT, COST OF PROCUREMENT OF SUBSTITUTE GOODS, TECHNOLOGY
// OR SERVICES, ANY CLAIMS BY THIRD PARTIES (INCLUDING BUT NOT LIMITED
// TO ANY DEFENSE THEREOF), ANY CLAIMS FOR INDEMNITY OR CONTRIBUTION,
// OR OTHER SIMILAR COSTS.
//
// To the fullest extent allowed by law, Microchip and its licensors
// liability shall not exceed the amount of fees, if any, that you
// have paid directly to Microchip to use this software.
//
// MICROCHIP PROVIDES THIS SOFTWARE CONDITIONALLY UPON YOUR ACCEPTANCE
// OF THESE TERMS.	


#ifndef __HARDWAREPROFILE_PIC32MX_OBDCONV_H__
#define __HARDWAREPROFILE_PIC32MX_OBDCONV_H__



    /*******************************************************************/
    /******** USB stack hardware selection options *********************/
    /*******************************************************************/
    //This section is the set of definitions required by the MCHPFSUSB
    //  framework.  These definitions tell the firmware what mode it is
    //  running in, and where it can find the results to some information
    //  that the stack needs.
    //These definitions are required by every application developed with
    //  this revision of the MCHPFSUSB framework.  Please review each
    //  option carefully and determine which options are desired/required
    //  for your application.

    //#define USE_SELF_POWER_SENSE_IO
    #define tris_self_power     TRISAbits.TRISA2    // Input
    #define self_power          1

    //#define USE_USB_BUS_SENSE_IO
    #define tris_usb_bus_sense  TRISBbits.TRISB5    // Input
    #define USB_BUS_SENSE       1 

    /*******************************************************************/
    /*******************************************************************/
    /*******************************************************************/
    /******** Application specific definitions *************************/
    /*******************************************************************/
    /*******************************************************************/
    /*******************************************************************/

    /** Board definition ***********************************************/
    //These defintions will tell the main() function which board is
    //  currently selected.  This will allow the application to add
    //  the correct configuration bits as well use the correct
    //  initialization functions for the board.  These defitions are only
    //  required for this demo.  They are not required in
    //  final application design.
	#define mLED              LATBbits.LATB12
	// Blinks LED 1
	#define BlinkLED() (mLED = ((ReadCoreTimer() & 0x0800000) != 0))
	#define InitLED() do { \
		AD1PCFG = 0xffff; \
		DDPCONbits.JTAGEN = 0; \
		TRISBbits.TRISB12 = 0; \
		TRISBbits.TRISB13 = 0; \
		LATBbits.LATB12 = 1; LATBbits.LATB13 = 0; \
		} while(0)
	
	// Switch S3 on USB Starter Kit.
	//#define ReadSwitchStatus() (PORTReadBits(IOPORT_D, BIT_13) & BIT_13)
        // Error indication.	
	#define Error()   do { \
		LATBbits.LATB12 = 0; \
		LATBbits.LATB13 = 0; \
		} while(0);

// Description: Macro used to enable the SD-SPI physical layer (SD-SPI.c and .h)
#define USE_SD_INTERFACE_WITH_SPI

/* SD Card definitions: Change these to fit your application when using
   an SD-card-based physical layer                                   */

#ifdef USE_SD_INTERFACE_WITH_SPI
    #if defined (__PIC32MX__)
        // Registers for the SPI module you want to use
        //#define MDD_USE_SPI_1
        #define MDD_USE_SPI_2

        //SPI Configuration
        #define SPI_START_CFG_1     (PRI_PRESCAL_64_1 | SEC_PRESCAL_8_1 | MASTER_ENABLE_ON | SPI_CKE_ON | SPI_SMP_ON)
        #define SPI_START_CFG_2     (SPI_ENABLE)

        // Define the SPI frequency
        #define SPI_FREQUENCY			(20000000)
//        #define SPI_FREQUENCY			(10000000)
    
        #if defined MDD_USE_SPI_1
            // Description: SD-SPI Chip Select Output bit
            #define SD_CS               LATBbits.LATB1
            // Description: SD-SPI Chip Select TRIS bit
            #define SD_CS_TRIS          TRISBbits.TRISB1
            
            // Description: SD-SPI Card Detect Input bit
            #define SD_CD               PORTDbits.RD8
            // Description: SD-SPI Card Detect TRIS bit
            #define SD_CD_TRIS          TRISDbits.TRISD8

            // Description: SD-SPI Write Protect Check Input bit
            #define SD_WE               PORTDbits.RD9
            // Description: SD-SPI Write Protect Check TRIS bit
            #define SD_WE_TRIS          TRISDbits.TRISD9
                   
            // Description: The main SPI control register
            #define SPICON1             SPI1CON
            // Description: The SPI status register
            #define SPISTAT             SPI1STAT
            // Description: The SPI Buffer
            #define SPIBUF              SPI1BUF
            // Description: The receive buffer full bit in the SPI status register
            #define SPISTAT_RBF         SPI1STATbits.SPIRBF
            // Description: The bitwise define for the SPI control register (i.e. _____bits)
            #define SPICON1bits         SPI1CONbits
            // Description: The bitwise define for the SPI status register (i.e. _____bits)
            #define SPISTATbits         SPI1STATbits
            // Description: The enable bit for the SPI module
            #define SPIENABLE           SPICON1bits.ON
            // Description: The definition for the SPI baud rate generator register (PIC32)
            #define SPIBRG			    SPI1BRG

            // Tris pins for SCK/SDI/SDO lines

            // Tris pins for SCK/SDI/SDO lines
            #if defined(__32MX460F512L__)
            	// Description: The TRIS bit for the SCK pin
            	#define SPICLOCK            TRISDbits.TRISD10
            	// Description: The TRIS bit for the SDI pin
            	#define SPIIN               TRISCbits.TRISC4
            	// Description: The TRIS bit for the SDO pin
            	#define SPIOUT              TRISDbits.TRISD0
           #else // example: PIC32MX360F512L
            	// Description: The TRIS bit for the SCK pin
            	#define SPICLOCK            TRISFbits.TRISF6
            	// Description: The TRIS bit for the SDI pin
            	#define SPIIN               TRISFbits.TRISF7
            	// Description: The TRIS bit for the SDO pin
            	#define SPIOUT              TRISFbits.TRISF8
            #endif /* __32MX460F512L__ */

            //SPI library functions
            #define putcSPI             putcSPI1
            #define getcSPI             getcSPI1
            #define OpenSPI(config1, config2)   OpenSPI1(config1, config2)
            
        #elif defined MDD_USE_SPI_2
            // Description: SD-SPI Chip Select Output bit
            #define SD_CS               LATGbits.LATG9
            // Description: SD-SPI Chip Select TRIS bit
            #define SD_CS_TRIS          TRISGbits.TRISG9

            // Description: SD-SPI Card Detect Input bits
            #define SD_CD1              PORTDbits.RD8
            // Description: SD-SPI Card Detect TRIS bit
            #define SD_CD1_TRIS         TRISDbits.TRISD8

            #define SD_CD2              PORTDbits.RD9
            // Description: SD-SPI Card Detect TRIS bit
            #define SD_CD2_TRIS         TRISDbits.TRISD9

            // Description: SD-SPI Write Protect Check Input bit
            #define SD_WE               0
            // Description: SD-SPI Write Protect Check TRIS bit
            //#define SD_WE_TRIS          TRISDbits.TRISD9

            // power on/off
            #define SD_PWR		LATBbits.LATB10
            #define SD_PWR_TRIS		TRISBbits.TRISB10
            
            // Description: The main SPI control register
            #define SPICON1             SPI2CON
            // Description: The SPI status register
            #define SPISTAT             SPI2STAT
            // Description: The SPI Buffer
            #define SPIBUF              SPI2BUF
            // Description: The receive buffer full bit in the SPI status register
            #define SPISTAT_RBF         SPI2STATbits.SPIRBF
            // Description: The bitwise define for the SPI control register (i.e. _____bits)
            #define SPICON1bits         SPI2CONbits
            // Description: The bitwise define for the SPI status register (i.e. _____bits)
            #define SPISTATbits         SPI2STATbits
            // Description: The enable bit for the SPI module
            #define SPIENABLE           SPI2CONbits.ON
            // Description: The definition for the SPI baud rate generator register (PIC32)
            #define SPIBRG              SPI2BRG

            // Tris pins for SCK/SDI/SDO lines

            // Description: The TRIS bit for the SCK pin
            #define SPICLOCK            TRISGbits.TRISG6
            // Description: The TRIS bit for the SDI pin
            #define SPIIN               TRISGbits.TRISG7
            // Description: The TRIS bit for the SDO pin
            #define SPIOUT              TRISGbits.TRISG8
            //SPI library functions
            #define putcSPI             putcSPI2
            #define getcSPI             getcSPI2
            #define OpenSPI(config1, config2)   OpenSPI2(config1, config2)
        #endif /* defined MDD_USE_SPI_2 */
        // Will generate an error if the clock speed is too low to interface to the card
        #if (GetSystemClock() < 100000)
            #error Clock speed must exceed 100 kHz
        #endif

/* Presence of SD card is detected in software way */
#define MEDIA_SOFT_DETECT	1

    #else
#error Only PIC32MX supported
    #endif /* __PIC32MX__ */
#endif /* USE_SD_INTERFACE_WITH_SPI */

#endif
