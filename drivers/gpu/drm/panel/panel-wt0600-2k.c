// SPDX-License-Identifier: GPL-2.0-only
/*
 * DRM driver for wt0600/wt0630 2K dual-DSI video mode panels
 *
 * Copyright (c) 2025, Kancy Joe <kancy2333@outlook.com>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_connector.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct wt0600_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi[2];
	const struct panel_desc *desc;

	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;
	enum drm_panel_orientation orientation;
};

static const struct regulator_bulk_data wt0600_supplies[] = {
	{ .supply = "vddio" },
	{ .supply = "vci" },
	{ .supply = "vdd" },
	{ .supply = "avdd" },
};

struct panel_desc {
	const struct drm_display_mode *mode;
	unsigned int lanes;
	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
	uint16_t height_mm;
	uint16_t width_mm;
	const struct mipi_dsi_device_info dsi_info;
};

static const struct drm_display_mode wt0600_mode = {
	/* Dual dsi */
	.clock = 2 * (720 + 100 + 8 + 40) * (2560 + 15 + 2 + 8) * 60 / 1000,
	.hdisplay = 2 * 720,
	.hsync_start = 2 * (720 + 100),
	.hsync_end = 2 * (720 + 100 + 8),
	.htotal = 2 * (720 + 100 + 8 + 40),
	.vdisplay = 2560,
	.vsync_start = 2560 + 15,
	.vsync_end = 2560 + 15 + 2,
	.vtotal = 2560 + 15 + 2 + 8,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static struct panel_desc wt0600_desc = {
	.lanes = 4,
	.width_mm = 74,
	.height_mm = 131,
	.mode = &wt0600_mode,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
		      MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM,
	.format = MIPI_DSI_FMT_RGB888,
	.dsi_info = {
		.type = "wt0600-2k",
		.channel = 0,
		.node = NULL,
	},
};

static struct panel_desc wt0630_desc = {
	.lanes = 4,
	.width_mm = 78,
	.height_mm = 140,
	.mode = &wt0600_mode,  /* wt0600 only has different screen size */
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
		      MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM,
	.format = MIPI_DSI_FMT_RGB888,
	.dsi_info = {
		.type = "wt0630-2k",
		.channel = 0,
		.node = NULL,
	},
};

static inline struct wt0600_panel *to_wt0600_panel(struct drm_panel *panel)
{
	return container_of(panel, struct wt0600_panel, panel);
}

static void wt0600_reset(struct wt0600_panel *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(55);
}

static int wt0600_on(struct wt0600_panel *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi[0] };
	struct mipi_dsi_multi_context dsi_ctx1 = { .dsi = ctx->dsi[1] };

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx1);
	mipi_dsi_msleep(&dsi_ctx, 150);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx1);
	mipi_dsi_msleep(&dsi_ctx, 50);

	return dsi_ctx.accum_err;
}

static int wt0600_disable(struct drm_panel *panel)
{
	struct wt0600_panel *ctx = to_wt0600_panel(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi[0] };
	struct mipi_dsi_multi_context dsi_ctx1 = { .dsi = ctx->dsi[1] };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx1);
	mipi_dsi_msleep(&dsi_ctx, 50);

	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx1);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int wt0600_prepare(struct drm_panel *panel)
{
	struct wt0600_panel *ctx = to_wt0600_panel(panel);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(wt0600_supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	wt0600_reset(ctx);

	ret = wt0600_on(ctx);
	if (ret < 0) {
		dev_err(panel->dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(wt0600_supplies),
				       ctx->supplies);
		return ret;
	}

	return 0;
}

static int wt0600_unprepare(struct drm_panel *panel)
{
	struct wt0600_panel *ctx = to_wt0600_panel(panel);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	regulator_bulk_disable(ARRAY_SIZE(wt0600_supplies), ctx->supplies);

	return 0;
}

static int wt0600_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	struct wt0600_panel *ctx = to_wt0600_panel(panel);

	mode = drm_mode_duplicate(connector->dev, ctx->desc->mode);
	if (!mode)
		return -ENOMEM;

	/* Update panel size */
	mode->width_mm = ctx->desc->width_mm;
	mode->height_mm = ctx->desc->height_mm;

	drm_mode_set_name(mode);
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static enum drm_panel_orientation
wt0600_get_orientation(struct drm_panel *panel)
{
	struct wt0600_panel *ctx = to_wt0600_panel(panel);

	return ctx->orientation;
}

static const struct drm_panel_funcs wt0600_panel_funcs = {
	.disable = wt0600_disable,
	.prepare = wt0600_prepare,
	.unprepare = wt0600_unprepare,
	.get_modes = wt0600_get_modes,
	.get_orientation = wt0600_get_orientation,
};

static int wt0600_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct wt0600_panel *ctx;
	struct device_node *dsi1_node;
	struct mipi_dsi_host *dsi1_host;
	int ret, i;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->desc = of_device_get_match_data(dev);
	if (!ctx->desc)
		return dev_err_probe(dev, -ENODEV,
				     "Failed to get panel description\n");

	ret = devm_regulator_bulk_get_const(&dsi->dev,
					    ARRAY_SIZE(wt0600_supplies),
					    wt0600_supplies, &ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset gpio\n");

	/* Get second DSI host */
	dsi1_node = of_graph_get_remote_node(dsi->dev.of_node, 1, -1);
	if (!dsi1_node)
		return dev_err_probe(
			dev, -ENODEV,
			"Failed to get remote node for second DSI\n");

	dsi1_host = of_find_mipi_dsi_host_by_node(dsi1_node);
	of_node_put(dsi1_node);
	if (!dsi1_host)
		return dev_err_probe(dev, -EPROBE_DEFER,
				     "Failed to find second DSI host\n");

	/* Register the second DSI device */
	ctx->dsi[1] = devm_mipi_dsi_device_register_full(dev, dsi1_host,
							 &ctx->desc->dsi_info);
	if (IS_ERR(ctx->dsi[1]))
		return dev_err_probe(dev, PTR_ERR(ctx->dsi[1]),
				     "Failed to register second DSI device\n");

	/* Get panel orientation */
	ret = of_drm_get_panel_orientation(dev->of_node, &ctx->orientation);
	if (ret < 0 && ret != -ENODEV)
		return dev_err_probe(dev, ret,
				     "Failed to get panel orientation\n");

	ctx->dsi[0] = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	drm_panel_init(&ctx->panel, dev, &wt0600_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&ctx->panel);

	/* Configure and attach both DSI devices */
	for (i = 0; i < ARRAY_SIZE(ctx->dsi); i++) {
		ctx->dsi[i]->lanes = ctx->desc->lanes;
		ctx->dsi[i]->format = ctx->desc->format;
		ctx->dsi[i]->mode_flags = ctx->desc->mode_flags;

		ret = mipi_dsi_attach(ctx->dsi[i]);
		if (ret < 0) {
			drm_panel_remove(&ctx->panel);
			return dev_err_probe(dev, ret,
					     "Failed to attach DSI device %d\n",
					     i);
		}
	}

	return 0;
}

static void wt0600_remove(struct mipi_dsi_device *dsi)
{
	struct wt0600_panel *ctx = mipi_dsi_get_drvdata(dsi);
	int i;

	for (i = 0; i < ARRAY_SIZE(ctx->dsi); i++)
		mipi_dsi_detach(ctx->dsi[i]);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id wt0600_of_match[] = {
	{
		.compatible = "ayaneo,wt0600-2k",
		.data = &wt0600_desc,
	},
	{
		.compatible = "ayaneo,wt0630-2k",
		.data = &wt0630_desc,
	},
	{}
};
MODULE_DEVICE_TABLE(of, wt0600_of_match);

static struct mipi_dsi_driver wt0600_driver = {
	.probe = wt0600_probe,
	.remove = wt0600_remove,
	.driver = {
		.name = "panel-wt0600-2k",
		.of_match_table = wt0600_of_match,
	},
};
module_mipi_dsi_driver(wt0600_driver);

MODULE_AUTHOR("Kancy Joe <kancy2333@outlook.com>");
MODULE_DESCRIPTION("DRM driver for wt0600/wt0630 2k video mode dsi panel");
MODULE_LICENSE("GPL");
