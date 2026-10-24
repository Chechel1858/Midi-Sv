#include "board.h"

#include <string.h>

#include "hal.h"
#include "tusb.h"

board_info_t g_board;
volatile uint32_t g_ms;
uint32_t SystemCoreClock = 16000000u; // used by CMSIS / TinyUSB

// ---------------------------------------------------------------------------
// GPIO helpers

enum { MODE_IN = 0, MODE_OUT = 1, MODE_AF = 2, MODE_ANALOG = 3 };
enum { PULL_NONE = 0, PULL_UP = 1, PULL_DOWN = 2 };

static void gpio_config(GPIO_TypeDef* port, uint32_t pin, uint32_t mode, uint32_t af, uint32_t pull) {
  port->MODER = (port->MODER & ~(3u << (pin * 2))) | (mode << (pin * 2));
  port->PUPDR = (port->PUPDR & ~(3u << (pin * 2))) | (pull << (pin * 2));
  port->OSPEEDR |= 3u << (pin * 2); // very high speed (needed for USB, harmless elsewhere)
  port->OTYPER &= ~(1u << pin);     // push-pull
  if (mode == MODE_AF) {
    volatile uint32_t* afr = &port->AFR[pin >> 3];
    uint32_t shift = (pin & 7u) * 4u;
    *afr = (*afr & ~(0xFu << shift)) | (af << shift);
  }
}

static inline void gpio_write(GPIO_TypeDef* port, uint32_t pin, bool level) {
  port->BSRR = level ? (1u << pin) : (1u << (pin + 16));
}

// ---------------------------------------------------------------------------
// Clock

// Measures the HSE crystal with TIM11 while the core runs on HSI (16 MHz).
// TIM11 CH1 can be internally connected to HSE_RTC = HSE / RTCPRE, so we
// count HSI cycles per 31*8 HSE cycles. Returns 0 if there is no crystal.
static uint32_t measure_hse_hz(void) {
  RCC->CR |= RCC_CR_HSEON;
  for (uint32_t i = 0; !(RCC->CR & RCC_CR_HSERDY); i++) {
    if (i > 500000u) { // ~150 ms
      RCC->CR &= ~RCC_CR_HSEON;
      return 0;
    }
  }

  const uint32_t rtcpre = 31, icpsc = 8, periods = 32;
  RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_RTCPRE) | (rtcpre << RCC_CFGR_RTCPRE_Pos);
  RCC->APB2ENR |= RCC_APB2ENR_TIM11EN;
  (void) RCC->APB2ENR;

  TIM11->CR1 = 0;
  TIM11->OR = 2u;                    // TI1_RMP = HSE_RTC
  TIM11->PSC = 0;
  TIM11->ARR = 0xFFFF;
  TIM11->CCMR1 = (1u << 0)           // CC1S = TI1
               | (3u << 2);          // IC1PSC = /8
  TIM11->CCER = TIM_CCER_CC1E;
  TIM11->EGR = TIM_EGR_UG;
  TIM11->SR = 0;
  TIM11->CR1 = TIM_CR1_CEN;

  uint32_t sum = 0, ok = 1;
  uint16_t prev = 0;
  for (int i = -1; i < (int) periods && ok; i++) {
    uint32_t t = 0;
    while (!(TIM11->SR & TIM_SR_CC1IF)) {
      if (++t > 100000u) {
        ok = 0;
        break;
      }
    }
    uint16_t c = (uint16_t) TIM11->CCR1; // clears CC1IF
    if (i >= 0) sum += (uint16_t) (c - prev);
    prev = c;
  }

  TIM11->CR1 = 0;
  TIM11->OR = 0;
  RCC->APB2ENR &= ~RCC_APB2ENR_TIM11EN;

  if (!ok || sum == 0) {
    // Crystal runs but the measurement failed: assume the common 8 MHz
    return 8000000u;
  }
  uint64_t hz = (uint64_t) 16000000u * rtcpre * icpsc * periods / sum;
  return (uint32_t) hz;
}

static void clock_init(void) {
  // At reset the core runs on HSI 16 MHz
  uint32_t hse = measure_hse_hz();
  uint32_t mhz = (hse + 500000u) / 1000000u;
  if (mhz < 4 || mhz > 26) mhz = 0; // outside what the PLL setup below supports
  g_board.hse_hz = mhz ? mhz * 1000000u : 0;

  // VCO input 1 MHz, VCO 240 MHz: valid for F2 (needs N >= 192) and F4
  uint32_t pllm = mhz ? mhz : 16u;
  uint32_t plln = 240u;
  uint32_t pllp = 0;  // /2 -> 120 MHz SYSCLK
  uint32_t pllq = 5;  // /5 -> 48 MHz USB

  if (!mhz) RCC->CR &= ~RCC_CR_HSEON;

  RCC->CR &= ~RCC_CR_PLLON;
  while (RCC->CR & RCC_CR_PLLRDY) {}
  RCC->PLLCFGR = (RCC->PLLCFGR & 0xF0000000u) // keep PLLR (F446) / reserved bits
               | (pllm << RCC_PLLCFGR_PLLM_Pos)
               | (plln << RCC_PLLCFGR_PLLN_Pos)
               | (pllp << RCC_PLLCFGR_PLLP_Pos)
               | (mhz ? RCC_PLLCFGR_PLLSRC_HSE : RCC_PLLCFGR_PLLSRC_HSI)
               | (pllq << RCC_PLLCFGR_PLLQ_Pos);
  RCC->CR |= RCC_CR_PLLON;
  while (!(RCC->CR & RCC_CR_PLLRDY)) {}

  // 3 wait states for 120 MHz at 2.7..3.6 V (F2 and F4). Prefetch stays off
  // (it is broken on the first F405 silicon revision and not needed here).
  FLASH->ACR = FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_LATENCY_3WS;
  while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_3WS) {}

  RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2))
            | RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;
  RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
  while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}

  SystemCoreClock = SYSCLK_HZ;
}

static void chip_detect(void) {
  uint32_t id = DBGMCU->IDCODE;
  g_board.dev_id = (uint16_t) (id & 0xFFFu);
  g_board.rev_id = (uint16_t) (id >> 16);
  switch (g_board.dev_id) {
    case 0x411: g_board.chip = CHIP_F205; break;
    case 0x413: g_board.chip = CHIP_F405; break;
    case 0x421: g_board.chip = CHIP_F446; break;
    default: g_board.chip = CHIP_UNKNOWN; break; // some early revisions read 0 without a debugger
  }
}

const char* board_chip_name(void) {
  switch (g_board.chip) {
    case CHIP_F205: return "STM32F2xx";
    case CHIP_F405: return "STM32F405/407";
    case CHIP_F446: return "STM32F446";
    default: return "unknown STM32F2/F4";
  }
}

// ---------------------------------------------------------------------------
// UART ring buffers

typedef struct {
  volatile uint16_t head, tail;
  uint8_t buf[256];
} ring_t;

static inline bool ring_put(ring_t* r, uint8_t b) {
  uint16_t next = (uint16_t) ((r->head + 1u) & 0xFFu);
  if (next == r->tail) return false;
  r->buf[r->head] = b;
  r->head = next;
  return true;
}

static inline bool ring_get(ring_t* r, uint8_t* b) {
  if (r->tail == r->head) return false;
  *b = r->buf[r->tail];
  r->tail = (uint16_t) ((r->tail + 1u) & 0xFFu);
  return true;
}

static ring_t midi_rx, midi_tx, dbg_rx, dbg_tx;
static volatile uint32_t midi_err;

static void uart_irq(USART_TypeDef* u, ring_t* rx, ring_t* tx, volatile uint32_t* err) {
  uint32_t sr = u->SR;
  if (sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) {
    uint8_t b = (uint8_t) u->DR; // reading SR then DR clears the error flags
    if (sr & (USART_SR_ORE | USART_SR_FE | USART_SR_NE)) {
      if (err) (*err)++;
    }
    if (sr & USART_SR_RXNE) {
      if (!ring_put(rx, b) && err) (*err)++;
    }
  }
  if ((u->CR1 & USART_CR1_TXEIE) && (sr & USART_SR_TXE)) {
    uint8_t b;
    if (ring_get(tx, &b)) {
      u->DR = b;
    } else {
      u->CR1 &= ~USART_CR1_TXEIE;
    }
  }
}

void USART2_IRQHandler(void) { uart_irq(USART2, &midi_rx, &midi_tx, &midi_err); }
void USART1_IRQHandler(void) { uart_irq(USART1, &dbg_rx, &dbg_tx, NULL); }

static void uart_send(USART_TypeDef* u, ring_t* tx, uint8_t b) {
  if (ring_put(tx, b)) u->CR1 |= USART_CR1_TXEIE;
}

bool midi_uart_read(uint8_t* b) { return ring_get(&midi_rx, b); }
void midi_uart_write(uint8_t b) { uart_send(USART2, &midi_tx, b); }
uint32_t midi_uart_errors(void) { return midi_err; }

bool debug_uart_read(uint8_t* b) { return ring_get(&dbg_rx, b); }
void debug_uart_write(const uint8_t* p, uint32_t n) {
  while (n--) uart_send(USART1, &dbg_tx, *p++);
}

static void uart_init(void) {
  RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
  RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
  (void) RCC->APB2ENR;

  // MIDI: 31250 baud 8N1
  USART2->BRR = (APB1_HZ + 31250u / 2) / 31250u;
  USART2->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;

  // Debug: 115200 baud 8N1
  USART1->BRR = (APB2_HZ + 115200u / 2) / 115200u;
  USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;

  NVIC_SetPriority(USART2_IRQn, 1); // MIDI bytes must never be lost
  NVIC_EnableIRQ(USART2_IRQn);
  NVIC_SetPriority(USART1_IRQn, 5);
  NVIC_EnableIRQ(USART1_IRQn);
}

// ---------------------------------------------------------------------------
// GPIO, DAC, SysTick

static void gpio_init(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
  (void) RCC->AHB1ENR;

  // Outputs: set the idle level before switching the pin to output
  gpio_write(GPIOA, 6, true);  // GATE off (inverted by Q18)
  gpio_config(GPIOA, 6, MODE_OUT, 0, PULL_NONE);
  gpio_write(GPIOB, 8, true);  // CLOCK off (active low)
  gpio_config(GPIOB, 8, MODE_OUT, 0, PULL_NONE);
  gpio_write(GPIOC, 0, false); // BACKLIGHT LED
  gpio_config(GPIOC, 0, MODE_OUT, 0, PULL_NONE);
  gpio_write(GPIOC, 1, false); // NOTEON LED
  gpio_config(GPIOC, 1, MODE_OUT, 0, PULL_NONE);

  // LEARN button (external 10k pull-up, internal one as a backup)
  gpio_config(GPIOC, 3, MODE_IN, 0, PULL_UP);

  // UARTs (AF7)
  gpio_config(GPIOA, 2, MODE_AF, 7, PULL_NONE);
  gpio_config(GPIOA, 3, MODE_AF, 7, PULL_UP);
  gpio_config(GPIOA, 9, MODE_AF, 7, PULL_NONE);
  gpio_config(GPIOA, 10, MODE_AF, 7, PULL_UP);

  // DAC outputs
  gpio_config(GPIOA, 4, MODE_ANALOG, 0, PULL_NONE);
  gpio_config(GPIOA, 5, MODE_ANALOG, 0, PULL_NONE);
}

static void dac_init(void) {
  RCC->APB1ENR |= RCC_APB1ENR_DACEN;
  (void) RCC->APB1ENR;
  DAC->DHR12R1 = 0;
  DAC->DHR12R2 = 0;
  // Output buffers off: the DAC drives a high-impedance op-amp input and
  // without the buffer the output really reaches 0 V and VREF.
  DAC->CR = DAC_CR_EN1 | DAC_CR_BOFF1 | DAC_CR_EN2 | DAC_CR_BOFF2;
}

void SysTick_Handler(void) { g_ms++; }

void board_init(void) {
  g_board.reset_flags = RCC->CSR;
  RCC->CSR |= RCC_CSR_RMVF;

  chip_detect();
  gpio_init(); // first, so GATE/CLOCK get their idle levels right away
  clock_init();
  dac_init();
  uart_init();

  SysTick_Config(SYSCLK_HZ / 1000u);
  NVIC_SetPriority(SysTick_IRQn, 2);
}

bool board_button_pressed(void) { return (GPIOC->IDR & (1u << 3)) == 0; }

// ---------------------------------------------------------------------------
// USB (OTG_HS core, internal full-speed PHY on PB14/PB15)

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

void board_usb_init(void) {
  gpio_config(GPIOB, 14, MODE_AF, 12, PULL_NONE);
  gpio_config(GPIOB, 15, MODE_AF, 12, PULL_NONE);

  RCC->AHB1ENR |= RCC_AHB1ENR_OTGHSEN;
  RCC->AHB1LPENR &= ~RCC_AHB1LPENR_OTGHSULPILPEN;
  (void) RCC->AHB1ENR;

  NVIC_SetPriority(OTG_HS_IRQn, 4);
  usb_vbus_sense_off();

  tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
  tusb_init(1, &dev_init);

  usb_vbus_sense_off();
}

void OTG_HS_IRQHandler(void) { tusb_int_handler(1, true); }

uint32_t tusb_time_millis_api(void) { return g_ms; }

// ---------------------------------------------------------------------------
// Watchdog / reset

void board_watchdog_init(void) {
  DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP; // don't reset while halted in a debugger
  IWDG->KR = 0x5555;
  IWDG->PR = 3;      // LSI ~32 kHz / 32 -> ~1 kHz
  IWDG->RLR = 2000;  // ~2 s, covers a flash sector erase
  IWDG->KR = 0xAAAA;
  IWDG->KR = 0xCCCC;
}

void board_watchdog_feed(void) { IWDG->KR = 0xAAAA; }

void board_reboot(void) { NVIC_SystemReset(); }

// ---------------------------------------------------------------------------
// HAL for the engine

uint32_t hal_millis(void) { return g_ms; }

void hal_dac_write(uint8_t ch, uint16_t code) {
  if (code > DAC_MAX) code = DAC_MAX;
  if (ch == CV_OUT_JACK) DAC->DHR12R1 = code;
  else if (ch == CV_OUT_FILTER) DAC->DHR12R2 = code;
}

void hal_gate_write(bool on) { gpio_write(GPIOA, 6, !on); }   // Q18 inverts
void hal_clock_write(bool on) { gpio_write(GPIOB, 8, !on); }  // active low
void hal_led_note(bool on) { gpio_write(GPIOC, 1, on); }
void hal_led_aux(bool on) { gpio_write(GPIOC, 0, on); }

bool hal_storage_read(void* dst, uint32_t len) {
  memcpy(dst, (const void*) SETTINGS_FLASH_ADDR, len);
  return true;
}

static bool flash_wait(void) {
  while (FLASH->SR & FLASH_SR_BSY) {}
  return (FLASH->SR & (FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR)) == 0;
}

bool hal_storage_write(const void* src, uint32_t len) {
  const uint32_t errs = FLASH_SR_EOP | FLASH_SR_SOP | FLASH_SR_WRPERR | FLASH_SR_PGAERR |
                        FLASH_SR_PGPERR | FLASH_SR_PGSERR;
  bool ok = true;

  board_watchdog_feed();
  FLASH->KEYR = 0x45670123u;
  FLASH->KEYR = 0xCDEF89ABu;
  flash_wait();
  FLASH->SR = errs;

  // Erase sector 3 (x32 parallelism, 2.7..3.6 V)
  FLASH->CR = FLASH_CR_PSIZE_1 | FLASH_CR_SER | (SETTINGS_FLASH_SECTOR << FLASH_CR_SNB_Pos);
  FLASH->CR |= FLASH_CR_STRT;
  ok = flash_wait();

  if (ok) {
    FLASH->CR = FLASH_CR_PSIZE_1 | FLASH_CR_PG;
    const uint8_t* p = (const uint8_t*) src;
    for (uint32_t off = 0; off < len && ok; off += 4) {
      uint32_t w = 0xFFFFFFFFu;
      memcpy(&w, p + off, (len - off) >= 4 ? 4 : (len - off));
      *(volatile uint32_t*) (SETTINGS_FLASH_ADDR + off) = w;
      ok = flash_wait();
    }
  }
  FLASH->CR = FLASH_CR_LOCK;

  // Drop stale data cache lines for the rewritten sector
  FLASH->ACR &= ~FLASH_ACR_DCEN;
  FLASH->ACR |= FLASH_ACR_DCRST;
  FLASH->ACR &= ~FLASH_ACR_DCRST;
  FLASH->ACR |= FLASH_ACR_DCEN;

  board_watchdog_feed();
  return ok && memcmp(src, (const void*) SETTINGS_FLASH_ADDR, len) == 0;
}
