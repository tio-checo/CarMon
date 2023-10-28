#
# Copyright(c) 2015, 2023
#

#TOOLS_DIR= /opt/mipsel-10.3.0
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
-I../common/Include/peripheral -I../common/Include/peripheral/int/ \
-I../common/Include/peripheral/can

# compiler flags
#  - code optimized for size
#  - soft float
CFLAGS= -D__XC32 -D__PIC32MX__ -D__32MX795F512H__ -D__PIC32_FEATURE_SET__=795 \
-DOBDCONV_BOARD -DOBD_MODULE -DCANHAL_MODULE -DCARMON_MODULE \
-DI2CSW_MODULE -DI2CHAL_MODULE -DPCF8563_MODULE -DLCDTERM_MODULE \
-DDLFS_MODULE -DGPS_MODULE -DEE24_MODULE -DMCOM_USBACM_MODULE \
-fgnu89-inline -nostdinc -Os -Wall -msoft-float -march=m4k $(HEADER_DIRS)

# linker flags
#  - soft float
LDFLAGS= -msoft-float -march=m4k -nostdlib

# directories containing libraries
LIB_DIRS= -L $(TOOLS_DIR)/lib/microchip

OBJFILES= $(BUILD_DIR)/main.o $(BUILD_DIR)/rt_light.o $(BUILD_DIR)/bt.o \
    $(BUILD_DIR)/helpers.o $(BUILD_DIR)/pic_core.o \
    $(BUILD_DIR)/master_comm.o $(BUILD_DIR)/mon.o \
    $(BUILD_DIR)/utils.o $(BUILD_DIR)/obd.o \
    $(BUILD_DIR)/usb_app.o $(BUILD_DIR)/usb_descriptors.o \
    $(BUILD_DIR)/usb_device.o \
    $(BUILD_DIR)/usb_function_cdc.o \
    $(BUILD_DIR)/dma_chn_mem_crc_lib.o \
    $(BUILD_DIR)/dma_chn_start_txfer_lib.o \
    $(BUILD_DIR)/dma_reg_map_tbl_lib.o \
    $(BUILD_DIR)/int_disable_interrupts_lib.o \
    $(BUILD_DIR)/int_enable_interrupts_lib.o \
    $(BUILD_DIR)/int_enable_mv_int_lib.o \
    $(BUILD_DIR)/int_tbl_lib.o \
    $(BUILD_DIR)/spi_brg_tbl_lib_legacy.o \
    $(BUILD_DIR)/spi_getc_spi2_lib_legacy.o \
    $(BUILD_DIR)/spi_open_spi2_lib_legacy.o \
    $(BUILD_DIR)/SD-SPI.o \
    $(BUILD_DIR)/CANConfiguration.o \
    $(BUILD_DIR)/CANFilterFunctions.o \
    $(BUILD_DIR)/CANErrorFunctions.o \
    $(BUILD_DIR)/CANEventFunctions.o \
    $(BUILD_DIR)/CANInfoFunctions.o \
    $(BUILD_DIR)/CANReceiveFunctions.o \
    $(BUILD_DIR)/CANTransmitFunctions.o \
    $(BUILD_DIR)/can_hal.o \
    $(BUILD_DIR)/can_snoop.o \
    $(BUILD_DIR)/carmon.o \
    $(BUILD_DIR)/i2c_hal.o \
    $(BUILD_DIR)/i2c_sw.o \
    $(BUILD_DIR)/lcd_term.o \
    $(BUILD_DIR)/dlfs.o \
    $(BUILD_DIR)/pcf8563.o \
    $(BUILD_DIR)/gps.o \
    $(BUILD_DIR)/ee24.o

APPNAME= main

all: $(BUILD_DIR) $(APPNAME).elf $(APPNAME).dis $(APPNAME).hex \
    $(APPNAME).hdr $(APPNAME).sym

$(BUILD_DIR):
	[[ ! -d $@ ]] && mkdir $@

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

$(BUILD_DIR)/ee24.o: ../common/Source/ee24.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/bt.o: ../common/Source/bt.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/CANConfiguration.o: ../common/Source/peripheral/can/CANConfiguration.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/CANErrorFunctions.o: ../common/Source/peripheral/can/CANErrorFunctions.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/CANEventFunctions.o: ../common/Source/peripheral/can/CANEventFunctions.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/CANFilterFunctions.o: ../common/Source/peripheral/can/CANFilterFunctions.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/CANInfoFunctions.o: ../common/Source/peripheral/can/CANInfoFunctions.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/CANReceiveFunctions.o: ../common/Source/peripheral/can/CANReceiveFunctions.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/CANTransmitFunctions.o: ../common/Source/peripheral/can/CANTransmitFunctions.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/can_hal.o: ../common/Source/can_hal.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/can_snoop.o: ../common/Source/can_snoop.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/carmon.o: ../common/Source/carmon.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/dlfs.o: ../common/Source/dlfs.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/gps.o: ../common/Source/gps.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/i2c_hal.o: ../common/Source/i2c_hal.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/i2c_sw.o: ../common/Source/i2c_sw.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/lcd_term.o: ../common/Source/lcd_term.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/master_comm.o: ../common/Source/master_comm.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/mon.o: ../common/Source/mon.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/obd.o: ../common/Source/obd.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/pcf8563.o: ../common/Source/pcf8563.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/pic_core.o: ../common/Source/pic_core.c
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

$(BUILD_DIR)/dma_chn_mem_crc_lib.o: ../common/Source/peripheral/dma/dma_chn_mem_crc_lib.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/dma_chn_start_txfer_lib.o: ../common/Source/peripheral/dma/dma_chn_start_txfer_lib.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(BUILD_DIR)/dma_reg_map_tbl_lib.o: ../common/Source/peripheral/dma/dma_reg_map_tbl_lib.c
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
