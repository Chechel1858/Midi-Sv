// USB hardware: OTG_HS core with the internal full-speed PHY on PB14/PB15
// (TinyUSB rhport 1). Shared by the bootloader and the application.
#ifndef USB_HW_H
#define USB_HW_H

#define USB_RHPORT 1

// Configures pins/clocks and starts TinyUSB in device mode
void usb_hw_init(void);

#endif
