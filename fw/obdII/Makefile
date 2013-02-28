TOOLS_DIR= /opt/mipsel
TOOLS_BIN= $(TOOLS_DIR)/bin

CC= $(TOOLS_BIN)/mipsel-gcc
OBJCOPY= $(TOOLS_BIN)/mipsel-objcopy
OBJDUMP= $(TOOLS_BIN)/mipsel-objdump
NM= $(TOOLS_BIN)/mipsel-nm

# build directory
BUILD_DIR= build

# project directories containing header files
HEADER_DIRS= -I. -I./Include -I./Include/peripheral \
-I./Include/HardwareProfile/ -I./Source/ -I $(TOOLS_DIR)/include/microchip

# compiler flags
#  - code optimized for size
#  - soft float
CFLAGS= -D__32MX795F512H__ -D__PIC32_FEATURE_SET__=795 -DOBDCONV_BOARD \
-DDEBUG_KWP2K -Os -Wall -msoft-float -march=m4k $(HEADER_DIRS)

# linker flags
#  - soft float
LDFLAGS= -msoft-float -march=m4k -nostartfiles

# directories containing libraries
LIB_DIRS= -L $(TOOLS_DIR)/lib/microchip

OBJFILES= $(BUILD_DIR)/main.o $(BUILD_DIR)/rt_light.o $(BUILD_DIR)/bt.o \
    $(BUILD_DIR)/kwp2k.o $(BUILD_DIR)/master_comm.o $(BUILD_DIR)/mon.o \
    $(BUILD_DIR)/data_acq.o $(BUILD_DIR)/utils.o

APPNAME= main

all: $(APPNAME).elf $(APPNAME).dis $(APPNAME).hex $(APPNAME).hdr \
    $(APPNAME).sym

$(APPNAME).elf: $(OBJFILES) $(BUILD_DIR)/crt0-mipsel-app.o
	$(CC) $(LDFLAGS) -o $(BUILD_DIR)/$@ $(OBJFILES) $(LIB_DIRS) \
	-ldsp -T ld/elf32pic32mx-app.x

$(APPNAME).dis: $(BUILD_DIR)/$(APPNAME).elf
	$(OBJDUMP) -D $? > $(BUILD_DIR)/$@

$(APPNAME).hdr: $(BUILD_DIR)/$(APPNAME).elf
	$(OBJDUMP) -h $? > $(BUILD_DIR)/$@

$(APPNAME).hex: $(BUILD_DIR)/$(APPNAME).elf
	$(OBJCOPY) -I elf32-little -O ihex --change-addresses -0x80000000 $? \
	$(BUILD_DIR)/$@

#	$(OBJCOPY) -I elf32-little -O ihex --change-addresses -0x80000000 \
#   --change-section-lma .reset=0x1fc00000 --change-section-lma \
#    .configsfrs=0x1fc02ff0 $? $@

$(APPNAME).sym: $(BUILD_DIR)/$(APPNAME).elf
	$(NM) -S -n $? > $(BUILD_DIR)/$@

$(BUILD_DIR)/crt0-mipsel-app.o: Source/crt0-mipsel-app.S
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/bt.o: Source/bt.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/data_acq.o: Source/data_acq.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/kwp2k.o: Source/kwp2k.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/master_comm.o: Source/master_comm.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/main.o: Source/main.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/mon.o: Source/mon.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/rt_light.o: Source/rt_light.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/utils.o: Source/utils.c
	$(CC) -o $@ -c $(CFLAGS) $?

clean:
	$(RM) $(BUILD_DIR)/*
