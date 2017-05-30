/*
 * Copyright (c) 2017 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#ifndef __GXBB_AOCLKC_H
#define __GXBB_AOCLKC_H

struct aoclk_cec_32k {
	struct clk_hw hw;
	void __iomem *crt_base;
	void __iomem *rtc_base;
	struct regmap *pwr_regmap;
	spinlock_t *lock;
};

#define to_aoclk_cec_32k(_hw) container_of(_hw, struct aoclk_cec_32k, hw)

extern const struct clk_ops meson_aoclk_cec_32k_ops;

#endif /* __GXBB_AOCLKC_H */
