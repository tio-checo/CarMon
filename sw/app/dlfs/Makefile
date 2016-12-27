CC= gcc

# build directory
BUILD_DIR= build

# project directories containing header files
HEADER_DIRS= -I./

# compiler flags
CFLAGS= -Wall -m64 $(HEADER_DIRS)

# linker flags
LDFLAGS= -m64

OBJDUMP= $(BUILD_DIR)/dump.o
OBJFORMAT= $(BUILD_DIR)/format.o

APPDUMP = dlfsdump
APPFORMAT = dlfsformat

all: $(BUILD_DIR) $(APPDUMP) $(APPFORMAT)

$(BUILD_DIR):
	[[ ! -d $@ ]] && mkdir $@

$(APPDUMP): $(OBJDUMP)
	$(CC) $(LDFLAGS) -o $(BUILD_DIR)/$@ $(OBJDUMP)

$(BUILD_DIR)/dump.o: dump.c
	$(CC) -o $@ -c $(CFLAGS) $?

$(APPFORMAT): $(OBJFORMAT)
	$(CC) $(LDFLAGS) -o $(BUILD_DIR)/$@ $(OBJFORMAT)

$(BUILD_DIR)/format.o: format.c
	$(CC) -o $@ -c $(CFLAGS) $?

clean:
	$(RM) $(BUILD_DIR)/*
