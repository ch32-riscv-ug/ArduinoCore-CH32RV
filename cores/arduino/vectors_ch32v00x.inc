/* CH32V00X (CH32V002/V004/V005/V006/V007/CH32M007) interrupt vector map.
 * Own transcription of the factual interrupt number map (CH32V00X Reference
 * Manual). To be replaced by generation from ch32-device-data.
 * Entry 0 (reset) is emitted by crt0_ch32.S; this file starts at entry 1.
 */
    CH32_RSV                            /*  1 reserved */
    CH32_IRQ NMI_Handler                /*  2 NMI */
    CH32_IRQ HardFault_Handler          /*  3 HardFault */
    CH32_RSV                            /*  4 */
    CH32_RSV                            /*  5 */
    CH32_RSV                            /*  6 */
    CH32_RSV                            /*  7 */
    CH32_RSV                            /*  8 */
    CH32_RSV                            /*  9 */
    CH32_RSV                            /* 10 */
    CH32_RSV                            /* 11 */
    CH32_IRQ SysTick_Handler            /* 12 SysTick */
    CH32_RSV                            /* 13 */
    CH32_IRQ SW_Handler                 /* 14 software */
    CH32_RSV                            /* 15 */
    CH32_IRQ WWDG_IRQHandler            /* 16 window watchdog */
    CH32_IRQ PVD_IRQHandler             /* 17 PVD (EXTI) */
    CH32_IRQ FLASH_IRQHandler           /* 18 flash */
    CH32_IRQ RCC_IRQHandler             /* 19 RCC */
    CH32_IRQ EXTI7_0_IRQHandler         /* 20 EXTI 7..0 */
    CH32_IRQ AWU_IRQHandler             /* 21 auto wakeup */
    CH32_IRQ DMA1_Channel1_IRQHandler   /* 22 */
    CH32_IRQ DMA1_Channel2_IRQHandler   /* 23 */
    CH32_IRQ DMA1_Channel3_IRQHandler   /* 24 */
    CH32_IRQ DMA1_Channel4_IRQHandler   /* 25 */
    CH32_IRQ DMA1_Channel5_IRQHandler   /* 26 */
    CH32_IRQ DMA1_Channel6_IRQHandler   /* 27 */
    CH32_IRQ DMA1_Channel7_IRQHandler   /* 28 */
    CH32_IRQ ADC1_IRQHandler            /* 29 */
    CH32_IRQ I2C1_EV_IRQHandler         /* 30 */
    CH32_IRQ I2C1_ER_IRQHandler         /* 31 */
    CH32_IRQ USART1_IRQHandler          /* 32 */
    CH32_IRQ SPI1_IRQHandler            /* 33 */
    CH32_IRQ TIM1_BRK_IRQHandler        /* 34 */
    CH32_IRQ TIM1_UP_IRQHandler         /* 35 */
    CH32_IRQ TIM1_TRG_COM_IRQHandler    /* 36 */
    CH32_IRQ TIM1_CC_IRQHandler         /* 37 */
    CH32_IRQ TIM2_IRQHandler            /* 38 */
    CH32_IRQ USART2_IRQHandler          /* 39 */
    CH32_IRQ OPCM_IRQHandler            /* 40 */
