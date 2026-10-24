#include "usb_hw.h"

#include "clock.h"
#include "stm32f4xx.h"
#include "tusb.h"

#define OTG_GOTGCTL (*(volatile uint32_t*) (USB_OTG_HS_PERIPH_BASE + 0x00u))
#define OTG_GCCFG   (*(volatile uint32_t*) (USB_OTG_HS_PERIPH_BASE + 0x38u))
#define OTG_GSNPSID (*(volatile uint32_t*) (USB_OTG_HS_PERIPH_BASE + 0x40u))

// VBUS is not wired to the MCU (PB13 is unconnected), so tell the core that
// the B-session is always valid. F2/F405 (core 2.81a) and F446 (core 3.x)
// use different bits for this.
static void usb_vbus_sense_off(void) {
  if ((OTG_GSNPSID & 0xFFFFu) >= 0x300Au) {
    OTG_GCCFG &= ~(1u << 21);                  // VBDEN = 0
    OTG_GOTGCTL |= (1u << 6) | (1u << 7);      // BVALOEN, BVALOVAL
  } else {
    OTG_GCCFG &= ~((1u << 18) | (1u << 19));   // VBUSASEN, VBUSBSEN
    OTG_GCCFG |= (1u << 21);                   // NOVBUSSENS
  }
}

static void usb_pin(uint32_t pin) {
  GPIOB->MODER = (GPIOB->MODER & ~(3u << (pin * 2))) | (2u << (pin * 2)); // AF
  GPIOB->OSPEEDR |= 3u << (pin * 2);
  GPIOB->PUPDR &= ~(3u << (pin * 2));
  GPIOB->OTYPER &= ~(1u << pin);
  volatile uint32_t* afr = &GPIOB->AFR[pin >> 3];
  uint32_t shift = (pin & 7u) * 4u;
  *afr = (*afr & ~(0xFu << shift)) | (12u << shift); // AF12 = OTG_HS_FS
}

void usb_hw_init(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
  (void) RCC->AHB1ENR;
  usb_pin(14);
  usb_pin(15);

  RCC->AHB1ENR |= RCC_AHB1ENR_OTGHSEN;
  RCC->AHB1LPENR &= ~RCC_AHB1LPENR_OTGHSULPILPEN;
  (void) RCC->AHB1ENR;

  NVIC_SetPriority(OTG_HS_IRQn, 4);
  usb_vbus_sense_off();

  tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
  tusb_init(USB_RHPORT, &dev_init);

  usb_vbus_sense_off();
}

void OTG_HS_IRQHandler(void) { tusb_int_handler(USB_RHPORT, true); }

uint32_t tusb_time_millis_api(void) { return g_ms; }
