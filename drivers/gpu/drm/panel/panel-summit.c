// SPDX-License-Identifier: GPL-2.0-only

#include <linux/backlight.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_mode.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>
#include <video/mipi_display.h>

struct summit_panel_data {
	bool generic_brightness;
	bool has_fixed_mode;
};

struct summit_data {
	struct mipi_dsi_device *dsi;
	struct backlight_device *bl;
	struct drm_panel panel;
	const struct summit_panel_data *data;
};

static int summit_write_brightness(struct summit_data *s_data,
				   unsigned int level)
{
	u8 payload[] = {
		MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
		level & 0xff,
		level >> 8,
	};
	ssize_t ret;

	if (!s_data->data->generic_brightness)
		return mipi_dsi_dcs_set_display_brightness(s_data->dsi,
							   level);

	ret = mipi_dsi_generic_write(s_data->dsi, payload,
				     sizeof(payload));

	return ret < 0 ? ret : 0;
}

static int summit_set_brightness(struct device *dev)
{
	struct summit_data *s_data = dev_get_drvdata(dev);
	unsigned int level = backlight_get_brightness(s_data->bl);

	return summit_write_brightness(s_data, level);
}

static int summit_bl_update_status(struct backlight_device *dev)
{
	return summit_set_brightness(&dev->dev);
}

static const struct backlight_ops summit_bl_ops = {
	.update_status	= summit_bl_update_status,
};

static struct drm_display_mode summit_mode = {
	.vdisplay = 2008,
	.hdisplay = 60,
	.hsync_start = 60 + 8,
	.hsync_end = 60 + 8 + 80,
	.htotal = 60 + 8 + 80 + 40,
	.vsync_start = 2008 + 1,
	.vsync_end = 2008 + 1 + 15,
	.vtotal = 2008 + 1 + 15 + 6,
	.clock = ((60 + 8 + 80 + 40) * (2008 + 1 + 15 + 6) * 60) / 1000,
	.type = DRM_MODE_TYPE_DRIVER,
	.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static int summit_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	struct summit_data *s_data =
		container_of(panel, struct summit_data, panel);

	if (!s_data->data->has_fixed_mode)
		return 0;

	connector->display_info.non_desktop = true;
	drm_object_property_set_value(&connector->base,
				      connector->dev->mode_config.non_desktop_property,
				      connector->display_info.non_desktop);

	return drm_connector_helper_get_modes_fixed(connector, &summit_mode);
}

static const struct drm_panel_funcs summit_panel_funcs = {
	.get_modes = summit_get_modes,
};

static int summit_probe(struct mipi_dsi_device *dsi)
{
	struct backlight_properties props = { 0 };
	struct device *dev = &dsi->dev;
	const struct summit_panel_data *data;
	struct summit_data *s_data;
	u32 brightness;
	int ret;

	data = device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	s_data = devm_drm_panel_alloc(dev, struct summit_data, panel,
				      &summit_panel_funcs,
				      DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(s_data))
		return PTR_ERR(s_data);

	mipi_dsi_set_drvdata(dsi, s_data);
	s_data->dsi = dsi;
	s_data->data = data;

	ret = device_property_read_u32(dev, "max-brightness",
				       &props.max_brightness);
	if (ret)
		return ret;

	if (!device_property_read_u32(dev, "default-brightness",
				      &brightness)) {
		if (brightness > props.max_brightness)
			return -EINVAL;

		props.brightness = brightness;
	}

	props.type = BACKLIGHT_RAW;

	s_data->bl = devm_backlight_device_register(dev, dev_name(dev),
						    dev, s_data, &summit_bl_ops, &props);
	if (IS_ERR(s_data->bl))
		return PTR_ERR(s_data->bl);

	drm_panel_add(&s_data->panel);

	return mipi_dsi_attach(dsi);
}

static void summit_remove(struct mipi_dsi_device *dsi)
{
	struct summit_data *s_data = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&s_data->panel);
}

static int summit_suspend(struct device *dev)
{
	struct summit_data *s_data = dev_get_drvdata(dev);

	return summit_write_brightness(s_data, 0);
}

static DEFINE_SIMPLE_DEV_PM_OPS(summit_pm_ops, summit_suspend,
				summit_set_brightness);

static const struct summit_panel_data summit_touchbar_data = {
	.has_fixed_mode = true,
};

static const struct summit_panel_data summit_d421_data = {
	.generic_brightness = true,
};

static const struct of_device_id summit_of_match[] = {
	{
		.compatible = "apple,d421-summit",
		.data = &summit_d421_data,
	},
	{
		.compatible = "apple,summit",
		.data = &summit_touchbar_data,
	},
	{},
};

MODULE_DEVICE_TABLE(of, summit_of_match);

static struct mipi_dsi_driver summit_driver = {
	.probe = summit_probe,
	.remove = summit_remove,
	.driver = {
		.name = "panel-summit",
		.of_match_table = summit_of_match,
		.pm = pm_sleep_ptr(&summit_pm_ops),
	},
};
module_mipi_dsi_driver(summit_driver);

MODULE_DESCRIPTION("Summit Display Panel Driver");
MODULE_LICENSE("GPL");
