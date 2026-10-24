// USB descriptors: composite device = USB MIDI + CDC virtual COM port.
#include <string.h>

#include "tusb.h"

// pid.codes test VID/PID, fine for a prototype; replace before production
#define USB_VID 0x1209
#define USB_PID 0x0001

static const tusb_desc_device_t desc_device = {
  .bLength = sizeof(tusb_desc_device_t),
  .bDescriptorType = TUSB_DESC_DEVICE,
  .bcdUSB = 0x0200,
  // IAD (needed for CDC in a composite device)
  .bDeviceClass = TUSB_CLASS_MISC,
  .bDeviceSubClass = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol = MISC_PROTOCOL_IAD,
  .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
  .idVendor = USB_VID,
  .idProduct = USB_PID,
  .bcdDevice = 0x0100,
  .iManufacturer = 1,
  .iProduct = 2,
  .iSerialNumber = 3,
  .bNumConfigurations = 1,
};

const uint8_t* tud_descriptor_device_cb(void) { return (const uint8_t*) &desc_device; }

enum {
  ITF_NUM_MIDI = 0,
  ITF_NUM_MIDI_STREAMING,
  ITF_NUM_CDC,
  ITF_NUM_CDC_DATA,
  ITF_NUM_TOTAL
};

#define EPNUM_MIDI_OUT  0x01
#define EPNUM_MIDI_IN   0x81
#define EPNUM_CDC_NOTIF 0x82
#define EPNUM_CDC_OUT   0x03
#define EPNUM_CDC_IN    0x83

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN + TUD_CDC_DESC_LEN)

static const uint8_t desc_configuration[] = {
  // The board takes its power from USB (boost converter for +/-15 V)
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 500),
  TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

const uint8_t* tud_descriptor_configuration_cb(uint8_t index) {
  (void) index;
  return desc_configuration;
}

enum {
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
  STRID_SERIAL,
  STRID_CDC,
};

static const char* const string_desc_arr[] = {
  NULL,             // language, handled below
  "Pribore",        // manufacturer
  "MIDI-CV Alpha",  // product (name shown in DAWs)
  NULL,             // serial, from the MCU unique ID
  "MIDI-CV Console",
};

static uint16_t desc_str[32 + 1];

const uint16_t* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  size_t n = 0;

  if (index == STRID_LANGID) {
    desc_str[1] = 0x0409;
    n = 1;
  } else if (index == STRID_SERIAL) {
    // 96-bit unique ID at 0x1FFF7A10 (same address on F2 and F4)
    const uint32_t* uid = (const uint32_t*) 0x1FFF7A10u;
    static const char hex[] = "0123456789ABCDEF";
    for (int w = 0; w < 3; w++) {
      for (int nib = 7; nib >= 0; nib--) desc_str[1 + n++] = (uint16_t) hex[(uid[w] >> (nib * 4)) & 0xF];
    }
  } else {
    if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0]) || !string_desc_arr[index]) return NULL;
    const char* s = string_desc_arr[index];
    n = strlen(s);
    if (n > 32) n = 32;
    for (size_t i = 0; i < n; i++) desc_str[1 + i] = (uint16_t) s[i];
  }

  desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * n + 2));
  return desc_str;
}
