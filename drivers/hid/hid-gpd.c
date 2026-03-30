// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID report descriptor fixup for GPD Win handhelds.
 *
 *  The OEM HID interface (VID 2f24 / GameSir, PID 0137) declares Report ID 1
 *  with Report Count 63 (8-bit fields) for both Input and Feature, but the
 *  firmware only sends 11 bytes of payload after the report ID.
 */

#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

#define RDESC_LEN		38
#define RPT_COUNT_INPUT_OFF	21
#define RPT_COUNT_FEATURE_OFF	34

static const __u8 *gpd_report_fixup(struct hid_device *hdev, __u8 *rdesc,
				    unsigned int *rsize)
{
	if (*rsize != RDESC_LEN)
		return rdesc;

	if (rdesc[RPT_COUNT_INPUT_OFF - 1] == 0x95 &&
	    rdesc[RPT_COUNT_INPUT_OFF] == 0x3f &&
	    rdesc[RPT_COUNT_FEATURE_OFF - 1] == 0x95 &&
	    rdesc[RPT_COUNT_FEATURE_OFF] == 0x3f) {
		hid_info(hdev, "fixing report counts (63 -> 11 bytes)\n");
		rdesc[RPT_COUNT_INPUT_OFF] = 11;
		rdesc[RPT_COUNT_FEATURE_OFF] = 11;
	}

	return rdesc;
}

static const struct hid_device_id gpd_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_GAMESIR, USB_DEVICE_ID_GAMESIR_0137) },
	{ }
};
MODULE_DEVICE_TABLE(hid, gpd_devices);

static struct hid_driver gpd_driver = {
	.name = "gpd",
	.id_table = gpd_devices,
	.report_fixup = gpd_report_fixup,
};

module_hid_driver(gpd_driver);

MODULE_DESCRIPTION("HID report descriptor fix for GPD Win handheld (GameSir 2f24:0137)");
MODULE_LICENSE("GPL");
