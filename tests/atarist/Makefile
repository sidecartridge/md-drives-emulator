# Version from file
VERSION := $(shell cat version.txt)

# Folder and file names
ODIR = ./obj
SOURCES_DIR = ./src
BUILD_DIR = ./build
DIST_DIR = ./dist
EXE = FSTESTS.TOS

# VASM PARAMETERS
VASMFLAGS=-Faout -quiet -x -m68000 -spaces -showopt -devpac
VASM = vasm 
VLINK = vlink

# LIBCMINI PARAMETERS
# IMPORTANT! There is functional verson of the LIBCMINI library in the docker image
# To reference the library, it must set the absolute path inside the container image
# The library is stored in /freemint/libcmini 
# More info about the library: https://github.com/freemint/libcmini
LIBCMINI = /freemint/libcmini

# GCC PARAMETERS
CC = m68k-atari-mint-gcc
CFLAGS = -c -std=gnu99 -I$(LIBCMINI)/include -I$(SOURCES_DIR)/include -DVERSION=\"$(VERSION)\"

# GCC OPTIMIZATION
# Add the -O3 option to optimize the binary
# Add the -fomit-frame-pointer option to optimize the binary
# Add the -s option to strip the binary
# Add the -fno-exceptions option to optimize the binary
# Add the -fno-strict-aliasing option to optimize the binary
# Add the -fno-exceptions option to optimize the binary
#CFLAGS += -O3 -fomit-frame-pointer -fno-exceptions -fno-strict-aliasing -s
CFLAGS += -Os -fomit-frame-pointer -fno-exceptions -fno-strict-aliasing -s  # Optimization and strip symbols

$(info LOG is $(LOG))

# Check if LOG is not empty
ifneq ($(LOG),)
    CFLAGS += -D_LOG=$(LOG)    
endif

# LINKER PARAMETERS
LINKFLAGS = -nostdlib -L$(LIBCMINI)/lib -lcmini -lgcc -Wl,--traditional-format -s  # Strip symbols in linking

_OBJS = 

OBJS = $(patsubst %,$(ODIR)/%,$(_OBJS))

.PHONY: all
all: dist prepare

.PHONY: release
release: dist prepare

.PHONY: prepare
prepare: clean
	mkdir -p $(BUILD_DIR)

clean-compile : clean main.o test_runner.o folder_tests.o folder_listing_tests.o workdir_tests.o files_tests.o chksum_tests.o
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/$@ $<

# Compilation Rules
%.o: $(SOURCES_DIR)/%.c prepare
	$(CC) $(CFLAGS) $< -o $(BUILD_DIR)/$@

main: main.o test_runner.o folder_tests.o folder_listing_tests.o workdir_tests.o files_tests.o chksum_tests.o
	$(CC) $(LIBCMINI)/lib/crt0.o \
		  $(BUILD_DIR)/main.o \
		  $(BUILD_DIR)/test_runner.o \
		  $(BUILD_DIR)/folder_tests.o \
		  $(BUILD_DIR)/folder_listing_tests.o \
		  $(BUILD_DIR)/workdir_tests.o \
		  $(BUILD_DIR)/files_tests.o \
		  $(BUILD_DIR)/chksum_tests.o \
		  -o $(BUILD_DIR)/$(EXE) $(LINKFLAGS)

.PHONY: dist
dist: main
	mkdir -p $(DIST_DIR)
	cp $(BUILD_DIR)/$(EXE) $(DIST_DIR)    

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

## Tag this version
.PHONY: tag
tag:
	git tag v$(VERSION) && git push origin v$(VERSION) && \
	echo "Tagged: $(VERSION)"
