/*
 * driver/s3c6410-fb.c
 *
 * s3c6410 framebuffer drivers.
 *
 * Copyright (c) 2007-2009  jianjun jiang <jerryjianjun@gmail.com>
 * website: http://xboot.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <configs.h>
#include <default.h>
#include <types.h>
#include <string.h>
#include <malloc.h>
#include <div64.h>
#include <io.h>
#include <time/delay.h>
#include <xboot/log.h>
#include <xboot/ioctl.h>
#include <xboot/clk.h>
#include <xboot/printk.h>
#include <xboot/initcall.h>
#include <xboot/resource.h>
#include <s3c6410/reg-gpio.h>
#include <s3c6410/reg-lcd.h>
#include <fb/fb.h>


/* the lcd module - lw500 */
#define	LCD_WIDTH		(800)
#define	LCD_HEIGHT		(480)
#define	LCD_BPP			(16)
#define	LCD_FORMAT		(FORMAT_RGB_565)
#define VBPD			(33)
#define VFPD			(10)
#define VSPW			(2)
#define HBPD			(90)
#define HFPD			(75)
#define HSPW			(128)
#define FREQ			(50)
#define PIXEL_CLOCK		(FREQ * (HFPD + HSPW + HBPD + LCD_WIDTH) * (VFPD + VSPW + VBPD + LCD_HEIGHT))

#define	REGS_VIDCON0	( S3C6410_VIDCON0_PROGRESSIVE | S3C6410_VIDCON0_VIDOUT_RGBIF | S3C6410_VIDCON0_SUB_16_MODE | S3C6410_VIDCON0_MAIN_16_MODE | \
						S3C6410_VIDCON0_PNRMODE_RGB_P | S3C6410_VIDCON0_CLKVALUP_ALWAYS | S3C6410_VIDCON0_VCLKFREE_NORMAL | \
						S3C6410_VIDCON0_CLKDIR_DIVIDED | S3C6410_VIDCON0_CLKSEL_F_HCLK | S3C6410_VIDCON0_ENVID_DISABLE | S3C6410_VIDCON0_ENVID_F_DISABLE )
#define	REGS_VIDCON1	( S3C6410_VIDCON1_IHSYNC_INVERT | S3C6410_VIDCON1_IVSYNC_INVERT  | S3C6410_VIDCON1_IVDEN_NORMAL | S3C6410_VIDCON1_IVCLK_RISE_EDGE )
#define	REGS_VIDTCON0	( S3C6410_VIDTCON0_VBPDE(VBPD-1) | S3C6410_VIDTCON0_VBPD(VBPD-1) | S3C6410_VIDTCON0_VFPD(VFPD-1) | S3C6410_VIDTCON0_VSPW(VSPW-1) )
#define	REGS_VIDTCON1	( S3C6410_VIDTCON1_VFPDE(VFPD-1) | S3C6410_VIDTCON1_HBPD(HBPD-1) | S3C6410_VIDTCON1_HFPD(HFPD-1) | S3C6410_VIDTCON1_HSPW(HSPW-1) )
#define	REGS_VIDTCON2	( S3C6410_VIDTCON2_LINEVAL(LCD_HEIGHT-1) | S3C6410_VIDTCON2_HOZVAL(LCD_WIDTH-1) )
#define	REGS_DITHMODE	( (S3C6410_DITHMODE_RDITHPOS_5BIT | S3C6410_DITHMODE_GDITHPOS_6BIT | S3C6410_DITHMODE_BDITHPOS_5BIT ) & (~S3C6410_DITHMODE_DITHERING_ENABLE) )

/*
 * video ram buffer for lcd.
 */
static x_u8 vram[LCD_WIDTH*LCD_HEIGHT*LCD_BPP/8 + 4];
static x_u16 * pvram;

static struct fb_info info = {
	.name		= "fb0",
	.width		= LCD_WIDTH,
	.height		= LCD_HEIGHT,
	.format		= LCD_FORMAT,
	.bpp		= LCD_BPP,
	.stride		= LCD_WIDTH*LCD_BPP/8,
	.pos		= 0,
	.base		= 0,
};

static void fb_init(void)
{
	x_u64 hclk;

	/* setting vram base address */
	info.base = (void *)( (x_u32)(((x_u32)(&vram) + 4 - 1) & ~(4 - 1)) );
	pvram = (x_u16 *)((x_u32)info.base);

	/* set GPF15(backlight pin) output and pull up and low level */
	writel(S3C6410_GPFCON, (readl(S3C6410_GPFCON) & ~(0x3<<30)) | (0x1<<30));
	writel(S3C6410_GPFPUD, (readl(S3C6410_GPFPUD) & ~(0x3<<30)) | (0x2<<30));
	writel(S3C6410_GPFDAT, (readl(S3C6410_GPFDAT) & ~(0x1<<15)) | (0x0<<15));

	/* gpm5 low level for enable lcd power */
	writel(S3C6410_GPMCON, (readl(S3C6410_GPMCON) & ~(0xf<<20)) | (0x1<<20));
	writel(S3C6410_GPMPUD, (readl(S3C6410_GPMPUD) & ~(0x3<<10)) | (0x2<<10));
	writel(S3C6410_GPMDAT, (readl(S3C6410_GPMDAT) & ~(0x1<<5)) | (0x0<<5));

	/* gpb4 high level for enable display */
	writel(S3C6410_GPBCON, (readl(S3C6410_GPBCON) & ~(0xf<<16)) | (0x1<<16));
	writel(S3C6410_GPBPUD, (readl(S3C6410_GPBPUD) & ~(0x3<<8)) | (0x2<<8));
	writel(S3C6410_GPBDAT, (readl(S3C6410_GPBDAT) & ~(0x1<<4)) | (0x1<<4));

	/* must be '0' for normal-path instead of by-pass */
	writel(S3C6410_MIFPCON, 0);

	/* select tft lcd type (rgb i/f) */
	writel(S3C6410_SPCON, (readl(S3C6410_SPCON) & ~(0x3<<0)) | (0x1<<0));

	/* lcd port config */
	writel(S3C6410_GPICON, 0xaaaaaaaa);
	writel(S3C6410_GPJCON, 0xaaaaaaaa);

	/* initial lcd controler */
	writel(S3C6410_VIDCON1, REGS_VIDCON1);
	writel(S3C6410_VIDTCON0, REGS_VIDTCON0);
	writel(S3C6410_VIDTCON1, REGS_VIDTCON1);
	writel(S3C6410_VIDTCON2, REGS_VIDTCON2);
	writel(S3C6410_DITHMODE, REGS_DITHMODE);

	/* get hclk for lcd */
	clk_get_rate("hclk", &hclk);
	writel(S3C6410_VIDCON0, (REGS_VIDCON0 | S3C6410_VIDCON0_CLKVAL_F((x_u32)(div64(hclk, PIXEL_CLOCK) - 1)) ) );

	/* turn all windows off */
	writel(S3C6410_WINCON0, (readl(S3C6410_WINCON0) & ~0x1));
	writel(S3C6410_WINCON1, (readl(S3C6410_WINCON1) & ~0x1));
	writel(S3C6410_WINCON2, (readl(S3C6410_WINCON2) & ~0x1));
	writel(S3C6410_WINCON3, (readl(S3C6410_WINCON3) & ~0x1));
	writel(S3C6410_WINCON4, (readl(S3C6410_WINCON4) & ~0x1));

	/* turn all windows color map off */
	writel(S3C6410_WIN0MAP, (readl(S3C6410_WIN0MAP) & ~(1<<24)));
	writel(S3C6410_WIN1MAP, (readl(S3C6410_WIN1MAP) & ~(1<<24)));
	writel(S3C6410_WIN2MAP, (readl(S3C6410_WIN2MAP) & ~(1<<24)));
	writel(S3C6410_WIN3MAP, (readl(S3C6410_WIN3MAP) & ~(1<<24)));
	writel(S3C6410_WIN4MAP, (readl(S3C6410_WIN4MAP) & ~(1<<24)));

	/* turn all windows color key off */
	writel(S3C6410_W1KEYCON0, (readl(S3C6410_W1KEYCON0) & ~(3<<25)));
	writel(S3C6410_W2KEYCON0, (readl(S3C6410_W2KEYCON0) & ~(3<<25)));
	writel(S3C6410_W3KEYCON0, (readl(S3C6410_W3KEYCON0) & ~(3<<25)));
	writel(S3C6410_W4KEYCON0, (readl(S3C6410_W4KEYCON0) & ~(3<<25)));

	/* config window 0 */
	writel(S3C6410_WINCON0, (readl(S3C6410_WINCON0) & ~(0x1<<22 | 0x1<<16 | 0x3<<9 | 0xf<<2 | 0x1<<0)) | (0x5<<2 | 0x1<<16));

	/* window 0 frambuffer addresss */
	writel(S3C6410_VIDW00ADD0B0, (x_u32)info.base);
	writel(S3C6410_VIDW00ADD0B1, (x_u32)info.base);
	writel(S3C6410_VIDW00ADD1B0, ((x_u32)info.base + LCD_WIDTH*LCD_HEIGHT*LCD_BPP/8)& 0x00ffffff);
	writel(S3C6410_VIDW00ADD1B1, ((x_u32)info.base + LCD_WIDTH*LCD_HEIGHT*LCD_BPP/8)& 0x00ffffff);
	writel(S3C6410_VIDW00ADD2, (LCD_WIDTH*LCD_BPP/8) & 0x00001fff);

	/* config view port */
	writel(S3C6410_VIDOSD0A, OSD_LTX(0) | OSD_LTY(0));
	writel(S3C6410_VIDOSD0B, OSD_RBX(LCD_WIDTH) | OSD_RBY(LCD_HEIGHT));
	writel(S3C6410_VIDOSD0C, OSDSIZE(LCD_WIDTH * LCD_HEIGHT));

	/* enable window 0 */
	writel(S3C6410_WINCON0, (readl(S3C6410_WINCON0) | 0x1));

	/* enable video controller output */
	writel(S3C6410_VIDCON0, (readl(S3C6410_VIDCON0) | 0x3));

	/* delay for avoid flash screen */
	mdelay(50);
}

static void fb_exit(void)
{
	/* disable video output */
	writel(S3C6410_VIDCON0, (readl(S3C6410_VIDCON0) & (~0x3)));
}

static void fb_bl(x_u8 brightness)
{
	if(brightness)
		writel(S3C6410_GPFDAT, (readl(S3C6410_GPFDAT) & ~(0x1<<15)) | (0x1<<15));
	else
		writel(S3C6410_GPFDAT, (readl(S3C6410_GPFDAT) & ~(0x1<<15)) | (0x0<<15));
}

static void fb_set_pixel(x_u32 x, x_u32 y, x_u32 c)
{
	*(pvram + LCD_WIDTH*y + x) = c;
}

static x_u32 fb_get_pixel(x_u32 x, x_u32 y)
{
	return *(pvram + LCD_WIDTH*y + x);
}

static void fb_hline(x_u32 x0, x_u32 y0, x_u32 x, x_u32 c)
{
	x_u16 * p = (x_u16 *)(pvram + LCD_WIDTH*y0 + x0);

	while(x--)
		*(p++) = c;
}

static void fb_vline(x_u32 x0, x_u32 y0, x_u32 y, x_u32 c)
{
	x_u16 * p = (x_u16 *)(pvram + LCD_WIDTH*y0 + x0);

	while(y--)
	{
		*p = c;
		p += LCD_WIDTH;
	}
}

static x_s32 fb_ioctl(x_u32 cmd, void * arg)
{
	return -1;
}

static struct fb s3c6410_fb = {
	.info		= &info,
	.init		= fb_init,
	.exit		= fb_exit,
	.bl			= fb_bl,
	.set_pixel	= fb_set_pixel,
	.get_pixel	= fb_get_pixel,
	.hline		= fb_hline,
	.vline		= fb_vline,
	.ioctl		= fb_ioctl,
};

static __init void s3c6410_fb_init(void)
{
	if(!clk_get_rate("hclk", 0))
	{
		LOG_E("can't get the clock of \'hclk\'");
		return;
	}

	if(!register_framebuffer(&s3c6410_fb))
		LOG_E("failed to register framebuffer driver '%s'", s3c6410_fb.info->name);
}

static __exit void s3c6410_fb_exit(void)
{
	if(!unregister_framebuffer(&s3c6410_fb))
		LOG_E("failed to unregister framebuffer driver '%s'", s3c6410_fb.info->name);
}

module_init(s3c6410_fb_init, LEVEL_DRIVER);
module_exit(s3c6410_fb_exit, LEVEL_DRIVER);
