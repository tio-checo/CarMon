TOOLS_DIR= /opt/mipsel
TOOLS_BIN= $(TOOLS_DIR)/bin

CC= $(TOOLS_BIN)/mipsel-gcc
OBJCOPY= $(TOOLS_BIN)/mipsel-objcopy
OBJDUMP= $(TOOLS_BIN)/mipsel-objdump
NM= $(TOOLS_BIN)/mipsel-nm

# build directory
BUILD_DIR= build

# project directories containing header files
HEADER_DIRS= -I. -I./Include -I./Source/ -I./Include/HardwareProfile/ \
-I../common/Include -I../common/Include/USB/ -I../common/Include/MDDFS/ \
-I../common/Include/peripheral -I../commmon/Include/peripheral/int/

# compiler flags
#  - code optimized for size
#  - soft float
CFLAGS= -D__XC32 -D__PIC32MX__ -D__32MX795F512H__ -D__PIC32_FEATURE_SET__=795 \
-DOBDCONV_BOARD -DRFID_MODULE -DMCP9801_MODULE \
-nostdinc -Os -Wall -msoft-float \-march=m4k $(HEADER_DIRS)

# linker flags
#  - soft float
LDFLAGS= -msoft-float -march=m4k -nostdlib

# directories containing libraries
LIB_DIRS= -L $(TOOLS_DIR)/lib/microchip

OBJFILES= $(BUILD_DIR)/main.o $(BUILD_DIR)/rt_light.o $(BUILD_DIR)/bt.o \
    $(BUILD_DIR)/helpers.o $(BUILD_DIR)/pic_core.o $(BUILD_DIR)/rfid.o \
    $(BUILD_DIR)/master_comm.o $(BUILD_DIR)/mcp9801.o $(BUILD_DIR)/mon.o \
    $(BUILD_DIR)/data_acq.o $(BUILD_DIR)/log.o $(BUILD_DIR)/utils.o \
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
	-ldsp -lc_s_n -lgcc_s_n -T ../common/ld/elf32pic32mx-app.x

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

$(BUILD_DIR)/crt0-mipsel-app.o: ../common/Source/crt0-mipsel-app.S
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/bt.o: ../common/Source/bt.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/data_acq.o: ../common/Source/data_acq.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/log.o: ../common/Source/log.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/master_comm.o: ../common/Source/master_comm.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/mcp9801.o: ../common/Source/mcp9801.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/mon.o: ../common/Source/mon.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/pic_core.o: ../common/Source/pic_core.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/rfid.o: ../common/Source/rfid.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/rt_light.o: ../common/Source/rt_light.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/helpers.o: ../common/Source/helpers.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/utils.o: ../common/Source/utils.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/usb_device.o: ../common/Source/USB/usb_device.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/usb_function_cdc.o: ../common/Source/USB/usb_function_cdc.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/int_disable_interrupts_lib.o: ../common/Source/peripheral/int/int_disable_interrupts_lib.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/int_enable_interrupts_lib.o: ../common/Source/peripheral/int/int_enable_interrupts_lib.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/int_tbl_lib.o: ../common/Source/peripheral/int/int_tbl_lib.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/int_enable_mv_int_lib.o: ../common/Source/peripheral/int/int_enable_mv_int_lib.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/spi_brg_tbl_lib_legacy.o: ../common/Source/peripheral/spi/legacy/spi_brg_tbl_lib_legacy.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/spi_getc_spi2_lib_legacy.o: ../common/Source/peripheral/spi/legacy/spi_getc_spi2_lib_legacy.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/spi_open_spi2_lib_legacy.o: ../common/Source/peripheral/spi/legacy/spi_open_spi2_lib_legacy.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/FSIO.o: ../common/Source/MDDFS/FSIO.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/SD-SPI.o: ../common/Source/MDDFS/SD-SPI.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/main.o: Source/main.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/usb_descriptors.o: Source/usb_descriptors.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/usb_app.o: Source/usb_app.c
	$(CC) -o $@ -c $(CFLAGS) $?

clean:
	$(RM) $(BUILD_DIR)/*
