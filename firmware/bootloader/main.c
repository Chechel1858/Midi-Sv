// USB DFU bootloader for the MIDI Analog Filter alpha board.
//
// Lives in flash sectors 0-1. At reset it starts the application at
// APP_BASE right away, unless:
//   - LEARN is held while the board powers up,
//   - the application asked for it (console command "dfu"),
//   - there is no valid application (blank chip or an interrupted update).
// In DFU mode the NOTEON LED blinks and the board shows up on USB as
// "MIDI-CV Alpha Updater"; flash it with:  dfu-util -a 0 -D midi_cv_alpha.dfu
#include <stdbool.h>

#include "clock.h"
#include "dfu_image.h"
#include "memory_map.h"
#include "stm32f4xx.h"
#include "tusb.h"
#include "usb_hw.h"

#define LED_PIN      1u  // PC1 NOTEON LED
#define AUX_LED_PIN  0u  // PC0 BACKLIGHT LED test point
#define BUTTON_PIN   3u  // PC3 LEARN, pressed = low
#define GATE_PIN     6u  // PA6, high = gate off
#define CLOCK_PIN    8u  // PB8, high = clock off

TU_VERIFY_STATIC(DFU_BLOCK_SIZE == CFG_TUD_DFU_XFER_BUFSIZE, "DFU block size mismatch");

static uint32_t reboot_at; // g_ms timestamp, 0 = none
static bool busy;          // a download is in progress

// ---------------------------------------------------------------------------

static bool button_held_at_boot(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
  (void) RCC->AHB1ENR;
  GPIOC->PUPDR = (GPIOC->PUPDR & ~(3u << (BUTTON_PIN * 2))) | (1u << (BUTTON_PIN * 2)); // pull-up
  GPIOC->MODER &= ~(3u << (BUTTON_PIN * 2));                                           // input

  // A few ms on HSI: let the RC filter (R76/C41) settle, require a steady "pressed"
  uint32_t pressed = 0;
  for (uint32_t i = 0; i < 20000u; i++) {
    if (!(GPIOC->IDR & (1u << BUTTON_PIN))) pressed++;
  }
  return pressed > 19000u;
}

static void jump_to_app(void) {
  // Leave GPIOC as it was after reset
  RCC->AHB1RSTR |= RCC_AHB1RSTR_GPIOCRST;
  RCC->AHB1RSTR &= ~RCC_AHB1RSTR_GPIOCRST;
  RCC->AHB1ENR &= ~RCC_AHB1ENR_GPIOCEN;

  const uint32_t* v = (const uint32_t*) APP_BASE;
  SCB->VTOR = APP_BASE;
  __asm volatile(
    "msr msp, %0\n"
    "bx %1\n" ::"r"(v[0]),
    "r"(v[1]));
  for (;;) {}
}

// If the application had started the watchdog and it survived the reset,
// stretch it to the maximum (~32 s); it is fed in the main loop.
static void watchdog_relax(void) {
  IWDG->KR = 0x5555;
  IWDG->PR = 6;       // /256
  IWDG->RLR = 0xFFF;
  for (uint32_t i = 0; (IWDG->SR & 3u) && i < 1000000u; i++) {}
  IWDG->KR = 0xAAAA;
}

void img_feed_watchdog(void) { IWDG->KR = 0xAAAA; }

static void gpio_out(GPIO_TypeDef* port, uint32_t pin, bool level) {
  port->BSRR = level ? (1u << pin) : (1u << (pin + 16));
  port->MODER = (port->MODER & ~(3u << (pin * 2))) | (1u << (pin * 2));
}

// ---------------------------------------------------------------------------
// DFU callbacks (called from tud_task)

static uint8_t dfu_status(img_status_t s) {
  switch (s) {
    case IMG_OK: return DFU_STATUS_OK;
    case IMG_ERR_TARGET: return DFU_STATUS_ERR_TARGET;
    case IMG_ERR_ADDRESS: return DFU_STATUS_ERR_ADDRESS;
    case IMG_ERR_WRITE: return DFU_STATUS_ERR_WRITE;
    case IMG_ERR_VERIFY: return DFU_STATUS_ERR_VERIFY;
    default: return DFU_STATUS_ERR_FIRMWARE;
  }
}

void tud_dfu_download_cb(uint8_t alt, uint16_t block_num, const uint8_t* data, uint16_t length) {
  (void) alt;
  busy = true;
  img_status_t s = img_write_block(block_num, data, length);
  if (s != IMG_OK) busy = false;
  tud_dfu_finish_flashing(dfu_status(s));
}

void tud_dfu_manifest_cb(uint8_t alt) {
  (void) alt;
  img_status_t s = img_finish();
  busy = false;
  if (s == IMG_OK) reboot_at = g_ms + 1500; // let dfu-util finish, then start the new firmware
  tud_dfu_finish_flashing(dfu_status(s));
}

uint16_t tud_dfu_upload_cb(uint8_t alt, uint16_t block_num, uint8_t* data, uint16_t length) {
  (void) alt;
  return img_read_block(block_num, data, length);
}

uint32_t tud_dfu_get_timeout_cb(uint8_t alt, uint8_t state) {
  (void) alt;
  (void) state;
  return 10; // the work is done synchronously in the callbacks above
}

void tud_dfu_abort_cb(uint8_t alt) {
  (void) alt;
  img_abort();
  busy = false;
}

void tud_dfu_detach_cb(void) { reboot_at = g_ms + 100; }

// ---------------------------------------------------------------------------

int main(void) {
  uint32_t request = BOOT_REQUEST;
  BOOT_REQUEST = 0;
  bool button = button_held_at_boot();

  if (request != BOOT_REQUEST_DFU && !button && img_app_valid()) jump_to_app();

  // ---- DFU mode ----
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
  (void) RCC->AHB1ENR;
  gpio_out(GPIOA, GATE_PIN, true);   // gate off (inverted by Q18)
  gpio_out(GPIOB, CLOCK_PIN, true);  // clock off
  gpio_out(GPIOC, AUX_LED_PIN, false);
  gpio_out(GPIOC, LED_PIN, true);

  watchdog_relax();
  clock_init();
  systick_init();
  usb_hw_init();

  bool btn_released = !button;
  uint32_t btn_ms = 0, last_ms = g_ms;

  for (;;) {
    tud_task();
    img_feed_watchdog();

    uint32_t now = g_ms;
    if (now != last_ms) {
      last_ms = now;

      // Blink: slow while waiting, fast while a download is going on
      uint32_t period = busy ? 100u : 500u;
      gpio_out(GPIOC, LED_PIN, (now / period) & 1u);

      // A new press of LEARN leaves DFU mode (if there is something to start)
      bool pressed = !(GPIOC->IDR & (1u << BUTTON_PIN));
      if (!pressed) {
        btn_released = true;
        btn_ms = 0;
      } else if (btn_released && ++btn_ms == 50 && !busy && img_app_valid()) {
        reboot_at = now + 1;
      }
    }

    if (reboot_at && (int32_t) (now - reboot_at) >= 0) NVIC_SystemReset();
  }
}
