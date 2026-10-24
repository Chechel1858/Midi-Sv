// USB descriptors of the bootloader: a plain DFU 1.1 device, plus Microsoft
// OS 2.0 descriptors so Windows binds WinUSB by itself (no Zadig needed).
#include <string.h>

#include "tusb.h"

// pid.codes test VID; a different PID than the application, so the OS keeps
// separate drivers for "MIDI-CV Alpha" and its updater
#define USB_VID 0x1209
#define USB_PID 0x0002

#define VENDOR_REQUEST_MICROSOFT 1

static const tusb_desc_device_t desc_device = {
  .bLength = sizeof(tusb_desc_device_t),
  .bDescriptorType = TUSB_DESC_DEVICE,
  .bcdUSB = 0x0210, // 2.1: has a BOS descriptor
  .bDeviceClass = 0x00,
  .bDeviceSubClass = 0x00,
  .bDeviceProtocol = 0x00,
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

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_DFU_DESC_LEN(1))

// Manifestation tolerant: after the download the device stays reachable and
// reboots into the new firmware by itself a moment later.
#define DFU_ATTRS (DFU_ATTR_CAN_DOWNLOAD | DFU_ATTR_CAN_UPLOAD | DFU_ATTR_MANIFESTATION_TOLERANT)

static const uint8_t desc_configuration[] = {
  TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN, 0x00, 500),
  TUD_DFU_DESCRIPTOR(0, 1, 4, DFU_ATTRS, 1000, CFG_TUD_DFU_XFER_BUFSIZE),
};

const uint8_t* tud_descriptor_configuration_cb(uint8_t index) {
  (void) index;
  return desc_configuration;
}

// ---------------------------------------------------------------------------
// BOS + Microsoft OS 2.0 (WinUSB for the whole, non-composite device)

#define MS_OS_20_DESC_LEN (10 + 20 + 132)
#define BOS_TOTAL_LEN     (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)

static const uint8_t desc_bos[] = {
  TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 1),
  TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, VENDOR_REQUEST_MICROSOFT),
};

const uint8_t* tud_descriptor_bos_cb(void) { return desc_bos; }

static const uint8_t desc_ms_os_20[] = {
  // Set header: length, type, Windows version (8.1+), total length
  U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR), U32_TO_U8S_LE(0x06030000),
  U16_TO_U8S_LE(MS_OS_20_DESC_LEN),

  // Compatible ID: WINUSB
  U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID), 'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

  // Registry property: DeviceInterfaceGUIDs = {7C5D5B6E-2C1A-4E43-9C51-6A2F8D0B1F31}
  U16_TO_U8S_LE(132), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
  U16_TO_U8S_LE(0x0007), U16_TO_U8S_LE(0x002A), // REG_MULTI_SZ, name length
  'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0, 'I', 0, 'n', 0, 't', 0, 'e', 0,
  'r', 0, 'f', 0, 'a', 0, 'c', 0, 'e', 0, 'G', 0, 'U', 0, 'I', 0, 'D', 0, 's', 0, 0, 0,
  U16_TO_U8S_LE(0x0050), // data length
  '{', 0, '7', 0, 'C', 0, '5', 0, 'D', 0, '5', 0, 'B', 0, '6', 0, 'E', 0, '-', 0,
  '2', 0, 'C', 0, '1', 0, 'A', 0, '-', 0, '4', 0, 'E', 0, '4', 0, '3', 0, '-', 0,
  '9', 0, 'C', 0, '5', 0, '1', 0, '-', 0, '6', 0, 'A', 0, '2', 0, 'F', 0, '8', 0,
  'D', 0, '0', 0, 'B', 0, '1', 0, 'F', 0, '3', 0, '1', 0, '}', 0, 0, 0, 0, 0,
};

TU_VERIFY_STATIC(sizeof(desc_ms_os_20) == MS_OS_20_DESC_LEN, "Incorrect MS OS 2.0 descriptor size");

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t* request) {
  if (stage != CONTROL_STAGE_SETUP) return true;
  if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR && request->bRequest == VENDOR_REQUEST_MICROSOFT &&
      request->wIndex == 7) {
    return tud_control_xfer(rhport, request, (void*) (uintptr_t) desc_ms_os_20, sizeof(desc_ms_os_20));
  }
  return false;
}

// ---------------------------------------------------------------------------
// Strings

enum {
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
  STRID_SERIAL,
  STRID_DFU_ALT0,
};

static const char* const string_desc_arr[] = {
  NULL,
  "Pribore",
  "MIDI-CV Alpha Updater",
  NULL,
  "Application",
};

static uint16_t desc_str[32 + 1];

const uint16_t* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  size_t n = 0;

  if (index == STRID_LANGID) {
    desc_str[1] = 0x0409;
    n = 1;
  } else if (index == STRID_SERIAL) {
    const uint32_t* uid = (const uint32_t*) 0x1FFF7A10u; // same unique ID as the app reports
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
