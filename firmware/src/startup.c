// Minimal startup for STM32F2/F4: vector table, .data/.bss init, call main.
#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;

int main(void);

void Reset_Handler(void);
void Default_Handler(void);

// Core exceptions
void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void) __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));

// Peripheral interrupts actually used (IRQ numbers are the same on F2 and F4)
void USART1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void USART2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void OTG_HS_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));

#define IRQ_COUNT 98 // enough for STM32F446 (largest of the supported parts)
#define USART1_IRQn_ 37
#define USART2_IRQn_ 38
#define OTG_HS_IRQn_ 77

typedef void (*vector_t)(void);

__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[16 + IRQ_COUNT] = {
  [0] = (vector_t) &_estack,
  [1] = Reset_Handler,
  [2] = NMI_Handler,
  [3] = HardFault_Handler,
  [4] = MemManage_Handler,
  [5] = BusFault_Handler,
  [6] = UsageFault_Handler,
  [11] = SVC_Handler,
  [12] = DebugMon_Handler,
  [14] = PendSV_Handler,
  [15] = SysTick_Handler,
  [16 ... 16 + USART1_IRQn_ - 1] = Default_Handler,
  [16 + USART1_IRQn_] = USART1_IRQHandler,
  [16 + USART2_IRQn_] = USART2_IRQHandler,
  [16 + USART2_IRQn_ + 1 ... 16 + OTG_HS_IRQn_ - 1] = Default_Handler,
  [16 + OTG_HS_IRQn_] = OTG_HS_IRQHandler,
  [16 + OTG_HS_IRQn_ + 1 ... 16 + IRQ_COUNT - 1] = Default_Handler,
};

void Reset_Handler(void) {
  uint32_t* src = &_sidata;
  for (uint32_t* dst = &_sdata; dst < &_edata;) *dst++ = *src++;
  for (uint32_t* dst = &_sbss; dst < &_ebss;) *dst++ = 0;
  main();
  for (;;) {}
}

// Unexpected interrupt or fault: stop here, the watchdog will reset the MCU.
void Default_Handler(void) {
  for (;;) {}
}
