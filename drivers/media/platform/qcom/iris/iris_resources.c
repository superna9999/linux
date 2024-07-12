// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/interconnect.h>
#include <linux/pm_domain.h>
#include <linux/pm_opp.h>
#include <linux/reset.h>

#include "iris_core.h"
#include "iris_resources.h"

static int iris_init_icc(struct iris_core *core)
{
	const struct icc_info *icc_tbl;
	u32 ret, i = 0;

	icc_tbl = core->iris_platform_data->icc_tbl;

	core->icc_count = core->iris_platform_data->icc_tbl_size;
	core->icc_tbl = devm_kzalloc(core->dev,
				     sizeof(struct icc_bulk_data) * core->icc_count,
				     GFP_KERNEL);
	if (!core->icc_tbl)
		return -ENOMEM;

	for (i = 0; i < core->icc_count; i++) {
		core->icc_tbl[i].name = icc_tbl[i].name;
		core->icc_tbl[i].avg_bw = icc_tbl[i].bw_min_kbps;
		core->icc_tbl[i].peak_bw = 0;
	}

	ret = devm_of_icc_bulk_get(core->dev, core->icc_count, core->icc_tbl);
	if (ret)
		dev_err(core->dev, "failed to get interconnect paths, NoC will stay unconfigured!\n");

	return ret;
}

static int iris_pd_get(struct iris_core *core)
{
	int ret;

	struct dev_pm_domain_attach_data iris_pd_data = {
		.pd_names = core->iris_platform_data->pmdomain_tbl,
		.num_pd_names = core->iris_platform_data->pmdomain_tbl_size,
		.pd_flags = PD_FLAG_NO_DEV_LINK,
	};

	ret = devm_pm_domain_attach_list(core->dev, &iris_pd_data, &core->pmdomain_tbl);
	if (ret < 0)
		return ret;

	return 0;
}

static void iris_opp_dl_release(void *res)
{
	struct device_link *link = (struct device_link *)res;

	device_link_del(link);
}

static int iris_opp_dl_get(struct device *dev, struct device *supplier)
{
	u32 flag = DL_FLAG_RPM_ACTIVE | DL_FLAG_PM_RUNTIME | DL_FLAG_STATELESS;
	struct device_link *link = NULL;
	int ret;

	link = device_link_add(dev, supplier, flag);
	if (!link)
		return -EINVAL;

	ret = devm_add_action_or_reset(dev, iris_opp_dl_release, (void *)link);

	return ret;
}

static int iris_init_power_domains(struct iris_core *core)
{
	const struct platform_clk_data *clk_tbl;
	struct device **opp_vdevs = NULL;
	const char * const *opp_pd_tbl;
	u32 opp_pd_cnt, clk_cnt, i;
	int ret;

	ret = iris_pd_get(core);
	if (ret)
		return ret;

	opp_pd_tbl = core->iris_platform_data->opp_pd_tbl;
	opp_pd_cnt = core->iris_platform_data->opp_pd_tbl_size;

	clk_tbl = core->iris_platform_data->clk_tbl;
	clk_cnt = core->iris_platform_data->clk_tbl_size;

	ret = devm_pm_opp_attach_genpd(core->dev, opp_pd_tbl, &opp_vdevs);
	if (ret)
		return ret;

	for (i = 0; i < (opp_pd_cnt - 1) ; i++) {
		ret = iris_opp_dl_get(core->dev, opp_vdevs[i]);
		if (ret) {
			dev_err(core->dev, "failed to create dl: %s\n", dev_name(opp_vdevs[i]));
			return ret;
		}
	}

	for (i = 0; i < clk_cnt; i++) {
		if (clk_tbl[i].clk_type == IRIS_HW_CLK) {
			ret = devm_pm_opp_set_clkname(core->dev, clk_tbl[i].clk_name);
			if (ret)
				return ret;
		}
	}

	ret = devm_pm_opp_of_add_table(core->dev);
	if (ret) {
		dev_err(core->dev, "failed to add opp table\n");
		return ret;
	}

	return ret;
}

static int iris_init_clocks(struct iris_core *core)
{
	int ret;

	ret = devm_clk_bulk_get_all(core->dev, &core->clock_tbl);
	if (ret < 0) {
		dev_err(core->dev, "failed to get bulk clock\n");
		return ret;
	}

	core->clk_count = ret;

	return 0;
}

static int iris_init_resets(struct iris_core *core)
{
	const char * const *rst_tbl;
	u32 rst_tbl_size;
	u32 i = 0, ret;

	rst_tbl = core->iris_platform_data->clk_rst_tbl;
	rst_tbl_size = core->iris_platform_data->clk_rst_tbl_size;

	core->resets = devm_kzalloc(core->dev,
				    sizeof(*core->resets) * rst_tbl_size,
				    GFP_KERNEL);
	if (rst_tbl_size && !core->resets)
		return -ENOMEM;

	for (i = 0; i < rst_tbl_size; i++)
		core->resets[i].id = rst_tbl[i];

	ret = devm_reset_control_bulk_get_exclusive(core->dev, rst_tbl_size, core->resets);
	if (ret) {
		dev_err(core->dev, "failed to get resets\n");
		return ret;
	}

	return 0;
}

int iris_init_resources(struct iris_core *core)
{
	int ret;

	ret = iris_init_icc(core);
	if (ret)
		return ret;

	ret = iris_init_power_domains(core);
	if (ret)
		return ret;

	ret = iris_init_clocks(core);
	if (ret)
		return ret;

	ret = iris_init_resets(core);

	return ret;
}
