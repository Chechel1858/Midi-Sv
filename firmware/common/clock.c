#include "clock.h"

#include "stm32f4xx.h"

volatile uint32_t g_ms;
uint32_t SystemCoreClock = 16000000u; // used by CMSIS / TinyUSB

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

uint32_t clock_init(void) {
  // At reset the core runs on HSI 16 MHz
  uint32_t hse = measure_hse_hz();
  uint32_t mhz = (hse + 500000u) / 1000000u;
  if (mhz < 4 || mhz > 26) mhz = 0; // outside what the PLL setup below supports

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
  return mhz * 1000000u;
}

void SysTick_Handler(void) { g_ms++; }

void systick_init(void) {
  SysTick_Config(SYSCLK_HZ / 1000u);
  NVIC_SetPriority(SysTick_IRQn, 2);
}
