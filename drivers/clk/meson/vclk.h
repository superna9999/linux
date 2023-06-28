/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Neil Armstrong <neil.armstrong@linaro.org>
 */

#ifndef __VCLK_H
#define __VCLK_H

#include "clk-regmap.h"

/**
 * struct clk_regmap_vclk_data - vclk regmap backed specific data
 *
 * @offset:		offset of the register controlling gate
 * @enable_bit_idx:	single bit controlling vclk enable
 * @reset_bit_idx:	single bit controlling vclk reset
 * @flags:		hardware-specific flags
 *
 * Flags:
 * Same as clk_gate except CLK_GATE_HIWORD_MASK which is ignored
 */
struct clk_regmap_vclk_data {
	unsigned int	offset;
	u8		enable_bit_idx;
	u8		reset_bit_idx;
	u8		flags;
};

static inline struct clk_regmap_vclk_data *
clk_get_regmap_vclk_data(struct clk_regmap *clk)
{
	return (struct clk_regmap_vclk_data *)clk->data;
}

extern const struct clk_ops clk_regmap_vclk_ops;

/**
 * struct clk_regmap_vclk_div_data - vclk_div regmap back specific data
 *
 * @offset:	offset of the register controlling the divider
 * @shift:	shift to the divider bit field
 * @width:	width of the divider bit field
 * @enable_bit_idx:	single bit controlling vclk divider enable
 * @reset_bit_idx:	single bit controlling vclk divider reset
 * @table:	array of value/divider pairs, last entry should have div = 0
 *
 * Flags:
 * Same as clk_divider except CLK_DIVIDER_HIWORD_MASK which is ignored
 */
struct clk_regmap_vclk_div_data {
	unsigned int	offset;
	u8		shift;
	u8		width;
	u8		enable_bit_idx;
	u8		reset_bit_idx;
	const struct clk_div_table      *table;
	u8		flags;
};

static inline struct clk_regmap_vclk_div_data *
clk_get_regmap_vclk_div_data(struct clk_regmap *clk)
{
	return (struct clk_regmap_vclk_div_data *)clk->data;
}

extern const struct clk_ops clk_regmap_vclk_div_ops;

#endif /* __VCLK_H */
