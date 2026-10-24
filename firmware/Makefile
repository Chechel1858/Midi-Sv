# MIDI-CV Alpha firmware: USB DFU bootloader + application
#
#   make          build everything into build/:
#                   midi_cv_alpha.dfu   application, for updates over USB (dfu-util)
#                   midi_cv_alpha.bin   the same without the DFU suffix
#                   midi_cv_full.hex    bootloader + application, first flash with ST-Link
#                   bootloader.hex      bootloader alone
#   make test     run the logic tests on this PC (gcc)
#   make dfu      update over USB:   dfu-util -a 0 -D build/midi_cv_alpha.dfu
#   make flash    first flash with an ST-Link (st-flash from stlink-tools)
#   make release  build and copy the results to prebuilt/

BUILD    = build

CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy
SIZE     = $(CROSS)size

TUSB     = lib/tinyusb/src

COMMON_SRC = \
  common/startup.c \
  common/clock.c \
  common/flash.c \
  common/flash_layout.c \
  common/usb_hw.c

TUSB_CORE = \
  $(TUSB)/tusb.c \
  $(TUSB)/common/tusb_fifo.c \
  $(TUSB)/device/usbd.c \
  $(TUSB)/device/usbd_control.c \
  $(TUSB)/portable/synopsys/dwc2/dcd_dwc2.c \
  $(TUSB)/portable/synopsys/dwc2/dwc2_common.c

APP_SRC = \
  src/main.c \
  src/board.c \
  src/engine.c \
  src/midi_parser.c \
  src/settings.c \
  src/console.c \
  src/fmt.c \
  src/usb_descriptors.c \
  $(COMMON_SRC) \
  $(TUSB_CORE) \
  $(TUSB)/class/midi/midi_device.c \
  $(TUSB)/class/cdc/cdc_device.c

BOOT_SRC = \
  bootloader/main.c \
  bootloader/dfu_image.c \
  bootloader/usb_descriptors.c \
  $(COMMON_SRC) \
  $(TUSB_CORE) \
  $(TUSB)/class/dfu/dfu_device.c

LIB_INC = -Icommon -I$(TUSB) -Ilib/cmsis/core -Ilib/cmsis/device

# Cortex-M3 code runs on the F205 (M3) and on the F405/F446 (M4F)
MCU = -mcpu=cortex-m3 -mthumb -mfloat-abi=soft

CFLAGS = $(MCU) -std=gnu11 -Os -g3 -Wall -Wextra -Wno-unused-parameter \
         -ffunction-sections -fdata-sections -fno-common -DSTM32F405xx

LDFLAGS = $(MCU) -Wl,--gc-sections --specs=nano.specs -nostartfiles -Wl,--print-memory-usage

APP_OBJ  = $(patsubst %.c,$(BUILD)/app/%.o,$(APP_SRC))
BOOT_OBJ = $(patsubst %.c,$(BUILD)/boot/%.o,$(BOOT_SRC))

all: $(BUILD)/midi_cv_alpha.dfu $(BUILD)/midi_cv_alpha.bin $(BUILD)/midi_cv_alpha.hex $(BUILD)/bootloader.hex $(BUILD)/midi_cv_full.hex

# Each image has its own tusb_config.h, so objects are built per image
$(BUILD)/app/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Isrc $(LIB_INC) -MMD -MP -c $< -o $@

$(BUILD)/boot/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Ibootloader $(LIB_INC) -MMD -MP -c $< -o $@

$(BUILD)/midi_cv_alpha.elf: $(APP_OBJ) app.ld
	$(CC) $(LDFLAGS) -Tapp.ld -Wl,-Map=$(BUILD)/midi_cv_alpha.map $(APP_OBJ) -o $@
	$(SIZE) $@

$(BUILD)/bootloader.elf: $(BOOT_OBJ) bootloader/bootloader.ld
	$(CC) $(LDFLAGS) -Tbootloader/bootloader.ld -Wl,-Map=$(BUILD)/bootloader.map $(BOOT_OBJ) -o $@
	$(SIZE) $@

$(BUILD)/%.hex: $(BUILD)/%.elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/%.bin: $(BUILD)/%.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/midi_cv_alpha.dfu: $(BUILD)/midi_cv_alpha.bin tools/add_dfu_suffix.py
	python3 tools/add_dfu_suffix.py $< $@

# Bootloader + application in one file: drop the bootloader's end-of-file record
$(BUILD)/midi_cv_full.hex: $(BUILD)/bootloader.hex $(BUILD)/midi_cv_alpha.hex
	grep -v '^:00000001FF' $(BUILD)/bootloader.hex > $@
	cat $(BUILD)/midi_cv_alpha.hex >> $@

test:
	@mkdir -p $(BUILD)/test
	gcc -std=c11 -Wall -Wextra -O1 -g tests/test_main.c src/engine.c src/midi_parser.c src/settings.c -o $(BUILD)/test/run_tests
	./$(BUILD)/test/run_tests
	gcc -std=c11 -Wall -Wextra -O1 -g -Icommon tests/test_bootloader.c bootloader/dfu_image.c common/flash_layout.c -o $(BUILD)/test/run_boot_tests
	./$(BUILD)/test/run_boot_tests

dfu: $(BUILD)/midi_cv_alpha.dfu
	dfu-util -a 0 -D $<

flash: $(BUILD)/midi_cv_full.hex
	st-flash --format ihex --reset write $<

release: all
	@mkdir -p prebuilt
	rm -f prebuilt/*
	cp $(BUILD)/midi_cv_alpha.dfu $(BUILD)/midi_cv_alpha.bin $(BUILD)/midi_cv_full.hex $(BUILD)/bootloader.hex prebuilt/

clean:
	rm -rf $(BUILD)

.PHONY: all test dfu flash release clean

-include $(APP_OBJ:.o=.d) $(BOOT_OBJ:.o=.d)
