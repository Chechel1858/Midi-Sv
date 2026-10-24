#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

// STM32F2/F4 OTG_HS core (rhport 1) with its internal full-speed PHY
#define CFG_TUSB_MCU          OPT_MCU_STM32F4
#define CFG_TUSB_OS           OPT_OS_NONE
#define CFG_TUSB_DEBUG        0

#define CFG_TUD_ENABLED       1
#define CFG_TUD_MAX_SPEED     OPT_MODE_FULL_SPEED

#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN    __attribute__((aligned(4)))

#define CFG_TUD_ENDPOINT0_SIZE 64

#define CFG_TUD_CDC           1   // virtual COM port: settings console
#define CFG_TUD_MIDI          1   // class-compliant USB MIDI
#define CFG_TUD_MSC           0
#define CFG_TUD_HID           0
#define CFG_TUD_VENDOR        0

#define CFG_TUD_CDC_RX_BUFSIZE  64
#define CFG_TUD_CDC_TX_BUFSIZE  512
#define CFG_TUD_CDC_EP_BUFSIZE  64

#define CFG_TUD_MIDI_RX_BUFSIZE 256
#define CFG_TUD_MIDI_TX_BUFSIZE 256

#endif
