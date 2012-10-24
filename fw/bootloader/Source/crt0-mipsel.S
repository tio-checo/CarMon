/*
 * crt0-mipsel.S
 *
 * startup file for Microchip pic32mx CPUs containing MIPS4k core
 *
 * Copyright (c) 1995, 1996, 1997, 2001 Cygnus Support
 *
 * The authors hereby grant permission to use, copy, modify, distribute,
 * and license this software and its documentation for any purpose, provided
 * that existing copyright notices are retained in all copies and that this
 * notice is included verbatim in any distributions. No written agreement,
 * license, or royalty fee is required for any of the authorized uses.
 * Modifications to this software may be copyrighted by their authors
 * and need not follow the licensing terms described here, provided that
 * the new terms are clearly indicated on the first page of each file where
 * they apply.
 */

#ifdef __mips16
/* This file contains 32 bit assembly code.  */
	.set nomips16
#endif

#include "regs.S"

/*
 * Set up some room for a stack. We just grab a chunk of memory.
 */
#define STACK_SIZE  0x4000
#define GLOBAL_SIZE 0x2000

#define STARTUP_STACK_SIZE	0x0100

/* definition of configuration bits */
	.section .configsfrs,"aw",@progbits
/* DEVCFG3
 * 31:   FVBUSIO  = 1 - Vbuson pin is controlled by the USB module
 * 30:   FUSBIDIO = 1 - USBID pin is controlled by the USB module
 * 29:   FSCM1IO  = 1 - Default pin for SCM1C
 * 28-27:res          - reserved
 * 26:   FCANIO   = 1 - Default CAN I/O Pins
 * 25:   FETHIO   = 1 - Default Ethernet I/O Pins
 * 24:   FMIIEN   = 1 - MII enabled
 * 23-19:res          - reserved
 * 18-16:FSRSSEL<2:0> = 111 - Interrupt priority 7 assigned to
                              a Shadow Register Set.
 * 15-0: USERID<15:0> = 55aa - user defined value reachable via ICSP & JTAG
 */
	.word	0xffff55aa

/* DEVCFG2
 * 31-19:res          - reserved
 * 18-16:FPLLODIV<2:0> = 000 - 1:2 PLL postscaler
 * 15:   FUPLLEN = 0  - Disable USB PLL
 * 14-11:res          - reserved
 * 10-8: FUPLLIDIV<2:0> = 001 - USB PLL Input Divider - divide by 2
 * 7:    res          - reserved
 * 6-4:  FPLLMULT<2:0>  = 101 - Initial PLL Multiplier Value - 20x
 * 3:    res          - reserved
 * 2-0:  FPLLIDIV<2:0>  = 001 - PLL Input Divider - divide by 2
 */
/*	.word	0xfff8f9D9 FPLLMULT = x20 - Fosc=80MHz */
	.word	0xfff97989 /* FPLLMULT = x15 - Fosc=30MHz */


/* DEVCFG1
 * 31-24:res          - reserved
 * 23:   FWDTEN = 0   - WDT is not enabled. It can be enabled in software.
 * 22:   WINDIS = 1   - Windowed Watchdog Timer is disabled
 * 21:   res          - reserved
 * 20-16:WDTPS<4:0>  = 0xb - 1:1024 WDT Postscaler
 * 15-14:FCKSM<1:0>  = 00 - Clock switching is enabled,
 *                          fail-safe clock monitor is enabled
 * 13-12:FPBDIV<1:0> = 00 - PBCLK is SYSCLK divided by 1
 * 11:   res          - reserved
 * 10:   OSCIOFNC = 0 - CLKO output disabled
 * 
 * 9-8:  POSCMD<1:0> = 10 - HS Oscillator mode selected
 * 7:    IESO = 0     - Internal External Switch Over mode disabled
 *                      (Two-Speed Start-up disabled)
 * 6:    res          - reserved
 * 5:    FSOSCEN = 0  - Disable Secondary Oscillator
 * 4-3:  res          - reserved
 * 2-0:  FNOSC<2:0>  = 011 - Primary Oscillator (POSC) with PLL Module
 *                           (XT + PLL, HS + PLL, EC + PLL)
 */
	.word	0xff6b0a5b

/* DEVCFG0
 * 31:   SIGN = 0   - signature bit (automatically programmed to zero)
 * 30-29:res        - reserved
 * 28:   CP   = 1   - Code protection disabled
 * 27-25:res        - reserved
 * 24:   BWP  = 1   - Boot Flash is writable
 * 23-20:res        - reserved
 * 19-12:PWP<19:12> = 1 - Program Flash is writable
 * 11-4: res        - reserved
 * 3:    ICESEL = 1 - ICD used PGC2/PGD2 pins
 * 2:    res        - reserved
 * 1-0:  DEBUG<1:0> - Background Debugger disabled
 */
	.word	0x7fffffff

/* This is for referencing addresses that are not in the .sdata or
   .sbss section under embedded-pic, or before we've set up gp.  */
#ifdef __mips_embedded_pic
#  define LA(t,x) la t,x-PICBASE ; addu t,s0,t
#else /* __mips_embedded_pic */
# define LA(t,x) la t,x
#endif /* __mips_embedded_pic */

        .section .reset,"ax",@progbits

/* !HA!
	.comm	__memsize, 12
	.comm	__lstack, STARTUP_STACK_SIZE
	.comm	__stack, STARTUP_STACK_SIZE
*/

/*
 * This is important for an assembly programmer. This directive tells
 * the assembler not to optimize the order of the instructions as well as
 * not to insert 'nop' instructions after jumps and branches.
*/
	.set	noreorder

/*
 * Handling situation when device was woken up by Watchdog
 * from either IDLE or SLEEP mode. In that case, just 'eret' which will
 * resume program from instruction followin 'wait' which put device into
 * IDLE or SLEEP.
 *
 * Inspect RCON (0xbf80_f600) register to detect why we got here.
 * Do not clean status bits, let application take care of that,
 * so that application can inform user what happenned.
 *
 * Use k0,k1 registers, as they are reserved for kernel.
 */
	lui	k0, 0xbf81
	lw	k1, -2560(k0)
	andi	k0, k1, 0x10	/* NMI caused by Watchdog ? */

	beqz	k0, 1f		/* nope */
	nop

	andi	k0, k1, 0x0c	/* Wake from IDLE or SLEEP ? */
	beqz	k0, 1f		/* nope */
	nop

	eret			/* yep */
	nop

1:
	
/*
 * We are here, since one of regular RESETs occured, thus previous state
 * of peripheries and registers has been discarded.
 */

/* Initialize stack  grows down from top of RAM */
	la	sp, 0xa0020000-0x40

        la      k0, _start
        jr      k0                      # Jump to startup code
        nop

        .section .vector_0,"ax",@progbits
	j	isr_vector0
	nop

	.text

/* Without the following nop, GDB thinks _start is a data variable.
 * This is probably a bug in GDB in handling a symbol that is at the
 * start of the .text section.
 */
	nop

	.globl	hardware_hazard_hook .text
	.globl	_start
	.ent	_start
_start:
	.set	noreorder
#ifdef __mips_embedded_pic
#define PICBASE start_PICBASE
	PICBASE = .+8
        bal	PICBASE
	nop
	move	s0,$31
#endif

/* Fix high bits, if any, of the PC so that exception handling
   doesn't get confused.  */
	LA (v0, 3f)
	jr	v0
	nop
3:

	# set the global data pointer
	LA (gp, _gp)				

        ##################################################################
        # Initialize Global Pointer in Shadow Set
        #   The SRSCtl's PSS field must be set to the shadow set in which
        #   to initialize the global pointer.  Since we only have a
        #   single shadow set (besides the normal), we will initialize
        #   SRSCtl<PSS> to SRSCtl<HSS>.  We then write the global pointer
        #   to the previous shadow set to ensure that on interrupt, the
        #   global pointer has been initialized.
        ##################################################################

        mfc0    t1,C0_SRSCTL            # Read SRSCtl register
        add     t3,t1,zero              # Save off current SRSCtl
        ext     t2,t1,26,4              # to obtain HSS field
        ins     t1,t2,6,4               # Put HSS field
        mtc0    t1,C0_SRSCTL            # into SRSCtl<PSS>
        ehb                             # Clear hazard before using new SRSCTL
        wrpgpr  gp,gp                   # Set global pointer in PSS
        mtc0    t3,C0_SRSCTL            # Restore SRSCtl
	.end _start

/*
 * zero out the bss section.
 */
	.globl	__memsize
/* !HA!	.globl	get_mem_info .text */
	.globl	__stack
	.globl	__global
	.ent	zerobss
zerobss:
	LA (v0, _fbss)
	LA (v1, _end)
3:
	sw	zero,0(v0)
	bltu	v0,v1,3b
	addiu	v0,v0,4				# executed in delay slot

#if 0 /* !HA! */
	la	t0, __lstack			# make a small stack so we
	addiu	sp, t0, STARTUP_STACK_SIZE	# can run some C code
	la	a0, __memsize			# get the usable memory size
	jal	get_mem_info
	nop

	/* setup the stack pointer */
	LA (t0, __stack)			# is __stack set ?
	bne	t0,zero,4f
	nop

	/* NOTE: a0[0] contains the amount of memory available, and
	         not the last memory address. */
	la	a0, __memsize
	lw	t0,0(a0)			# last address of memory available
	la	t1,K0BASE			# cached kernel memory
	addu	t0,t0,t1			# get the end of memory address
	/* Allocate 32 bytes for the register parameters.  Allocate 16
	   bytes for a null argv and envp.  Round the result up to 64
	   bytes to preserve alignment.  */
	subu	t0,t0,64
4:
	move	sp,t0				# set stack pointer
#endif
	.end	zerobss

/*
 * Copy initialized data from program flash to data memory
 * src=_data_image_begin dst=_data_begin stop=_data_end
 */
        la      t0,_data_image_begin
        la      t1,_data_begin
        la      t2,_data_end
        b       _init_check
        nop

_init_data:
        lw      t3,(t0)
        sw      t3,(t1)
        addu    t0,4
        addu    t1,4
_init_check:
        bltu    t1,t2,_init_data
        nop


#if 0 /* !HA! */
/*
 * initialize target specific stuff. Only execute these
 * functions it they exist.
 */
	.globl	hardware_init_hook .text
	.globl	software_init_hook .text
#endif  /* !HA! */
	.type	_fini,@function
	.type	_init,@function

#if 0 /* !HA! */
	.globl	atexit .text
	.globl	exit .text
#endif  /* !HA! */
	.ent	init
init:
#if 0 /* !HA! */
	LA (t9, hardware_init_hook)		# init the hardware if needed
	beq	t9,zero,6f
	nop
	jal	t9
	nop
6:
	LA (t9, software_init_hook)		# init the hardware if needed
	beq	t9,zero,7f
	nop
	jal	t9
	nop
7:
	LA (a0, _fini)
	jal	atexit
	nop

#ifdef GCRT0
	.globl	_ftext
	.globl	_extext
	LA (a0, _ftext)
	LA (a1, _etext)
	jal	monstartup
	nop
#endif
	jal	_init				# run global constructors
	nop
#endif /* !HA! */


        ##################################################################
        # Initialize CP0 registers
        ##################################################################
        # Initialize Count register
        ##################################################################
        mtc0    zero,C0_COUNT

        ##################################################################
        # Initialize Compare register
        ##################################################################
        li      t2,-1
        mtc0    t2,C0_COMPARE

        ##################################################################
        # Initialize EBase register
        ##################################################################
        la      t1,_ebase_address
        mtc0    t1,C0_EBASE

        ##################################################################
        # Initialize IntCtl register
        ##################################################################
        la      t1,_vector_spacing
        li      t2,0                    # Clear t2 and
        ins     t2,t1,5,5               # shift value to VS field
        mtc0    t2,C0_INTCTL

        ##################################################################
        # Initialize CAUSE registers
        # - Enable counting of Count register <DC = 0>
        # - Use special exception vector <IV = 1>
        # - Clear pending software interrupts <IP1:IP0 = 0>
        ##################################################################
        li      t1,0x00800000
        mtc0    t1,C0_CAUSE

        ##################################################################
        # Initialize STATUS register
        # - Access to Coprocessor 0 not allowed in user mode <CU0 = 0>
        # - User mode uses configured endianness <RE = 0>
        # - Preserve Bootstrap Exception vectors <BEV>
        # - Preserve soft reset <SR> and non-maskable interrupt <NMI>
        # - CorExtend enabled based on whether CorExtend User Defined
        #   Instructions have been implemented <CEE = Config<UDI>>
        # - Disable any pending interrups <IM7..IM2 = 0, IM1..IM0 = 0>
        # - Disable hardware interrupts <IPL7:IPL2 = 0>
        # - Base mode is Kernel mode <UM = 0>
        # - Error level is normal <ERL = 0>
        # - Exception level is normal <EXL = 0>
        # - Interrupts are disabled <IE = 0>
        ##################################################################
        mfc0    t0,C0_CONFIG
        ext     t1,t0,22,1              # Extract UDI from Config register
        sll     t1,t1,17                # Move UDI to Status.CEE location
        mfc0    t0,C0_SR
        and     t0,t0,0x00580000        # Preserve SR, NMI, and BEV
        or      t0,t1,t0                # Include Status.CEE (from UDI)
        mtc0    t0,C0_SR

	addiu	a1,sp,32			# argv = sp + 32
	addiu	a2,sp,40			# envp = sp + 40
	sw	zero,(a1)			# argv[0] = NULL
	sw	zero,(a2)			# envp[0] = NULL

	jal	main				# call the program start function
	move	a0,zero				# set argc to 0

#if 0 /* !HA! */
	# fall through to the "exit" routine
	jal	exit				# call libc exit to run the G++
						# destructors
#endif
	move	a0,v0				# pass through the exit code
1:
	j	1b
	nop
	.end	init

 
/* Assume the PICBASE set up above is no longer valid below here.  */
#ifdef __mips_embedded_pic
#undef PICBASE
#endif
	
/*
 * _exit -- Exit from the application. Normally we cause a user trap
 *          to return to the ROM monitor for another run. NOTE: This is
 *	    the only other routine we provide in the crt0.o object, since
 *          it may be tied to the "_start" routine. It also allows
 *          executables that contain a complete world to be linked with
 *          just the crt0.o object.
 */
#if 0 /* !HA! */
	.globl	hardware_exit_hook .text
	.globl	_exit
	.ent _exit
_exit:
7:
#ifdef __mips_embedded_pic
	/* Need to reinit PICBASE, since we might be called via exit()
	   rather than via a return path which would restore old s0.  */
#define PICBASE exit_PICBASE
	PICBASE = .+8
	bal	PICBASE
	nop
	move	s0,$31
#endif
#ifdef GCRT0
	LA (t0, _mcleanup)
	jal	t0
	nop
#endif
	LA (t0, hardware_exit_hook)
	beq	t0,zero,1f
	nop
	jal	t0
	nop
1:

	# break instruction can cope with 0xfffff, but GAS limits the range:
	break	1023
	b	7b				# but loop back just in-case
	nop
	.end _exit
 
/* Assume the PICBASE set up above is no longer valid below here.  */
#ifdef __mips_embedded_pic
#undef PICBASE
#endif
#endif /* !HA! */

/* EOF crt0.S */
