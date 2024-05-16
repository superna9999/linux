// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2024 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/module.h>
#include <linux/of.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct boe_tv101wum_ll2 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *backlight_en_gpio;
	struct regulator_bulk_data supplies[2];
};

static inline struct boe_tv101wum_ll2 *to_boe_tv101wum_ll2(struct drm_panel *panel)
{
	return container_of(panel, struct boe_tv101wum_ll2, panel);
}

static void boe_tv101wum_ll2_reset(struct boe_tv101wum_ll2 *ctx)
{
	//gpiod_set_value_cansleep(ctx->backlight_en_gpio, 0);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(35);
	gpiod_set_value_cansleep(ctx->backlight_en_gpio, 1);
}

static int boe_tv101wum_ll2_on(struct boe_tv101wum_ll2 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(120);
	mipi_dsi_generic_write_seq(dsi, 0x50, 0x5a, 0x23);
	mipi_dsi_generic_write_seq(dsi, 0x90, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0x94, 0x2c, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0x50, 0x5a, 0x19);
	mipi_dsi_generic_write_seq(dsi, 0xa2, 0x38);
	mipi_dsi_generic_write_seq(dsi, 0x50, 0x5a, 0x0c);
	mipi_dsi_generic_write_seq(dsi, 0x80, 0xfd);
	mipi_dsi_generic_write_seq(dsi, 0x50, 0x00);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}
	msleep(20);

	return 0;
}

static int boe_tv101wum_ll2_off(struct boe_tv101wum_ll2 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}
	msleep(70);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(20);

	mipi_dsi_dcs_write_seq(dsi, 0x04, 0x5a);
	mipi_dsi_dcs_write_seq(dsi, 0x05, 0x5a);
	msleep(150);

	return 0;
}

static int boe_tv101wum_ll2_prepare(struct drm_panel *panel)
{
	struct boe_tv101wum_ll2 *ctx = to_boe_tv101wum_ll2(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies),
				    ctx->supplies);
	if (ret < 0)
		return ret;

	return 0;

	boe_tv101wum_ll2_reset(ctx);

	ret = boe_tv101wum_ll2_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		gpiod_set_value_cansleep(ctx->backlight_en_gpio, 0);
		return ret;
	}

	return 0;
}

static int boe_tv101wum_ll2_unprepare(struct drm_panel *panel)
{
	struct boe_tv101wum_ll2 *ctx = to_boe_tv101wum_ll2(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	return 0;

	ret = boe_tv101wum_ll2_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	gpiod_set_value_cansleep(ctx->backlight_en_gpio, 0);

	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode boe_tv101wum_ll2_mode = {
	.clock = (1200 + 27 + 8 + 12) * (1920 + 155 + 8 + 32) * 60 / 1000,
	.hdisplay = 1200,
	.hsync_start = 1200 + 27,
	.hsync_end = 1200 + 27 + 8,
	.htotal = 1200 + 27 + 8 + 12,
	.vdisplay = 1920,
	.vsync_start = 1920 + 155,
	.vsync_end = 1920 + 155 + 8,
	.vtotal = 1920 + 155 + 8 + 32,
	.width_mm = 0,
	.height_mm = 0,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int boe_tv101wum_ll2_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &boe_tv101wum_ll2_mode);
}

static const struct drm_panel_funcs boe_tv101wum_ll2_panel_funcs = {
	.prepare = boe_tv101wum_ll2_prepare,
	.unprepare = boe_tv101wum_ll2_unprepare,
	.get_modes = boe_tv101wum_ll2_get_modes,
};

static int boe_tv101wum_ll2_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	//dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	//ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	ret = mipi_dsi_dcs_set_display_brightness(dsi, brightness);
	if (ret < 0)
		return ret;

//	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static int boe_tv101wum_ll2_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	//dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	//ret = mipi_dsi_dcs_get_display_brightness_large(dsi, &brightness);
	ret = mipi_dsi_dcs_get_display_brightness(dsi, &brightness);
	if (ret < 0)
		return ret;

	//dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness;
}

static const struct backlight_ops boe_tv101wum_ll2_bl_ops = {
	.update_status = boe_tv101wum_ll2_bl_update_status,
	.get_brightness = boe_tv101wum_ll2_bl_get_brightness,
};

static struct backlight_device *
boe_tv101wum_ll2_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 3768,
		.max_brightness = 3768,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &boe_tv101wum_ll2_bl_ops, &props);
}

static int boe_tv101wum_ll2_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct boe_tv101wum_ll2 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;


	ctx->supplies[0].supply = "vsp";
	ctx->supplies[1].supply = "vsn";

	ret = devm_regulator_bulk_get(&dsi->dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->backlight_en_gpio = devm_gpiod_get(dev, "backlight-en", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->backlight_en_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->backlight_en_gpio),
				     "Failed to get backlight-en-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST;
			  //MIPI_DSI_MODE_VIDEO_HSE |
			  //MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &boe_tv101wum_ll2_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

#if 0
	ctx->panel.backlight = boe_tv101wum_ll2_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");
#elif 0

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");
#endif

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void boe_tv101wum_ll2_remove(struct mipi_dsi_device *dsi)
{
	struct boe_tv101wum_ll2 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id boe_tv101wum_ll2_of_match[] = {
	{ .compatible = "boe,tv101wum-ll2" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, boe_tv101wum_ll2_of_match);

static struct mipi_dsi_driver boe_tv101wum_ll2_driver = {
	.probe = boe_tv101wum_ll2_probe,
	.remove = boe_tv101wum_ll2_remove,
	.driver = {
		.name = "panel-boe-tv101wum_ll2",
		.of_match_table = boe_tv101wum_ll2_of_match,
	},
};
module_mipi_dsi_driver(boe_tv101wum_ll2_driver);

MODULE_DESCRIPTION("DRM driver for Boe TV101WUM-LL2 Panel");
MODULE_LICENSE("GPL");
