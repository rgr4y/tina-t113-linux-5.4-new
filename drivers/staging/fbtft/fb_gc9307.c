// SPDX-License-Identifier: GPL-2.0+
/*
 * FB driver for the GC9307 LCD display controller
 *
 * Reconstructed from the vendor-compiled kernel image (5.4.61, mk10-alpine,
 * compatible = "syk,gc9307"). Init sequence, window layout and the gamma
 * routine were recovered by disassembling init_display()/set_addr_win()/
 * set_var()/set_gamma() at c0633a6c / c0633f78 / c0634004 / c063400c and
 * reading the fbtft_display descriptor at c0d570b8.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <video/mipi_display.h>

#include "fbtft.h"

#define DRVNAME		"fb_gc9307"
#define WIDTH		240
#define HEIGHT		296
#define TXBUFLEN	(16 * 1024)
#define DEFAULT_GAMMA	"1F 1A 18 0A 0F 06 45 87 32 0A 07 02 07 05 00\n" \
			"00 25 27 05 10 09 3A 78 4D 05 18 0D 38 3A 1F"

/*
 * Optional boot logo. A raw little-endian RGB565 image, exactly one
 * framebuffer (240x296x2 = 142080 bytes), is copied into the framebuffer
 * before the first update. With no logo the first full update is skipped so
 * whatever the bootloader left in the panel GRAM stays on screen.
 */
static char *logo = "";
module_param(logo, charp, 0444);
MODULE_PARM_DESC(logo, "path to a raw RGB565 240x296 image shown at load");

static bool logo_first_update_done;

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye);

/*
 * Copy the boot logo, if one was given, into the framebuffer and mark it as
 * the first update. Runs with the init sequence already applied, so the panel
 * and its window are ready to take pixels.
 */
static void show_logo(struct fbtft_par *par)
{
	void *buf;
	loff_t size;
	size_t want = par->info->fix.smem_len;
	int ret;

	if (!logo || !*logo)
		return;

	ret = kernel_read_file_from_path(logo, &buf, &size, want,
					 READING_UNKNOWN);
	if (ret) {
		dev_warn(par->info->device,
			 "cannot read logo \"%s\": %d, keeping bootloader image\n",
			 logo, ret);
		return;
	}

	if (size != want) {
		dev_warn(par->info->device,
			 "logo \"%s\" is %lld bytes, want %zu; keeping bootloader image\n",
			 logo, size, want);
		goto out;
	}

	memcpy(par->info->screen_buffer, buf, want);
	logo_first_update_done = true;

out:
	vfree(buf);
}

static int init_display(struct fbtft_par *par)
{
	par->fbtftops.reset(par);

	write_reg(par, MIPI_DCS_SOFT_RESET);
	mdelay(10);

	write_reg(par, 0xDF, 0x98, 0x53);
	write_reg(par, 0xDE, 0x00);
	write_reg(par, 0xB2, 0x25);
	write_reg(par, 0xB7, 0x00, 0x29, 0x00, 0x51);
	write_reg(par, 0xBB, 0x4F, 0x1A, 0x55, 0x73, 0x63, 0xF0);
	write_reg(par, 0xC0, 0x44, 0xA4);
	write_reg(par, 0xC1, 0x12);
	write_reg(par, 0xC3, 0x7D, 0x07, 0x14, 0x06, 0xC8, 0x71, 0x6C, 0x77);
	write_reg(par, 0xC4, 0x00, 0x00, 0x94, 0x79, 0x25, 0x0A, 0x16, 0x79,
		  0x25, 0x0A, 0x16, 0x82);
	write_reg(par, 0xC8, 0x3F, 0x32, 0x2A, 0x22, 0x26, 0x25, 0x1E, 0x1E,
		  0x1C, 0x1C, 0x19, 0x0E, 0x09, 0x02, 0x06, 0x00,
		  0x3F, 0x32, 0x2A, 0x22, 0x26, 0x25, 0x1E, 0x1E,
		  0x1C, 0x1C, 0x19, 0x0E, 0x09, 0x02, 0x06, 0x00);
	write_reg(par, 0xD0, 0x04, 0x06, 0x6B, 0x0F, 0x00);
	write_reg(par, 0xD7, 0x00, 0x30);
	write_reg(par, 0xE6, 0x10);
	write_reg(par, 0xDE, 0x01);
	write_reg(par, 0xB7, 0x03, 0x30, 0x6B, 0x35, 0x35);
	write_reg(par, 0xC1, 0x14, 0x30, 0x6B);
	write_reg(par, 0xC2, 0x06, 0x3A);
	write_reg(par, 0xC4, 0x72, 0x12);
	write_reg(par, 0xBE, 0x00);
	write_reg(par, 0xDE, 0x00);
	write_reg(par, 0x35, 0x00);
	write_reg(par, MIPI_DCS_SET_ADDRESS_MODE, 0x00);
	write_reg(par, MIPI_DCS_SET_PIXEL_FORMAT, 0x05);	/* 16 bpp */

	write_reg(par, MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x00, 0x00, 0xEF);
	write_reg(par, MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x00, 0x01, 0x27);
	mdelay(120);

	write_reg(par, MIPI_DCS_EXIT_SLEEP_MODE);
	mdelay(100);

	write_reg(par, MIPI_DCS_SET_DISPLAY_ON);
	mdelay(20);

	show_logo(par);

	return 0;
}

static void update_display(struct fbtft_par *par, unsigned int start_line,
			   unsigned int end_line)
{
	size_t offset = start_line * par->info->fix.line_length;
	size_t len = (end_line - start_line + 1) * par->info->fix.line_length;

	/*
	 * The core calls us once for the whole screen at registration. Without a
	 * logo, leave the GRAM alone so the bootloader's image survives.
	 */
	if (!logo_first_update_done && start_line == 0 &&
	    end_line == par->info->var.yres - 1)
		return;

	set_addr_win(par, 0, start_line, par->info->var.xres - 1, end_line);
	par->fbtftops.write_vmem(par, offset, len);
}

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	write_reg(par, MIPI_DCS_SET_COLUMN_ADDRESS,
		  (xs >> 8) & 0xFF, xs & 0xFF, (xe >> 8) & 0xFF, xe & 0xFF);
	write_reg(par, MIPI_DCS_SET_PAGE_ADDRESS,
		  (ys >> 8) & 0xFF, ys & 0xFF, (ye >> 8) & 0xFF, ye & 0xFF);
	write_reg(par, MIPI_DCS_WRITE_MEMORY_START);
}

static int set_var(struct fbtft_par *par)
{
	return 0;
}

#define CURVE(num, idx)  curves[(num) * par->gamma.num_values + (idx)]
static int set_gamma(struct fbtft_par *par, u32 *curves)
{
	int i;

	for (i = 0; i < par->gamma.num_curves; i++)
		write_reg(par, 0xE0 + i,
			  CURVE(i, 0), CURVE(i, 1), CURVE(i, 2),
			  CURVE(i, 3), CURVE(i, 4), CURVE(i, 5),
			  CURVE(i, 6), CURVE(i, 7), CURVE(i, 8),
			  CURVE(i, 9), CURVE(i, 10), CURVE(i, 11),
			  CURVE(i, 12), CURVE(i, 13), CURVE(i, 14));

	return 0;
}
#undef CURVE

static struct fbtft_display display = {
	.regwidth = 8,
	.width = WIDTH,
	.height = HEIGHT,
	.txbuflen = TXBUFLEN,
	.gamma_num = 2,
	.gamma_len = 15,
	.gamma = DEFAULT_GAMMA,
	.fbtftops = {
		.init_display = init_display,
		.update_display = update_display,
		.set_addr_win = set_addr_win,
		.set_var = set_var,
		.set_gamma = set_gamma,
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "syk,gc9307", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:gc9307");
MODULE_ALIAS("platform:gc9307");

MODULE_DESCRIPTION("FB driver for the gc9307 LCD display controller");
MODULE_AUTHOR("SYK");
MODULE_LICENSE("GPL");
