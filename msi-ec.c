// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * msi-ec.c - MSI Embedded Controller for laptops support.
 *
 * This driver exports a few files in /sys/devices/platform/msi-laptop:
 *   webcam            Integrated webcam activation
 *   fn_key            Function key location
 *   win_key           Windows key location
 *   battery_mode      Battery health options
 *   cooler_boost      Cooler boost function
 *   shift_mode        CPU & GPU performance modes
 *   fan_mode          FAN performance modes
 *   fw_version        Firmware version
 *   fw_release_date   Firmware release date
 *   cpu/..            CPU related options
 *   gpu/..            GPU related options
 *
 * In addition to these platform device attributes the driver
 * registers itself in the Linux power_supply subsystem and is
 * available to userspace under /sys/class/power_supply/<power_supply>:
 *
 *   charge_control_start_threshold
 *   charge_control_end_threshold
 * 
 * This driver also registers available led class devices for
 * mute, micmute and keyboard_backlight leds
 *
 * This driver might not work on other laptops produced by MSI. Also, and until
 * future enhancements, no DMI data are used to identify your compatibility
 *
 */

#include "constants.h"

#include <acpi/battery.h>
#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#define streq(x, y) (strcmp(x, y) == 0 || strcmp(x, y "\n") == 0)

static int ec_read_seq(u8 addr, u8 *buf, int len)
{
	int result;
	u8 i;
	for (i = 0; i < len; i++) {
		result = ec_read(addr + i, buf + i);
		if (result < 0)
			return result;
	}
	return 0;
}

// ============================================================ //
// Sysfs power_supply subsystem
// ============================================================ //

static ssize_t charge_control_threshold_show(u8 offset, struct device *device,
					     struct device_attribute *attr,
					     char *buf)
{
	u8 rdata;
	int result;

	result = ec_read(MSI_EC_CHARGE_CONTROL_ADDRESS, &rdata);
	if (result < 0)
		return result;

	return sprintf(buf, "%i\n", rdata - offset);
}

static ssize_t
charge_control_start_threshold_show(struct device *device,
				    struct device_attribute *attr, char *buf)
{
	return charge_control_threshold_show(MSI_EC_CHARGE_CONTROL_OFFSET_START,
					     device, attr, buf);
}

static ssize_t charge_control_end_threshold_show(struct device *device,
						 struct device_attribute *attr,
						 char *buf)
{
	return charge_control_threshold_show(MSI_EC_CHARGE_CONTROL_OFFSET_END,
					     device, attr, buf);
}

static ssize_t charge_control_threshold_store(u8 offset, struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t count)
{
	u8 wdata;
	int result;

	result = kstrtou8(buf, 10, &wdata);
	if (result < 0)
		return result;

	wdata += offset;
	if (wdata < MSI_EC_CHARGE_CONTROL_RANGE_MIN ||
	    wdata > MSI_EC_CHARGE_CONTROL_RANGE_MAX)
		return -EINVAL;

	result = ec_write(MSI_EC_CHARGE_CONTROL_ADDRESS, wdata);
	if (result < 0)
		return result;

	return count;
}

static ssize_t
charge_control_start_threshold_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	return charge_control_threshold_store(
		MSI_EC_CHARGE_CONTROL_OFFSET_START, dev, attr, buf, count);
}

static ssize_t charge_control_end_threshold_store(struct device *dev,
						  struct device_attribute *attr,
						  const char *buf, size_t count)
{
	return charge_control_threshold_store(MSI_EC_CHARGE_CONTROL_OFFSET_END,
					      dev, attr, buf, count);
}

static DEVICE_ATTR_RW(charge_control_start_threshold);
static DEVICE_ATTR_RW(charge_control_end_threshold);

static struct attribute *msi_battery_attrs[] = {
	&dev_attr_charge_control_start_threshold.attr,
	&dev_attr_charge_control_end_threshold.attr,
	NULL,
};

ATTRIBUTE_GROUPS(msi_battery);

static int msi_battery_add(struct power_supply *battery)
{
	if (device_add_groups(&battery->dev, msi_battery_groups))
		return -ENODEV;
	return 0;
}

static int msi_battery_remove(struct power_supply *battery)
{
	device_remove_groups(&battery->dev, msi_battery_groups);
	return 0;
}

static struct acpi_battery_hook battery_hook = {
	.add_battery = msi_battery_add,
	.remove_battery = msi_battery_remove,
	.name = MSI_DRIVER_NAME,
};

// ============================================================ //
// Sysfs platform device attributes (root)
// ============================================================ //

static ssize_t webcam_show(struct device *device, struct device_attribute *attr,
			   char *buf)
{
	u8 rdata;
	int result;

	result = ec_read(MSI_EC_WEBCAM_ADDRESS, &rdata);
	if (result < 0)
		return result;

	switch (rdata) {
	case MSI_EC_WEBCAM_ON:
		return sprintf(buf, "%s\n", "on");
	case MSI_EC_WEBCAM_OFF:
		return sprintf(buf, "%s\n", "off");
	default:
		return sprintf(buf, "%s (%i)\n", "unknown", rdata);
	}
}

static ssize_t webcam_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	int result = -EINVAL;

	if (streq(buf, "on"))
		result = ec_write(MSI_EC_WEBCAM_ADDRESS, MSI_EC_WEBCAM_ON);

	if (streq(buf, "off"))
		result = ec_write(MSI_EC_WEBCAM_ADDRESS, MSI_EC_WEBCAM_OFF);

	if (result < 0)
		return result;

	return count;
}

static ssize_t fn_key_show(struct device *device, struct device_attribute *attr,
			   char *buf)
{
	u8 rdata;
	int result;

	result = ec_read(MSI_EC_FN_WIN_ADDRESS, &rdata);
	if (result < 0)
		return result;

	switch (rdata) {
	case MSI_EC_FN_KEY_LEFT:
		return sprintf(buf, "%s\n", "left");
	case MSI_EC_FN_KEY_RIGHT:
		return sprintf(buf, "%s\n", "right");
	default:
		return sprintf(buf, "%s (%i)\n", "unknown", rdata);
	}
}

static ssize_t fn_key_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	int result = -EINVAL;

	if (streq(buf, "left"))
		result = ec_write(MSI_EC_FN_WIN_ADDRESS, MSI_EC_FN_KEY_LEFT);

	if (streq(buf, "right"))
		result = ec_write(MSI_EC_FN_WIN_ADDRESS, MSI_EC_FN_KEY_RIGHT);

	if (result < 0)
		return result;

	return count;
}

static ssize_t win_key_show(struct device *device,
			    struct device_attribute *attr, char *buf)
{
	u8 rdata;
	int result;

	result = ec_read(MSI_EC_FN_WIN_ADDRESS, &rdata);
	if (result < 0)
		return result;

	switch (rdata) {
	case MSI_EC_WIN_KEY_LEFT:
		return sprintf(buf, "%s\n", "left");
	case MSI_EC_WIN_KEY_RIGHT:
		return sprintf(buf, "%s\n", "right");
	default:
		return sprintf(buf, "%s (%i)\n", "unknown", rdata);
	}
}

static ssize_t win_key_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	int result = -EINVAL;

	if (streq(buf, "left"))
		result = ec_write(MSI_EC_FN_WIN_ADDRESS, MSI_EC_WIN_KEY_LEFT);

	if (streq(buf, "right"))
		result = ec_write(MSI_EC_FN_WIN_ADDRESS, MSI_EC_WIN_KEY_RIGHT);

	if (result < 0)
		return result;

	return count;
}

static ssize_t battery_mode_show(struct device *device,
				 struct device_attribute *attr, char *buf)
{
	u8 rdata;
	int result;

	result = ec_read(MSI_EC_BATTERY_MODE_ADDRESS, &rdata);
	if (result < 0)
		return result;

	switch (rdata) {
	case MSI_EC_BATTERY_MODE_MAX_CHARGE:
		return sprintf(buf, "%s\n", "max");
	case MSI_EC_BATTERY_MODE_MEDIUM_CHARGE:
		return sprintf(buf, "%s\n", "medium");
	case MSI_EC_BATTERY_MODE_MIN_CHARGE:
		return sprintf(buf, "%s\n", "min");
	default:
		return sprintf(buf, "%s (%i)\n", "unknown", rdata);
	}
}

static ssize_t battery_mode_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	int result = -EINVAL;

	if (streq(buf, "max"))
		result = ec_write(MSI_EC_BATTERY_MODE_ADDRESS,
				  MSI_EC_BATTERY_MODE_MAX_CHARGE);

	if (streq(buf, "medium"))
		result = ec_write(MSI_EC_BATTERY_MODE_ADDRESS,
				  MSI_EC_BATTERY_MODE_MEDIUM_CHARGE);

	if (streq(buf, "min"))
		result = ec_write(MSI_EC_BATTERY_MODE_ADDRESS,
				  MSI_EC_BATTERY_MODE_MIN_CHARGE);

	if (result < 0)
		return result;

	return count;
}

static ssize_t cooler_boost_show(struct device *device,
				 struct device_attribute *attr, char *buf)
{
	u8 rdata;
	int result;

	result = ec_read(MSI_EC_COOLER_BOOST_ADDRESS, &rdata);
	if (result < 0)
		return result;

	switch (rdata) {
	case MSI_EC_COOLER_BOOST_ON:
		return sprintf(buf, "%s\n", "on");
	case MSI_EC_COOLER_BOOST_OFF:
		return sprintf(buf, "%s\n", "off");
	default:
		return sprintf(buf, "%s (%i)\n", "unknown", rdata);
	}
}

static ssize_t cooler_boost_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	int result = -EINVAL;

	if (streq(buf, "on"))
		result = ec_write(MSI_EC_COOLER_BOOST_ADDRESS,
				  MSI_EC_COOLER_BOOST_ON);

	if (streq(buf, "off"))
		result = ec_write(MSI_EC_COOLER_BOOST_ADDRESS,
				  MSI_EC_COOLER_BOOST_OFF);

	if (result < 0)
		return result;

	return count;
}

static ssize_t shift_mode_show(struct device *device,
			       struct device_attribute *attr, char *buf)
{
	u8 rdata;
	int result;

	result = ec_read(MSI_EC_SHIFT_MODE_ADDRESS, &rdata);
	if (result < 0)
		return result;

	switch (rdata) {
	case MSI_EC_SHIFT_MODE_PERFORMANCE:
		return sprintf(buf, "%s\n", "performance");
	case MSI_EC_SHIFT_MODE_BALANCED:
		return sprintf(buf, "%s\n", "balanced");
	case MSI_EC_SHIFT_MODE_ECO:
		return sprintf(buf, "%s\n", "eco");
	case MSI_EC_SHIFT_MODE_OFF:
		return sprintf(buf, "%s\n", "off");
	default:
		return sprintf(buf, "%s (%i)\n", "unknown", rdata);
	}
}

static ssize_t shift_mode_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	int result = -EINVAL;

	if (streq(buf, "performance"))
		result = ec_write(MSI_EC_SHIFT_MODE_ADDRESS,
				  MSI_EC_SHIFT_MODE_PERFORMANCE);

	if (streq(buf, "balanced"))
		result = ec_write(MSI_EC_SHIFT_MODE_ADDRESS,
				  MSI_EC_SHIFT_MODE_BALANCED);

	if (streq(buf, "eco"))
		result = ec_write(MSI_EC_SHIFT_MODE_ADDRESS,
				  MSI_EC_SHIFT_MODE_ECO);

	if (streq(buf, "off"))
		result = ec_write(MSI_EC_SHIFT_MODE_ADDRESS,
				  MSI_EC_SHIFT_MODE_OFF);

	if (result < 0)
		return result;

	return count;
}

static ssize_t fan_mode_show(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	u8 rdata;
	int result;

	result = ec_read(MSI_EC_FAN_MODE_ADDRESS, &rdata);
	if (result < 0)
		return result;

	switch (rdata) {
	case MSI_EC_FAN_MODE_SILENT:
		return sprintf(buf, "%s\n", "silent");
	case MSI_EC_FAN_MODE_BASIC:
		return sprintf(buf, "%s\n", "basic");
	case MSI_EC_FAN_MODE_ADVANCED:
		return sprintf(buf, "%s\n", "advanced");
	default:
		return sprintf(buf, "%s (%i)\n", "unknown", rdata);
	}
}

static ssize_t fan_mode_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	int result = -EINVAL;

	if (streq(buf, "silent"))
		result =
			ec_write(MSI_EC_FAN_MODE_ADDRESS, MSI_EC_FAN_MODE_SILENT);

	if (streq(buf, "basic"))
		result = ec_write(MSI_EC_FAN_MODE_ADDRESS,
				  MSI_EC_FAN_MODE_BASIC);

	if (streq(buf, "advanced"))
		result = ec_write(MSI_EC_FAN_MODE_ADDRESS,
				  MSI_EC_FAN_MODE_ADVANCED);

	if (result < 0)
		return result;

	return count;
}

static ssize_t fw_version_show(struct device *device,
			       struct device_attribute *attr, char *buf)
{
	u8 rdata[MSI_EC_FW_VERSION_LENGTH + 1];
	int result;

	memset(rdata, 0, MSI_EC_FW_VERSION_LENGTH + 1);
	result = ec_read_seq(MSI_EC_FW_VERSION_ADDRESS, rdata,
			     MSI_EC_FW_VERSION_LENGTH);
	if (result < 0)
		return result;

	return sprintf(buf, "%s\n", rdata);
}

static ssize_t fw_release_date_show(struct device *device,
				    struct device_attribute *attr, char *buf)
{
	u8 rdate[MSI_EC_FW_DATE_LENGTH + 1];
	u8 rtime[MSI_EC_FW_TIME_LENGTH + 1];
	int result;
	int year, month, day, hour, minute, second;

	memset(rdate, 0, MSI_EC_FW_DATE_LENGTH + 1);
	result = ec_read_seq(MSI_EC_FW_DATE_ADDRESS, rdate,
			     MSI_EC_FW_DATE_LENGTH);
	if (result < 0)
		return result;
	sscanf(rdate, "%02d%02d%04d", &month, &day, &year);

	memset(rtime, 0, MSI_EC_FW_TIME_LENGTH + 1);
	result = ec_read_seq(MSI_EC_FW_TIME_ADDRESS, rtime,
			     MSI_EC_FW_TIME_LENGTH);
	if (result < 0)
		return result;
	sscanf(rtime, "%02d:%02d:%02d", &hour, &minute, &second);

	return sprintf(buf, "%04d/%02d/%02d %02d:%02d:%02d\n", year, month, day,
		       hour, minute, second);
}

static DEVICE_ATTR_RW(webcam);
static DEVICE_ATTR_RW(fn_key);
static DEVICE_ATTR_RW(win_key);
static DEVICE_ATTR_RW(battery_mode);
static DEVICE_ATTR_RW(cooler_boost);
static DEVICE_ATTR_RW(shift_mode);
static DEVICE_ATTR_RW(fan_mode);
static DEVICE_ATTR_RO(fw_version);
static DEVICE_ATTR_RO(fw_release_date);

static struct attribute *msi_root_attrs[] = {
	&dev_attr_webcam.attr,		&dev_attr_fn_key.attr,
	&dev_attr_win_key.attr,		&dev_attr_battery_mode.attr,
	&dev_attr_cooler_boost.attr,	&dev_attr_shift_mode.attr,
	&dev_attr_fan_mode.attr,	&dev_attr_fw_version.attr,
	&dev_attr_fw_release_date.attr, NULL,
};

static const struct attribute_group msi_root_group = {
	.attrs = msi_root_attrs,
};

// ============================================================ //
// Sysfs platform device attributes (cpu)
// ============================================================ //

static ssize_t cpu_realtime_temperature_show(struct device *device,
					     struct device_attribute *attr,
					     char *buf)
{
	u8 rdata;
	int result;

	result = ec_read(MSI_EC_CPU_REALTIME_TEMPERATURE_ADDRESS, &rdata);
	if (result < 0)
		return result;

	return sprintf(buf, "%i\n", rdata);
}

static ssize_t cpu_realtime_fan_speed_show(struct device *device,
					   struct device_attribute *attr,
					   char *buf)
{
	u8 rdata;
	int result;

	result = ec_read(MSI_EC_CPU_REALTIME_FAN_SPEED_ADDRESS, &rdata);
	if (result < 0)
		return result;

	if (rdata < MSI_EC_CPU_REALTIME_FAN_SPEED_BASE_MIN ||
	    rdata > MSI_EC_CPU_REALTIME_FAN_SPEED_BASE_MAX)
		return -EINVAL;

	return sprintf(buf, "%i\n",
		       100 * (rdata - MSI_EC_CPU_REALTIME_FAN_SPEED_BASE_MIN) /
			       (MSI_EC_CPU_REALTIME_FAN_SPEED_BASE_MAX -
				MSI_EC_CPU_REALTIME_FAN_SPEED_BASE_MIN));
}

static ssize_t cpu_basic_fan_speed_show(struct device *device,
					struct device_attribute *attr,
					char *buf)
{
	u8 rdata;
	int result;

	result = ec_read(MSI_EC_CPU_BASIC_FAN_SPEED_ADDRESS, &rdata);
	if (result < 0)
		return result;

	if (rdata < MSI_EC_CPU_BASIC_FAN_SPEED_BASE_MIN ||
	    rdata > MSI_EC_CPU_BASIC_FAN_SPEED_BASE_MAX)
		return -EINVAL;

	return sprintf(buf, "%i\n",
		       100 * (rdata - MSI_EC_CPU_BASIC_FAN_SPEED_BASE_MIN) /
			       (MSI_EC_CPU_BASIC_FAN_SPEED_BASE_MAX -
				MSI_EC_CPU_BASIC_FAN_SPEED_BASE_MIN));
}

static ssize_t cpu_basic_fan_speed_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	u8 wdata;
	int result;

	result = kstrtou8(buf, 10, &wdata);
	if (result < 0)
		return result;

	if (wdata > 100)
		return -EINVAL;

	result = ec_write(MSI_EC_CPU_BASIC_FAN_SPEED_ADDRESS,
			  (wdata * (MSI_EC_CPU_BASIC_FAN_SPEED_BASE_MAX -
				    MSI_EC_CPU_BASIC_FAN_SPEED_BASE_MIN) +
			   100 * MSI_EC_CPU_BASIC_FAN_SPEED_BASE_MIN) /
				  100);
	if (result < 0)
		return result;

	return count;
}

static struct device_attribute dev_attr_cpu_realtime_temperature = {
	.attr = {
		.name = "realtime_temperature",
		.mode = 0444,
	},
	.show = cpu_realtime_temperature_show,
};

static struct device_attribute dev_attr_cpu_realtime_fan_speed = {
	.attr = {
		.name = "realtime_fan_speed",
		.mode = 0444,
	},
	.show = cpu_realtime_fan_speed_show,
};

static struct device_attribute dev_attr_cpu_basic_fan_speed = {
	.attr = {
		.name = "basic_fan_speed",
		.mode = 0644,
	},
	.show = cpu_basic_fan_speed_show,
	.store = cpu_basic_fan_speed_store,
};

static struct attribute *msi_cpu_attrs[] = {
	&dev_attr_cpu_realtime_temperature.attr,
	&dev_attr_cpu_realtime_fan_speed.attr,
	&dev_attr_cpu_basic_fan_speed.attr,
	NULL,
};

static const struct attribute_group msi_cpu_group = {
	.name = "cpu",
	.attrs = msi_cpu_attrs,
};

// ============================================================ //
// Sysfs platform device attributes (gpu)
// ============================================================ //

static ssize_t gpu_realtime_temperature_show(struct device *device,
					     struct device_attribute *attr,
					     char *buf)
{
	u8 rdata;
	int result;

	result = ec_read(MSI_EC_GPU_REALTIME_TEMPERATURE_ADDRESS, &rdata);
	if (result < 0)
		return result;

	return sprintf(buf, "%i\n", rdata);
}

static ssize_t gpu_realtime_fan_speed_show(struct device *device,
					   struct device_attribute *attr,
					   char *buf)
{
	u8 rdata;
	int result;

	result = ec_read(MSI_EC_GPU_REALTIME_FAN_SPEED_ADDRESS, &rdata);
	if (result < 0)
		return result;

	return sprintf(buf, "%i\n", rdata);
}

static struct device_attribute dev_attr_gpu_realtime_temperature = {
	.attr = {
		.name = "realtime_temperature",
		.mode = 0444,
	},
	.show = gpu_realtime_temperature_show,
};

static struct device_attribute dev_attr_gpu_realtime_fan_speed = {
	.attr = {
		.name = "realtime_fan_speed",
		.mode = 0444,
	},
	.show = gpu_realtime_fan_speed_show,
};

static struct attribute *msi_gpu_attrs[] = {
	&dev_attr_gpu_realtime_temperature.attr,
	&dev_attr_gpu_realtime_fan_speed.attr,
	NULL,
};

static const struct attribute_group msi_gpu_group = {
	.name = "gpu",
	.attrs = msi_gpu_attrs,
};

static const struct attribute_group *msi_platform_groups[] = {
	&msi_root_group,
	&msi_cpu_group,
	&msi_gpu_group,
	NULL,
};

static int msi_platform_probe(struct platform_device *pdev)
{
	int result;
	result = sysfs_create_groups(&pdev->dev.kobj, msi_platform_groups);
	if (result < 0)
		return result;
	return 0;
}

static int msi_platform_remove(struct platform_device *pdev)
{
	sysfs_remove_groups(&pdev->dev.kobj, msi_platform_groups);
	return 0;
}

static struct platform_device *msi_platform_device;

static struct platform_driver msi_platform_driver = {
	.driver = {
		.name = MSI_DRIVER_NAME,
	},
	.probe = msi_platform_probe,
	.remove = msi_platform_remove,
};

// ============================================================ //
// Sysfs leds subsystem
// ============================================================ //

static int micmute_led_sysfs_set(struct led_classdev *led_cdev,
				 enum led_brightness brightness)
{
	u8 state = brightness ? MSI_EC_MIC_LED_STATE_ON : MSI_EC_MIC_LED_STATE_OFF;
	int result = ec_write(MSI_EC_LED_MICMUTE_ADDRESS, state);
	if (result < 0)
		return result;
	return 0;
}

static int mute_led_sysfs_set(struct led_classdev *led_cdev,
			      enum led_brightness brightness)
{
	u8 state = brightness ? MSI_EC_MUTE_LED_STATE_ON : MSI_EC_MUTE_LED_STATE_OFF;
	int result = ec_write(MSI_EC_LED_MUTE_ADDRESS, state);
	if (result < 0)
		return result;
	return 0;
}

static enum led_brightness kbd_bl_sysfs_get(struct led_classdev *led_cdev)
{
	u8 rdata;
	int result = ec_read(MSI_EC_KBD_BL_ADDRESS, &rdata);
	if (result < 0)
		return 0;
	return rdata & MSI_EC_KBD_BL_STATE_MASK;
}

static int kbd_bl_sysfs_set(struct led_classdev *led_cdev,
			    enum led_brightness brightness)
{
	u8 wdata;
	if (brightness < 0 || brightness > 3)
		return -1;
	wdata = MSI_EC_KBD_BL_STATE[brightness];
	return ec_write(MSI_EC_KBD_BL_ADDRESS, wdata);
}

static struct led_classdev micmute_led_cdev = {
	.name = "platform::micmute",
	.max_brightness = 1,
	.brightness_set_blocking = &micmute_led_sysfs_set,
	.default_trigger = "audio-micmute",
};

static struct led_classdev mute_led_cdev = {
	.name = "platform::mute",
	.max_brightness = 1,
	.brightness_set_blocking = &mute_led_sysfs_set,
	.default_trigger = "audio-mute",
};

static struct led_classdev msiacpi_led_kbdlight = {
	.name = "msiacpi::kbd_backlight",
	.max_brightness = 3,
	.flags = LED_BRIGHT_HW_CHANGED,
	.brightness_set_blocking = &kbd_bl_sysfs_set,
	.brightness_get = &kbd_bl_sysfs_get,
};

// ============================================================ //
// Module load/unload
// ============================================================ //

static int __init msi_ec_init(void)
{
	int result;

	if (acpi_disabled) {
		pr_err("Unable to init because ACPI needs to be enabled first!\n");
		return -ENODEV;
	}

	result = platform_driver_register(&msi_platform_driver);
	if (result < 0) {
		return result;
	}

	msi_platform_device = platform_device_alloc(MSI_DRIVER_NAME, -1);
	if (msi_platform_device == NULL) {
		platform_driver_unregister(&msi_platform_driver);
		return -ENOMEM;
	}

	result = platform_device_add(msi_platform_device);
	if (result < 0) {
		platform_device_del(msi_platform_device);
		platform_driver_unregister(&msi_platform_driver);
		return result;
	}

	battery_hook_register(&battery_hook);

	led_classdev_register(&msi_platform_device->dev, &micmute_led_cdev);
	led_classdev_register(&msi_platform_device->dev, &mute_led_cdev);
	led_classdev_register(&msi_platform_device->dev, &msiacpi_led_kbdlight);

	pr_info("msi-ec: module_init\n");
	return 0;
}

static void __exit msi_ec_exit(void)
{
	led_classdev_unregister(&mute_led_cdev);
	led_classdev_unregister(&micmute_led_cdev);
	led_classdev_unregister(&msiacpi_led_kbdlight);

	battery_hook_unregister(&battery_hook);

	platform_driver_unregister(&msi_platform_driver);
	platform_device_del(msi_platform_device);

	pr_info("msi-ec: module_exit\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jose Angel Pastrana <japp0005@red.ujaen.es>");
MODULE_AUTHOR("Aakash Singh <mail@singhaakash.dev>");
MODULE_DESCRIPTION("MSI Embedded Controller");
MODULE_VERSION("0.08");

module_init(msi_ec_init);
module_exit(msi_ec_exit);
