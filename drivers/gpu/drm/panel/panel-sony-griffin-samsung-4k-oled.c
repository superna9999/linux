// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2022 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>

struct sony_griffin_samsung_4k_oled {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	bool prepared;
};

static inline
struct sony_griffin_samsung_4k_oled *to_sony_griffin_samsung_4k_oled(struct drm_panel *panel)
{
	return container_of(panel, struct sony_griffin_samsung_4k_oled, panel);
}

static void sony_griffin_samsung_4k_oled_reset(struct sony_griffin_samsung_4k_oled *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int sony_griffin_samsung_4k_oled_on(struct sony_griffin_samsung_4k_oled *ctx)
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
	usleep_range(10000, 11000);

	ret = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret < 0) {
		dev_err(dev, "Failed to set tear on: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_compression_mode(ctx->dsi, !!ctx->dsi->dsc);
	if (ret < 0) {
		dev_err(dev, "Failed to set compression mode: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xd7, 0x07);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20);
	msleep(110);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq(dsi, 0xe2, 0x00); // TODO: Set to 01 for 2.5k
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0xa5, 0xa5);

	ret = mipi_dsi_dcs_set_column_address(dsi, 0x0000, 1643); // 1095
	if (ret < 0) {
		dev_err(dev, "Failed to set column address: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_set_page_address(dsi, 0x0000, 3839); // 2559
	if (ret < 0) {
		dev_err(dev, "Failed to set page address: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x70);
	mipi_dsi_dcs_write_seq(dsi, 0xb9, 0x00, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq(dsi, 0xc5, 0x2e, 0x21);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0xa5, 0xa5);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}

	return 0;
}

static int sony_griffin_samsung_4k_oled_off(struct sony_griffin_samsung_4k_oled *ctx)
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
	msleep(20);

	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20);
	usleep_range(17000, 18000);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(100);

	return 0;
}

static int sony_griffin_samsung_4k_oled_prepare(struct drm_panel *panel)
{
	struct sony_griffin_samsung_4k_oled *ctx = to_sony_griffin_samsung_4k_oled(panel);
	struct drm_dsc_picture_parameter_set pps;
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->prepared)
		return 0;

	sony_griffin_samsung_4k_oled_reset(ctx);

	ret = sony_griffin_samsung_4k_oled_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	if (ctx->dsi->dsc) {
		drm_dsc_pps_payload_pack(&pps, ctx->dsi->dsc);
		print_hex_dump(KERN_DEBUG, "DSC params:", DUMP_PREFIX_NONE,
			       16, 1, &pps, sizeof(pps), false);

		BUG_ON(pps.dsc_version != 0x11);
		BUG_ON(pps.pps_identifier != 0);

		ret = mipi_dsi_picture_parameter_set(ctx->dsi, &pps);
		if (ret < 0) {
			dev_err(dev, "failed to set pps: %d\n", ret);
			return ret;
		}

		msleep(28);
	}

	ctx->prepared = true;
	return 0;
}

static int sony_griffin_samsung_4k_oled_unprepare(struct drm_panel *panel)
{
	struct sony_griffin_samsung_4k_oled *ctx = to_sony_griffin_samsung_4k_oled(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = sony_griffin_samsung_4k_oled_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	ctx->prepared = false;
	return 0;
}

static const struct drm_display_mode sony_griffin_samsung_4k_oled_mode = {
	.clock = (1644 + 60 + 8 + 8) * (3840 + 8 + 8 + 8) * 60 / 1000,
	.hdisplay = 1644,
	.hsync_start = 1644 + 60,
	.hsync_end = 1644 + 60 + 8,
	.htotal = 1644 + 60 + 8 + 8,
	.vdisplay = 3840,
	.vsync_start = 3840 + 8,
	.vsync_end = 3840 + 8 + 8,
	.vtotal = 3840 + 8 + 8 + 8,
	.width_mm = 65,
	.height_mm = 152,
};

static int sony_griffin_samsung_4k_oled_get_modes(struct drm_panel *panel,
						  struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &sony_griffin_samsung_4k_oled_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs sony_griffin_samsung_4k_oled_panel_funcs = {
	.prepare = sony_griffin_samsung_4k_oled_prepare,
	.unprepare = sony_griffin_samsung_4k_oled_unprepare,
	.get_modes = sony_griffin_samsung_4k_oled_get_modes,
};

static int sony_griffin_samsung_4k_oled_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	brightness = cpu_to_be16(brightness);

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static int sony_griffin_samsung_4k_oled_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	brightness = be16_to_cpu(brightness);

	return brightness;
}

static const struct backlight_ops sony_griffin_samsung_4k_oled_bl_ops = {
	.update_status = sony_griffin_samsung_4k_oled_bl_update_status,
	.get_brightness = sony_griffin_samsung_4k_oled_bl_get_brightness,
};

static struct backlight_device *
sony_griffin_samsung_4k_oled_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 400,
		.max_brightness = 4095,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &sony_griffin_samsung_4k_oled_bl_ops, &props);
}

static int sony_griffin_samsung_4k_oled_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct sony_griffin_samsung_4k_oled *ctx;
	struct drm_dsc_config *dsc;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &sony_griffin_samsung_4k_oled_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ctx->panel.backlight = sony_griffin_samsung_4k_oled_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	/* This panel only supports DSC; unconditionally enable it */
	dsi->dsc = dsc = devm_kzalloc(&dsi->dev, sizeof(*dsc), GFP_KERNEL);
	if (!dsc)
		return -ENOMEM;

	dsc->dsc_version_major = 0x1;
	dsc->dsc_version_minor = 0x1;

	dsc->slice_height = 32;
	dsc->slice_width = 822; // 548
	dsc->slice_count = 2;
	dsc->bits_per_component = 8;
	dsc->bits_per_pixel = 8 << 4; /* 4 fractional bits */
	dsc->block_pred_enable = true;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void sony_griffin_samsung_4k_oled_remove(struct mipi_dsi_device *dsi)
{
	struct sony_griffin_samsung_4k_oled *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id sony_griffin_samsung_4k_oled_of_match[] = {
	{ .compatible = "sony,griffin-samsung-4k-oled" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sony_griffin_samsung_4k_oled_of_match);

static struct mipi_dsi_driver sony_griffin_samsung_4k_oled_driver = {
	.probe = sony_griffin_samsung_4k_oled_probe,
	.remove = sony_griffin_samsung_4k_oled_remove,
	.driver = {
		.name = "panel-sony-griffin-samsung-4k-oled",
		.of_match_table = sony_griffin_samsung_4k_oled_of_match,
	},
};
module_mipi_dsi_driver(sony_griffin_samsung_4k_oled_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for Sony Xperia 1 Samsung OLED panel");
MODULE_LICENSE("GPL");
