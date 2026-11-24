// SPDX-License-Identifier: GPL-2.0+
/*
 * FB driver for the GC9107 LCD display controller
 *
 * Reconstructed from the vendor-compiled kernel image (5.4.61, mk10-alpine,
 * compatible = "syk,gc9107"). Init sequence, window offsets and MADCTL bits
 * were recovered by disassembling init_display()/set_addr_win()/set_var() at
 * c06335a8 / c063391c / c06339f8 and reading the fbtft_display descriptor.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <video/mipi_display.h>

#include "fbtft.h"

#define DRVNAME		"fb_gc9107"
#define WIDTH		128
#define HEIGHT		128
#define TXBUFLEN	(16 * 1024)
#define DEFAULT_GAMMA	"1F 1A 18 0A 0F 06 45 87 32 0A 07 02 07 05 00\n" \
			"00 25 27 05 10 09 3A 78 4D 05 18 0D 38 3A 1F"

static int init_display(struct fbtft_par *par)
{
	par->fbtftops.reset(par);

	write_reg(par, 0xB1, 0x05, 0x3C, 0x3C);
	write_reg(par, 0xB2, 0x05, 0x3C, 0x3C);
	write_reg(par, 0xB3, 0x05, 0x3C, 0x3C, 0x05, 0x3C, 0x3C);
	write_reg(par, 0xB4, 0x03);
	write_reg(par, 0xC0, 0xAB, 0x0B, 0x04);
	write_reg(par, 0xC1, 0xC5);
	write_reg(par, 0xC2, 0x0D, 0x00);
	write_reg(par, 0xC3, 0x8D, 0x6A);
	write_reg(par, 0xC4, 0x8D, 0xEE);
	write_reg(par, 0xC5, 0x0F);

	/* positive gamma */
	write_reg(par, 0xE0, 0x07, 0x0E, 0x08, 0x07, 0x10, 0x07, 0x02, 0x07,
		  0x09, 0x0F, 0x25, 0x36, 0x00, 0x08, 0x04, 0x10);
	/* negative gamma */
	write_reg(par, 0xE1, 0x0A, 0x0D, 0x08, 0x07, 0x0F, 0x07, 0x02, 0x07,
		  0x09, 0x0F, 0x25, 0x35, 0x00, 0x09, 0x04, 0x10);

	write_reg(par, 0xFC, 0x80);		/* idle mode / power save */
	write_reg(par, MIPI_DCS_SET_PIXEL_FORMAT, 0x05); /* 16 bpp */
	write_reg(par, MIPI_DCS_SET_ADDRESS_MODE, 0xC8);
	write_reg(par, MIPI_DCS_ENTER_INVERT_MODE);
	write_reg(par, MIPI_DCS_SET_DISPLAY_ON);

	write_reg(par, MIPI_DCS_SET_COLUMN_ADDRESS, 0x00, 0x1A, 0x00, 0x69);
	write_reg(par, MIPI_DCS_SET_PAGE_ADDRESS, 0x00, 0x01, 0x00, 0xA0);
	write_reg(par, MIPI_DCS_WRITE_MEMORY_START);
	write_reg(par, MIPI_DCS_EXIT_SLEEP_MODE);

	/* binary: 20 x __const_udelay(1000 * UDELAY_MULT), i.e. mdelay(20) */
	mdelay(20);

	write_reg(par, MIPI_DCS_SET_DISPLAY_ON);

	return 0;
}

static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye)
{
	xs += 2;
	xe += 2;
	ys += 3;
	ye += 3;

	write_reg(par, MIPI_DCS_SET_COLUMN_ADDRESS,
		  (xs >> 8) & 0xFF, xs & 0xFF, (xe >> 8) & 0xFF, xe & 0xFF);
	write_reg(par, MIPI_DCS_SET_PAGE_ADDRESS,
		  (ys >> 8) & 0xFF, ys & 0xFF, (ye >> 8) & 0xFF, ye & 0xFF);
	write_reg(par, MIPI_DCS_WRITE_MEMORY_START);
}

#define MEM_Y   BIT(7) /* MY row address order */
#define MEM_X   BIT(6) /* MX column address order */
#define MEM_V   BIT(5) /* MV row / column exchange */
#define MEM_L   BIT(4) /* ML vertical refresh order */
#define MEM_BGR (3)    /* RGB-BGR Order */

static int set_var(struct fbtft_par *par)
{
	switch (par->info->var.rotate) {
	case 0:
		break;
	case 90:
		write_reg(par, MIPI_DCS_SET_ADDRESS_MODE,
			  MEM_Y | MEM_X | MEM_V | (par->bgr << MEM_BGR));
		break;
	case 180:
		write_reg(par, MIPI_DCS_SET_ADDRESS_MODE,
			  MEM_Y | (par->bgr << MEM_BGR));
		break;
	case 270:
		write_reg(par, MIPI_DCS_SET_ADDRESS_MODE,
			  MEM_V | MEM_L | (par->bgr << MEM_BGR));
		break;
	}

	return 0;
}

static int set_gamma(struct fbtft_par *par, u32 *curves)
{
	return 0;
}

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
		.set_addr_win = set_addr_win,
		.set_var = set_var,
		.set_gamma = set_gamma,
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "syk,gc9107", &display);

MODULE_ALIAS("spi:" DRVNAME);
MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("spi:gc9107");
MODULE_ALIAS("platform:gc9107");

MODULE_DESCRIPTION("FB driver for the gc9107 LCD display controller");
MODULE_AUTHOR("SYK");
MODULE_LICENSE("GPL");
