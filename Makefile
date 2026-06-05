# Project Name
TARGET = triptech

USE_DAISYSP_LGPL = 1

# Optimise for size: the STM32H750 has only 128 KB internal flash (APP_TYPE
# BOOT_NONE) and the menu UI pushes us right up against it. -Os on this TU keeps
# the image inside the region without moving to the QSPI bootloader.
OPT = -Os

# Sources
CPP_SOURCES = Triptech.cpp

# Library Locations
LIBDAISY_DIR = ./libDaisy
DAISYSP_DIR = ./DaisySP

# Core location, and generic makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# Build all dependencies then the project
all: libs
	$(MAKE) build/$(TARGET).elf build/$(TARGET).hex build/$(TARGET).bin

libs:
	$(MAKE) -C $(LIBDAISY_DIR)
	$(MAKE) -C $(DAISYSP_DIR)
	$(MAKE) -C $(DAISYSP_DIR)/DaisySP-LGPL

lint:
	clang-format --dry-run --Werror $(CPP_SOURCES)

.PHONY: all libs lint
