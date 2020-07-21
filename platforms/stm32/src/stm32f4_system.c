/*
 * Copyright (c) 2014-2018 Cesanta Software Limited
 * All rights reserved
 *
 * Licensed under the Apache License, Version 2.0 (the ""License"");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an ""AS IS"" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>

#include "stm32_sdk_hal.h"
#include "stm32_system.h"

#include <stm32f4xx_ll_rcc.h>

uint32_t SystemCoreClock = HSI_VALUE;
const uint8_t AHBPrescTable[16] = {0, 0, 0, 0, 0, 0, 0, 0,
                                   1, 2, 3, 4, 6, 7, 8, 9};
const uint8_t APBPrescTable[8] = {0, 0, 0, 0, 1, 2, 3, 4};
#define VECT_TAB_OFFSET 0x0

void stm32_system_init(void) {
#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
  SCB->CPACR |=
      ((3UL << 10 * 2) | (3UL << 11 * 2)); /* set CP10 and CP11 Full Access */
#endif
  /* Reset the RCC clock configuration to the default reset state ------------*/
  /* Set HSION bit */
  RCC->CR |= (uint32_t) 0x00000001;

  /* Reset CFGR register */
  RCC->CFGR = 0x00000000;

  /* Reset HSEON, CSSON and PLLON bits */
  RCC->CR &= (uint32_t) 0xFEF6FFFF;

  /* Reset PLLCFGR register */
  RCC->PLLCFGR = 0x24003010;

  /* Reset HSEBYP bit */
  RCC->CR &= (uint32_t) 0xFFFBFFFF;

  /* Disable all interrupts */
  RCC->CIR = 0x00000000;

  __HAL_FLASH_INSTRUCTION_CACHE_ENABLE();
  __HAL_FLASH_DATA_CACHE_ENABLE();
  __HAL_FLASH_PREFETCH_BUFFER_ENABLE();
}

void stm32_clock_config(void) {
  RCC_ClkInitTypeDef cc;
  RCC_OscInitTypeDef oc;
  memset(&cc, 0, sizeof(cc));
  memset(&oc, 0, sizeof(oc));

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

/* Configure the clock source and PLL. */
#if HSE_VALUE == 0
  oc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  oc.HSIState = RCC_HSI_ON;
  oc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  oc.PLL.PLLM = HSI_VALUE / 1000000;  // VCO input = 1 MHz
#else
  oc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  oc.HSEState = RCC_HSE_ON;
  oc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  oc.PLL.PLLM = HSE_VALUE / 1000000;  // VCO input = 1 MHz
#endif
  oc.PLL.PLLState = RCC_PLL_ON;
  oc.PLL.PLLN = 384;            // VCO output = 1 * 384 = 384 MHz
  oc.PLL.PLLP = RCC_PLLP_DIV4;  // SYSCLK = 384 / 4 = 96 MHz
  oc.PLL.PLLQ = 8;              // USB FS clock = 384 / 8 = 48 MHz
  oc.PLL.PLLR = 7;
  HAL_RCC_OscConfig(&oc);

  cc.ClockType = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
  cc.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  cc.AHBCLKDivider = RCC_SYSCLK_DIV1;  // 96 MHz
  cc.APB1CLKDivider = RCC_HCLK_DIV1;
  cc.APB2CLKDivider = RCC_HCLK_DIV1;
  HAL_RCC_ClockConfig(&cc, FLASH_LATENCY_3);

  /* Use system PLL for USB and RNG. */
  LL_RCC_SetCK48MClockSource(LL_RCC_CK48M_CLKSOURCE_PLL);

/* Turn off the unused oscilaltor. */
#if HSE_VALUE == 0
  oc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  oc.HSEState = RCC_HSE_OFF;
#else
  oc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  oc.HSIState = RCC_HSI_OFF;
#endif
  oc.PLL.PLLState = RCC_PLL_NONE; /* Don't touch the PLL config */
  HAL_RCC_OscConfig(&oc);
}

IRAM void stm32_flush_caches(void) {
  __HAL_FLASH_DATA_CACHE_DISABLE();
  __HAL_FLASH_DATA_CACHE_RESET();
  __HAL_FLASH_DATA_CACHE_ENABLE();
  __HAL_FLASH_INSTRUCTION_CACHE_DISABLE();
  __HAL_FLASH_INSTRUCTION_CACHE_RESET();
  __HAL_FLASH_INSTRUCTION_CACHE_ENABLE();
}
