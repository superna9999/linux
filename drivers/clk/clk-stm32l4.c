/*
 * Author: Daniel Thompson <daniel.thompson@linaro.org>
 * Copyright (C) 2017 Baylibre, SAS.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * Inspired by clk-stm32l4.c and meson/gxbb.c
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>

#include <dt-bindings/clock/stm32lx-clock.h>
#include <dt-bindings/reset/stm32lx-reset.h>

#define STM32L4_RCC_CR			0x00
#define STM32L4_RCC_ICSCR		0x04
#define STM32L4_RCC_CFGR		0x08
#define STM32L4_RCC_PLLCFGR		0x0c
#define STM32L4_RCC_PLLSAI1CFGR		0x10
#define STM32L4_RCC_PLLSAI2CFGR		0x14
#define STM32L4_RCC_CIER		0x18
#define STM32L4_RCC_CIFR		0x1c
#define STM32L4_RCC_CICR		0x20
#define STM32L4_RCC_AHB1RSTR		0x28
#define STM32L4_RCC_AHB2RSTR		0x2c
#define STM32L4_RCC_AHB3RSTR		0x30
#define STM32L4_RCC_APB1RSTR1		0x38
#define STM32L4_RCC_APB1RSTR2		0x3c
#define STM32L4_RCC_APB2RSTR		0x40
#define STM32L4_RCC_AHB1ENR		0x48
#define STM32L4_RCC_AHB2ENR		0x4c
#define STM32L4_RCC_AHB3ENR		0x50
#define STM32L4_RCC_APB1ENR1		0x58
#define STM32L4_RCC_APB1ENR2		0x5c
#define STM32L4_RCC_APB2ENR		0x60
#define STM32L4_RCC_AHB1SMENR		0x68
#define STM32L4_RCC_AHB2SMENR		0x6c
#define STM32L4_RCC_AHB3SMENR		0x70
#define STM32L4_RCC_APB1SMENR1		0x78
#define STM32L4_RCC_APB1SMENR2		0x7c
#define STM32L4_RCC_APB2SMENR		0x80
#define STM32L4_RCC_CCIPR		0x88
#define STM32L4_RCC_BDCR		0x90
#define STM32L4_RCC_CSR			0x94
#define STM32L4_RCC_CRRCR		0x98
#define STM32L4_RCC_CCIPR2		0x9c

static DEFINE_SPINLOCK(clk_lock);

static const char * const lsi_parents[] = {
	"lsi_osc",
};

static const char * const hsi48_parents[] = {
	"hsi48_osc",
};

static const char * const hse_parents[] = {
	"xtal",
};

static const char * const lse_parents[] = {
	"xtal_32768",
};

static const char * const hsi_parents[] = {
	"hsi_osc",
};

static const char * const msi_parents[] = {
	"msi_rc",
};

static const char * const sysclk_parents[] = {
	"msi", "hsi", "hse", "pllclk",
};

static const char * const mco_div_mux_parents[] = {
	"off", "sysclk", "msi", "hsi", "hse", "pllclk", "lsi", "lse", "hsi48",
};

static const char * const mco_div_parents[] = {
	"mco_div_mux",
};

static const char * const mco_parents[] = {
	"mco_div",
};

static const char * const pll_prediv_mux_parents[] = {
	"off", "msi", "hsi", "hse",
};

static const char * const pll_prediv_parents[] = {
	"pll_prediv_mux",
};

static const char * const pll_parents[] = {
	"pll_prediv",
};

static const char * const pll_p_q_r_parents[] = {
	"pll",
};

static const char * const pllsai3clk_parents[] = {
	"pll_p",
};

static const char * const pll48m1clk_parents[] = {
	"pll_q",
};

static const char * const pllclk_parents[] = {
	"pll_r",
};

static const char * const pllsai1_p_q_r_parents[] = {
	"pllsai1",
};

static const char * const pllsai1clk_parents[] = {
	"pllsai1_p",
};

static const char * const pll48m2clk_parents[] = {
	"pllsai1_q",
};

static const char * const plladc1clk_parents[] = {
	"pllsai1_r",
};

static const char * const pllsai2_p_q_r_parents[] = {
	"pllsai2",
};

static const char * const pllsai2clk_parents[] = {
	"pllsai2_p",
};

static const char * const plladc2clk_parents[] = {
	"pllsai2_r",
};

static const char * const rtc_lcd_mux_parents[] = {
	"off", "lse", "lsi", "hse_div32",
};

static const char * const rtc_lcd_parents[] = {
	"rtc_lcd_mux",
};

static const char * const hse_div32_parents[] = {
	"hse",
};

static const char * const lsco_mux_parents[] = {
	"lsi", "lse",
};

static const char * const lsco_parents[] = {
	"lsco_mux",
};

static const char * const iwdg_parents[] = {
	"lsi",
};

static const char * const clk48_mux_parents[] = {
	"hsi48", "pll48m2clk", "pll48m1clk", "msi",
};

static const char * const clk48_parents[] = {
	"clk48_mux",
};

static const char * const adc_mux_parents[] = {
	"off", "plladc1clk", "plladc2clk", "sysclk",
};

static const char * const adc_parents[] = {
	"adc_mux",
};

static const char * const sai1_mux_parents[] = {
	"pllsai1clk", "pllsai2clk", "pllsai3clk", "sai1_extclk",
};

static const char * const sai1_parents[] = {
	"sai1_mux",
};

static const char * const sai2_mux_parents[] = {
	"pllsai1clk", "pllsai2clk", "pllsai3clk", "sai2_extclk",
};

static const char * const sai2_parents[] = {
	"sai2_mux",
};

static const char * const pwr_parents[] = {
	"sysclk",
};

static const char * const ahb_presc_parents[] = {
	"sysclk",
};

static const char * const hclk_parents[] = {
	"ahb_presc",
};

static const char * const cortex_free_running_parents[] = {
	"hclk",
};

static const char * const ahb_bus_core_memory_dma_parents[] = {
	"hclk",
};

static const char * const hclk_div8_parents[] = {
	"hclk",
};

static const char * const cortex_systick_parents[] = {
	"hclk_div8",
};

static const char * const apb1_presc_parents[] = {
	"hclk",
};

static const char * const pclk1_parents[] = {
	"apb1_presc",
};

static const char * const apb1_periphs_parents[] = {
	"pclk1",
};

static const char * const apb2_presc_parents[] = {
	"hclk",
};

static const char * const pclk2_parents[] = {
	"apb2_presc",
};

static const char * const apb2_periphs_parents[] = {
	"pclk2",
};

static const char * const uart_2_5_mux_parents[] = {
	"pclk1", "sysclk", "hsi", "lse",
};

static const char * const uart2_parents[] = {
	"uart2_mux",
};

static const char * const uart3_parents[] = {
	"uart3_mux",
};

static const char * const uart4_parents[] = {
	"uart4_mux",
};

static const char * const uart5_parents[] = {
	"uart5_mux",
};

static const char * const lpuart1_parents[] = {
	"lpuart1_mux",
};

static const char * const i2c_mux_parents[] = {
	"pclk1", "sysclk", "hsi",
};

static const char * const i2c1_parents[] = {
	"i2c1_mux",
};

static const char * const i2c2_parents[] = {
	"i2c2_mux",
};

static const char * const i2c3_parents[] = {
	"i2c3_mux",
};

static const char * const i2c4_parents[] = {
	"i2c4_mux",
};

static const char * const lptim_mux_parents[] = {
	"pclk1", "lsi", "hsi", "lse",
};

static const char * const lptim1_parents[] = {
	"lptim1_mux",
};

static const char * const lptim2_parents[] = {
	"lptim2_mux",
};

static const char * const swpmi_mux_parents[] = {
	"pclk1", "hsi",
};

static const char * const swpmi_parents[] = {
	"swpmi_mux",
};

static const char * const dfsdm1_mux_parents[] = {
	"pclk2", "sysclk",
};

static const char * const dfsdm1_parents[] = {
	"dfsdm1_mux",
};

static const char * const uart1_mux_parents[] = {
	"pclk2", "sysclk", "hsi", "lse",
};

static const char * const uart1_parents[] = {
	"uart1_mux",
};

/* Fixed Clocks */

#define RCC_FIXED(_name, _rate, _flags)					\
struct clk_fixed_rate _name = {						\
	.fixed_rate = (_rate),						\
	.hw.init = &(struct clk_init_data){				\
		.name = #_name,						\
		.ops = &clk_fixed_rate_ops,				\
		.flags = (_flags),					\
	},								\
};

static RCC_FIXED(lsi_osc, 32000, 0);
static RCC_FIXED(hsi48_osc, 48000000, 0);
static RCC_FIXED(hsi_osc, 16000000, 0);

/* 1:1 Clocks */

#define RCC_CLK(_name, _mult, _div, __parents, _flags)			\
struct clk_fixed_factor _name = {					\
	.mult = (_mult),						\
	.div = (_div),							\
	.hw.init = &(struct clk_init_data){				\
		.name = #_name,						\
		.ops = &clk_fixed_factor_ops,				\
		.parent_names = __parents ## _parents,			\
		.num_parents = ARRAY_SIZE(__parents ## _parents),	\
		.flags = CLK_SET_RATE_PARENT | (_flags),		\
	},								\
};

static RCC_CLK(iwdg, 1, 1, iwdg, 0);
static RCC_CLK(mco, 1, 1, mco, 0);
static RCC_CLK(cortex_free_running, 1, 1, cortex_free_running, CLK_IGNORE_UNUSED);
static RCC_CLK(cortex_systick, 1, 1, cortex_systick, CLK_IGNORE_UNUSED);
static RCC_CLK(hclk_div8, 1, 8, hclk_div8, CLK_IGNORE_UNUSED);
static RCC_CLK(hse_div32, 1, 32, hse_div32, 0);

/* Clock Gates */

#define RCC_GATE(_name, _reg, _bit, __parents, _flags)			\
struct clk_gate _name = {						\
	.reg = (void __iomem *) _reg,					\
	.bit_idx = (_bit),						\
	.lock = &clk_lock,						\
	.hw.init = &(struct clk_init_data){				\
		.name = #_name,						\
		.ops = &clk_gate_ops,					\
		.parent_names = __parents ## _parents,			\
		.num_parents = ARRAY_SIZE(__parents ## _parents),	\
		.flags = CLK_SET_RATE_PARENT | (_flags),		\
	},								\
};

static RCC_GATE(hse, STM32L4_RCC_CR, 16, hse, 0);
static RCC_GATE(hsi, STM32L4_RCC_CR, 8, hsi, 0);
static RCC_GATE(msi, STM32L4_RCC_CR, 0, msi, 0);

static RCC_GATE(pllclk, STM32L4_RCC_PLLCFGR, 24, pllclk, 0);
static RCC_GATE(pll48m1clk, STM32L4_RCC_PLLCFGR, 20, pll48m1clk, 0);
static RCC_GATE(pll48m2clk, STM32L4_RCC_PLLSAI1CFGR, 20, pll48m2clk, 0);
static RCC_GATE(pllsai1clk, STM32L4_RCC_PLLSAI1CFGR, 16, pllsai1clk, 0);
static RCC_GATE(pllsai2clk, STM32L4_RCC_PLLSAI2CFGR, 16, pllsai2clk, 0);
static RCC_GATE(plladc1clk, STM32L4_RCC_PLLSAI1CFGR, 24, plladc1clk, 0);
static RCC_GATE(plladc2clk, STM32L4_RCC_PLLSAI2CFGR, 24, plladc2clk, 0);

static RCC_GATE(dma1, STM32L4_RCC_AHB1ENR, 0, ahb_bus_core_memory_dma, 0);
static RCC_GATE(dma2, STM32L4_RCC_AHB1ENR, 1, ahb_bus_core_memory_dma, 0);
static RCC_GATE(flash, STM32L4_RCC_AHB1ENR, 8, ahb_bus_core_memory_dma, 0);
static RCC_GATE(crc, STM32L4_RCC_AHB1ENR, 12, ahb_bus_core_memory_dma, 0);
static RCC_GATE(tsc, STM32L4_RCC_AHB1ENR, 16, ahb_bus_core_memory_dma, 0);
static RCC_GATE(dma2d, STM32L4_RCC_AHB1ENR, 17, ahb_bus_core_memory_dma, 0);

static RCC_GATE(gpioa, STM32L4_RCC_AHB2ENR,  0,	ahb_bus_core_memory_dma, 0);
static RCC_GATE(gpiob, STM32L4_RCC_AHB2ENR,  1,	ahb_bus_core_memory_dma, 0);
static RCC_GATE(gpioc, STM32L4_RCC_AHB2ENR,  2,	ahb_bus_core_memory_dma, 0);
static RCC_GATE(gpiod, STM32L4_RCC_AHB2ENR,  3,	ahb_bus_core_memory_dma, 0);
static RCC_GATE(gpioe, STM32L4_RCC_AHB2ENR,  4,	ahb_bus_core_memory_dma, 0);
static RCC_GATE(gpiof, STM32L4_RCC_AHB2ENR,  5,	ahb_bus_core_memory_dma, 0);
static RCC_GATE(gpiog, STM32L4_RCC_AHB2ENR,  6,	ahb_bus_core_memory_dma, 0);
static RCC_GATE(gpioh, STM32L4_RCC_AHB2ENR,  7,	ahb_bus_core_memory_dma, 0);
static RCC_GATE(gpioi, STM32L4_RCC_AHB2ENR,  8,	ahb_bus_core_memory_dma, 0);
static RCC_GATE(otgfs, STM32L4_RCC_AHB2ENR,  12, clk48, 0);
static RCC_GATE(adc, STM32L4_RCC_AHB2ENR,  13,	adc, 0);
static RCC_GATE(aes, STM32L4_RCC_AHB2ENR,  16,	ahb_bus_core_memory_dma, 0);
static RCC_GATE(rng, STM32L4_RCC_AHB2ENR,  18,	clk48, 0);

static RCC_GATE(fmc, STM32L4_RCC_AHB3ENR,  0, ahb_bus_core_memory_dma,
		CLK_IGNORE_UNUSED);
static RCC_GATE(qspi, STM32L4_RCC_AHB3ENR,  8, 	ahb_bus_core_memory_dma,
		CLK_IGNORE_UNUSED);

static RCC_GATE(tim2, STM32L4_RCC_APB1ENR1,  0,	pclk1, 0);
static RCC_GATE(tim3, STM32L4_RCC_APB1ENR1,  1,	pclk1, 0);
static RCC_GATE(tim4, STM32L4_RCC_APB1ENR1,  2,	pclk1, 0);
static RCC_GATE(tim5, STM32L4_RCC_APB1ENR1,  3,	pclk1, 0);
static RCC_GATE(tim6, STM32L4_RCC_APB1ENR1,  4,	pclk1, 0);
static RCC_GATE(tim7, STM32L4_RCC_APB1ENR1,  5,	pclk1, 0);
static RCC_GATE(lcd, STM32L4_RCC_APB1ENR1,  9,	rtc_lcd, 0);
static RCC_GATE(wwdg, STM32L4_RCC_APB1ENR1, 11,	pclk1, 0);
static RCC_GATE(spi2, STM32L4_RCC_APB1ENR1, 14,	pclk1, 0);
static RCC_GATE(spi3, STM32L4_RCC_APB1ENR1, 15,	pclk1, 0);
static RCC_GATE(uart2, STM32L4_RCC_APB1ENR1, 17, uart2, 0);
static RCC_GATE(uart3, STM32L4_RCC_APB1ENR1, 18, uart3, 0);
static RCC_GATE(uart4, STM32L4_RCC_APB1ENR1, 19, uart4, 0);
static RCC_GATE(uart5, STM32L4_RCC_APB1ENR1, 20, uart5, 0);
static RCC_GATE(i2c1, STM32L4_RCC_APB1ENR1, 21,	i2c1, 0);
static RCC_GATE(i2c2, STM32L4_RCC_APB1ENR1, 22,	i2c2, 0);
static RCC_GATE(i2c3, STM32L4_RCC_APB1ENR1, 23,	i2c3, 0);
static RCC_GATE(can1, STM32L4_RCC_APB1ENR1, 25,	pclk1, 0);
static RCC_GATE(pwr, STM32L4_RCC_APB1ENR1, 28,	pwr, 0);
static RCC_GATE(dac, STM32L4_RCC_APB1ENR1, 29,	pclk1, 0);
static RCC_GATE(opamp, STM32L4_RCC_APB1ENR1, 30, pclk1, 0);
static RCC_GATE(lptim1, STM32L4_RCC_APB1ENR1, 31, lptim1, 0);
static RCC_GATE(lpuart1, STM32L4_RCC_APB1ENR2, 0, lpuart1, 0);
static RCC_GATE(i2c4, STM32L4_RCC_APB1ENR2, 1, i2c4, 0);
static RCC_GATE(swpmi1, STM32L4_RCC_APB1ENR2, 2, pclk1, 0);
static RCC_GATE(lptim2, STM32L4_RCC_APB1ENR2, 5, lptim2, 0);

static RCC_GATE(syscfg, STM32L4_RCC_APB2ENR,  0, pclk2, 0);
static RCC_GATE(fw, STM32L4_RCC_APB2ENR,  7, pclk2, 0);
static RCC_GATE(sdmmc1, STM32L4_RCC_APB2ENR, 10, clk48, 0);
static RCC_GATE(tim1, STM32L4_RCC_APB2ENR, 11, pclk2, 0);
static RCC_GATE(spi1, STM32L4_RCC_APB2ENR, 12, pclk2, 0);
static RCC_GATE(tim8, STM32L4_RCC_APB2ENR, 13, pclk2, 0);
static RCC_GATE(uart1, STM32L4_RCC_APB2ENR, 14,	uart1, 0);
static RCC_GATE(tim15, STM32L4_RCC_APB2ENR, 16, pclk2, 0);
static RCC_GATE(tim16, STM32L4_RCC_APB2ENR, 17, pclk2, 0);
static RCC_GATE(tim17, STM32L4_RCC_APB2ENR, 18, pclk2, 0);
static RCC_GATE(sai1, STM32L4_RCC_APB2ENR, 21,	sai1, 0);
static RCC_GATE(sai2, STM32L4_RCC_APB2ENR, 22,	sai2, 0);
static RCC_GATE(dfsdm1, STM32L4_RCC_APB2ENR, 24, dfsdm1, 0);

static RCC_GATE(lsco, STM32L4_RCC_BDCR, 24, lsco, 0);
static RCC_GATE(rtc, STM32L4_RCC_BDCR, 15, rtc_lcd, 0);
static RCC_GATE(lse, STM32L4_RCC_BDCR, 0, lse, 0);

static RCC_GATE(lsi, STM32L4_RCC_CSR, 0, lsi, 0);

static RCC_GATE(hsi48, STM32L4_RCC_CRRCR, 0, hsi48, 0);

/* Muxes */

#define RCC_MUX(_name, _reg, _shift, _width, __parents, _flags)		\
struct clk_mux _name = {						\
	.reg = (void __iomem *) _reg,					\
	.mask = GENMASK(0, _width),					\
	.shift = (_shift),						\
	.lock = &clk_lock,						\
	.hw.init = &(struct clk_init_data){				\
		.name = #_name,						\
		.ops = &clk_mux_ops,					\
		.parent_names = __parents ## _parents,			\
		.num_parents = ARRAY_SIZE(__parents ## _parents),	\
		.flags = CLK_SET_RATE_PARENT | (_flags),		\
	},								\
};

static RCC_MUX(pll_prediv_mux, STM32L4_RCC_PLLCFGR, 0, 2, pll_prediv_mux, 0);

static RCC_MUX(rtc_lcd_mux, STM32L4_RCC_BDCR, 8, 2, rtc_lcd_mux, 0);
static RCC_MUX(lsco_mux, STM32L4_RCC_BDCR, 25, 1, lsco_mux, 0);

static RCC_MUX(mco_div_mux, STM32L4_RCC_CFGR, 24, 4, mco_div_mux, 0);
static RCC_MUX(sysclk, STM32L4_RCC_CFGR, 0, 2, sysclk, CLK_IS_CRITICAL);

static RCC_MUX(uart1_mux, STM32L4_RCC_CCIPR, 0, 2, uart1_mux, 0);
static RCC_MUX(uart2_mux, STM32L4_RCC_CCIPR, 2, 2, uart_2_5_mux, 0);
static RCC_MUX(uart3_mux, STM32L4_RCC_CCIPR, 4, 2, uart_2_5_mux, 0);
static RCC_MUX(uart4_mux, STM32L4_RCC_CCIPR, 6, 2, uart_2_5_mux, 0);
static RCC_MUX(uart5_mux, STM32L4_RCC_CCIPR, 8, 2, uart_2_5_mux, 0);
static RCC_MUX(lpuart1_mux, STM32L4_RCC_CCIPR, 10, 2, uart_2_5_mux, 0);
static RCC_MUX(i2c1_mux, STM32L4_RCC_CCIPR, 12, 2, i2c_mux, 0);
static RCC_MUX(i2c2_mux, STM32L4_RCC_CCIPR, 14, 2, i2c_mux, 0);
static RCC_MUX(i2c3_mux, STM32L4_RCC_CCIPR, 16, 2, i2c_mux, 0);
static RCC_MUX(lptim1_mux, STM32L4_RCC_CCIPR, 18, 2, lptim_mux, 0);
static RCC_MUX(lptim2_mux, STM32L4_RCC_CCIPR, 18, 2, lptim_mux, 0);
static RCC_MUX(sai1_mux, STM32L4_RCC_CCIPR, 22, 2, sai1_mux, 0);
static RCC_MUX(sai2_mux, STM32L4_RCC_CCIPR, 24, 2, sai2_mux, 0);
static RCC_MUX(clk48_mux, STM32L4_RCC_CCIPR, 26, 2, clk48_mux, 0);
static RCC_MUX(adc_mux, STM32L4_RCC_CCIPR, 28, 2, adc_mux, 0);
static RCC_MUX(swpmi_mux, STM32L4_RCC_CCIPR, 30, 1, swpmi_mux, 0);
static RCC_MUX(dfsdm1_mux, STM32L4_RCC_CCIPR, 31, 1, dfsdm1_mux, 0);

static RCC_MUX(i2c4_mux, STM32L4_RCC_CCIPR2, 0, 2, i2c_mux, 0);

/* Dividers */

#define RCC_DIV(_name, _reg, _shift, _width, _table, __parents, _flags)	\
struct clk_divider _name = {						\
	.reg = (void __iomem *) _reg,					\
	.shift = (_shift),						\
	.width = (_width),						\
	.table = _table,						\
	.lock = &clk_lock,						\
	.hw.init = &(struct clk_init_data){				\
		.name = #_name,						\
		.ops = &clk_divider_ops,				\
		.parent_names = __parents ## _parents,			\
		.num_parents = ARRAY_SIZE(__parents ## _parents),	\
		.flags =  (_flags),					\
	},								\
};

static const struct clk_div_table pll_p_table[] = {
	{0, 7},
	{1, 17},
};

static RCC_DIV(pll_p, STM32L4_RCC_PLLCFGR, 17, 1,
	       pll_p_table, pll_p_q_r, 0);

static RCC_DIV(pllsai1_p, STM32L4_RCC_PLLSAI1CFGR, 17, 1,
	       pll_p_table, pllsai1_p_q_r, 0);

static RCC_DIV(pllsai2_p, STM32L4_RCC_PLLSAI2CFGR, 17, 1,
	       pll_p_table, pllsai2_p_q_r, 0);

static const struct clk_div_table pll_q_r_table[] = {
	{0, 2},
	{1, 4},
	{2, 6},
	{3, 8},
};

static RCC_DIV(pll_q, STM32L4_RCC_PLLCFGR, 21, 2,
	       pll_q_r_table, pll_p_q_r, 0);
static RCC_DIV(pll_r, STM32L4_RCC_PLLCFGR, 25, 2,
	       pll_q_r_table, pll_p_q_r, 0);

static RCC_DIV(pllsai1_q, STM32L4_RCC_PLLSAI1CFGR, 21, 2,
	       pll_q_r_table, pllsai1_p_q_r, 0);
static RCC_DIV(pllsai1_r, STM32L4_RCC_PLLSAI1CFGR, 25, 2,
	       pll_q_r_table, pllsai1_p_q_r, 0);

static RCC_DIV(pllsai2_r, STM32L4_RCC_PLLSAI2CFGR, 25, 2,
	       pll_q_r_table, pllsai2_p_q_r, 0);

static const struct clk_div_table pll_prediv_table[] = {
	{0, 1},
	{1, 2}, 
	{2, 3},
	{3, 4},
	{4, 5},
	{5, 6},
	{6, 7},
	{7, 8},
};

static RCC_DIV(pll_prediv, STM32L4_RCC_PLLCFGR, 4, 3,
	       pll_prediv_table, pll_prediv, 0);

static const struct clk_div_table mco_apb_div_table[] = {
	{0, 2},
	{1, 4},
	{2, 6},
	{3, 8},
	{4, 16},
};

static RCC_DIV(mco_div, STM32L4_RCC_CFGR, 28, 3,
	       mco_apb_div_table, mco_div, 0);

static RCC_DIV(apb1_presc, STM32L4_RCC_CFGR, 8, 3,
	       mco_apb_div_table, apb1_presc, 0);
static RCC_DIV(apb2_presc, STM32L4_RCC_CFGR, 11, 3,
	       mco_apb_div_table, apb2_presc, 0);

static const struct clk_div_table ahb_presc_table[] = {
	{0, 1},
	{1, 1},
	{2, 1},
	{3, 1},
	{4, 1},
	{5, 1},
	{6, 1},
	{7, 1},
	{8, 2},
	{9, 4},
	{10, 8},
	{11, 16},
	{12, 64},
	{13, 128},
	{14, 256},
	{15, 512},
};

static RCC_DIV(ahb_presc, STM32L4_RCC_CFGR, 4, 4,
	       ahb_presc_table, ahb_presc, 0);

/* Configurable clocks */

struct clk_rcc_range {
	struct clk_hw	hw;
	void __iomem	*base;
	unsigned long	csr_reg;
	u8		csr_shift;
	u8		csr_width;
	const unsigned int *csr_table;
	unsigned long	cr_reg;
	u8		cr_shift;
	u8		cr_width;
	const unsigned int *cr_table;
	unsigned long	sel_reg;
	u8		sel_shift;
	spinlock_t	*lock;
};

#define to_clk_rcc_range(_hw) container_of(_hw, struct clk_rcc_range, hw)

static unsigned long rcc_range_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct clk_rcc_range *range = to_clk_rcc_range(hw);
	unsigned int val;

	/* Switch between CR and CSR range register */
	if ((clk_readl(range->base + range->sel_reg)
		& (1 << range->sel_shift))) {
		val = clk_readl(range->base + range->cr_reg)
				>> range->cr_shift;
		val &= ((1 << (range->cr_width)) - 1);

		return range->cr_table[val];
	} else {
		val = clk_readl(range->base + range->csr_reg)
				>> range->csr_shift;
		val &= ((1 << (range->csr_width)) - 1);

		return range->csr_table[val];
	}
}

/* TOFIX make it configurable */
const struct clk_ops rcc_range_ops = {
	.recalc_rate = rcc_range_recalc_rate,
};

#define RCC_RANGE(_name, _csr_reg, _csr_shift, _csr_width, _csr_table,	\
		  _cr_reg, _cr_shift, _cr_width, _cr_table,		\
		  _sel_reg, _sel_shift, _flags)				\
struct clk_rcc_range _name = {						\
	.csr_reg = (_csr_reg),						\
	.csr_shift = (_csr_shift),					\
	.csr_width = (_csr_width),					\
	.csr_table = _csr_table,					\
	.cr_reg = (_cr_reg),						\
	.cr_shift = (_cr_shift),					\
	.cr_width = (_cr_width),					\
	.cr_table = _cr_table,						\
	.sel_reg = (_sel_reg),						\
	.sel_shift = (_sel_shift),					\
	.lock = &clk_lock,						\
	.hw.init = &(struct clk_init_data){				\
		.name = #_name,						\
		.ops = &rcc_range_ops,					\
		.flags = (_flags),					\
	},								\
};

static const unsigned int msi_csr_freq_table[] = {
	0,
	0,
	0,
	0,
	1000000,
	2000000,
	4000000,
	8000000,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0
};

static const unsigned int msi_cr_freq_table[] = {
	100000,
	200000,
	400000,
	800000,
	1000000,
	2000000,
	4000000,
	8000000,
	16000000,
	24000000,
	32000000,
	48000000,
	0,
	0,
	0,
	0
};

static RCC_RANGE(msi_rc, STM32L4_RCC_CSR, 8, 4, msi_csr_freq_table,
		 STM32L4_RCC_CR, 4, 4, msi_cr_freq_table,
		 STM32L4_RCC_CR, 3, 0);

/* PLLs */

struct clk_rcc_pll {
	struct clk_hw	hw;
	void __iomem	*base;
	unsigned int	stat_reg;
	u8		gate_shift;
	u8		ready_shift;
	unsigned int	mult_reg;
	u8		mult_shift;
	u8		mult_width;
	u8		mult_min;
	u8		mult_max;
	spinlock_t	*lock;
};

#define to_clk_rcc_pll(_hw) container_of(_hw, struct clk_rcc_pll, hw)

static unsigned long rcc_pll_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct clk_rcc_pll *pll = to_clk_rcc_pll(hw);
	unsigned int val;

	if (!(clk_readl(pll->base + pll->stat_reg) & (1 << pll->gate_shift)) ||
	    !(clk_readl(pll->base + pll->stat_reg) & (1 << pll->ready_shift)))
		return 0;
	
	val = clk_readl(pll->base + pll->mult_reg) >> pll->mult_shift;
	val &= ((1 << (pll->mult_width)) - 1);

	return parent_rate * val;
}

/* TOFIX make it configurable */
const struct clk_ops rcc_pll_ops = {
	.recalc_rate = rcc_pll_recalc_rate,
};

#define RCC_PLL(_name, _s_reg, _g_shift, _r_shift,			\
		_m_reg, _m_shift, _m_width, 				\
		_min, _max, __parents, _flags)				\
struct clk_rcc_pll _name = {						\
	.stat_reg = (_s_reg),						\
	.gate_shift = (_g_shift),					\
	.ready_shift = (_r_shift),					\
	.mult_reg = (_m_reg),						\
	.mult_shift = (_m_shift),					\
	.mult_width = (_m_width),					\
	.mult_min = (_min),						\
	.mult_max = (_max),						\
	.lock = &clk_lock,						\
	.hw.init = &(struct clk_init_data){				\
		.name = #_name,						\
		.ops = &rcc_pll_ops,					\
		.parent_names = __parents ## _parents,			\
		.num_parents = ARRAY_SIZE(__parents ## _parents),	\
		.flags = (_flags),					\
	},								\
};

static RCC_PLL(pll, STM32L4_RCC_CR, 24, 25,
	       STM32L4_RCC_PLLCFGR, 8, 7,
	       8, 86, pll, 0);
static RCC_PLL(pllsai1, STM32L4_RCC_CR, 26, 27,
	       STM32L4_RCC_PLLSAI1CFGR, 8, 7,
	       8, 86, pll, 0);
static RCC_PLL(pllsai2, STM32L4_RCC_CR, 28, 29,
	       STM32L4_RCC_PLLSAI2CFGR, 8, 7,
	       8, 86, pll, 0);
/* Resets */

struct rcc_reset_data {
	unsigned long reg;
	unsigned int shift;
};

struct rcc_reset_controller {
	struct reset_controller_dev reset;
	const struct rcc_reset_data *data;
	void __iomem *base;
};

static int rcc_reset_assert(struct reset_controller_dev *rcdev,
			    unsigned long id)
{
	struct rcc_reset_controller *reset =
		container_of(rcdev, struct rcc_reset_controller, reset);
	u32 val;

	if (!reset->data)
		return -EINVAL;

	val = readl(reset->base + reset->data[id].reg);
	val |= BIT(reset->data[id].shift);
	writel(val, reset->base + reset->data[id].reg);

	return 0;
}

static int rcc_reset_deassert(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct rcc_reset_controller *reset =
		container_of(rcdev, struct rcc_reset_controller, reset);
	u32 val;

	if (!reset->data)
		return -EINVAL;

	val = readl(reset->base + reset->data[id].reg);
	val &= ~BIT(reset->data[id].shift);
	writel(val, reset->base + reset->data[id].reg);

	return 0;
}

static const struct reset_control_ops rcc_reset_ops = {
	.assert = rcc_reset_assert,
	.deassert = rcc_reset_deassert,
};

#define RCC_RESET(_id, _reg, _shift) 					\
	[RESETID_##_id] = {						\
		.reg = STM32L4_RCC_##_reg,				\
		.shift = (_shift),					\
	}

static const struct rcc_reset_data rcc_resets_stm32l476[] = {
	RCC_RESET(DMA1, AHB1RSTR, 0),
	RCC_RESET(DMA2, AHB1RSTR, 1),
	RCC_RESET(FLASH, AHB1RSTR, 8),
	RCC_RESET(CRC, AHB1RSTR, 12),
	RCC_RESET(TSC, AHB1RSTR, 16),
	RCC_RESET(DMA2D, AHB1RSTR, 17),	//946
	RCC_RESET(GPIOA, AHB2RSTR, 0),
	RCC_RESET(GPIOB, AHB2RSTR, 1),
	RCC_RESET(GPIOC, AHB2RSTR, 2),
	RCC_RESET(GPIOD, AHB2RSTR, 3),
	RCC_RESET(GPIOE, AHB2RSTR, 4),
	RCC_RESET(GPIOF, AHB2RSTR, 5),
	RCC_RESET(GPIOG, AHB2RSTR, 6),
	RCC_RESET(GPIOH, AHB2RSTR, 7),
	RCC_RESET(GPIOI, AHB2RSTR, 8), //946
	RCC_RESET(OTGFS, AHB2RSTR, 12),
	RCC_RESET(ADC, AHB2RSTR, 13),
	RCC_RESET(DCMI, AHB2RSTR, 14), //946
	RCC_RESET(AES, AHB2RSTR, 16),
	RCC_RESET(HASH, AHB2RSTR, 17), //946
	RCC_RESET(RNG, AHB2RSTR, 18),
	RCC_RESET(FMC, AHB3RSTR, 0),
	RCC_RESET(QSPI, AHB3RSTR, 8),
	RCC_RESET(TIM2, APB1RSTR1, 0),
	RCC_RESET(TIM3, APB1RSTR1, 1),
	RCC_RESET(TIM4, APB1RSTR1, 2),
	RCC_RESET(TIM5, APB1RSTR1, 3),
	RCC_RESET(TIM6, APB1RSTR1, 4),
	RCC_RESET(TIM7, APB1RSTR1, 5),
	RCC_RESET(LCD, APB1RSTR1, 9),
	RCC_RESET(SPI2, APB1RSTR1, 14),
	RCC_RESET(SPI3, APB1RSTR1, 15),
	RCC_RESET(USART2, APB1RSTR1, 17),
	RCC_RESET(USART3, APB1RSTR1, 18),
	RCC_RESET(USART4, APB1RSTR1, 19),
	RCC_RESET(USART5, APB1RSTR1, 20),
	RCC_RESET(I2C1, APB1RSTR1, 21),
	RCC_RESET(I2C2, APB1RSTR1, 22),
	RCC_RESET(I2C3, APB1RSTR1, 23),
	RCC_RESET(CRS, APB1RSTR1, 24), //946
	RCC_RESET(CAN1, APB1RSTR1, 25),
	RCC_RESET(CAN2, APB1RSTR1, 26), //946
	RCC_RESET(PWR, APB1RSTR1, 28),
	RCC_RESET(DAC1, APB1RSTR1, 29),
	RCC_RESET(OPAMP, APB1RSTR1, 30),
	RCC_RESET(LPTIM1, APB1RSTR1, 31),
	RCC_RESET(LPUART1, APB1RSTR2, 0),
	RCC_RESET(I2C4, APB1RSTR2, 1), //946
	RCC_RESET(SWPMI1, APB1RSTR2, 2),
	RCC_RESET(LPTIM2, APB1RSTR2, 5),
	RCC_RESET(SYSCFG, APB2RSTR, 0),
	RCC_RESET(SDMMC1, APB2RSTR, 10),
	RCC_RESET(TIM1, APB2RSTR, 11),
	RCC_RESET(SPI1, APB2RSTR, 12),
	RCC_RESET(TIM8, APB2RSTR, 13),
	RCC_RESET(USART1, APB2RSTR, 14),
	RCC_RESET(TIM15, APB2RSTR, 16),
	RCC_RESET(TIM16, APB2RSTR, 17),
	RCC_RESET(TIM17, APB2RSTR, 18),
	RCC_RESET(SAI1, APB2RSTR, 21),
	RCC_RESET(SAI2, APB2RSTR, 22),
	RCC_RESET(DFSDM1, APB2RSTR, 24),
};


/* Clocks Registry & Bindings tables */

static struct clk_hw_onecell_data stm32l476_hw_onecell_data = {
	.hws = {
		[CLKID_LSI_OSC]	= &lsi_osc.hw,
		[CLKID_HSI_OSC]	= &hsi_osc.hw,
		[CLKID_IWDG]	= &iwdg.hw,
		[CLKID_MCO]	= &mco.hw,
		[CLKID_CORTEX_FR]	= &cortex_free_running.hw,
		[CLKID_CORTEX_TICK]	= &cortex_systick.hw,
		[CLKID_HCLK_DIV8]	= &hclk_div8.hw,
		[CLKID_HSE_DIV32]	= &hse_div32.hw,
		[CLKID_HSE]	= &hse.hw,
		[CLKID_HSI]	= &hsi.hw,
		[CLKID_MSI]	= &msi.hw,
		[CLKID_PLLCLK]	= &pllclk.hw,
		[CLKID_PLL48M1CLK]	= &pll48m1clk.hw,
		[CLKID_PLL48M2CLK]	= &pll48m2clk.hw,
		[CLKID_PLLSAI1CLK]	= &pllsai1clk.hw,
		[CLKID_PLLSAI2CLK]	= &pllsai2clk.hw,
		[CLKID_PLLADC1CLK]	= &plladc1clk.hw,
		[CLKID_PLLADC2CLK]	= &plladc2clk.hw,
		[CLKID_DMA1]	= &dma1.hw,
		[CLKID_DMA2]	= &dma2.hw,
		[CLKID_FLASH]	= &flash.hw,
		[CLKID_CRC]	= &crc.hw,
		[CLKID_TSC]	= &tsc.hw,
		[CLKID_DMA2D]	= &dma2d.hw,
		[CLKID_GPIOA]	= &gpioa.hw,
		[CLKID_GPIOB]	= &gpiob.hw,
		[CLKID_GPIOC]	= &gpioc.hw,
		[CLKID_GPIOD]	= &gpiod.hw,
		[CLKID_GPIOE]	= &gpioe.hw,
		[CLKID_GPIOF]	= &gpiof.hw,
		[CLKID_GPIOG]	= &gpiog.hw,
		[CLKID_GPIOH]	= &gpioh.hw,
		[CLKID_OTGFS]	= &otgfs.hw,
		[CLKID_ADC]	= &adc.hw,
		[CLKID_AES]	= &aes.hw,
		[CLKID_RNG]	= &rng.hw,
		[CLKID_FMC]	= &fmc.hw,
		[CLKID_QSPI]	= &qspi.hw,
		[CLKID_TIM2]	= &tim2.hw,
		[CLKID_TIM3]	= &tim3.hw,
		[CLKID_TIM4]	= &tim4.hw,
		[CLKID_TIM5]	= &tim5.hw,
		[CLKID_TIM6]	= &tim6.hw,
		[CLKID_TIM7]	= &tim7.hw,
		[CLKID_LCD]	= &lcd.hw,
		[CLKID_WWDG]	= &wwdg.hw,
		[CLKID_SPI2]	= &spi2.hw,
		[CLKID_SPI3]	= &spi3.hw,
		[CLKID_USART2]	= &uart2.hw,
		[CLKID_USART3]	= &uart3.hw,
		[CLKID_USART4]	= &uart4.hw,
		[CLKID_USART5]	= &uart5.hw,
		[CLKID_I2C1]	= &i2c1.hw,
		[CLKID_I2C2]	= &i2c2.hw,
		[CLKID_I2C3]	= &i2c3.hw,
		[CLKID_CAN1]	= &can1.hw,
		[CLKID_PWR]	= &pwr.hw,
		[CLKID_DAC]	= &dac.hw,
		[CLKID_OPAMP]	= &opamp.hw,
		[CLKID_LPTIM1]	= &lptim1.hw,
		[CLKID_LPUART1]	= &lpuart1.hw,
		[CLKID_I2C4]	= &i2c4.hw,
		[CLKID_SWPMI1]	= &swpmi1.hw,
		[CLKID_LPTIM2]	= &lptim2.hw,
		[CLKID_SYSCFG]	= &syscfg.hw,
		[CLKID_FW]	= &fw.hw,
		[CLKID_SDMMC1]	= &sdmmc1.hw,
		[CLKID_TIM1]	= &tim1.hw,
		[CLKID_SPI1]	= &spi1.hw,
		[CLKID_TIM8]	= &tim8.hw,
		[CLKID_USART1]	= &uart1.hw,
		[CLKID_TIM15]	= &tim15.hw,
		[CLKID_TIM16]	= &tim16.hw,
		[CLKID_TIM17]	= &tim17.hw,
		[CLKID_SAI1]	= &sai1.hw,
		[CLKID_SAI2]	= &sai2.hw,
		[CLKID_DFSDM1]	= &dfsdm1.hw,
		[CLKID_LSCO]	= &lsco.hw,
		[CLKID_RTC]	= &rtc.hw,
		[CLKID_LSE]	= &lse.hw,
		[CLKID_LSI]	= &lsi.hw,
		[CLKID_PLL_SEL]	= &pll_prediv_mux.hw,
		[CLKID_RTC_LCD_SEL]	= &rtc_lcd_mux.hw,
		[CLKID_LSCO_SEL]	= &lsco_mux.hw,
		[CLKID_MCO_SEC]	= &mco_div_mux.hw,
		[CLKID_SYSCLK]	= &sysclk.hw,
		[CLKID_USART1_SEL]	= &uart1_mux.hw,
		[CLKID_USART2_SEL]	= &uart2_mux.hw,
		[CLKID_USART3_SEL]	= &uart3_mux.hw,
		[CLKID_USART4_SEL]	= &uart4_mux.hw,
		[CLKID_USART5_SEL]	= &uart5_mux.hw,
		[CLKID_LPUART1_SEL]	= &lpuart1_mux.hw,
		[CLKID_I2C1_SEL]	= &i2c1_mux.hw,
		[CLKID_I2C2_SEL]	= &i2c2_mux.hw,
		[CLKID_I2C3_SEL]	= &i2c3_mux.hw,
		[CLKID_LPTIM1_SEL]	= &lptim1_mux.hw,
		[CLKID_LPTIM2_SEL]	= &lptim2_mux.hw,
		[CLKID_SAI1_SEL]	= &sai1_mux.hw,
		[CLKID_SAI2_SEL]	= &sai2_mux.hw,
		[CLKID_CLK48_SEL]	= &clk48_mux.hw,
		[CLKID_ADC_SEL]	= &adc_mux.hw,
		[CLKID_SWPMI_SEL]	= &swpmi_mux.hw,
		[CLKID_DFSDM1_SEL]	= &dfsdm1_mux.hw,
		[CLKID_I2C4_SEL]	= &i2c4_mux.hw,
		[CLKID_PLL_P]	= &pll_p.hw,
		[CLKID_PLLSAI1_P]	= &pllsai1_p.hw,
		[CLKID_PLLSAI2_P]	= &pllsai2_p.hw,
		[CLKID_PLL_Q]	= &pll_q.hw,
		[CLKID_PLL_R]	= &pll_r.hw,
		[CLKID_PLLSAI1_Q]	= &pllsai1_q.hw,
		[CLKID_PLLSAI1_R]	= &pllsai1_r.hw,
		[CLKID_PLLSAI2_R]	= &pllsai2_r.hw,
		[CLKID_PLL_PREDIV]	= &pll_prediv.hw,
		[CLKID_MCO_DIV]	= &mco_div.hw,
		[CLKID_APB1_PRESC]	= &apb1_presc.hw,
		[CLKID_APB2_PRESC]	= &apb2_presc.hw,
		[CLKID_AHB_PRESC]	= &ahb_presc.hw,
		[CLKID_PLL]	= &pll.hw,
		[CLKID_PLLSAI1]	= &pllsai1.hw,
		[CLKID_PLLSAI2]	= &pllsai2.hw,
		[CLKID_MSI_RC]	= &msi_rc.hw,
	},
	.num = NR_CLKS,
};

static struct clk_hw_onecell_data stm32l496_hw_onecell_data = {
	.hws = {
		[CLKID_LSI_OSC]	= &lsi_osc.hw,
		[CLKID_HSI_OSC]	= &hsi_osc.hw,
		[CLKID_HSI48_OSC]	= &hsi48_osc.hw,
		[CLKID_IWDG]	= &iwdg.hw,
		[CLKID_MCO]	= &mco.hw,
		[CLKID_CORTEX_FR]	= &cortex_free_running.hw,
		[CLKID_CORTEX_TICK]	= &cortex_systick.hw,
		[CLKID_HCLK_DIV8]	= &hclk_div8.hw,
		[CLKID_HSE_DIV32]	= &hse_div32.hw,
		[CLKID_HSE]	= &hse.hw,
		[CLKID_HSI]	= &hsi.hw,
		[CLKID_MSI]	= &msi.hw,
		[CLKID_PLLCLK]	= &pllclk.hw,
		[CLKID_PLL48M1CLK]	= &pll48m1clk.hw,
		[CLKID_PLL48M2CLK]	= &pll48m2clk.hw,
		[CLKID_PLLSAI1CLK]	= &pllsai1clk.hw,
		[CLKID_PLLSAI2CLK]	= &pllsai2clk.hw,
		[CLKID_PLLADC1CLK]	= &plladc1clk.hw,
		[CLKID_PLLADC2CLK]	= &plladc2clk.hw,
		[CLKID_DMA1]	= &dma1.hw,
		[CLKID_DMA2]	= &dma2.hw,
		[CLKID_FLASH]	= &flash.hw,
		[CLKID_CRC]	= &crc.hw,
		[CLKID_TSC]	= &tsc.hw,
		[CLKID_DMA2D]	= &dma2d.hw,
		[CLKID_GPIOA]	= &gpioa.hw,
		[CLKID_GPIOB]	= &gpiob.hw,
		[CLKID_GPIOC]	= &gpioc.hw,
		[CLKID_GPIOD]	= &gpiod.hw,
		[CLKID_GPIOE]	= &gpioe.hw,
		[CLKID_GPIOF]	= &gpiof.hw,
		[CLKID_GPIOG]	= &gpiog.hw,
		[CLKID_GPIOH]	= &gpioh.hw,
		[CLKID_GPIOI]	= &gpioi.hw,
		[CLKID_OTGFS]	= &otgfs.hw,
		[CLKID_ADC]	= &adc.hw,
		[CLKID_AES]	= &aes.hw,
		[CLKID_RNG]	= &rng.hw,
		[CLKID_FMC]	= &fmc.hw,
		[CLKID_QSPI]	= &qspi.hw,
		[CLKID_TIM2]	= &tim2.hw,
		[CLKID_TIM3]	= &tim3.hw,
		[CLKID_TIM4]	= &tim4.hw,
		[CLKID_TIM5]	= &tim5.hw,
		[CLKID_TIM6]	= &tim6.hw,
		[CLKID_TIM7]	= &tim7.hw,
		[CLKID_LCD]	= &lcd.hw,
		[CLKID_WWDG]	= &wwdg.hw,
		[CLKID_SPI2]	= &spi2.hw,
		[CLKID_SPI3]	= &spi3.hw,
		[CLKID_USART2]	= &uart2.hw,
		[CLKID_USART3]	= &uart3.hw,
		[CLKID_USART4]	= &uart4.hw,
		[CLKID_USART5]	= &uart5.hw,
		[CLKID_I2C1]	= &i2c1.hw,
		[CLKID_I2C2]	= &i2c2.hw,
		[CLKID_I2C3]	= &i2c3.hw,
		[CLKID_CAN1]	= &can1.hw,
		[CLKID_PWR]	= &pwr.hw,
		[CLKID_DAC]	= &dac.hw,
		[CLKID_OPAMP]	= &opamp.hw,
		[CLKID_LPTIM1]	= &lptim1.hw,
		[CLKID_LPUART1]	= &lpuart1.hw,
		[CLKID_I2C4]	= &i2c4.hw,
		[CLKID_SWPMI1]	= &swpmi1.hw,
		[CLKID_LPTIM2]	= &lptim2.hw,
		[CLKID_SYSCFG]	= &syscfg.hw,
		[CLKID_FW]	= &fw.hw,
		[CLKID_SDMMC1]	= &sdmmc1.hw,
		[CLKID_TIM1]	= &tim1.hw,
		[CLKID_SPI1]	= &spi1.hw,
		[CLKID_TIM8]	= &tim8.hw,
		[CLKID_USART1]	= &uart1.hw,
		[CLKID_TIM15]	= &tim15.hw,
		[CLKID_TIM16]	= &tim16.hw,
		[CLKID_TIM17]	= &tim17.hw,
		[CLKID_SAI1]	= &sai1.hw,
		[CLKID_SAI2]	= &sai2.hw,
		[CLKID_DFSDM1]	= &dfsdm1.hw,
		[CLKID_LSCO]	= &lsco.hw,
		[CLKID_RTC]	= &rtc.hw,
		[CLKID_LSE]	= &lse.hw,
		[CLKID_LSI]	= &lsi.hw,
		[CLKID_HSI48]	= &hsi48.hw,
		[CLKID_PLL_SEL]	= &pll_prediv_mux.hw,
		[CLKID_RTC_LCD_SEL]	= &rtc_lcd_mux.hw,
		[CLKID_LSCO_SEL]	= &lsco_mux.hw,
		[CLKID_MCO_SEC]	= &mco_div_mux.hw,
		[CLKID_SYSCLK]	= &sysclk.hw,
		[CLKID_USART1_SEL]	= &uart1_mux.hw,
		[CLKID_USART2_SEL]	= &uart2_mux.hw,
		[CLKID_USART3_SEL]	= &uart3_mux.hw,
		[CLKID_USART4_SEL]	= &uart4_mux.hw,
		[CLKID_USART5_SEL]	= &uart5_mux.hw,
		[CLKID_LPUART1_SEL]	= &lpuart1_mux.hw,
		[CLKID_I2C1_SEL]	= &i2c1_mux.hw,
		[CLKID_I2C2_SEL]	= &i2c2_mux.hw,
		[CLKID_I2C3_SEL]	= &i2c3_mux.hw,
		[CLKID_LPTIM1_SEL]	= &lptim1_mux.hw,
		[CLKID_LPTIM2_SEL]	= &lptim2_mux.hw,
		[CLKID_SAI1_SEL]	= &sai1_mux.hw,
		[CLKID_SAI2_SEL]	= &sai2_mux.hw,
		[CLKID_CLK48_SEL]	= &clk48_mux.hw,
		[CLKID_ADC_SEL]	= &adc_mux.hw,
		[CLKID_SWPMI_SEL]	= &swpmi_mux.hw,
		[CLKID_DFSDM1_SEL]	= &dfsdm1_mux.hw,
		[CLKID_I2C4_SEL]	= &i2c4_mux.hw,
		[CLKID_PLL_P]	= &pll_p.hw,
		[CLKID_PLLSAI1_P]	= &pllsai1_p.hw,
		[CLKID_PLLSAI2_P]	= &pllsai2_p.hw,
		[CLKID_PLL_Q]	= &pll_q.hw,
		[CLKID_PLL_R]	= &pll_r.hw,
		[CLKID_PLLSAI1_Q]	= &pllsai1_q.hw,
		[CLKID_PLLSAI1_R]	= &pllsai1_r.hw,
		[CLKID_PLLSAI2_R]	= &pllsai2_r.hw,
		[CLKID_PLL_PREDIV]	= &pll_prediv.hw,
		[CLKID_MCO_DIV]	= &mco_div.hw,
		[CLKID_APB1_PRESC]	= &apb1_presc.hw,
		[CLKID_APB2_PRESC]	= &apb2_presc.hw,
		[CLKID_AHB_PRESC]	= &ahb_presc.hw,
		[CLKID_PLL]	= &pll.hw,
		[CLKID_PLLSAI1]	= &pllsai1.hw,
		[CLKID_PLLSAI2]	= &pllsai2.hw,
		[CLKID_MSI_RC]	= &msi_rc.hw,
	},
	.num = NR_CLKS,
};

static struct clk_gate *stm32l476_clk_gates[] = {
	&hse,
	&hsi,
	&msi,
	&pllclk,
	&pll48m1clk,
	&pll48m2clk,
	&pllsai1clk,
	&pllsai2clk,
	&plladc1clk,
	&plladc2clk,
	&dma1,
	&dma2,
	&flash,
	&crc,
	&tsc,
	&dma2d,
	&gpioa,
	&gpiob,
	&gpioc,
	&gpiod,
	&gpioe,
	&gpiof,
	&gpiog,
	&gpioh,
	&otgfs,
	&adc,
	&aes,
	&rng,
	&fmc,
	&qspi,
	&tim2,
	&tim3,
	&tim4,
	&tim5,
	&tim6,
	&tim7,
	&lcd,
	&wwdg,
	&spi2,
	&spi3,
	&uart2,
	&uart3,
	&uart4,
	&uart5,
	&i2c1,
	&i2c2,
	&i2c3,
	&can1,
	&pwr,
	&dac,
	&opamp,
	&lptim1,
	&lpuart1,
	&i2c4,
	&swpmi1,
	&lptim2,
	&syscfg,
	&fw,
	&sdmmc1,
	&tim1,
	&spi1,
	&tim8,
	&uart1,
	&tim15,
	&tim16,
	&tim17,
	&sai1,
	&sai2,
	&dfsdm1,
	&lsco,
	&rtc,
	&lse,
	&lsi,
};

static struct clk_gate *stm32l496_clk_gates[] = {
	&hse,
	&hsi,
	&msi,
	&pllclk,
	&pll48m1clk,
	&pll48m2clk,
	&pllsai1clk,
	&pllsai2clk,
	&plladc1clk,
	&plladc2clk,
	&dma1,
	&dma2,
	&flash,
	&crc,
	&tsc,
	&dma2d,
	&gpioa,
	&gpiob,
	&gpioc,
	&gpiod,
	&gpioe,
	&gpiof,
	&gpiog,
	&gpioh,
	&gpioi,
	&otgfs,
	&adc,
	&aes,
	&rng,
	&fmc,
	&qspi,
	&tim2,
	&tim3,
	&tim4,
	&tim5,
	&tim6,
	&tim7,
	&lcd,
	&wwdg,
	&spi2,
	&spi3,
	&uart2,
	&uart3,
	&uart4,
	&uart5,
	&i2c1,
	&i2c2,
	&i2c3,
	&can1,
	&pwr,
	&dac,
	&opamp,
	&lptim1,
	&lpuart1,
	&i2c4,
	&swpmi1,
	&lptim2,
	&syscfg,
	&fw,
	&sdmmc1,
	&tim1,
	&spi1,
	&tim8,
	&uart1,
	&tim15,
	&tim16,
	&tim17,
	&sai1,
	&sai2,
	&dfsdm1,
	&lsco,
	&rtc,
	&lse,
	&lsi,
	&hsi48,
};

static struct clk_mux *stm32l476_clk_muxes[] = {
	&pll_prediv_mux,
	&rtc_lcd_mux,
	&lsco_mux,
	&mco_div_mux,
	&sysclk,
	&uart1_mux,
	&uart2_mux,
	&uart3_mux,
	&uart4_mux,
	&uart5_mux,
	&lpuart1_mux,
	&i2c1_mux,
	&i2c2_mux,
	&i2c3_mux,
	&lptim1_mux,
	&lptim2_mux,
	&sai1_mux,
	&sai2_mux,
	&clk48_mux,
	&adc_mux,
	&swpmi_mux,
	&dfsdm1_mux,
	&i2c4_mux,
};

static struct clk_divider *stm32l476_clk_dividers[] = {
	&pll_p,
	&pllsai1_p,
	&pllsai2_p,
	&pll_q,
	&pll_r,
	&pllsai1_q,
	&pllsai1_r,
	&pllsai2_r,
	&pll_prediv,
	&mco_div,
	&apb1_presc,
	&apb2_presc,
	&ahb_presc,
};

static struct clk_rcc_pll *stm32l476_clk_rcc_plls[] = {
	&pll,
	&pllsai1,
	&pllsai2,
};

static struct clk_rcc_range *stm32l476_clk_rcc_ranges[] = {
	&msi_rc,
};

struct stm32l4_rcc_data {
	struct clk_gate *const *clk_gates;
	unsigned int clk_gates_count;
	struct clk_mux *const *clk_muxes;
	unsigned int clk_muxes_count;
	struct clk_divider *const *clk_dividers;
	unsigned int clk_dividers_count;
	struct clk_rcc_pll *const *clk_rcc_plls;
	unsigned int clk_rcc_plls_count;
	struct clk_rcc_range *const *clk_rcc_ranges;
	unsigned int clk_rcc_ranges_count;
	struct clk_hw_onecell_data *hw_onecell_data;
	const struct rcc_reset_data *rcc_resets;
	unsigned int rcc_resets_count;
};

static const struct stm32l4_rcc_data stm32l476_rcc_data = {
	.clk_gates = stm32l476_clk_gates,
	.clk_gates_count = ARRAY_SIZE(stm32l476_clk_gates),
	.clk_muxes = stm32l476_clk_muxes,
	.clk_muxes_count = ARRAY_SIZE(stm32l476_clk_muxes),
	.clk_dividers = stm32l476_clk_dividers,
	.clk_dividers_count = ARRAY_SIZE(stm32l476_clk_dividers),
	.clk_rcc_plls = stm32l476_clk_rcc_plls,
	.clk_rcc_plls_count = ARRAY_SIZE(stm32l476_clk_rcc_plls),
	.clk_rcc_ranges = stm32l476_clk_rcc_ranges,
	.clk_rcc_ranges_count = ARRAY_SIZE(stm32l476_clk_rcc_ranges),
	.hw_onecell_data = &stm32l476_hw_onecell_data,
	.rcc_resets = rcc_resets_stm32l476,
	.rcc_resets_count = ARRAY_SIZE(rcc_resets_stm32l476),
};

static const struct stm32l4_rcc_data stm32l496_rcc_data = {
	.clk_gates = stm32l496_clk_gates,
	.clk_gates_count = ARRAY_SIZE(stm32l496_clk_gates),
	.clk_muxes = stm32l476_clk_muxes,
	.clk_muxes_count = ARRAY_SIZE(stm32l476_clk_muxes),
	.clk_dividers = stm32l476_clk_dividers,
	.clk_dividers_count = ARRAY_SIZE(stm32l476_clk_dividers),
	.clk_rcc_plls = stm32l476_clk_rcc_plls,
	.clk_rcc_plls_count = ARRAY_SIZE(stm32l476_clk_rcc_plls),
	.clk_rcc_ranges = stm32l476_clk_rcc_ranges,
	.clk_rcc_ranges_count = ARRAY_SIZE(stm32l476_clk_rcc_ranges),
	.hw_onecell_data = &stm32l496_hw_onecell_data,
};

static const struct of_device_id stm32l4_rcc_match_table[] = {
	{ .compatible = "st,stm32l476-rcc", .data = &stm32l476_rcc_data },
	{ .compatible = "st,stm32l496-rcc", .data = &stm32l496_rcc_data },
	{},
};

static int stm32l4_rcc_probe(struct platform_device *pdev)
{
	const struct stm32l4_rcc_data *rcc_data;
	struct rcc_reset_controller *rstc;
	void __iomem *rcc_io_base;
	int ret, clkid, i;
	struct device *dev = &pdev->dev;

	rcc_data = of_device_get_match_data(&pdev->dev);
	if (!rcc_data)
		return -EINVAL;

	/*  Generic clocks and PLLs */
	rcc_io_base = of_iomap(dev->of_node, 0);
	if (!rcc_io_base) {
		pr_err("%s: Unable to map clk base\n", __func__);
		return -ENXIO;
	}

	/* Reset Controller */
	rstc->base = rcc_io_base;
	rstc->data = rcc_data->rcc_resets;
	rstc->reset.ops = &rcc_reset_ops;
	rstc->reset.nr_resets = rcc_data->rcc_resets_count;
	rstc->reset.of_node = dev->of_node;
	ret = devm_reset_controller_register(dev, &rstc->reset);
	if (ret)
		goto iounmap;

	/* Populate base address for gates */
	for (i = 0; i < rcc_data->clk_gates_count; i++)
		rcc_data->clk_gates[i]->reg = rcc_io_base +
			(u32)rcc_data->clk_gates[i]->reg;

	/* Populate base address for muxes */
	for (i = 0; i < rcc_data->clk_muxes_count; i++)
		rcc_data->clk_muxes[i]->reg = rcc_io_base +
			(u32)rcc_data->clk_muxes[i]->reg;

	/* Populate base address for dividers */
	for (i = 0; i < rcc_data->clk_dividers_count; i++)
		rcc_data->clk_dividers[i]->reg = rcc_io_base +
			(u32)rcc_data->clk_dividers[i]->reg;

	/* Populate base address for rcc_plls */
	for (i = 0; i < rcc_data->clk_rcc_plls_count; i++)
		rcc_data->clk_rcc_plls[i]->base = rcc_io_base;

	/* Populate base address for rcc_ranges */
	for (i = 0; i < rcc_data->clk_rcc_ranges_count; i++)
		rcc_data->clk_rcc_ranges[i]->base = rcc_io_base;

	/*
	 * register all clks
	 */
	for (clkid = 0; clkid < rcc_data->hw_onecell_data->num; clkid++) {
		if (!rcc_data->hw_onecell_data->hws[clkid])
			continue;

		ret = devm_clk_hw_register(dev,
					rcc_data->hw_onecell_data->hws[clkid]);
		if (ret)
			goto iounmap;
	}

	return of_clk_add_hw_provider(dev->of_node, of_clk_hw_onecell_get,
			rcc_data->hw_onecell_data);

iounmap:
	iounmap(rcc_io_base);
	return ret;
}

static struct platform_driver stm32l4_rcc_driver = {
	.probe		= stm32l4_rcc_probe,
	.driver		= {
		.name	= "stm32l4-rcc",
		.of_match_table = stm32l4_rcc_match_table,
	},
};

builtin_platform_driver(stm32l4_rcc_driver);
