CC= /opt/mipsel/bin/mipsel-gcc
OBJCOPY= /opt/mipsel/bin/mipsel-objcopy
OBJDUMP= /opt/mipsel/bin/mipsel-objdump
NM= /opt/mipsel/bin/mipsel-nm

# project directories containing header files
HEADER_DIRS= -I. -I./Include -I./Include/peripheral \
-I./Include/HardwareProfile/

# compiler flags
#  - code optimized for size
#  - soft float
CFLAGS= -D__32MX795F512H__ -D__PIC32_FEATURE_SET__=795 -DOBDCONV_BOARD \
-Os -Wall -msoft-float -march=m4k $(HEADER_DIRS)

# linker flags
#  - soft float
LDFLAGS= -msoft-float -march=m4k -nostartfiles

# application header files
HDRFILES= ./Include/rt_light.h ./Include/bt.h ./Include/kwp2k.h \
    ./Include/master_comm.h ./Include/mon.h

OBJFILES= main.o rt_light.o bt.o kwp2k.o master_comm.o mon.o data_acq.o

APPNAME= main

all: $(APPNAME).elf $(APPNAME).dis $(APPNAME).hex $(APPNAME).hdr \
    $(APPNAME).sym

$(APPNAME).elf: $(OBJFILES) crt0-mipsel-app.o
	$(CC) $(LDFLAGS) -o $@ $(OBJFILES) -T ld/elf32pic32mx-app.x

$(APPNAME).dis: $(APPNAME).elf
	$(OBJDUMP) -D $? > $@

$(APPNAME).hdr: $(APPNAME).elf
	$(OBJDUMP) -h $? > $@

$(APPNAME).hex: $(APPNAME).elf
	$(OBJCOPY) -I elf32-little -O ihex --change-addresses -0x80000000 $? $@

#	$(OBJCOPY) -I elf32-little -O ihex --change-addresses -0x80000000 \
#   --change-section-lma .reset=0x1fc00000 --change-section-lma \
#    .configsfrs=0x1fc02ff0 $? $@

$(APPNAME).sym: $(APPNAME).elf
	$(NM) $? > $@

crt0-mipsel-app.o: Source/crt0-mipsel-app.S
	$(CC) -c $(CFLAGS) $?

bt.o: Source/bt.c
	$(CC) -c $(CFLAGS) $?

data_acq.o: Source/data_acq.c
	$(CC) -c $(CFLAGS) $?

kwp2k.o: Source/kwp2k.c
	$(CC) -c $(CFLAGS) $?

master_comm.o: Source/master_comm.c
	$(CC) -c $(CFLAGS) $?

mon.o: Source/mon.c
	$(CC) -c $(CFLAGS) $?

rt_light.o: Source/rt_light.c
	$(CC) -c $(CFLAGS) $?

main.o: Source/main.c
	$(CC) -c $(CFLAGS) $?

clean:
	$(RM) $(APPNAME).elf $(APPNAME).dis $(APPNAME).hex $(APPNAME).hdr \
$(APPNAME).sym $(OBJFILES) crt0-mipsel-app.o
