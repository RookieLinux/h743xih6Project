/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-04-29     RealThread   first version
 */

#include <rtthread.h>
#include <board.h>
#include <drv_common.h>
#include <touch.h>
#include "gt911.h"

#define DBG_TAG "drv_board"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#ifndef RT_WEAK
#define RT_WEAK rt_weak
#endif

RT_WEAK void rt_hw_board_init()
{
    extern void hw_board_init(char *clock_src, int32_t clock_src_freq, int32_t clock_target_freq);

    /* Heap initialization */
#if defined(RT_USING_HEAP)
    rt_system_heap_init((void *) HEAP_BEGIN, (void *) HEAP_END);
#endif

    hw_board_init(BSP_CLOCK_SOURCE, BSP_CLOCK_SOURCE_FREQ_MHZ, BSP_CLOCK_SYSTEM_FREQ_MHZ);

    /* Set the shell console output device */
#if defined(RT_USING_DEVICE) && defined(RT_USING_CONSOLE)
    rt_console_set_device(RT_CONSOLE_DEVICE_NAME);
#endif

    /* Board underlying hardware initialization */
#ifdef RT_USING_COMPONENTS_INIT
    rt_components_board_init();
#endif

}

QSPI_HandleTypeDef hqspi;

/* QUADSPI init function */
void MX_QUADSPI_Init(void)
{

  /* USER CODE BEGIN QUADSPI_Init 0 */

  /* USER CODE END QUADSPI_Init 0 */

  /* USER CODE BEGIN QUADSPI_Init 1 */

  /* USER CODE END QUADSPI_Init 1 */
  hqspi.Instance = QUADSPI;
  hqspi.Init.FifoThreshold = 1;
  hqspi.Init.SampleShifting = QSPI_SAMPLE_SHIFTING_HALFCYCLE;
  hqspi.Init.FlashSize = 22;//Flash容量的字节数 = 2的（FlashSize + 1）次方
  hqspi.Init.ClockPrescaler = 3; //120MHz / 3 == 40MHz
  hqspi.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_5_CYCLE;
  hqspi.Init.ClockMode = QSPI_CLOCK_MODE_0;
  hqspi.Init.FlashID = QSPI_FLASH_ID_1;
  hqspi.Init.DualFlash = QSPI_DUALFLASH_DISABLE;
  if (HAL_QSPI_Init(&hqspi) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN QUADSPI_Init 2 */

  /* USER CODE END QUADSPI_Init 2 */

}

void HAL_QSPI_MspInit(QSPI_HandleTypeDef* qspiHandle)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
    if(qspiHandle->Instance==QUADSPI)
    {
        /* USER CODE BEGIN QUADSPI_MspInit 0 */

        /* USER CODE END QUADSPI_MspInit 0 */

        /** Initializes the peripherals clock
        */
          PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_QSPI;
          PeriphClkInitStruct.QspiClockSelection = RCC_QSPICLKSOURCE_PLL;
          if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
          {
            Error_Handler();
          }

          /* QUADSPI clock enable */
          __HAL_RCC_QSPI_CLK_ENABLE();

          __HAL_RCC_GPIOB_CLK_ENABLE();
          __HAL_RCC_GPIOF_CLK_ENABLE();
          /**QUADSPI GPIO Configuration
          PB6     ------> QUADSPI_BK1_NCS
          PF6     ------> QUADSPI_BK1_IO3
          PF7     ------> QUADSPI_BK1_IO2
          PF8     ------> QUADSPI_BK1_IO0
          PF9     ------> QUADSPI_BK1_IO1
          PB2     ------> QUADSPI_CLK
          */
          GPIO_InitStruct.Pin = GPIO_PIN_6;
          GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
          GPIO_InitStruct.Pull = GPIO_NOPULL;
          GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
          GPIO_InitStruct.Alternate = GPIO_AF10_QUADSPI;
          HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

          GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
          GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
          GPIO_InitStruct.Pull = GPIO_NOPULL;
          GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
          GPIO_InitStruct.Alternate = GPIO_AF9_QUADSPI;
          HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

          GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9;
          GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
          GPIO_InitStruct.Pull = GPIO_NOPULL;
          GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
          GPIO_InitStruct.Alternate = GPIO_AF10_QUADSPI;
          HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

          GPIO_InitStruct.Pin = GPIO_PIN_2;
          GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
          GPIO_InitStruct.Pull = GPIO_NOPULL;
          GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
          GPIO_InitStruct.Alternate = GPIO_AF9_QUADSPI;
          HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

        /* USER CODE BEGIN QUADSPI_MspInit 1 */

        /* USER CODE END QUADSPI_MspInit 1 */
    }
}

void HAL_QSPI_MspDeInit(QSPI_HandleTypeDef* qspiHandle)
{

  if(qspiHandle->Instance==QUADSPI)
  {
  /* USER CODE BEGIN QUADSPI_MspDeInit 0 */

  /* USER CODE END QUADSPI_MspDeInit 0 */
  /* Peripheral clock disable */
  __HAL_RCC_QSPI_CLK_DISABLE();

  /**QUADSPI GPIO Configuration
  PB6     ------> QUADSPI_BK1_NCS
  PF6     ------> QUADSPI_BK1_IO3
  PF7     ------> QUADSPI_BK1_IO2
  PF8     ------> QUADSPI_BK1_IO0
  PF9     ------> QUADSPI_BK1_IO1
  PB2     ------> QUADSPI_CLK
  */
  HAL_GPIO_DeInit(GPIOB, GPIO_PIN_6|GPIO_PIN_2);

  HAL_GPIO_DeInit(GPIOF, GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9);

  /* USER CODE BEGIN QUADSPI_MspDeInit 1 */

  /* USER CODE END QUADSPI_MspDeInit 1 */
  }
}

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x20000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_128KB;
  MPU_InitStruct.SubRegionDisable = 0;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* 配置SDRAM区域为Normal, 可缓存 */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress = 0xC0000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_32MB;
  MPU_InitStruct.SubRegionDisable = 0;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* 配置AXI DRAM区域为Normal, 可缓存 */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x24000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_512KB;
  MPU_InitStruct.SubRegionDisable = 0;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 8;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}


static SDRAM_HandleTypeDef hsdram1;

/* FMC initialization function */
void MX_FMC_Init(void)
{
  FMC_SDRAM_TimingTypeDef SdramTiming = {0};

  /** Perform the SDRAM1 memory initialization sequence
  */
  hsdram1.Instance = FMC_SDRAM_DEVICE;
  /* hsdram1.Init */
  hsdram1.Init.SDBank = FMC_SDRAM_BANK1;
  hsdram1.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_9;
  hsdram1.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_12;
  hsdram1.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_32;
  hsdram1.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
  hsdram1.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_2;
  hsdram1.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
  hsdram1.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;//200Mhz/2=100Mhz
  hsdram1.Init.ReadBurst = FMC_SDRAM_RBURST_ENABLE;
  hsdram1.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_0;
  /* SdramTiming */
  SdramTiming.LoadToActiveDelay = 2;
  SdramTiming.ExitSelfRefreshDelay = 7;
  SdramTiming.SelfRefreshTime = 5;
  SdramTiming.RowCycleDelay = 6;
  SdramTiming.WriteRecoveryTime = 3;
  SdramTiming.RPDelay = 2;
  SdramTiming.RCDDelay = 2;

  if (HAL_SDRAM_Init(&hsdram1, &SdramTiming) != HAL_OK)
  {
    Error_Handler( );
  }
}

static uint32_t FMC_Initialized = 0;

static void HAL_FMC_MspInit(void){
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if (FMC_Initialized) {
    return;
  }
  FMC_Initialized = 1;
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_FMC;
    PeriphClkInitStruct.PLL2.PLL2M = 25;
    PeriphClkInitStruct.PLL2.PLL2N = 200;
    PeriphClkInitStruct.PLL2.PLL2P = 2;
    PeriphClkInitStruct.PLL2.PLL2Q = 2;
    PeriphClkInitStruct.PLL2.PLL2R = 1;
    PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_0;
    PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
    PeriphClkInitStruct.FmcClockSelection = RCC_FMCCLKSOURCE_PLL2;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

  /* Peripheral clock enable */
  __HAL_RCC_FMC_CLK_ENABLE();

  /** FMC GPIO Configuration
  PI6   ------> FMC_D28
  PI5   ------> FMC_NBL3
  PI4   ------> FMC_NBL2
  PI1   ------> FMC_D25
  PI0   ------> FMC_D24
  PI7   ------> FMC_D29
  PE1   ------> FMC_NBL1
  PI2   ------> FMC_D26
  PH15   ------> FMC_D23
  PH14   ------> FMC_D22
  PE0   ------> FMC_NBL0
  PI3   ------> FMC_D27
  PG15   ------> FMC_SDNCAS
  PD0   ------> FMC_D2
  PH13   ------> FMC_D21
  PI9   ------> FMC_D30
  PD1   ------> FMC_D3
  PI10   ------> FMC_D31
  PG8   ------> FMC_SDCLK
  PF2   ------> FMC_A2
  PF1   ------> FMC_A1
  PF0   ------> FMC_A0
  PG5   ------> FMC_BA1
  PF3   ------> FMC_A3
  PG4   ------> FMC_BA0
  PF5   ------> FMC_A5
  PF4   ------> FMC_A4
  PC0   ------> FMC_SDNWE
  PC2   ------> FMC_SDNE0
  PC3   ------> FMC_SDCKE0
  PE10   ------> FMC_D7
  PF13   ------> FMC_A7
  PF14   ------> FMC_A8
  PE9   ------> FMC_D6
  PE11   ------> FMC_D8
  PH10   ------> FMC_D18
  PH11   ------> FMC_D19
  PD15   ------> FMC_D1
  PD14   ------> FMC_D0
  PF12   ------> FMC_A6
  PF15   ------> FMC_A9
  PE12   ------> FMC_D9
  PE15   ------> FMC_D12
  PH9   ------> FMC_D17
  PH12   ------> FMC_D20
  PF11   ------> FMC_SDNRAS
  PG0   ------> FMC_A10
  PE8   ------> FMC_D5
  PE13   ------> FMC_D10
  PH8   ------> FMC_D16
  PD10   ------> FMC_D15
  PD9   ------> FMC_D14
  PG1   ------> FMC_A11
  PE7   ------> FMC_D4
  PE14   ------> FMC_D11
  PD8   ------> FMC_D13
  */
  /* GPIO_InitStruct */
  GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_5|GPIO_PIN_4|GPIO_PIN_1
                          |GPIO_PIN_0|GPIO_PIN_7|GPIO_PIN_2|GPIO_PIN_3
                          |GPIO_PIN_9|GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF12_FMC;

  HAL_GPIO_Init(GPIOI, &GPIO_InitStruct);

  /* GPIO_InitStruct */
  GPIO_InitStruct.Pin = GPIO_PIN_1|GPIO_PIN_0|GPIO_PIN_10|GPIO_PIN_9
                          |GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_15|GPIO_PIN_8
                          |GPIO_PIN_13|GPIO_PIN_7|GPIO_PIN_14;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF12_FMC;

  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /* GPIO_InitStruct */
  GPIO_InitStruct.Pin = GPIO_PIN_15|GPIO_PIN_14|GPIO_PIN_13|GPIO_PIN_10
                          |GPIO_PIN_11|GPIO_PIN_9|GPIO_PIN_12|GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF12_FMC;

  HAL_GPIO_Init(GPIOH, &GPIO_InitStruct);

  /* GPIO_InitStruct */
  GPIO_InitStruct.Pin = GPIO_PIN_15|GPIO_PIN_8|GPIO_PIN_5|GPIO_PIN_4
                          |GPIO_PIN_0|GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF12_FMC;

  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /* GPIO_InitStruct */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_15|GPIO_PIN_14
                          |GPIO_PIN_10|GPIO_PIN_9|GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF12_FMC;

  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /* GPIO_InitStruct */
  GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_1|GPIO_PIN_0|GPIO_PIN_3
                          |GPIO_PIN_5|GPIO_PIN_4|GPIO_PIN_13|GPIO_PIN_14
                          |GPIO_PIN_12|GPIO_PIN_15|GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF12_FMC;

  HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

  /* GPIO_InitStruct */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_2|GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF12_FMC;

  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

void HAL_SDRAM_MspInit(SDRAM_HandleTypeDef* sdramHandle){
  HAL_FMC_MspInit();
}

static uint32_t FMC_DeInitialized = 0;

static void HAL_FMC_MspDeInit(void){
  if (FMC_DeInitialized) {
    return;
  }
  FMC_DeInitialized = 1;
  /* Peripheral clock enable */
  __HAL_RCC_FMC_CLK_DISABLE();

  /** FMC GPIO Configuration
  PI6   ------> FMC_D28
  PI5   ------> FMC_NBL3
  PI4   ------> FMC_NBL2
  PI1   ------> FMC_D25
  PI0   ------> FMC_D24
  PI7   ------> FMC_D29
  PE1   ------> FMC_NBL1
  PI2   ------> FMC_D26
  PH15   ------> FMC_D23
  PH14   ------> FMC_D22
  PE0   ------> FMC_NBL0
  PI3   ------> FMC_D27
  PG15   ------> FMC_SDNCAS
  PD0   ------> FMC_D2
  PH13   ------> FMC_D21
  PI9   ------> FMC_D30
  PD1   ------> FMC_D3
  PI10   ------> FMC_D31
  PG8   ------> FMC_SDCLK
  PF2   ------> FMC_A2
  PF1   ------> FMC_A1
  PF0   ------> FMC_A0
  PG5   ------> FMC_BA1
  PF3   ------> FMC_A3
  PG4   ------> FMC_BA0
  PF5   ------> FMC_A5
  PF4   ------> FMC_A4
  PC0   ------> FMC_SDNWE
  PC2   ------> FMC_SDNE0
  PC3   ------> FMC_SDCKE0
  PE10   ------> FMC_D7
  PF13   ------> FMC_A7
  PF14   ------> FMC_A8
  PE9   ------> FMC_D6
  PE11   ------> FMC_D8
  PH10   ------> FMC_D18
  PH11   ------> FMC_D19
  PD15   ------> FMC_D1
  PD14   ------> FMC_D0
  PF12   ------> FMC_A6
  PF15   ------> FMC_A9
  PE12   ------> FMC_D9
  PE15   ------> FMC_D12
  PH9   ------> FMC_D17
  PH12   ------> FMC_D20
  PF11   ------> FMC_SDNRAS
  PG0   ------> FMC_A10
  PE8   ------> FMC_D5
  PE13   ------> FMC_D10
  PH8   ------> FMC_D16
  PD10   ------> FMC_D15
  PD9   ------> FMC_D14
  PG1   ------> FMC_A11
  PE7   ------> FMC_D4
  PE14   ------> FMC_D11
  PD8   ------> FMC_D13
  */

  HAL_GPIO_DeInit(GPIOI, GPIO_PIN_6|GPIO_PIN_5|GPIO_PIN_4|GPIO_PIN_1
                          |GPIO_PIN_0|GPIO_PIN_7|GPIO_PIN_2|GPIO_PIN_3
                          |GPIO_PIN_9|GPIO_PIN_10);

  HAL_GPIO_DeInit(GPIOE, GPIO_PIN_1|GPIO_PIN_0|GPIO_PIN_10|GPIO_PIN_9
                          |GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_15|GPIO_PIN_8
                          |GPIO_PIN_13|GPIO_PIN_7|GPIO_PIN_14);

  HAL_GPIO_DeInit(GPIOH, GPIO_PIN_15|GPIO_PIN_14|GPIO_PIN_13|GPIO_PIN_10
                          |GPIO_PIN_11|GPIO_PIN_9|GPIO_PIN_12|GPIO_PIN_8);

  HAL_GPIO_DeInit(GPIOG, GPIO_PIN_15|GPIO_PIN_8|GPIO_PIN_5|GPIO_PIN_4
                          |GPIO_PIN_0|GPIO_PIN_1);

  HAL_GPIO_DeInit(GPIOD, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_15|GPIO_PIN_14
                          |GPIO_PIN_10|GPIO_PIN_9|GPIO_PIN_8);

  HAL_GPIO_DeInit(GPIOF, GPIO_PIN_2|GPIO_PIN_1|GPIO_PIN_0|GPIO_PIN_3
                          |GPIO_PIN_5|GPIO_PIN_4|GPIO_PIN_13|GPIO_PIN_14
                          |GPIO_PIN_12|GPIO_PIN_15|GPIO_PIN_11);

  HAL_GPIO_DeInit(GPIOC, GPIO_PIN_0|GPIO_PIN_2|GPIO_PIN_3);
}

void HAL_SDRAM_MspDeInit(SDRAM_HandleTypeDef* sdramHandle){
  HAL_FMC_MspDeInit();
}

/* SDRAM配置参数 */
#define SDRAM_MODEREG_BURST_LENGTH_1             ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_LENGTH_2             ((uint16_t)0x0001)
#define SDRAM_MODEREG_BURST_LENGTH_4             ((uint16_t)0x0002)
#define SDRAM_MODEREG_BURST_LENGTH_8             ((uint16_t)0x0004)
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL      ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_TYPE_INTERLEAVED     ((uint16_t)0x0008)
#define SDRAM_MODEREG_CAS_LATENCY_2              ((uint16_t)0x0020)
#define SDRAM_MODEREG_CAS_LATENCY_3              ((uint16_t)0x0030)
#define SDRAM_MODEREG_OPERATING_MODE_STANDARD    ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_PROGRAMMED ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE     ((uint16_t)0x0200)

void SDRAM_Init(void)
{
    MX_FMC_Init();

    FMC_SDRAM_CommandTypeDef  Command;
    /* 第一步，给SDRAM提供时钟 */
    Command.CommandMode           = FMC_SDRAM_CMD_CLK_ENABLE;
    Command.CommandTarget         = FMC_SDRAM_CMD_TARGET_BANK1;
    Command.AutoRefreshNumber     = 1;
    Command.ModeRegisterDefinition= 0;

    HAL_SDRAM_SendCommand(&hsdram1, &Command,0xFFFF);

    /* 第二步，延迟至少100us */
    HAL_Delay(1);

    /* 第三步，对所有BNAK进行预充电 */
    Command.CommandMode           = FMC_SDRAM_CMD_PALL;
    Command.CommandTarget         = FMC_SDRAM_CMD_TARGET_BANK1;
    Command.AutoRefreshNumber     = 1;
    Command.ModeRegisterDefinition= 0;

    HAL_SDRAM_SendCommand(&hsdram1, &Command,0xFFFF);

    /* 第四步，插入8个自动刷新周期 */
    Command.CommandMode           = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    Command.CommandTarget         = FMC_SDRAM_CMD_TARGET_BANK1;
    Command.AutoRefreshNumber     = 8;
    Command.ModeRegisterDefinition= 0;

    HAL_SDRAM_SendCommand(&hsdram1, &Command,0xFFFF);

    /* 第五步，编程加载模式寄存器 */
    __IO uint32_t temp =  (uint32_t)SDRAM_MODEREG_BURST_LENGTH_1| /* 设置突发长度:1(可以是1/2/4/8) */
            SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL   | /* 设置突发类型:连续(可以是连续/交错) */
            SDRAM_MODEREG_CAS_LATENCY_2           | /* 设置CAS值:2(可以是2/3) */
            SDRAM_MODEREG_OPERATING_MODE_STANDARD | /* 设置操作模式:0,标准模式 */
            SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;   /* 设置突发写模式:1,单点访问 */


    Command.CommandMode           = FMC_SDRAM_CMD_LOAD_MODE;
    Command.CommandTarget         = FMC_SDRAM_CMD_TARGET_BANK1;
    Command.AutoRefreshNumber     = 1;
    Command.ModeRegisterDefinition= temp;

    HAL_SDRAM_SendCommand(&hsdram1, &Command,0xFFFF);

    /* 第六步，配置自动刷新周期 */

    /* 刷新频率计数器(以SDCLK频率计数),计算方法:  */
    /* COUNT=SDRAM刷新周期/行数-20=SDRAM刷新周期(us)*SDCLK频率(Mhz)/行数 */
    /* 我们使用的SDRAM刷新周期为64ms,SDCLK=280/2=140Mhz,行数为4096(2^12) */
    /* 所以,COUNT=64*1000*140/4096-20=2168 */
    //  HAL_SDRAM_ProgramRefreshRate(&hsdram1,2168);//140MHz
    //  HAL_SDRAM_ProgramRefreshRate(&hsdram1,2059);//133MHz
    //  HAL_SDRAM_ProgramRefreshRate(&hsdram1,1855);//120MHz

    uint32_t Count = ((64 * 1000 * 100) / 4096) - 20;
    HAL_SDRAM_ProgramRefreshRate(&hsdram1, Count);//100MHz
}


#define LCD_BL_Pin GPIO_PIN_5
#define LCD_BL_GPIO_Port GPIOB
#define GT911_RST_Pin GPIO_PIN_8
#define GT911_RST_GPIO_Port GPIOI
#define GT911_SDA_Pin GPIO_PIN_7
#define GT911_SDA_GPIO_Port GPIOG
#define GT911_SCL_Pin GPIO_PIN_6
#define GT911_SCL_GPIO_Port GPIOH
#define GT911_INT_Pin GPIO_PIN_7
#define GT911_INT_GPIO_Port GPIOH
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOI_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOK_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOJ_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GT911_RST_GPIO_Port, GT911_RST_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GT911_SDA_GPIO_Port, GT911_SDA_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GT911_SCL_GPIO_Port, GT911_SCL_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : LCD_BL_Pin */
  GPIO_InitStruct.Pin = LCD_BL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(LCD_BL_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : GT911_RST_Pin */
  GPIO_InitStruct.Pin = GT911_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GT911_RST_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : GT911_SDA_Pin */
  GPIO_InitStruct.Pin = GT911_SDA_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GT911_SDA_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : GT911_SCL_Pin */
  GPIO_InitStruct.Pin = GT911_SCL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GT911_SCL_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : GT911_INT_Pin */
  GPIO_InitStruct.Pin = GT911_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GT911_INT_GPIO_Port, &GPIO_InitStruct);

}

#define LCD_WIDTH   800
#define LCD_HEIGHT  480

#define FB_ADDR     0xC0000000 //LTDC显存地址
LTDC_HandleTypeDef hltdc;

/* LTDC init function */
void MX_LTDC_Init(void)
{

  /* USER CODE BEGIN LTDC_Init 0 */

  /* USER CODE END LTDC_Init 0 */

  LTDC_LayerCfgTypeDef pLayerCfg = {0};

  /* USER CODE BEGIN LTDC_Init 1 */

  /* USER CODE END LTDC_Init 1 */

  hltdc.Instance = LTDC;
  hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
  hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
  hltdc.Init.HorizontalSync = 0;
  hltdc.Init.VerticalSync = 0;
  hltdc.Init.AccumulatedHBP = 46;
  hltdc.Init.AccumulatedVBP = 23;
  hltdc.Init.AccumulatedActiveW = 846;
  hltdc.Init.AccumulatedActiveH = 503;
  hltdc.Init.TotalWidth = 1056;
  hltdc.Init.TotalHeigh = 525;
  hltdc.Init.Backcolor.Blue = 0;
  hltdc.Init.Backcolor.Green = 0;
  hltdc.Init.Backcolor.Red = 0;
  if (HAL_LTDC_Init(&hltdc) != HAL_OK)
  {
    Error_Handler();
  }

  pLayerCfg.WindowX0 = 0;
  pLayerCfg.WindowX1 = LCD_WIDTH-1;
  pLayerCfg.WindowY0 = 0;
  pLayerCfg.WindowY1 = LCD_HEIGHT-1;
  pLayerCfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
  pLayerCfg.Alpha = 255;
  pLayerCfg.Alpha0 = 0;
  pLayerCfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
  pLayerCfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
  pLayerCfg.FBStartAdress = FB_ADDR;
  pLayerCfg.ImageWidth = LCD_WIDTH;
  pLayerCfg.ImageHeight = LCD_HEIGHT;
  pLayerCfg.Backcolor.Blue = 0;
  pLayerCfg.Backcolor.Green = 0;
  pLayerCfg.Backcolor.Red = 0;
  if (HAL_LTDC_ConfigLayer(&hltdc, &pLayerCfg, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LTDC_Init 2 */

  /* USER CODE END LTDC_Init 2 */

}

void HAL_LTDC_MspInit(LTDC_HandleTypeDef* ltdcHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(ltdcHandle->Instance==LTDC)
  {
  /* USER CODE BEGIN LTDC_MspInit 0 */

  /* USER CODE END LTDC_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
    PeriphClkInitStruct.PLL3.PLL3M = 1;
    PeriphClkInitStruct.PLL3.PLL3N = 6;
    PeriphClkInitStruct.PLL3.PLL3P = 2;
    PeriphClkInitStruct.PLL3.PLL3Q = 2;
    PeriphClkInitStruct.PLL3.PLL3R = 4;//150MHz/4=37.5MHz
    //PeriphClkInitStruct.PLL3.PLL3R = 3;//150MHz/3=50MHz
    PeriphClkInitStruct.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_3;
    PeriphClkInitStruct.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;
    PeriphClkInitStruct.PLL3.PLL3FRACN = 0;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* LTDC clock enable */
    __HAL_RCC_LTDC_CLK_ENABLE();

    __HAL_RCC_GPIOK_CLK_ENABLE();
    __HAL_RCC_GPIOJ_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();
    /**LTDC GPIO Configuration
    PK5     ------> LTDC_B6
    PK4     ------> LTDC_B5
    PJ15     ------> LTDC_B3
    PK6     ------> LTDC_B7
    PK3     ------> LTDC_B4
    PK7     ------> LTDC_DE
    PI12     ------> LTDC_HSYNC
    PI13     ------> LTDC_VSYNC
    PI14     ------> LTDC_CLK
    PK2     ------> LTDC_G7
    PK0     ------> LTDC_G5
    PK1     ------> LTDC_G6
    PJ11     ------> LTDC_G4
    PJ10     ------> LTDC_G3
    PJ9     ------> LTDC_G2
    PJ6     ------> LTDC_R7
    PJ5     ------> LTDC_R6
    PJ2     ------> LTDC_R3
    PJ3     ------> LTDC_R4
    PJ4     ------> LTDC_R5
    */
    GPIO_InitStruct.Pin = GPIO_PIN_5|GPIO_PIN_4|GPIO_PIN_6|GPIO_PIN_3
                          |GPIO_PIN_7|GPIO_PIN_2|GPIO_PIN_0|GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
    HAL_GPIO_Init(GPIOK, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_15|GPIO_PIN_11|GPIO_PIN_10|GPIO_PIN_9
                          |GPIO_PIN_6|GPIO_PIN_5|GPIO_PIN_2|GPIO_PIN_3
                          |GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
    HAL_GPIO_Init(GPIOJ, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF14_LTDC;
    HAL_GPIO_Init(GPIOI, &GPIO_InitStruct);

    /* LTDC interrupt Init */
    HAL_NVIC_SetPriority(LTDC_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(LTDC_IRQn);
    HAL_NVIC_SetPriority(LTDC_ER_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(LTDC_ER_IRQn);
  /* USER CODE BEGIN LTDC_MspInit 1 */

  /* USER CODE END LTDC_MspInit 1 */
  }
}

void HAL_LTDC_MspDeInit(LTDC_HandleTypeDef* ltdcHandle)
{

  if(ltdcHandle->Instance==LTDC)
  {
  /* USER CODE BEGIN LTDC_MspDeInit 0 */

  /* USER CODE END LTDC_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_LTDC_CLK_DISABLE();

    /**LTDC GPIO Configuration
    PK5     ------> LTDC_B6
    PK4     ------> LTDC_B5
    PJ15     ------> LTDC_B3
    PK6     ------> LTDC_B7
    PK3     ------> LTDC_B4
    PK7     ------> LTDC_DE
    PI12     ------> LTDC_HSYNC
    PI13     ------> LTDC_VSYNC
    PI14     ------> LTDC_CLK
    PK2     ------> LTDC_G7
    PK0     ------> LTDC_G5
    PK1     ------> LTDC_G6
    PJ11     ------> LTDC_G4
    PJ10     ------> LTDC_G3
    PJ9     ------> LTDC_G2
    PJ6     ------> LTDC_R7
    PJ5     ------> LTDC_R6
    PJ2     ------> LTDC_R3
    PJ3     ------> LTDC_R4
    PJ4     ------> LTDC_R5
    */
    HAL_GPIO_DeInit(GPIOK, GPIO_PIN_5|GPIO_PIN_4|GPIO_PIN_6|GPIO_PIN_3
                          |GPIO_PIN_7|GPIO_PIN_2|GPIO_PIN_0|GPIO_PIN_1);

    HAL_GPIO_DeInit(GPIOJ, GPIO_PIN_15|GPIO_PIN_11|GPIO_PIN_10|GPIO_PIN_9
                          |GPIO_PIN_6|GPIO_PIN_5|GPIO_PIN_2|GPIO_PIN_3
                          |GPIO_PIN_4);

    HAL_GPIO_DeInit(GPIOI, GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14);

    /* LTDC interrupt Deinit */
    HAL_NVIC_DisableIRQ(LTDC_IRQn);
    HAL_NVIC_DisableIRQ(LTDC_ER_IRQn);
  /* USER CODE BEGIN LTDC_MspDeInit 1 */

  /* USER CODE END LTDC_MspDeInit 1 */
  }
}

static DMA2D_HandleTypeDef hdma2d;

/* DMA2D init function */
void MX_DMA2D_Init(void)
{

  /* USER CODE BEGIN DMA2D_Init 0 */

  /* USER CODE END DMA2D_Init 0 */

  /* USER CODE BEGIN DMA2D_Init 1 */

  /* USER CODE END DMA2D_Init 1 */
  hdma2d.Instance = DMA2D;
  hdma2d.Init.Mode = DMA2D_M2M;
  hdma2d.Init.ColorMode = DMA2D_OUTPUT_RGB565;
  hdma2d.Init.OutputOffset = 0;
  hdma2d.LayerCfg[0].InputOffset = 0;
  hdma2d.LayerCfg[0].InputColorMode = DMA2D_INPUT_RGB565;
  hdma2d.LayerCfg[0].AlphaMode = DMA2D_NO_MODIF_ALPHA;
  hdma2d.LayerCfg[0].InputAlpha = 0;
  hdma2d.LayerCfg[0].AlphaInverted = DMA2D_REGULAR_ALPHA;
  hdma2d.LayerCfg[0].RedBlueSwap = DMA2D_RB_REGULAR;
  hdma2d.LayerCfg[0].ChromaSubSampling = DMA2D_NO_CSS;
  if (HAL_DMA2D_Init(&hdma2d) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_DMA2D_ConfigLayer(&hdma2d, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DMA2D_Init 2 */

  /* USER CODE END DMA2D_Init 2 */

}

void HAL_DMA2D_MspInit(DMA2D_HandleTypeDef* dma2dHandle)
{

    if(dma2dHandle->Instance==DMA2D)
    {
      /* USER CODE BEGIN DMA2D_MspInit 0 */

      /* USER CODE END DMA2D_MspInit 0 */
      /* DMA2D clock enable */
      __HAL_RCC_DMA2D_CLK_ENABLE();

      /* DMA2D interrupt Init */
      HAL_NVIC_SetPriority(DMA2D_IRQn, 0, 0);
      HAL_NVIC_EnableIRQ(DMA2D_IRQn);
      /* USER CODE BEGIN DMA2D_MspInit 1 */

      /* USER CODE END DMA2D_MspInit 1 */
    }
}

void HAL_DMA2D_MspDeInit(DMA2D_HandleTypeDef* dma2dHandle)
{

    if(dma2dHandle->Instance==DMA2D)
    {
      /* USER CODE BEGIN DMA2D_MspDeInit 0 */

      /* USER CODE END DMA2D_MspDeInit 0 */
      /* Peripheral clock disable */
      __HAL_RCC_DMA2D_CLK_DISABLE();

      /* DMA2D interrupt Deinit */
      HAL_NVIC_DisableIRQ(DMA2D_IRQn);
      /* USER CODE BEGIN DMA2D_MspDeInit 1 */

      /* USER CODE END DMA2D_MspDeInit 1 */
    }
}

/* 使用 DMA2D 将一个颜色填充到指定内存区域（RGB565） */
void dma2d_fill_color(uint32_t dst_addr, uint32_t color, uint32_t width, uint32_t height)
{
    const uint32_t dcache_line = 32U;
    uint32_t cache_start = dst_addr & ~(dcache_line - 1U);
    uint32_t cache_end = (dst_addr + width * height * 2U + dcache_line - 1U) & ~(dcache_line - 1U);

    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)cache_start,
                                      (int32_t)(cache_end - cache_start));
    /* 配置并使用 R2M 模式（寄存器到存储器）将常量颜色写入目标 */
    DMA2D->CR = 0;
    DMA2D->OPFCCR = DMA2D_OUTPUT_RGB565;   /* 输出格式 */
    DMA2D->OCOLR = (uint32_t)color;        /* 颜色值 */
    DMA2D->OMAR = dst_addr;                /* 目的地址 */
    DMA2D->OOR = 0;
    DMA2D->NLR = (width << 16) | height;
    DMA2D->CR = DMA2D_R2M | DMA2D_CR_START; /* 开始传输 */

    /* 等待完成 */
    while (DMA2D->CR & DMA2D_CR_START) { __NOP(); }

    /* 清除数据缓存，确保外设/CPU 可见 */
    SCB_InvalidateDCache_by_Addr((uint32_t *)cache_start,
                                 (int32_t)(cache_end - cache_start));
}
/* DMA2D拷贝 */
void dma2d_copy(uint32_t dst, uint32_t src, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
  uint32_t offset = ((y * LCD_WIDTH) + x) * 2;
  uint32_t src_addr = src + offset;
  uint32_t dst_addr = dst + offset;

  if (w == 0U || h == 0U || x + w > LCD_WIDTH || y + h > LCD_HEIGHT) return;

  /* Include the framebuffer stride between the first and last copied lines. */
  uint32_t span_size = (((h - 1U) * LCD_WIDTH) + w) * 2U;

  /* 对缓存行做对齐，SCB_*DCache_by_Addr 需要对齐到缓存线 */
  const uint32_t DCACHE_LINE = 32;
  uint32_t clean_start = src_addr & ~(DCACHE_LINE - 1);
  uint32_t clean_end = (src_addr + span_size + DCACHE_LINE - 1) & ~(DCACHE_LINE - 1);
  uint32_t clean_size = clean_end - clean_start;

  /* 在 DMA 读取源数据前，清理源地址的 DCache，保证外设读取到最新数据 */
  SCB_CleanDCache_by_Addr((uint32_t *)clean_start, clean_size);

  uint32_t inv_start = dst_addr & ~(DCACHE_LINE - 1);
  uint32_t inv_end = (dst_addr + span_size + DCACHE_LINE - 1) & ~(DCACHE_LINE - 1);
  uint32_t inv_size = inv_end - inv_start;
  SCB_CleanInvalidateDCache_by_Addr((uint32_t *)inv_start, (int32_t)inv_size);

  /* 清除可能的中断标志位，配置传输寄存器 */
  DMA2D->IFCR = DMA2D_IFCR_CTCIF | DMA2D_IFCR_CTEIF;
  DMA2D->CR       = DMA2D_M2M;
  DMA2D->FGPFCCR  = DMA2D_INPUT_RGB565;
  DMA2D->FGMAR    = src_addr;
  DMA2D->FGOR     = LCD_WIDTH - w;
  DMA2D->OPFCCR   = DMA2D_OUTPUT_RGB565;
  DMA2D->OMAR     = dst_addr;
  DMA2D->OOR      = LCD_WIDTH - w;
  DMA2D->NLR      = (w << 16) | (h & 0xFFFF);

  /* 不启用中断，使用轮询+超时，避免 ISR 或中断未处理导致挂起 */
  DMA2D->CR |= DMA2D_CR_START;                 //开始传输

  /* 等待完成，带超时以防止永久阻塞 */
  volatile uint32_t timeout = 0x1000000;
  while ((DMA2D->CR & DMA2D_CR_START) && --timeout) { __NOP(); }
  if (timeout == 0)
  {
    LOG_E("dma2d_copy timeout: dst=0x%08X src=0x%08X w=%u h=%u", dst_addr, src_addr, w, h);
    /* 清除中断标志，尝试复位状态 */
    DMA2D->IFCR = DMA2D_IFCR_CTCIF | DMA2D_IFCR_CTEIF;
  }

  /* DMA 写入后，失效目的地址的 DCache，使 CPU 能看到最新数据 */
  SCB_InvalidateDCache_by_Addr((uint32_t *)inv_start, (int32_t)inv_size);
}

int board_peripheral_init(void)
{
    MPU_Config();
    SCB_EnableICache();
    SCB_EnableDCache();
    MX_QUADSPI_Init();
    SDRAM_Init();
    MX_LTDC_Init();
    MX_DMA2D_Init();
    dma2d_fill_color(FB_ADDR, 0x00, LCD_WIDTH, LCD_HEIGHT);//清屏
    return 0;
}
INIT_BOARD_EXPORT(board_peripheral_init);

#define GT911_RST_PIN   GET_PIN(I, 8)   /* RST -> PI8 */
#define GT911_IRQ_PIN   GET_PIN(H, 7)   /* INT -> PH7 */

int rt_hw_gt911_port(void)
{
    int ret = -1;
    struct rt_touch_config cfg;
    rt_uint8_t rst_pin;

    rst_pin = GT911_RST_PIN;

    /* 指定使用软件 I2C1 总线 */
    cfg.dev_name = "i2c1";

    /* 配置中断引脚 */
    cfg.irq_pin.pin = GT911_IRQ_PIN;
    cfg.irq_pin.mode = PIN_MODE_INPUT_PULLDOWN;  /* 或 PIN_MODE_INPUT_PULLUP */

    /* 传入 RST 引脚信息 */
    cfg.user_data = &rst_pin;

    /* 初始化并注册 GT911 设备 */
    ret = rt_hw_gt911_init("gt911", &cfg);

    return ret;
}
INIT_DEVICE_EXPORT(rt_hw_gt911_port);

#if 1
#define SDRAM_TEST_SIZE    (32 * 1024 * 1024)
#define SDRAM_TEST_ADDR    ((uint32_t *)FB_ADDR)
void test_sdram(void)
{

    uint32_t *ptr = SDRAM_TEST_ADDR;
    uint32_t i, errors = 0;
    //int var = 0;
    rt_tick_t start, end;

    LOG_I("SDRAM Test Start @ 0x%08X", SDRAM_TEST_ADDR);
    start = rt_tick_get();
    /* 写入测试模式 */
    for(i = 0; i < SDRAM_TEST_SIZE/4; i++)
    {
       ptr[i] = i;  /* 写入地址相关的数据 */
       //SCB_CleanDCache_by_Addr(ptr+i, 4);
    }
    SCB_CleanInvalidateDCache(); // 关键：确保数据真正写入物理总线
    /* 验证 */
    for(i = 0; i < SDRAM_TEST_SIZE/4; i++)
    {
       //SCB_InvalidateDCache_by_Addr(ptr+i, 4);
       if(ptr[i] != i)
       {
           errors++;
           if(errors < 20)
           {
               rt_kprintf("[i=%d]Error at 0x%p read:0x%08X\n", i, &ptr[i], ptr[i]);
           }
       }
    }
    end = rt_tick_get();
    uint32_t time = end - start;
    LOG_I("SDRAM Test %s, %d errors, write and read %d Byte, time: %d ms\n",
              errors == 0 ? "PASSED" : "FAILED", errors, SDRAM_TEST_SIZE,   time);
}
MSH_CMD_EXPORT(test_sdram,Test sdram read and write commands);
#endif

#if 1
uint8_t  my_buffer[1024 * 512] __attribute__((section(".axi_sram"))) __attribute__((aligned(32)));
void test_dma2d_copy(void)
{
    memset((void*)(&my_buffer[0]), 0x0790, LCD_WIDTH * LCD_HEIGHT);
    dma2d_copy(FB_ADDR, (uint32_t)(&my_buffer[0]), 0, 0, LCD_WIDTH, LCD_HEIGHT/2);
}
MSH_CMD_EXPORT(test_dma2d_copy,Test dma2d commands);
#endif

#if 1
void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint16_t *fb = (uint16_t *)FB_ADDR;

    for (uint16_t row = 0; row < h; row++) {
        for (uint16_t col = 0; col < w; col++) {
            fb[(y + row) * LCD_WIDTH + (x + col)] = color;
        }
    }

    /* 只清理修改区域的 Cache，效率更高 */
    uint32_t clean_addr = FB_ADDR + (y * LCD_WIDTH + x) * 2;
    uint32_t clean_size = (h * LCD_WIDTH) * 2;  /* 清理整行范围，确保覆盖 */
    SCB_CleanDCache_by_Addr((uint32_t *)clean_addr, clean_size);
}
void test_lcd_using_cpu(void)
{
    uint16_t *ptr = (uint16_t *)FB_ADDR;

    /* 1. 全屏红色背景 */
    for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++) {
        ptr[i] = 0xF800;  /* 红色 */
    }
    SCB_CleanDCache_by_Addr((uint32_t *)FB_ADDR, LCD_WIDTH * LCD_HEIGHT * 2);

    /* 2. 左上角画 100x100 蓝色方块 */
    lcd_fill_rect(0, 0, 100, 100, 0x001F);  /* 蓝色 */

    /* 3. 右下角画 100x100 绿色方块 */
    lcd_fill_rect(LCD_WIDTH-100, LCD_HEIGHT-100, 100, 100, 0x07E0);  /* 绿色 */
}
MSH_CMD_EXPORT(test_lcd_using_cpu, Test lcd using CPU);
void test_lcd_using_dma(void)
{
  dma2d_fill_color(FB_ADDR, 0xF800, LCD_WIDTH, LCD_HEIGHT);
  rt_thread_mdelay(1500);
  dma2d_fill_color(FB_ADDR, 0x001F, LCD_WIDTH, LCD_HEIGHT);
}
MSH_CMD_EXPORT(test_lcd_using_dma, Test lcd using DMA2D);
#endif
