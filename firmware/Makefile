# MIDI-CV Alpha firmware
#   make          build build/midi_cv_alpha.{elf,hex,bin}
#   make test     run the logic tests on this PC (gcc)
#   make flash    flash with an ST-Link (needs st-flash from stlink-tools)
#   make release  build and copy hex/bin to prebuilt/

PROJECT  = midi_cv_alpha
BUILD    = build

CROSS   ?= arm-none-eabi-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy
SIZE     = $(CROSS)size

TUSB     = lib/tinyusb/src

SRC = \
  src/startup.c \
  src/main.c \
  src/board.c \
  src/engine.c \
  src/midi_parser.c \
  src/settings.c \
  src/console.c \
  src/fmt.c \
  src/usb_descriptors.c \
  $(TUSB)/tusb.c \
  $(TUSB)/common/tusb_fifo.c \
  $(TUSB)/device/usbd.c \
  $(TUSB)/device/usbd_control.c \
  $(TUSB)/class/midi/midi_device.c \
  $(TUSB)/class/cdc/cdc_device.c \
  $(TUSB)/portable/synopsys/dwc2/dcd_dwc2.c \
  $(TUSB)/portable/synopsys/dwc2/dwc2_common.c

INC = -Isrc -I$(TUSB) -Ilib/cmsis/core -Ilib/cmsis/device

# Cortex-M3 code runs on the F205 (M3) and on the F405/F446 (M4F)
MCU = -mcpu=cortex-m3 -mthumb -mfloat-abi=soft

CFLAGS = $(MCU) -std=gnu11 -Os -g3 -Wall -Wextra -Wno-unused-parameter \
         -ffunction-sections -fdata-sections -fno-common \
         -DSTM32F405xx $(INC)

LDFLAGS = $(MCU) -Tstm32.ld -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(PROJECT).map \
          --specs=nano.specs -nostartfiles -Wl,--print-memory-usage

OBJ = $(patsubst %.c,$(BUILD)/%.o,$(SRC))

all: $(BUILD)/$(PROJECT).hex $(BUILD)/$(PROJECT).bin

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/$(PROJECT).elf: $(OBJ) stm32.ld
	$(CC) $(LDFLAGS) $(OBJ) -o $@
	$(SIZE) $@

$(BUILD)/$(PROJECT).hex: $(BUILD)/$(PROJECT).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/$(PROJECT).bin: $(BUILD)/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

test:
	@mkdir -p $(BUILD)/test
	gcc -std=c11 -Wall -Wextra -O1 -g tests/test_main.c src/engine.c src/midi_parser.c src/settings.c -o $(BUILD)/test/run_tests
	./$(BUILD)/test/run_tests

flash: $(BUILD)/$(PROJECT).bin
	st-flash --reset write $< 0x08000000

release: all
	@mkdir -p prebuilt
	cp $(BUILD)/$(PROJECT).hex $(BUILD)/$(PROJECT).bin prebuilt/

clean:
	rm -rf $(BUILD)

.PHONY: all test flash release clean

-include $(OBJ:.o=.d)
