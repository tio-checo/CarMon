CC= gcc

# build directory
BUILD_DIR= build

# project directories containing header files
HEADER_DIRS= -I./

# compiler flags
CFLAGS= -Wall -m64 $(HEADER_DIRS)

# linker flags
LDFLAGS= -m64

OBJFILES= $(BUILD_DIR)/dump.o

APPNAME = dlfsdump

all: $(BUILD_DIR) $(APPNAME)

$(BUILD_DIR):
	[[ ! -d $@ ]] && mkdir $@

$(APPNAME): $(OBJFILES)
	$(CC) $(LDFLAGS) -o $(BUILD_DIR)/$@ $(OBJFILES)

$(BUILD_DIR)/dump.o: dump.c
	$(CC) -o $@ -c $(CFLAGS) $?

clean:
	$(RM) $(BUILD_DIR)/*
