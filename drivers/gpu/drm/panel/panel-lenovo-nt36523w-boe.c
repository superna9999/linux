// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Linaro Limited
 *
 * Generated with linux-mdss-dsi-panel-driver-generator with
 * some manual adjustments.
 */ 
#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct nt36523w_boe {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator *supply;
	struct gpio_desc *reset_gpio;
	enum drm_panel_orientation orientation;
	bool prepared;
};

static inline struct nt36523w_boe *to_nt36523w_boe(struct drm_panel *panel)
{
	return container_of(panel, struct nt36523w_boe, panel);
}

static void nt36523w_boe_reset(struct nt36523w_boe *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(2000, 3000);
}

static int nt36523w_boe_on(struct nt36523w_boe *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x05, 0xd9);
	mipi_dsi_dcs_write_seq(dsi, 0x07, 0x78);
	mipi_dsi_dcs_write_seq(dsi, 0x08, 0x5a);
	mipi_dsi_dcs_write_seq(dsi, 0x0d, 0x63);
	mipi_dsi_dcs_write_seq(dsi, 0x0e, 0x91);
	mipi_dsi_dcs_write_seq(dsi, 0x0f, 0x73);
	mipi_dsi_dcs_write_seq(dsi, 0x95, 0xeb);
	mipi_dsi_dcs_write_seq(dsi, 0x96, 0xeb);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_PARTIAL_ROWS, 0x11);
	mipi_dsi_dcs_write_seq(dsi, 0x6d, 0x66);
	mipi_dsi_dcs_write_seq(dsi, 0x75, 0xa2);
	mipi_dsi_dcs_write_seq(dsi, 0x77, 0xb3);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x08, 0x00, 0x23, 0x00, 0x4d, 0x00, 0x6d, 0x00,
			       0x89, 0x00, 0xa1, 0x00, 0xb6, 0x00, 0xc9);
	mipi_dsi_dcs_write_seq(dsi, 0xb1, 0x00, 0xda, 0x01, 0x13, 0x01, 0x3c, 0x01, 0x7e, 0x01,
			       0xab, 0x01, 0xf7, 0x02, 0x2f, 0x02, 0x31);
	mipi_dsi_dcs_write_seq(dsi, 0xb2, 0x02, 0x67, 0x02, 0xa6, 0x02, 0xd1, 0x03, 0x08, 0x03,
			       0x2e, 0x03, 0x5b, 0x03, 0x6b, 0x03, 0x7b);
	mipi_dsi_dcs_write_seq(dsi, 0xb3, 0x03, 0x8e, 0x03, 0xa2, 0x03, 0xb7, 0x03, 0xe7, 0x03,
			       0xfd, 0x03, 0xff);
	mipi_dsi_dcs_write_seq(dsi, 0xb4, 0x00, 0x08, 0x00, 0x23, 0x00, 0x4d, 0x00, 0x6d, 0x00,
			       0x89, 0x00, 0xa1, 0x00, 0xb6, 0x00, 0xc9);
	mipi_dsi_dcs_write_seq(dsi, 0xb5, 0x00, 0xda, 0x01, 0x13, 0x01, 0x3c, 0x01, 0x7e, 0x01,
			       0xab, 0x01, 0xf7, 0x02, 0x2f, 0x02, 0x31);
	mipi_dsi_dcs_write_seq(dsi, 0xb6, 0x02, 0x67, 0x02, 0xa6, 0x02, 0xd1, 0x03, 0x08, 0x03,
			       0x2e, 0x03, 0x5b, 0x03, 0x6b, 0x03, 0x7b);
	mipi_dsi_dcs_write_seq(dsi, 0xb7, 0x03, 0x8e, 0x03, 0xa2, 0x03, 0xb7, 0x03, 0xe7, 0x03,
			       0xfd, 0x03, 0xff);
	mipi_dsi_dcs_write_seq(dsi, 0xb8, 0x00, 0x08, 0x00, 0x23, 0x00, 0x4d, 0x00, 0x6d, 0x00,
			       0x89, 0x00, 0xa1, 0x00, 0xb6, 0x00, 0xc9);
	mipi_dsi_dcs_write_seq(dsi, 0xb9, 0x00, 0xda, 0x01, 0x13, 0x01, 0x3c, 0x01, 0x7e, 0x01,
			       0xab, 0x01, 0xf7, 0x02, 0x2f, 0x02, 0x31);
	mipi_dsi_dcs_write_seq(dsi, 0xba, 0x02, 0x67, 0x02, 0xa6, 0x02, 0xd1, 0x03, 0x08, 0x03,
			       0x2e, 0x03, 0x5b, 0x03, 0x6b, 0x03, 0x7b);
	mipi_dsi_dcs_write_seq(dsi, 0xbb, 0x03, 0x8e, 0x03, 0xa2, 0x03, 0xb7, 0x03, 0xe7, 0x03,
			       0xfd, 0x03, 0xff);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x21);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x45, 0x00, 0x65, 0x00,
			       0x81, 0x00, 0x99, 0x00, 0xae, 0x00, 0xc1);
	mipi_dsi_dcs_write_seq(dsi, 0xb1, 0x00, 0xd2, 0x01, 0x0b, 0x01, 0x34, 0x01, 0x76, 0x01,
			       0xa3, 0x01, 0xef, 0x02, 0x27, 0x02, 0x29);
	mipi_dsi_dcs_write_seq(dsi, 0xb2, 0x02, 0x5f, 0x02, 0x9e, 0x02, 0xc9, 0x03, 0x00, 0x03,
			       0x26, 0x03, 0x53, 0x03, 0x63, 0x03, 0x73);
	mipi_dsi_dcs_write_seq(dsi, 0xb3, 0x03, 0x86, 0x03, 0x9a, 0x03, 0xaf, 0x03, 0xdf, 0x03,
			       0xf5, 0x03, 0xf7);
	mipi_dsi_dcs_write_seq(dsi, 0xb4, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x45, 0x00, 0x65, 0x00,
			       0x81, 0x00, 0x99, 0x00, 0xae, 0x00, 0xc1);
	mipi_dsi_dcs_write_seq(dsi, 0xb5, 0x00, 0xd2, 0x01, 0x0b, 0x01, 0x34, 0x01, 0x76, 0x01,
			       0xa3, 0x01, 0xef, 0x02, 0x27, 0x02, 0x29);
	mipi_dsi_dcs_write_seq(dsi, 0xb6, 0x02, 0x5f, 0x02, 0x9e, 0x02, 0xc9, 0x03, 0x00, 0x03,
			       0x26, 0x03, 0x53, 0x03, 0x63, 0x03, 0x73);
	mipi_dsi_dcs_write_seq(dsi, 0xb7, 0x03, 0x86, 0x03, 0x9a, 0x03, 0xaf, 0x03, 0xdf, 0x03,
			       0xf5, 0x03, 0xf7);
	mipi_dsi_dcs_write_seq(dsi, 0xb8, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x45, 0x00, 0x65, 0x00,
			       0x81, 0x00, 0x99, 0x00, 0xae, 0x00, 0xc1);
	mipi_dsi_dcs_write_seq(dsi, 0xb9, 0x00, 0xd2, 0x01, 0x0b, 0x01, 0x34, 0x01, 0x76, 0x01,
			       0xa3, 0x01, 0xef, 0x02, 0x27, 0x02, 0x29);
	mipi_dsi_dcs_write_seq(dsi, 0xba, 0x02, 0x5f, 0x02, 0x9e, 0x02, 0xc9, 0x03, 0x00, 0x03,
			       0x26, 0x03, 0x53, 0x03, 0x63, 0x03, 0x73);
	mipi_dsi_dcs_write_seq(dsi, 0xbb, 0x03, 0x86, 0x03, 0x9a, 0x03, 0xaf, 0x03, 0xdf, 0x03,
			       0xf5, 0x03, 0xf7);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x23);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x80);
	mipi_dsi_dcs_write_seq(dsi, 0x07, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x11, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x12, 0x77);
	mipi_dsi_dcs_write_seq(dsi, 0x15, 0x07);
	mipi_dsi_dcs_write_seq(dsi, 0x16, 0x07);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x24);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x01, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x02, 0x1c);
	mipi_dsi_dcs_write_seq(dsi, 0x03, 0x1c);
	mipi_dsi_dcs_write_seq(dsi, 0x04, 0x1d);
	mipi_dsi_dcs_write_seq(dsi, 0x05, 0x1d);
	mipi_dsi_dcs_write_seq(dsi, 0x06, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0x07, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0x08, 0x0f);
	mipi_dsi_dcs_write_seq(dsi, 0x09, 0x0f);
	mipi_dsi_dcs_write_seq(dsi, 0x0a, 0x0e);
	mipi_dsi_dcs_write_seq(dsi, 0x0b, 0x0e);
	mipi_dsi_dcs_write_seq(dsi, 0x0c, 0x0d);
	mipi_dsi_dcs_write_seq(dsi, 0x0d, 0x0d);
	mipi_dsi_dcs_write_seq(dsi, 0x0e, 0x0c);
	mipi_dsi_dcs_write_seq(dsi, 0x0f, 0x0c);
	mipi_dsi_dcs_write_seq(dsi, 0x10, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0x11, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0x12, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x13, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x14, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x15, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x16, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x17, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x18, 0x1c);
	mipi_dsi_dcs_write_seq(dsi, 0x19, 0x1c);
	mipi_dsi_dcs_write_seq(dsi, 0x1a, 0x1d);
	mipi_dsi_dcs_write_seq(dsi, 0x1b, 0x1d);
	mipi_dsi_dcs_write_seq(dsi, 0x1c, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0x1d, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0x1e, 0x0f);
	mipi_dsi_dcs_write_seq(dsi, 0x1f, 0x0f);
	mipi_dsi_dcs_write_seq(dsi, 0x20, 0x0e);
	mipi_dsi_dcs_write_seq(dsi, 0x21, 0x0e);
	mipi_dsi_dcs_write_seq(dsi, 0x22, 0x0d);
	mipi_dsi_dcs_write_seq(dsi, 0x23, 0x0d);
	mipi_dsi_dcs_write_seq(dsi, 0x24, 0x0c);
	mipi_dsi_dcs_write_seq(dsi, 0x25, 0x0c);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_GAMMA_CURVE, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0x27, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0x28, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x29, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x2a, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x2b, 0x00);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_LUT, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0x2f, 0x0a);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_PARTIAL_ROWS, 0x44);
	mipi_dsi_dcs_write_seq(dsi, 0x33, 0x0c);
	mipi_dsi_dcs_write_seq(dsi, 0x34, 0x32);
	mipi_dsi_dcs_write_seq(dsi, 0x37, 0x44);
	mipi_dsi_dcs_write_seq(dsi, 0x38, 0x40);
	mipi_dsi_dcs_write_seq(dsi, 0x39, 0x00);

	ret = mipi_dsi_dcs_set_pixel_format(dsi, 0x9a);
	if (ret < 0) {
		dev_err(dev, "Failed to set pixel format: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, 0x3b, 0xa0);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_3D_CONTROL, 0x42);
	mipi_dsi_dcs_write_seq(dsi, 0x3f, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0x43, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0x47, 0x66);
	mipi_dsi_dcs_write_seq(dsi, 0x4a, 0x9a);
	mipi_dsi_dcs_write_seq(dsi, 0x4b, 0xa0);
	mipi_dsi_dcs_write_seq(dsi, 0x4c, 0x91);
	mipi_dsi_dcs_write_seq(dsi, 0x4d, 0x21);
	mipi_dsi_dcs_write_seq(dsi, 0x4e, 0x43);

	ret = mipi_dsi_dcs_set_display_brightness(dsi, 18);
	if (ret < 0) {
		dev_err(dev, "Failed to set display brightness: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, 0x52, 0x34);
	mipi_dsi_dcs_write_seq(dsi, 0x55, 0x82, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0x56, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0x58, 0x21);
	mipi_dsi_dcs_write_seq(dsi, 0x59, 0x30);
	mipi_dsi_dcs_write_seq(dsi, 0x5a, 0xba);
	mipi_dsi_dcs_write_seq(dsi, 0x5b, 0xa0);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_CABC_MIN_BRIGHTNESS, 0x00, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0x5f, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x65, 0x82);
	mipi_dsi_dcs_write_seq(dsi, 0x7e, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0x7f, 0x3c);
	mipi_dsi_dcs_write_seq(dsi, 0x82, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0x97, 0xc0);
	mipi_dsi_dcs_write_seq(dsi, 0xb6,
			       0x05, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05,
			       0x05, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x92, 0xc4);
	mipi_dsi_dcs_write_seq(dsi, 0x93, 0x1a);
	mipi_dsi_dcs_write_seq(dsi, 0x94, 0x5f);
	mipi_dsi_dcs_write_seq(dsi, 0xd7, 0x55);
	mipi_dsi_dcs_write_seq(dsi, 0xda, 0x0a);
	mipi_dsi_dcs_write_seq(dsi, 0xde, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0xdb, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xdc, 0xc4);
	mipi_dsi_dcs_write_seq(dsi, 0xdd, 0x22);
	mipi_dsi_dcs_write_seq(dsi, 0xdf, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xe0, 0xc4);
	mipi_dsi_dcs_write_seq(dsi, 0xe1, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xe2, 0xc4);
	mipi_dsi_dcs_write_seq(dsi, 0xe3, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xe4, 0xc4);
	mipi_dsi_dcs_write_seq(dsi, 0xe5, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xe6, 0xc4);
	mipi_dsi_dcs_write_seq(dsi, 0x5c, 0x88);
	mipi_dsi_dcs_write_seq(dsi, 0x5d, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0x8d, 0x88);
	mipi_dsi_dcs_write_seq(dsi, 0x8e, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0xb5, 0x90);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x25);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x05, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x19, 0x07);
	mipi_dsi_dcs_write_seq(dsi, 0x1f, 0xba);
	mipi_dsi_dcs_write_seq(dsi, 0x20, 0xa0);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_GAMMA_CURVE, 0xba);
	mipi_dsi_dcs_write_seq(dsi, 0x27, 0xa0);
	mipi_dsi_dcs_write_seq(dsi, 0x33, 0xba);
	mipi_dsi_dcs_write_seq(dsi, 0x34, 0xa0);
	mipi_dsi_dcs_write_seq(dsi, 0x3f, 0xe0);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_VSYNC_TIMING, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x44, 0x00);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_GET_SCANLINE, 0x40);
	mipi_dsi_dcs_write_seq(dsi, 0x48, 0xba);
	mipi_dsi_dcs_write_seq(dsi, 0x49, 0xa0);
	mipi_dsi_dcs_write_seq(dsi, 0x5b, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x5c, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x5d, 0x00);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_CABC_MIN_BRIGHTNESS, 0xd0);
	mipi_dsi_dcs_write_seq(dsi, 0x61, 0xba);
	mipi_dsi_dcs_write_seq(dsi, 0x62, 0xa0);
	mipi_dsi_dcs_write_seq(dsi, 0xf1, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x2a);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x64, 0x16);
	mipi_dsi_dcs_write_seq(dsi, 0x67, 0x16);
	mipi_dsi_dcs_write_seq(dsi, 0x6a, 0x16);
	mipi_dsi_dcs_write_seq(dsi, 0x70, 0x30);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_READ_PPS_START, 0xf3);
	mipi_dsi_dcs_write_seq(dsi, 0xa3, 0xff);
	mipi_dsi_dcs_write_seq(dsi, 0xa4, 0xff);
	mipi_dsi_dcs_write_seq(dsi, 0xa5, 0xff);
	mipi_dsi_dcs_write_seq(dsi, 0xd6, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x26);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0xa1);
	mipi_dsi_dcs_write_seq(dsi, 0x0a, 0xf2);
	mipi_dsi_dcs_write_seq(dsi, 0x04, 0x28);
	mipi_dsi_dcs_write_seq(dsi, 0x06, 0x30);
	mipi_dsi_dcs_write_seq(dsi, 0x0c, 0x13);
	mipi_dsi_dcs_write_seq(dsi, 0x0d, 0x0a);
	mipi_dsi_dcs_write_seq(dsi, 0x0f, 0x0a);
	mipi_dsi_dcs_write_seq(dsi, 0x11, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x12, 0x50);
	mipi_dsi_dcs_write_seq(dsi, 0x13, 0x51);
	mipi_dsi_dcs_write_seq(dsi, 0x14, 0x65);
	mipi_dsi_dcs_write_seq(dsi, 0x15, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x16, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0x17, 0xa0);
	mipi_dsi_dcs_write_seq(dsi, 0x18, 0x86);
	mipi_dsi_dcs_write_seq(dsi, 0x19, 0x11);
	mipi_dsi_dcs_write_seq(dsi, 0x1a, 0x7b);
	mipi_dsi_dcs_write_seq(dsi, 0x1b, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0x1c, 0xbb);
	mipi_dsi_dcs_write_seq(dsi, 0x22, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x23, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x2a, 0x11);
	mipi_dsi_dcs_write_seq(dsi, 0x2b, 0x7b);
	mipi_dsi_dcs_write_seq(dsi, 0x1d, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x1e, 0xc3);
	mipi_dsi_dcs_write_seq(dsi, 0x1f, 0xc3);
	mipi_dsi_dcs_write_seq(dsi, 0x24, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x25, 0xc3);
	mipi_dsi_dcs_write_seq(dsi, 0x2f, 0x05);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_PARTIAL_ROWS, 0xc3);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_PARTIAL_COLUMNS, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x32, 0xc3);
	mipi_dsi_dcs_write_seq(dsi, 0x39, 0x00);

	ret = mipi_dsi_dcs_set_pixel_format(dsi, 0xc3);
	if (ret < 0) {
		dev_err(dev, "Failed to set pixel format: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, 0x20, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x33, 0x11);
	mipi_dsi_dcs_write_seq(dsi, 0x34, 0x78);
	mipi_dsi_dcs_write_seq(dsi, 0x35, 0x16);
	mipi_dsi_dcs_write_seq(dsi, 0xc8, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0xc9, 0x82);
	mipi_dsi_dcs_write_seq(dsi, 0xca, 0x4e);
	mipi_dsi_dcs_write_seq(dsi, 0xcb, 0x00);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_READ_PPS_CONTINUE, 0x4c);
	mipi_dsi_dcs_write_seq(dsi, 0xaa, 0x47);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x27);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x56, 0x06);
	mipi_dsi_dcs_write_seq(dsi, 0x58, 0x80);
	mipi_dsi_dcs_write_seq(dsi, 0x59, 0x53);
	mipi_dsi_dcs_write_seq(dsi, 0x5a, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x5b, 0x14);
	mipi_dsi_dcs_write_seq(dsi, 0x5c, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x5d, 0x01);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_CABC_MIN_BRIGHTNESS, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0x5f, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0x60, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x61, 0x1d);
	mipi_dsi_dcs_write_seq(dsi, 0x62, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x63, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x64, 0x24);
	mipi_dsi_dcs_write_seq(dsi, 0x65, 0x1c);
	mipi_dsi_dcs_write_seq(dsi, 0x66, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x67, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x68, 0x25);
	mipi_dsi_dcs_write_seq(dsi, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x78, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xc3, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xd1, 0x24);
	mipi_dsi_dcs_write_seq(dsi, 0xd2, 0x30);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x2a);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x22, 0x2f);
	mipi_dsi_dcs_write_seq(dsi, 0x23, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0x24, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x25, 0xc3);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_GAMMA_CURVE, 0xf8);
	mipi_dsi_dcs_write_seq(dsi, 0x27, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x28, 0x1a);
	mipi_dsi_dcs_write_seq(dsi, 0x29, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x2a, 0x1a);
	mipi_dsi_dcs_write_seq(dsi, 0x2b, 0x00);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_LUT, 0x1a);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0xe0);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x14, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x16, 0xc0);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0xf0);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);

	ret = mipi_dsi_dcs_set_pixel_format(dsi, 0x08);
	if (ret < 0) {
		dev_err(dev, "Failed to set pixel format: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x24);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);

	ret = mipi_dsi_dcs_set_pixel_format(dsi, 0x5d);
	if (ret < 0) {
		dev_err(dev, "Failed to set pixel format: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, 0x3b, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x4a, 0x5d);
	mipi_dsi_dcs_write_seq(dsi, 0x4b, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x5a, 0x70);
	mipi_dsi_dcs_write_seq(dsi, 0x5b, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x91, 0x44);
	mipi_dsi_dcs_write_seq(dsi, 0x92, 0x75);
	mipi_dsi_dcs_write_seq(dsi, 0xdb, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xdc, 0x75);
	mipi_dsi_dcs_write_seq(dsi, 0xdd, 0x22);
	mipi_dsi_dcs_write_seq(dsi, 0xdf, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xe0, 0x75);
	mipi_dsi_dcs_write_seq(dsi, 0xe1, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xe2, 0x75);
	mipi_dsi_dcs_write_seq(dsi, 0xe3, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xe4, 0x75);
	mipi_dsi_dcs_write_seq(dsi, 0xe5, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xe6, 0x75);
	mipi_dsi_dcs_write_seq(dsi, 0x5c, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x5d, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x8d, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x8e, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x25);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x1f, 0x70);
	mipi_dsi_dcs_write_seq(dsi, 0x20, 0x60);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_GAMMA_CURVE, 0x70);
	mipi_dsi_dcs_write_seq(dsi, 0x27, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x33, 0x70);
	mipi_dsi_dcs_write_seq(dsi, 0x34, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x48, 0x70);
	mipi_dsi_dcs_write_seq(dsi, 0x49, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0x5b, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x61, 0x70);
	mipi_dsi_dcs_write_seq(dsi, 0x62, 0x60);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x26);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x02, 0x31);
	mipi_dsi_dcs_write_seq(dsi, 0x19, 0x0a);
	mipi_dsi_dcs_write_seq(dsi, 0x1a, 0x7f);
	mipi_dsi_dcs_write_seq(dsi, 0x1b, 0x0a);
	mipi_dsi_dcs_write_seq(dsi, 0x1c, 0x0c);
	mipi_dsi_dcs_write_seq(dsi, 0x2a, 0x0a);
	mipi_dsi_dcs_write_seq(dsi, 0x2b, 0x7f);
	mipi_dsi_dcs_write_seq(dsi, 0x1e, 0x75);
	mipi_dsi_dcs_write_seq(dsi, 0x1f, 0x75);
	mipi_dsi_dcs_write_seq(dsi, 0x25, 0x75);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_PARTIAL_ROWS, 0x75);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_PARTIAL_COLUMNS, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0x32, 0x8d);

	ret = mipi_dsi_dcs_set_pixel_format(dsi, 0x75);
	if (ret < 0) {
		dev_err(dev, "Failed to set pixel format: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x2a);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x25, 0x75);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xb9, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x18, 0x40);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xb9, 0x02);

	ret = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret < 0) {
		dev_err(dev, "Failed to set tear on: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, 0xbb, 0x13);
	mipi_dsi_dcs_write_seq(dsi, 0x3b, 0x03, 0x5f, 0x1a, 0x04, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0x10);
	usleep_range(10000, 11000);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x01);

	ret = mipi_dsi_dcs_set_display_brightness(dsi, 0);
	if (ret < 0) {
		dev_err(dev, "Failed to set display brightness: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x2c);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x68, 0x05, 0x01);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(100);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}
	msleep(30);

	return 0;
}

static int nt36523w_boe_off(struct nt36523w_boe *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(80);

	return 0;
}

static int nt36523w_boe_prepare(struct drm_panel *panel)
{
	struct nt36523w_boe *ctx = to_nt36523w_boe(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->prepared)
		return 0;

	ret = regulator_enable(ctx->supply);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulator: %d\n", ret);
		return ret;
	}

	nt36523w_boe_reset(ctx);

	ret = nt36523w_boe_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_disable(ctx->supply);
		return ret;
	}

	ctx->prepared = true;
	return 0;
}

static int nt36523w_boe_unprepare(struct drm_panel *panel)
{
	struct nt36523w_boe *ctx = to_nt36523w_boe(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = nt36523w_boe_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_disable(ctx->supply);

	ctx->prepared = false;
	return 0;
}

static const struct drm_display_mode nt36523w_boe_mode = {
	.clock = (1200 + 58 + 2 + 60) * (2000 + 26 + 2 + 93) * 60 / 1000,
	.hdisplay = 1200,
	.hsync_start = 1200 + 58,
	.hsync_end = 1200 + 58 + 2,
	.htotal = 1200 + 58 + 2 + 60,
	.vdisplay = 2000,
	.vsync_start = 2000 + 26,
	.vsync_end = 2000 + 26 + 2,
	.vtotal = 2000 + 26 + 2 + 93,
	.width_mm = 143,
	.height_mm = 235,
};

static int nt36523w_boe_get_modes(struct drm_panel *panel, struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &nt36523w_boe_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static enum drm_panel_orientation nt36523w_boe_panel_get_orientation(struct drm_panel *panel)
{
	struct nt36523w_boe *ctx = to_nt36523w_boe(panel);

	return ctx->orientation;
}

static const struct drm_panel_funcs nt36523w_boe_panel_funcs = {
	.prepare = nt36523w_boe_prepare,
	.unprepare = nt36523w_boe_unprepare,
	.get_modes = nt36523w_boe_get_modes,
	.get_orientation = nt36523w_boe_panel_get_orientation,
};

static int nt36523w_boe_bl_update_status(struct backlight_device *bl)
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

static int nt36523w_boe_bl_get_brightness(struct backlight_device *bl)
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

static const struct backlight_ops nt36523w_boe_bl_ops = {
	.update_status = nt36523w_boe_bl_update_status,
	.get_brightness = nt36523w_boe_bl_get_brightness,
};

static struct backlight_device * nt36523w_boe_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 512,
		.max_brightness = 4095,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &nt36523w_boe_bl_ops, &props);
}

static int nt36523w_boe_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct nt36523w_boe *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->supply = devm_regulator_get(dev, "vddio");
	if (IS_ERR(ctx->supply))
		return dev_err_probe(dev, PTR_ERR(ctx->supply),
				     "Failed to get vddio regulator\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &nt36523w_boe_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = of_drm_get_panel_orientation(dev->of_node, &ctx->orientation);
	if (ret < 0) {
		dev_err(dev, "%pOF: failed to get orientation %d\n", dev->of_node, ret);
		return ret;
	}

	ctx->panel.backlight = nt36523w_boe_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void nt36523w_boe_remove(struct mipi_dsi_device *dsi)
{
	struct nt36523w_boe *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id nt36523w_boe_of_match[] = {
	{ .compatible = "lenovo,nt36523w-boe-j606" },
	{ }
};
MODULE_DEVICE_TABLE(of, nt36523w_boe_of_match);

static struct mipi_dsi_driver nt36523w_boe_driver = {
	.probe = nt36523w_boe_probe,
	.remove = nt36523w_boe_remove,
	.driver = {
		.name = "panel-nt36523w-boe-2k",
		.of_match_table = nt36523w_boe_of_match,
	},
};
module_mipi_dsi_driver(nt36523w_boe_driver);

MODULE_DESCRIPTION("DRM panel driver for Lenovo Tab P11 BOE panel");
MODULE_LICENSE("GPL");
