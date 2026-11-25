// SPDX-License-Identifier: GPL-2.0
/*
 * MK10: /dev/atsha204a stand-in for the vendor KeyboardDevice app.
 *
 * At startup the app opens /dev/atsha204a O_RDWR and calls
 * ioctl(fd, 0x50, buf), where buf = { slot 11, a 32-byte constant from the app }.
 * It uses only the return value (KeyboardDevice 0xc7214, caller 0x31b38):
 * 0 continues startup, anything else shows "Check HardWare Error" and stops.
 * The vendor's ATSHA204A driver is not in this tree; the mainline
 * atmel-sha204a (hwrng) owns the chip and creates no char device.
 * This answers the check with 0 without talking to the chip.
 */
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>

#define MK10_ATSHA_CHECK	0x50

static long mk10_atsha_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return cmd == MK10_ATSHA_CHECK ? 0 : -ENOTTY;
}

static const struct file_operations mk10_atsha_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= mk10_atsha_ioctl,
	.llseek		= noop_llseek,
};

static struct miscdevice mk10_atsha_dev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "atsha204a",
	.fops	= &mk10_atsha_fops,
};
module_misc_device(mk10_atsha_dev);

MODULE_DESCRIPTION("MK10 /dev/atsha204a stand-in for the vendor app's hardware check");
MODULE_LICENSE("GPL");
