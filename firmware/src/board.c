#include "board.h"

#include <string.h>

#include "clock.h"
#include "flash.h"
#include "hal.h"
#include "memory_map.h"

board_info_t g_board;

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

static bool uart_send(USART_TypeDef* u, ring_t* tx, uint8_t b) {
  bool ok = ring_put(tx, b);
  if (ok) u->CR1 |= USART_CR1_TXEIE;
  return ok;
}

bool midi_uart_read(uint8_t* b) { return ring_get(&midi_rx, b); }
void midi_uart_write(uint8_t b) { (void) uart_send(USART2, &midi_tx, b); }
bool midi_uart_try_write(uint8_t b) { return uart_send(USART2, &midi_tx, b); }
uint32_t midi_uart_errors(void) { return midi_err; }

bool debug_uart_read(uint8_t* b) { return ring_get(&dbg_rx, b); }
void debug_uart_write(const uint8_t* p, uint32_t n) {
  while (n--) (void) uart_send(USART1, &dbg_tx, *p++);
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

void board_init(void) {
  g_board.reset_flags = RCC->CSR;
  RCC->CSR |= RCC_CSR_RMVF;

  chip_detect();
  gpio_init(); // first, so GATE/CLOCK get their idle levels right away
  g_board.hse_hz = clock_init();
  dac_init();
  uart_init();
  systick_init();
}

bool board_button_pressed(void) { return (GPIOC->IDR & (1u << 3)) == 0; }

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

bool hal_storage_write(const void* src, uint32_t len) {
  board_watchdog_feed();
  flash_unlock();
  bool ok = flash_erase_sector(SETTINGS_FLASH_SECTOR) && flash_program(SETTINGS_FLASH_ADDR, src, len);
  flash_lock();
  board_watchdog_feed();
  return ok && memcmp(src, (const void*) SETTINGS_FLASH_ADDR, len) == 0;
}

void board_reboot_to_bootloader(void) {
  BOOT_REQUEST = BOOT_REQUEST_DFU;
  NVIC_SystemReset();
}
