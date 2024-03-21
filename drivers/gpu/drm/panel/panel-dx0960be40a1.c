// SPDX-License-Identifier: GPL-2.0+
/*
 * Based on feixin-k101-im2ba02 driver
 * Copyright (C) 2019-2020 Icenowy Zheng <icenowy@aosc.io>
 * Copyright (C) 2022 Sergey Moryakov <sergey@nqnet.org>
 */

#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define K101_IM2BA02_INIT_CMD_LEN 1

static const char *const regulator_names[] = { "dvdd", "avdd", "cvdd" };

struct k101_im2ba02 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;

	struct regulator_bulk_data supplies[ARRAY_SIZE(regulator_names)];
	struct gpio_desc *reset;
};

static inline struct k101_im2ba02 *
panel_to_k101_im2ba02(struct drm_panel *panel)
{
	return container_of(panel, struct k101_im2ba02, panel);
}

struct k101_im2ba02_init_cmd {
	u8 data[K101_IM2BA02_INIT_CMD_LEN];
};

static const struct k101_im2ba02_init_cmd k101_im2ba02_init_cmds[] = {
	/* Switch to page 0 */
	{ .data = { 0x11 } },

	/* Seems to be some password */
	{ .data = { 0x29 } },
};

static const struct k101_im2ba02_init_cmd timed_cmds[] = {
	{ .data = { 0x11 } },
	{ .data = { 0x29 } },
};

static int k101_im2ba02_prepare(struct drm_panel *panel)
{
	struct k101_im2ba02 *ctx = panel_to_k101_im2ba02(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	unsigned int i;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret)
		return ret;

	msleep(30);

	gpiod_set_value(ctx->reset, 1);
	msleep(50);

	gpiod_set_value(ctx->reset, 0);
	msleep(50);

	gpiod_set_value(ctx->reset, 1);
	msleep(200);

	for (i = 0; i < ARRAY_SIZE(k101_im2ba02_init_cmds); i++) {
		const struct k101_im2ba02_init_cmd *cmd =
			&k101_im2ba02_init_cmds[i];

		ret = mipi_dsi_dcs_write_buffer(dsi, cmd->data,
						K101_IM2BA02_INIT_CMD_LEN);
		msleep(50);
		if (ret < 0)
			goto powerdown;
	}

	return 0;

powerdown:
	gpiod_set_value(ctx->reset, 0);
	msleep(50);

	return regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
}

static int k101_im2ba02_enable(struct drm_panel *panel)
{
	struct k101_im2ba02 *ctx = panel_to_k101_im2ba02(panel);
	// const struct k101_im2ba02_init_cmd *cmd0 = &timed_cmds[0];

	// const struct k101_im2ba02_init_cmd *cmd1 = &timed_cmds[1];
	int ret;

	msleep(150);

	ret = mipi_dsi_dcs_set_display_on(ctx->dsi);
	if (ret < 0)
		return ret;

	msleep(50);

	// ret =  mipi_dsi_dcs_write_buffer(ctx->dsi, cmd0->data, K101_IM2BA02_INIT_CMD_LEN);
	// if (ret < 0)
	// 	return ret;
	// ret = mipi_dsi_dcs_write_buffer(ctx->dsi, cmd1->data, K101_IM2BA02_INIT_CMD_LEN);
	// if (ret < 0)
	// 	return ret;
	return 0;
}

static int k101_im2ba02_disable(struct drm_panel *panel)
{
	struct k101_im2ba02 *ctx = panel_to_k101_im2ba02(panel);

	return mipi_dsi_dcs_set_display_off(ctx->dsi);
}

static int k101_im2ba02_unprepare(struct drm_panel *panel)
{
	struct k101_im2ba02 *ctx = panel_to_k101_im2ba02(panel);
	int ret;

	ret = mipi_dsi_dcs_set_display_off(ctx->dsi);
	if (ret < 0)
		dev_err(panel->dev, "failed to set display off: %d\n", ret);

	ret = mipi_dsi_dcs_enter_sleep_mode(ctx->dsi);
	if (ret < 0)
		dev_err(panel->dev, "failed to enter sleep mode: %d\n", ret);

	msleep(200);

	gpiod_set_value(ctx->reset, 0);
	msleep(20);

	return regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
}

static const struct drm_display_mode k101_im2ba02_default_mode = {
	.clock = 85000,

	.hdisplay = 1024,
	.hsync_start = 1024 + 250,
	.hsync_end = 1024 + 250 + 10,
	.htotal = 1024 + 250 + 10 + 60,

	.vdisplay = 600,
	.vsync_start = 600 + 12,
	.vsync_end = 600 + 12 + 1,
	.vtotal = 600 + 12 + 1 + 22,

	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	.width_mm = 196,
	.height_mm = 114,
};

static int k101_im2ba02_get_modes(struct drm_panel *panel,
				  struct drm_connector *connector)
{
	struct k101_im2ba02 *ctx = panel_to_k101_im2ba02(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &k101_im2ba02_default_mode);
	if (!mode) {
		dev_err(&ctx->dsi->dev, "failed to add mode %ux%u@%u\n",
			k101_im2ba02_default_mode.hdisplay,
			k101_im2ba02_default_mode.vdisplay,
			drm_mode_vrefresh(&k101_im2ba02_default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs k101_im2ba02_funcs = {
	.disable = k101_im2ba02_disable,
	.unprepare = k101_im2ba02_unprepare,
	.prepare = k101_im2ba02_prepare,
	.enable = k101_im2ba02_enable,
	.get_modes = k101_im2ba02_get_modes,
};

static int k101_im2ba02_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct k101_im2ba02 *ctx;
	unsigned int i;
	int ret;

	ctx = devm_kzalloc(&dsi->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dsi = dsi;

	for (i = 0; i < ARRAY_SIZE(ctx->supplies); i++)
		ctx->supplies[i].supply = regulator_names[i];

	ret = devm_regulator_bulk_get(&dsi->dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0) {
		dev_err(&dsi->dev, "Couldn't get regulators\n");
		return ret;
	}

	ctx->reset = devm_gpiod_get(&dsi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset)) {
		dev_err(&dsi->dev, "Couldn't get our reset GPIO\n");
		return PTR_ERR(ctx->reset);
	}

	drm_panel_init(&ctx->panel, &dsi->dev, &k101_im2ba02_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	drm_panel_add(&ctx->panel);

	dsi->mode_flags = MIPI_DSI_MODE_VIDEO;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->lanes = 4;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static int k101_im2ba02_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct k101_im2ba02 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id k101_im2ba02_of_match[] = {
	{
		.compatible = "dx,dx0960be40a1",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, k101_im2ba02_of_match);

static struct mipi_dsi_driver k101_im2ba02_driver = {
	.probe = k101_im2ba02_dsi_probe,
	.remove = k101_im2ba02_dsi_remove,
	.driver = {
		.name = "dx0960be40a1",
		.of_match_table = k101_im2ba02_of_match,
	},
};
module_mipi_dsi_driver(k101_im2ba02_driver);

MODULE_AUTHOR("Icenowy Zheng <icenowy@aosc.io>");
MODULE_DESCRIPTION("Feixin K101 IM2BA02 MIPI-DSI LCD panel");
MODULE_LICENSE("GPL");
