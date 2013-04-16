TOOLS_DIR= /opt/mipsel
TOOLS_BIN= $(TOOLS_DIR)/bin

CC= $(TOOLS_BIN)/mipsel-gcc
OBJCOPY= $(TOOLS_BIN)/mipsel-objcopy
OBJDUMP= $(TOOLS_BIN)/mipsel-objdump
NM= $(TOOLS_BIN)/mipsel-nm

# build directory
BUILD_DIR= build

# project directories containing header files
HEADER_DIRS= -I ../common/Include -I. -I./Include -I./Include/peripheral \
-I./Include/HardwareProfile/ -I./Source/ -I./Include/USB/ -I./Include/MDDFS/ \
-I./Include/peripheral/int/

# compiler flags
#  - code optimized for size
#  - soft float
CFLAGS= -D__XC32 -D__PIC32MX__ -D__32MX795F512H__ -D__PIC32_FEATURE_SET__=795 \
-DOBDCONV_BOARD -DDEBUG_KWP2K -nostdinc -Os -Wall -msoft-float -march=m4k \
$(HEADER_DIRS)

# linker flags
#  - soft float
LDFLAGS= -msoft-float -march=m4k -nostdlib

# directories containing libraries
LIB_DIRS= -L $(TOOLS_DIR)/lib/microchip

OBJFILES= $(BUILD_DIR)/main.o $(BUILD_DIR)/rt_light.o $(BUILD_DIR)/bt.o \
    $(BUILD_DIR)/helpers.o $(BUILD_DIR)/pic_core.o \
    $(BUILD_DIR)/kwp2k.o $(BUILD_DIR)/master_comm.o $(BUILD_DIR)/mon.o \
    $(BUILD_DIR)/data_acq.o $(BUILD_DIR)/utils.o $(BUILD_DIR)/obd.o \
    $(BUILD_DIR)/usb_app.o $(BUILD_DIR)/usb_descriptors.o \
    $(BUILD_DIR)/usb_device.o \
    $(BUILD_DIR)/usb_function_cdc.o $(BUILD_DIR)/int_disable_interrupts_lib.o \
    $(BUILD_DIR)/int_enable_interrupts_lib.o \
    $(BUILD_DIR)/int_enable_mv_int_lib.o \
    $(BUILD_DIR)/int_tbl_lib.o \
    $(BUILD_DIR)/spi_brg_tbl_lib_legacy.o \
    $(BUILD_DIR)/spi_getc_spi2_lib_legacy.o \
    $(BUILD_DIR)/spi_open_spi2_lib_legacy.o \
    $(BUILD_DIR)/FSIO.o $(BUILD_DIR)/SD-SPI.o \


APPNAME= main

all: $(APPNAME).elf $(APPNAME).dis $(APPNAME).hex $(APPNAME).hdr \
    $(APPNAME).sym

$(APPNAME).elf: $(OBJFILES) $(BUILD_DIR)/crt0-mipsel-app.o
	$(CC) $(LDFLAGS) -o $(BUILD_DIR)/$@ $(OBJFILES) $(LIB_DIRS) \
	-ldsp -lc_s_n -lgcc_s_n -T ld/elf32pic32mx-app.x

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

$(BUILD_DIR)/helpers.o: Source/helpers.c
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

$(BUILD_DIR)/usb_descriptors.o: Source/usb_descriptors.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/utils.o: Source/utils.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/obd.o: Source/obd.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/pic_core.o: Source/pic_core.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/usb_app.o: Source/usb_app.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/usb_device.o: Source/USB/usb_device.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/usb_function_cdc.o: Source/USB/usb_function_cdc.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/int_disable_interrupts_lib.o: Source/peripheral/int/int_disable_interrupts_lib.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/int_enable_interrupts_lib.o: Source/peripheral/int/int_enable_interrupts_lib.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/int_tbl_lib.o: Source/peripheral/int/int_tbl_lib.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/int_enable_mv_int_lib.o: Source/peripheral/int/int_enable_mv_int_lib.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/spi_brg_tbl_lib_legacy.o: Source/peripheral/spi/legacy/spi_brg_tbl_lib_legacy.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/spi_getc_spi2_lib_legacy.o: Source/peripheral/spi/legacy/spi_getc_spi2_lib_legacy.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/spi_open_spi2_lib_legacy.o: Source/peripheral/spi/legacy/spi_open_spi2_lib_legacy.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/FSIO.o: Source/MDDFS/FSIO.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/SD-SPI.o: Source/MDDFS/SD-SPI.c
	$(CC) -o $@ -c $(CFLAGS) $?

clean:
	$(RM) $(BUILD_DIR)/*
