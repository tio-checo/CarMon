CC= /opt/mipsel/bin/mipsel-gcc
OBJCOPY= /opt/mipsel/bin/mipsel-objcopy
OBJDUMP= /opt/mipsel/bin/mipsel-objdump
NM= /opt/mipsel/bin/mipsel-nm

CFLAGS= -D__XC32 -D__32MX795F512H__ -D__PIC32_FEATURE_SET__=795 \
-DOBDCONV_BOARD -DTRANSPORT_LAYER_UART -Os -Wall -msoft-float \
-march=m4k -I. -I./Include
LDFLAGS= -msoft-float -march=m4k -nostartfiles

OBJFILES = BootLoader.o NVMem.o Uart.o Framework.o

APPNAME= bootloader

all: $(APPNAME).elf $(APPNAME).dis $(APPNAME).hex $(APPNAME).hdr \
    $(APPNAME).sym

$(APPNAME).elf: $(OBJFILES) crt0-mipsel.o
	$(CC) $(LDFLAGS) -o $@ $(OBJFILES) -T ld/elf32pic32mx-boot.x

$(APPNAME).dis: $(APPNAME).elf
	$(OBJDUMP) -D $? > $@

$(APPNAME).hdr: $(APPNAME).elf
	$(OBJDUMP) -h $? > $@

$(APPNAME).hex: $(APPNAME).elf
	$(OBJCOPY) -I elf32-little -O ihex --change-addresses -0xa0000000 $? $@

#	$(OBJCOPY) -I elf32-little -O ihex --change-addresses -0x80000000 \
#   --change-section-lma .reset=0x1fc00000 --change-section-lma \
#    .configsfrs=0x1fc02ff0 $? $@

$(APPNAME).sym: $(APPNAME).elf
	$(NM) $? > $@

crt0-mipsel.o: Source/crt0-mipsel.S
	$(CC) -c $(CFLAGS) $?

BootLoader.o: Source/BootLoader.c
	$(CC) -c $(CFLAGS) $?

NVMem.o: Source/NVMem.c
	$(CC) -c $(CFLAGS) $?

Uart.o: Source/Uart/Uart.c
	$(CC) -c $(CFLAGS) $?

Framework.o: Source/FrameWork/Framework.c
	$(CC) -c $(CFLAGS) $?

clean:
	$(RM) $(APPNAME).elf $(APPNAME).dis $(APPNAME).hex $(APPNAME).hdr \
$(APPNAME).sym $(OBJFILES) crt0-mipsel.o
