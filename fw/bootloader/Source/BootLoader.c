/*********************************************************************
 *
 *                  PIC32 Boot Loader
 *
 *********************************************************************
 * FileName:        Bootloader.c
 * Dependencies:
 * Processor:       PIC32
 *
 * Compiler:        MPLAB C32
 *                  MPLAB IDE
 * Company:         Microchip Technology, Inc.
 *
 * Software License Agreement
 *
 * The software supplied herewith by Microchip Technology Incorporated
 * (the �Company�) for its PIC32 Microcontroller is intended
 * and supplied to you, the Company�s customer, for use solely and
 * exclusively on Microchip PIC32 Microcontroller products.
 * The software is owned by the Company and/or its supplier, and is
 * protected under applicable copyright laws. All rights are reserved.
 * Any use in violation of the foregoing restrictions may subject the
 * user to criminal sanctions under applicable laws, as well as to
 * civil liability for the breach of the terms and conditions of this
 * license.
 *
 * THIS SOFTWARE IS PROVIDED IN AN �AS IS� CONDITION. NO WARRANTIES,
 * WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING, BUT NOT LIMITED
 * TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE APPLY TO THIS SOFTWARE. THE COMPANY SHALL NOT,
 * IN ANY CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL OR
 * CONSEQUENTIAL DAMAGES, FOR ANY REASON WHATSOEVER.
 *
 *
 * $Id:  $
 * $Name: $
 *
 **********************************************************************/
#include "Include/GenericTypeDefs.h"
#include "Include/HardwareProfile/HardwareProfile.h"
#include "Include/BootLoader.h"
#include "Include/Framework/Framework.h"
#include <stdlib.h>
#include <plib.h>

void PutChar(UINT8 txChar);

/*********************************************************************
 * Function:        isr_vector0()
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    Interrupts are disabled
 *
 * Overview:        VECTOR0 Interrupt Service Routine
 *
 ********************************************************************/
//extern void isr_vector0(void) __attribute__ ((interrupt, use_shadow_register_set));
//extern void isr_vector0(void) __attribute__ ((interrupt, keep_interrupts_masked));
//extern void isr_vector0(void) __attribute__ ((interrupt));


void __attribute__ ((interrupt)) isr_vector0(void)
{
	static volatile int i = 123;

        i++;

	return;
}

/*
void __attribute__ ((naked)) isr_vector0(void)
{
	static volatile int i = 123;

        i++;

	return;
}
*/

/*********************************************************************
 * Function:        int_vector0()
 *
 * PreCondition:    None

 *
 * Input:           None
 *
 * Output:          None

 *
 * Side Effects:
 *
 * Overview:        VECTOR0 Interrupt
 *
 ********************************************************************/
/*
extern void int_vector0(void) __attribute__ ((section (".vector_0")));

void int_vector0(void)
{
	isr_vector0();
    asm volatile("j    isr_vector0");
}
*/

/*********************************************************************
 * Function:        mINTDisableInterrupts()
 *
 * PreCondition:    None
 *
 * Input:           None
 *
 * Output:          None
 *
 * Side Effects:    Interrupts are disabled
 *
 * Overview:        Interrupts are disabled by clearing the IE bit
 *                  in the status register
 ********************************************************************/
unsigned int __attribute__((nomips16)) INTDisableInterrupts(void)
{
    unsigned int status = 0;

    asm volatile("di    %0" : "=r"(status));

    return status;
}

/*********************************************************************
 * Function:        INTRestoreInterrupts(unsigned int status)
 *
 * PreCondition:    None
 *
 * Input:           value of the status registor
 *
 * Output:
 *
 * Side Effects:    Interrupts are restored to previous state
 *
 * Overview:        Interrupts are enabled by setting the IE bit
 *                  in the status register
 ********************************************************************/
void __attribute__((nomips16))  INTRestoreInterrupts(unsigned int status)
{
    if(status & 0x00000001)
        asm volatile("ei");
    else
        asm volatile("di");
}

/******************************************************************************
 * Function:        unsigned int ReadCoreTimer(void)
 *
 * Description:		Returns the current Core Timer value.
 *
 * PreCondition:    None
 *
 * Inputs:			None
 *
 * Output:          Count value.
 *
 * Example:			ReadCoreTimer()
 *
 *****************************************************************************/
unsigned int __attribute__((nomips16)) ReadCoreTimer(void)
{
    unsigned int timer;

    // get the count reg
    asm volatile("mfc0   %0, $9" : "=r"(timer));

    return timer;
}

/******************************************************************************
 * Function:        void WriteCoreTimer(unsigned int value)
 *
 * Description:		Loads 32-bit value into CoreTimer.
 *
 * PreCondition:    None
 *
 * Inputs:			value: The value to be loaded.
 *
 * Output:          None
 *
 * Example:			WriteCoreTimer(0x1234)
 *
 *****************************************************************************/
void __attribute__((nomips16)) WriteCoreTimer(unsigned int timer)
{
    /* get the count reg */
    asm volatile("mtc0   %0, $9": "+r"(timer));

}

/*********************************************************************
 * Function:        void cheKseg0CacheOn(void)
 *
 * PreCondition:    None
 * Input:           None
 * Output:          none
 * Side Effects:    Sets the CCA field in the Config register of Co-
 * 					processor 0 to the value "011"b
 * Overview:        This routine is used to enable cacheability of KSEG0.
 *
 * Note:
 *
 ********************************************************************/
void __attribute__ ((nomips16)) CheKseg0CacheOn()
{
	register unsigned long tmp;
	asm("mfc0 %0,$16,0" :  "=r"(tmp));
	tmp = (tmp & ~7) | 3;
	asm("mtc0 %0,$16,0" :: "r" (tmp));
}

int puts(const char *str)
{
	const char	*c = str;

	while (*c != '\0') {
		PutChar(*c++);
	}

	return 0;
}

// Configuring the Device Configuration Registers
// 80Mhz Core/Periph, Pri Osc w/PLL, Write protect Boot Flash
#if 0 /* !HA! */
#if defined(TRANSPORT_LAYER_USB) || defined(TRANSPORT_LAYER_USB_HOST)
    #pragma config UPLLEN   = ON        // USB PLL Enabled
    #pragma config UPLLIDIV = DIV_2     // USB PLL Input Divider = Divide by 2
#endif

#pragma config DEBUG    = OFF           // Background Debugger disabled
#pragma config FPLLMUL = MUL_20         // PLL Multiplier: Multiply by 20
#pragma config FPLLIDIV = DIV_2         // PLL Input Divider:  Divide by 2

#if defined(__PIC32MX1XX_2XX__)
    // For PIC32MX1xx, PIC32MX2xx devices the output divisor is set to 2 to produce max 40MHz clock.
    #pragma config FPLLODIV = DIV_2         // PLL Output Divider: Divide by 2
#elif defined (__PIC32MX3XX_7XX__)
    //For PIC32MX3xx, PIC32MX4xx, PIC32MX5xx, PIC32MX6xx and PIC32MX7xx devices, 
    //the output divisor is set to 1 to produce max 80MHz clock.
    #pragma config FPLLODIV = DIV_1         // PLL Output Divider: Divide by 1
#endif

#pragma config FWDTEN = OFF             // WD timer: OFF
#pragma config POSCMOD = HS             // Primary Oscillator Mode: High Speed xtal
#pragma config FNOSC = PRIPLL           // Oscillator Selection: Primary oscillator  w/ PLL
#pragma config FPBDIV = DIV_1           // Peripheral Bus Clock: Divide by 1
#pragma config BWP = OFF                // Boot write protect: OFF

#if defined(__PIC32MX1XX_2XX__)
    // For PIC32MX1xx, PIC32MX2xx devices there are jumpers on PIM to choose from PGx1/PGx2.
    #pragma config ICESEL = ICS_PGx1    // ICE pins configured on PGx1 (PGx2 is multiplexed with USB D+ and D- pins).
#elif defined(__PIC32MX3XX_7XX__)
    // For PIC32MX3xx, PIC32MX4xx, PIC32MX5xx, PIC32MX6xx and PIC32MX7xx 
    // devices the ICE connection is on PGx2. .
    #pragma config ICESEL = ICS_PGx2    // ICE pins configured on PGx2, Boot write protect OFF.
#endif
    
#if defined(TRANSPORT_LAYER_ETH)
	#pragma config FMIIEN = OFF, FETHIO = OFF	// external PHY in RMII/alternate configuration. Applicable for devices with internal MAC only.
#endif
#endif /* !HA! */

#define SWITCH_PRESSED 0

BOOL CheckTrigger(void);
void JumpToApp(void);
BOOL ValidAppPresent(void);
/********************************************************************
* Function: 	main()
*
* Precondition: 
*
* Input: 		None.
*
* Output:		None.
*
* Side Effects:	None.
*
* Overview: 	Main entry function. If there is a trigger or 
*				if there is no valid application, the device 
*				stays in firmware upgrade mode.
*
*			
* Note:		 	None.
********************************************************************/
INT main(void)
{
	UINT	pbClk;

	// Setup configuration
	pbClk = SYSTEMConfig(SYS_FREQ, SYS_CFG_WAIT_STATES | SYS_CFG_PCACHE);
	
	InitLED();

	LATBbits.LATB12 = 1;
	LATBbits.LATB13 = 1;

	/*
	 * Enter firmware upgrade mode if there is a trigger
	 * or if the application is not valid.
	 */
	while (CheckTrigger() || !ValidAppPresent()) {
		/*
		 * Initialize the transport layer
		 *  - UART/USB/Ethernet
		 */
		TRANS_LAYER_Init(pbClk);

		/*
		 * Loop untill framework recieves "run application"
		 * command from PC.
		 */
		while (!FRAMEWORK_ExitFirmwareUpgradeMode()) {
			// Enter firmware upgrade mode.
			// Be in loop, looking for commands from PC
			// Run Transport layer tasks
			TRANS_LAYER_Task();

			// Run frame work related tasks
			// (Handling Rx frame, process frame and so on)
			FRAMEWORK_FrameWorkTask();

			// Blink LED (Indicates the user that
			// bootloader is running).
			BlinkLED();	
		}

		// Close transport layer.
		TRANS_LAYER_Close();

		/*
		 * If there is a valid application, run it.
		 */
		if (ValidAppPresent())
			break;
	}

	// Run application.
	JumpToApp();
	
	return 0;
}			


/********************************************************************
* Function: 	CheckTrigger()
*
* Precondition: 
*
* Input: 		None.
*
* Output:		TRUE: If triggered
				FALSE: No trigger
*
* Side Effects:	None.
*
* Overview: 	Checks if there is a trigger to enter 
				firmware upgrade mode.
*
*			
* Note:		 	None.
********************************************************************/
BOOL
CheckTrigger(void)
{
	return (ReadSwitchStatus() == SWITCH_PRESSED);
}	


/********************************************************************
* Function: 	JumpToApp()
*
* Precondition: 
*
* Input: 		None.
*
* Output:		
*
* Side Effects:	No return from here.
*
* Overview: 	Jumps to application.
*
*			
* Note:		 	None.
********************************************************************/
void JumpToApp(void)
{	
	void (*fptr)(void);
	fptr = (void (*)(void))USER_APP_RESET_ADDRESS;
	fptr();
}	


/********************************************************************
* Function: 	ValidAppPresent()
*
* Precondition: 
*
* Input: 		None.
*
* Output:		TRUE: If application is valid.
*
* Side Effects:	None.
*
* Overview: 	Logic: Check application vector has 
				some value other than "0xFFFFFFFF"
*
*			
* Note:		 	None.
********************************************************************/
BOOL
ValidAppPresent(void)
{
	DWORD *AppPtr;
	AppPtr = (DWORD *)USER_APP_RESET_ADDRESS;
	return (*AppPtr != 0xFFFFFFFF);
}			


/*********************End of File************************************/


