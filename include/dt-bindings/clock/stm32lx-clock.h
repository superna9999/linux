/*
 * stm32lx-clock.h
 *
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef _DT_BINDINGS_CLK_STMLX_H
#define _DT_BINDINGS_CLK_STMLX_H

#define CLKID_LSI_OSC		0
#define CLKID_HSI48_OSC		1
#define CLKID_HSI_OSC		2
#define CLKID_IWDG		3
#define CLKID_MCO		4
#define CLKID_CORTEX_FR		5
#define CLKID_CORTEX_TICK	6	
#define CLKID_HCLK_DIV8		7
#define CLKID_HSE_DIV32		8
#define CLKID_HSE		9
#define CLKID_HSI		10
#define CLKID_MSI		11
#define CLKID_PLLCLK		12
#define CLKID_PLL48M1CLK	13	
#define CLKID_PLL48M2CLK	14	
#define CLKID_PLLSAI1CLK	15	
#define CLKID_PLLSAI2CLK	16	
#define CLKID_PLLADC1CLK	17	
#define CLKID_PLLADC2CLK	18	
#define CLKID_DMA1		19
#define CLKID_DMA2		20
#define CLKID_FLASH		21
#define CLKID_CRC		22
#define CLKID_TSC		23
#define CLKID_DMA2D		24
#define CLKID_GPIOA		25
#define CLKID_GPIOB		26
#define CLKID_GPIOC		27
#define CLKID_GPIOD		28
#define CLKID_GPIOE		29
#define CLKID_GPIOF		30
#define CLKID_GPIOG		31
#define CLKID_GPIOH		32
#define CLKID_GPIOI		33
#define CLKID_OTGFS		34
#define CLKID_ADC		35
#define CLKID_AES		36
#define CLKID_RNG		37
#define CLKID_FMC		38
#define CLKID_QSPI		39
#define CLKID_TIM2		40
#define CLKID_TIM3		41
#define CLKID_TIM4		42
#define CLKID_TIM5		43
#define CLKID_TIM6		44
#define CLKID_TIM7		45
#define CLKID_LCD		46
#define CLKID_WWDG		47
#define CLKID_SPI2		48
#define CLKID_SPI3		49
#define CLKID_USART2		50
#define CLKID_USART3		51
#define CLKID_USART4		52
#define CLKID_USART5		53
#define CLKID_I2C1		54
#define CLKID_I2C2		55
#define CLKID_I2C3		56
#define CLKID_CAN1		57
#define CLKID_PWR		58
#define CLKID_DAC		59
#define CLKID_OPAMP		60
#define CLKID_LPTIM1		61
#define CLKID_LPUART1		62
#define CLKID_I2C4		63	
#define CLKID_SWPMI1		64
#define CLKID_LPTIM2		65
#define CLKID_SYSCFG		66
#define CLKID_FW		67
#define CLKID_SDMMC1		68
#define CLKID_TIM1		69
#define CLKID_SPI1		70
#define CLKID_TIM8		71
#define CLKID_USART1		72
#define CLKID_TIM15		73
#define CLKID_TIM16		74
#define CLKID_TIM17		75
#define CLKID_SAI1		76
#define CLKID_SAI2		77
#define CLKID_DFSDM1		78
#define CLKID_LSCO		79
#define CLKID_RTC		80
#define CLKID_LSE		81
#define CLKID_LSI		82
#define CLKID_HSI48		83
#define CLKID_PLL_SEL		84
#define CLKID_RTC_LCD_SEL	85	
#define CLKID_LSCO_SEL		86	
#define CLKID_MCO_SEC		87
#define CLKID_SYSCLK		88
#define CLKID_USART1_SEL	89
#define CLKID_USART2_SEL	90
#define CLKID_USART3_SEL	91
#define CLKID_USART4_SEL	92
#define CLKID_USART5_SEL	93
#define CLKID_LPUART1_SEL	94	
#define CLKID_I2C1_SEL		95
#define CLKID_I2C2_SEL		96
#define CLKID_I2C3_SEL		97
#define CLKID_LPTIM1_SEL	98	
#define CLKID_LPTIM2_SEL	99	
#define CLKID_SAI1_SEL		100
#define CLKID_SAI2_SEL		101
#define CLKID_CLK48_SEL		102
#define CLKID_ADC_SEL		103
#define CLKID_SWPMI_SEL		104
#define CLKID_DFSDM1_SEL	105	
#define CLKID_I2C4_SEL		106
#define CLKID_PLL_P		107
#define CLKID_PLLSAI1_P		108
#define CLKID_PLLSAI2_P		109
#define CLKID_PLL_Q		110
#define CLKID_PLL_R		111
#define CLKID_PLLSAI1_Q		112
#define CLKID_PLLSAI1_R		113
#define CLKID_PLLSAI2_R		114
#define CLKID_PLL_PREDIV	115	
#define CLKID_MCO_DIV		116
#define CLKID_APB1_PRESC	117	
#define CLKID_APB2_PRESC	118	
#define CLKID_AHB_PRESC		119
#define CLKID_PLL		120
#define CLKID_PLLSAI1		121
#define CLKID_PLLSAI2		122
#define CLKID_MSI_RC		123

#define NR_CLKS			124

#endif
