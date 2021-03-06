# Makefile to cross-compile romdisk.device

FLAVOR?=_dbg
BUILD_DIR=BUILD
OBJ_DIR=$(BUILD_DIR)/obj$(FLAVOR)

# what flavors to build
FLAVORS=_rel _dbg _rel_td _dbg_td

# parse flavors
ifeq "_dbg" "$(findstring _dbg,$(FLAVOR))"
DEBUG=1
DEFINES+=DEBUG
else
OPTFLAGS=-O2
endif

ifeq "_td" "$(findstring _td,$(FLAVOR))"
FAKE_TD=1
DEFINES+=FAKE_TD
DEV_NAME=trackdisk.device
else
AUTOBOOT=1
DEFINES+=AUTOBOOT
DEV_NAME=romdisk.device
endif

# setup defines
CDEFINES=$(patsubst %,-D%=1,$(DEFINES))
include ../version.mk
QUOTE=\\\"
CDEFINES+=-DMYDEV_VERSION=$(PROJECT_MAJOR)
CDEFINES+=-DMYDEV_REVISION=$(PROJECT_MINOR)
CDEFINES+=-DMYDEV_VERSION_STR=$(QUOTE)$(PROJECT_MAJOR).$(PROJECT_MINOR)$(QUOTE)
CDEFINES+=-DMYDEV_DATE=$(QUOTE)$(PROJECT_DATE)$(QUOTE)
CDEFINES+=-DMYDEV_NAME=$(QUOTE)$(DEV_NAME)$(QUOTE)
CDEFINES+=-DMYDEV_WORKER=$(QUOTE)romdisk$(QUOTE)

# toolchain dir
# uses default dir of https://github.com/cahirwpz/amigaos-cross-toolchain
TC_DIR=/opt/m68k-amigaos

# cross compiler/assembler setup
CC=vc
CFLAGS=-c99 -g -sc $(CDEFINES) $(OPTFLAGS)
LDFLAGS=-g -nostdlib -lvcs -lamiga
AS=vasmm68k_mot
ASFLAGS=-quiet -Fhunk -I$(TC_DIR)/os-include -m68000

SRCS=device.c disk.c mydev.c worker.c unpack.c
ASRCS=rnc.s inflate.s lz4.s
HDRS=boot.h disk.h debug.h mydev.h worker.h unpack.h

# fetch sources from subdirs, too
VPATH=unpacker

# parse flags
ifeq "$(DEBUG)" "1"
SRCS+=debug.c
endif
ifeq "$(AUTOBOOT)" "1"
SRCS+=boot.c
ASRCS+=diag.s
endif

# end must be last
ASRCS+=end.s

OBJS=$(patsubst %.c,$(OBJ_DIR)/%.o,$(SRCS))
OBJS+=$(patsubst %.s,$(OBJ_DIR)/%.o,$(ASRCS))

DEVICE=$(BUILD_DIR)/$(DEV_NAME)$(FLAVOR)

# ----- rules -----

all: $(BUILD_DIR) $(OBJ_DIR) $(DEVICE)

flavors:
	@for f in $(FLAVORS) ; do \
		$(MAKE) FLAVOR=$$f || exit 1 ; \
	done
	@echo "---- done flavors: $(FLAVORS) -----"

$(DEVICE): $(OBJS)
	vc $^ -o $@ $(LDFLAGS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJS): $(SRCS) $(HDRS) $(ASRCS)

$(OBJ_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/%.o: %.s
	$(AS) $(ASFLAGS) -o $@ $<

clean:
	rm -rf $(BUILD_DIR)

dist:
	@for f in $(FLAVORS) ; do \
		$(MAKE) dist_flavor FLAVOR=$$f || exit 1 ; \
	done

dist_flavor: all
	cp $(DEVICE) $(DIST_DIR)/devs
