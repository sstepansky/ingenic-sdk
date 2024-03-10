/*
 * ov9281.c
 *
 * Copyright (C) 2012 Ingenic Semiconductor Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * Settings:
 * sboot        resolution      fps       interface              mode
 *   0          1280*800         120       mipi_2lane           linear
 *   1          640*400          210       mipi_2lane           linear
 */
#define DEBUG

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/proc_fs.h>
#include <soc/gpio.h>

#include <tx-isp-common.h>
#include <sensor-common.h>
#include <txx-funcs.h>

#define OV9281_CHIP_ID_H	(0x92)
#define OV9281_CHIP_ID_L	(0x81)

#define OV9281_REG_END		0xffff
#define OV9281_REG_DELAY	0xfffe

#define OV9281_SUPPORT_SCLK  (0x2d8*0x38e*120)
#define SENSOR_OUTPUT_MAX_FPS 210
#define SENSOR_OUTPUT_MIN_FPS 5
#define DRIVE_CAPABILITY_1
#define SENSOR_VERSION	"H20231115a"

static int reset_gpio = GPIO_PC(28);
static int pwdn_gpio = -1;
//static int data_interface = TX_SENSOR_DATA_INTERFACE_MIPI;

struct tx_isp_sensor_attribute ov9281_attr;

struct regval_list {
	uint16_t reg_num;
	unsigned char value;
};

/*
 * the part of driver maybe modify about different sensor and different board.
 */
struct again_lut {
	unsigned int value;
	unsigned int gain;
};

struct again_lut ov9281_again_lut[] = {
	{0x10, 0},
	{0x11, 5731},
	{0x12, 11136},
	{0x13, 16248},
	{0x14, 21097},
	{0x15, 25710},
	{0x16, 30109},
	{0x17, 34312},
	{0x18, 38336},
	{0x19, 42195},
	{0x1a, 45904},
	{0x1b, 49472},
	{0x1c, 52910},
	{0x1d, 56228},
	{0x1e, 59433},
	{0x1f, 62534},
	{0x20, 65536},
	{0x22, 71267},
	{0x24, 76672},
	{0x26, 81784},
	{0x28, 86633},
	{0x2a, 91246},
	{0x2c, 95645},
	{0x2e, 99848},
	{0x30, 103872},
	{0x32, 107731},
	{0x34, 111440},
	{0x36, 115008},
	{0x38, 118446},
	{0x3a, 121764},
	{0x3c, 124969},
	{0x3e, 128070},
	{0x40, 131072},
	{0x44, 136803},
	{0x48, 142208},
	{0x4c, 147320},
	{0x50, 152169},
	{0x54, 156782},
	{0x58, 161181},
	{0x5c, 165384},
	{0x60, 169408},
	{0x64, 173267},
	{0x68, 176976},
	{0x6c, 180544},
	{0x70, 183982},
	{0x74, 187300},
	{0x78, 190505},
	{0x7c, 193606},
	{0x80, 196608},
	{0x88, 202339},
	{0x90, 207744},
	{0x98, 212856},
	{0xa0, 217705},
	{0xa8, 222318},
	{0xb0, 226717},
	{0xb8, 230920},
	{0xc0, 234944},
	{0xc8, 238803},
	{0xd0, 242512},
	{0xd8, 246080},
	{0xe0, 249518},
	{0xe8, 252836},
	{0xf0, 256041},
	{0xf8, 259142},
};

struct tx_isp_sensor_attribute ov9281_attr;

unsigned int ov9281_alloc_again(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_again)
{
	struct again_lut *lut = ov9281_again_lut;

	while(lut->gain <= ov9281_attr.max_again) {
		if(isp_gain == 0) {
			*sensor_again = lut->value;
			return 0;
		}
		else if(isp_gain < lut->gain) {
			*sensor_again = (lut - 1)->value;
			return (lut - 1)->gain;
		}
		else{
			if((lut->gain == ov9281_attr.max_again) && (isp_gain >= lut->gain)) {
				*sensor_again = lut->value;
				return lut->gain;
			}
		}

		lut++;
	}

	return isp_gain;
}

unsigned int ov9281_alloc_dgain(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_dgain)
{
	return 0;
}

struct tx_isp_sensor_attribute ov9281_attr={
	.name = "ov9281",
	.chip_id = 0x9732,
	.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
	.cbus_mask = TISP_SBUS_MASK_SAMPLE_8BITS | TISP_SBUS_MASK_ADDR_16BITS,
	.cbus_device = 0x60,
	.dbus_type = TX_SENSOR_DATA_INTERFACE_MIPI,
	.mipi = {
		.mode = SENSOR_MIPI_OTHER_MODE,
		.clk = 800,
		.lans = 2,
		.settle_time_apative_en = 0,
		.mipi_sc.sensor_csi_fmt = TX_SENSOR_RAW10,
		.mipi_sc.hcrop_diff_en = 0,
		.mipi_sc.mipi_vcomp_en = 0,
		.mipi_sc.mipi_hcomp_en = 0,
		.image_twidth = 1280,
		.image_theight = 800,
		.mipi_sc.mipi_crop_start0x = 0,
		.mipi_sc.mipi_crop_start0y = 0,
		.mipi_sc.mipi_crop_start1x = 0,
		.mipi_sc.mipi_crop_start1y = 0,
		.mipi_sc.mipi_crop_start2x = 0,
		.mipi_sc.mipi_crop_start2y = 0,
		.mipi_sc.mipi_crop_start3x = 0,
		.mipi_sc.mipi_crop_start3y = 0,
		.mipi_sc.line_sync_mode = 0,
		.mipi_sc.work_start_flag = 0,
		.mipi_sc.data_type_en = 0,
		.mipi_sc.data_type_value = RAW10,
		.mipi_sc.del_start = 0,
		.mipi_sc.sensor_frame_mode = TX_SENSOR_DEFAULT_FRAME_MODE,
		.mipi_sc.sensor_fid_mode = 0,
		.mipi_sc.sensor_mode = TX_SENSOR_DEFAULT_MODE,
	},
	.max_again = 259142,
	.max_dgain = 0,
	.min_integration_time = 2,
	.min_integration_time_native = 2,
	.max_integration_time_native = 885,
	.integration_time_limit = 885,
	.total_width = 0x2d8,
	.total_height = 0x38e,
	.max_integration_time = 885,
	.one_line_expr_in_us = 41,
	.integration_time_apply_delay = 2,
	.again_apply_delay = 2,
	.dgain_apply_delay = 0,
	//.sensor_fsync_mode = TX_SENSOR_FSYNC_MSLAVE_MODE,
	.sensor_ctrl.alloc_again = ov9281_alloc_again,
	.sensor_ctrl.alloc_dgain = ov9281_alloc_dgain,
	//	void priv; /* point to struct tx_isp_sensor_board_info */
};


struct tx_isp_mipi_bus ov9281_mipi_640={
	.mode = SENSOR_MIPI_OTHER_MODE,
	.clk = 800,
	.lans = 2,
	.settle_time_apative_en = 0,
	.mipi_sc.sensor_csi_fmt = TX_SENSOR_RAW10,//RAW10
	.mipi_sc.hcrop_diff_en = 0,
	.mipi_sc.mipi_vcomp_en = 0,
	.mipi_sc.mipi_hcomp_en = 0,
	.mipi_sc.line_sync_mode = 0,
	.mipi_sc.work_start_flag = 0,
	.image_twidth = 640,
	.image_theight = 400,
	.mipi_sc.mipi_crop_start0x = 0,
	.mipi_sc.mipi_crop_start0y = 0,
	.mipi_sc.mipi_crop_start1x = 0,
	.mipi_sc.mipi_crop_start1y = 0,
	.mipi_sc.mipi_crop_start2x = 0,
	.mipi_sc.mipi_crop_start2y = 0,
	.mipi_sc.mipi_crop_start3x = 0,
	.mipi_sc.mipi_crop_start3y = 0,
	.mipi_sc.data_type_en = 0,
	.mipi_sc.data_type_value = RAW10,
	.mipi_sc.del_start = 0,
	.mipi_sc.sensor_frame_mode = TX_SENSOR_DEFAULT_FRAME_MODE,
	.mipi_sc.sensor_fid_mode = 0,
	.mipi_sc.sensor_mode = TX_SENSOR_DEFAULT_MODE,
};


static struct regval_list ov9281_init_regs_1280_800_120fps_mipi[] = {
	/*
	  @@ mipi interface 1280*720 25fps
	*/
	{0x0103, 0x01},
    {0x0302, 0x32},
    {0x030d, 0x50},
    {0x030e, 0x02},
    {0x3001, 0x00},
    {0x3004, 0x00},
    {0x3005, 0x00},
    {0x3006, 0x04},
    {0x3011, 0x0a},
    {0x3013, 0x18},
    {0x301c, 0xf0},
    {0x3022, 0x01},
    {0x3030, 0x10},
    {0x3039, 0x32},
    {0x303a, 0x00},
    {0x3500, 0x00},
    {0x3501, 0x2a},
    {0x3502, 0x90},
    {0x3503, 0x08},
    {0x3505, 0x8c},
    {0x3507, 0x03},
    {0x3508, 0x00},
    {0x3509, 0x10},
    {0x3610, 0x80},
    {0x3611, 0xa0},
    {0x3620, 0x6e},
    {0x3632, 0x56},
    {0x3633, 0x78},
    {0x3662, 0x05},
    {0x3666, 0x00},
    {0x366f, 0x5a},
    {0x3680, 0x84},
    {0x3712, 0x80},
    {0x372d, 0x22},
    {0x3731, 0x80},
    {0x3732, 0x30},
    {0x3778, 0x00},
    {0x377d, 0x22},
    {0x3788, 0x02},
    {0x3789, 0xa4},
    {0x378a, 0x00},
    {0x378b, 0x4a},
    {0x3799, 0x20},
    {0x3800, 0x00},
    {0x3801, 0x00},
    {0x3802, 0x00},
    {0x3803, 0x00},
    {0x3804, 0x05},
    {0x3805, 0x0f},
    {0x3806, 0x03},
    {0x3807, 0x2f},
    {0x3808, 0x05},
    {0x3809, 0x00},
    {0x380a, 0x03},
    {0x380b, 0x20},
    {0x380c, 0x02},
    {0x380d, 0xd8},
    {0x380e, 0x03},
    {0x380f, 0x8e},
    {0x3810, 0x00},
    {0x3811, 0x08},
    {0x3812, 0x00},
    {0x3813, 0x08},
    {0x3814, 0x11},
    {0x3815, 0x11},
    {0x3820, 0x40},
    {0x3821, 0x00},
    {0x382c, 0x05},
    {0x382d, 0xb0},
    {0x389d, 0x00},
    {0x3881, 0x42},
    {0x3882, 0x01},
    {0x3883, 0x00},
    {0x3885, 0x02},
    {0x38a8, 0x02},
    {0x38a9, 0x80},
    {0x38b1, 0x00},
    {0x38b3, 0x02},
    {0x38c4, 0x00},
    {0x38c5, 0xc0},
    {0x38c6, 0x04},
    {0x38c7, 0x80},
    {0x3920, 0xff},
    {0x4003, 0x40},
    {0x4008, 0x04},
    {0x4009, 0x0b},
    {0x400c, 0x00},
    {0x400d, 0x07},
    {0x4010, 0x40},
    {0x4043, 0x40},
    {0x4307, 0x30},
    {0x4317, 0x00},
    {0x4501, 0x00},
    {0x4507, 0x00},
    {0x4509, 0x00},
    {0x450a, 0x08},
    {0x4601, 0x04},
    {0x470f, 0x00},
    {0x4f07, 0x00},
    {0x4800, 0x00},
    {0x5000, 0x9f},
    {0x5001, 0x00},
    {0x5e00, 0x00},
    {0x5d00, 0x07},
    {0x5d01, 0x00},
    {0x4f00, 0x04},
    {0x4f10, 0x00},
    {0x4f11, 0x98},
    {0x4f12, 0x0f},
    {0x4f13, 0xc4},
    {OV9281_REG_DELAY, 50},	/* END MARKER */
    {0x0100, 0x01},


	{OV9281_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list ov9281_init_regs_640_400_210fps_mipi[] = {
    {0x0103 ,0x01},
    {0x0302 ,0x32},
    {0x030d ,0x50},
    {0x030e ,0x02},
    {0x3001 ,0x00},
    {0x3004 ,0x00},
    {0x3005 ,0x00},
    {0x3006 ,0x04},
    {0x3011 ,0x0a},
    {0x3013 ,0x18},
    {0x301c ,0xf0},
    {0x3022 ,0x01},
    {0x3030 ,0x10},
    {0x3039 ,0x32},
    {0x303a ,0x00},
    {0x3500 ,0x00},
    {0x3501 ,0x01},
    {0x3502 ,0xf4},
    {0x3503 ,0x08},
    {0x3505 ,0x8c},
    {0x3507 ,0x03},
    {0x3508 ,0x00},
    {0x3509 ,0x10},
    {0x3610 ,0x80},
    {0x3611 ,0xa0},
    {0x3620 ,0x6e},
    {0x3632 ,0x56},
    {0x3633 ,0x78},
    {0x3662 ,0x05},
    {0x3666 ,0x00},
    {0x366f ,0x5a},
    {0x3680 ,0x84},
    {0x3712 ,0x80},
    {0x372d ,0x22},
    {0x3731 ,0x80},
    {0x3732 ,0x30},
    {0x3778 ,0x10},
    {0x377d ,0x22},
    {0x3788 ,0x02},
    {0x3789 ,0xa4},
    {0x378a ,0x00},
    {0x378b ,0x4a},
    {0x3799 ,0x20},
    {0x3800 ,0x00},
    {0x3801 ,0x00},
    {0x3802 ,0x00},
    {0x3803 ,0x00},
    {0x3804 ,0x05},
    {0x3805 ,0x0f},
    {0x3806 ,0x03},
    {0x3807 ,0x2f},
    {0x3808 ,0x02},
    {0x3809 ,0x80},
    {0x380a ,0x01},
    {0x380b ,0x90},
    {0x380c ,0x02},
    {0x380d ,0xd8},
    {0x380e ,0x02},
    {0x380f ,0x08},
    {0x3810 ,0x00},
    {0x3811 ,0x04},
    {0x3812 ,0x00},
    {0x3813 ,0x04},
    {0x3814 ,0x31},
    {0x3815 ,0x22},
    {0x3820 ,0x60},
    {0x3821 ,0x01},
    {0x382c ,0x05},
    {0x382d ,0xb0},
    {0x389d ,0x00},
    {0x3881 ,0x42},
    {0x3882 ,0x01},
    {0x3883 ,0x00},
    {0x3885 ,0x02},
    {0x38a8 ,0x02},
    {0x38a9 ,0x80},
    {0x38b1 ,0x00},
    {0x38b3 ,0x02},
    {0x38c4 ,0x00},
    {0x38c5 ,0xc0},
    {0x38c6 ,0x04},
    {0x38c7 ,0x80},
    {0x3920 ,0xff},
    {0x4003 ,0x40},
    {0x4008 ,0x02},
    {0x4009 ,0x05},
    {0x400c ,0x00},
    {0x400d ,0x03},
    {0x4010 ,0x40},
    {0x4043 ,0x40},
    {0x4307 ,0x30},
    {0x4317 ,0x00},
    {0x4501 ,0x00},
    {0x4507 ,0x03},
    {0x4509 ,0x80},
    {0x450a ,0x08},
    {0x4601 ,0x04},
    {0x470f ,0x00},
    {0x4f07 ,0x00},
    {0x4800 ,0x00},
    {0x5000 ,0x9f},
    {0x5001 ,0x00},
    {0x5e00 ,0x00},
    {0x5d00 ,0x07},
    {0x5d01 ,0x00},
    {0x4f00 ,0x04},
    {0x4f10 ,0x00},
    {0x4f11 ,0x98},
    {0x4f12 ,0x0f},
    {0x4f13 ,0xc4},
    {0x0100 ,0x01},
    {OV9281_REG_DELAY, 50},	/* END MARKER */
    {0x0100, 0x01},
    {0x3501, 0x20},
    {0x3502, 0x20},
    {OV9281_REG_DELAY, 50},	/* END MARKER */
	{OV9281_REG_END, 0x00},	/* END MARKER */
};
/*
 * the order of the ov9281_win_sizes is [full_resolution, preview_resolution].
 */
static struct tx_isp_sensor_win_setting ov9281_win_sizes[] = {
	/* 1280*720 */
	{
		.width		= 1280,
		.height		= 800,
		.fps		= 120 << 16 | 1,
		.mbus_code	= TISP_VI_FMT_SBGGR10_1X10,
		.colorspace	= TISP_COLORSPACE_SRGB,
		.regs 		= ov9281_init_regs_1280_800_120fps_mipi,
	},
    	{
		.width		= 640,
		.height		= 400,
		.fps		= 210 << 16 | 1,
		.mbus_code	= TISP_VI_FMT_SBGGR10_1X10,
		.colorspace	= TISP_COLORSPACE_SRGB,
		.regs 		= ov9281_init_regs_640_400_210fps_mipi,
	}
};
struct tx_isp_sensor_win_setting *wsize = &ov9281_win_sizes[0];

static struct regval_list ov9281_stream_on_mipi[] = {
	//{0x0100, 0x01},
	{OV9281_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list ov9281_stream_off_mipi[] = {
	{0x0100, 0x00},
	{OV9281_REG_END, 0x00},	/* END MARKER */
};

int ov9281_read(struct tx_isp_subdev *sd, uint16_t reg,
		unsigned char *value)
{
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	uint8_t buf[2] = {(reg>>8)&0xff, reg&0xff};
	int ret;
	struct i2c_msg msg[2] = {
		[0] = {
			.addr	= client->addr,
			.flags	= 0,
			.len	= 2,
			.buf	= buf,
		},
		[1] = {
			.addr	= client->addr,
			.flags	= I2C_M_RD,
			.len	= 1,
			.buf	= value,
		}
	};

	ret = private_i2c_transfer(client->adapter, msg, 2);
	if (ret > 0)
		ret = 0;

	return ret;
}

static int ov9281_write(struct tx_isp_subdev *sd, uint16_t reg, unsigned char value)
{
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	uint8_t buf[3] = {(reg>>8)&0xff, reg&0xff, value};
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= 3,
		.buf	= buf,
	};

	int ret;
	ret = private_i2c_transfer(client->adapter, &msg, 1);
	if (ret > 0)
		ret = 0;

	return ret;
}

#if 0
static int ov9281_read_array(struct tx_isp_subdev *sd, struct regval_list *vals)
{
	int ret;
	unsigned char val;
	while (vals->reg_num != OV9281_REG_END) {
		if (vals->reg_num == OV9281_REG_DELAY) {
			msleep(vals->value);
		} else {
			ret = ov9281_read(sd, vals->reg_num, &val);
			if (ret < 0)
				return ret;
		}
		pr_debug("vals->reg_num:0x%02x, vals->value:0x%02x\n",vals->reg_num, val);
		vals++;
	}
	return 0;
}
#endif

static int ov9281_write_array(struct tx_isp_subdev *sd, struct regval_list *vals)
{
	int ret;
	while (vals->reg_num != OV9281_REG_END) {
		if (vals->reg_num == OV9281_REG_DELAY) {
			msleep(vals->value);
		} else {
			ret = ov9281_write(sd, vals->reg_num, vals->value);
            if (ret < 0)
				return ret;
		}
		/* pr_debug("vals->reg_num:%x, vals->value:%x\n",vals->reg_num, vals->value); */
		vals++;
	}
	return 0;
}

static int ov9281_reset(struct tx_isp_subdev *sd, struct tx_isp_initarg *init)
{
	return 0;
}

static int ov9281_detect(struct tx_isp_subdev *sd, unsigned int *ident)
{
	unsigned char v;
	int ret;
	ret = ov9281_read(sd, 0x300a, &v);
	pr_debug("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != OV9281_CHIP_ID_H)
		return -ENODEV;
	*ident = v;

	ret = ov9281_read(sd, 0x300b, &v);
	pr_debug("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != OV9281_CHIP_ID_L)
		return -ENODEV;
	*ident = (*ident << 8) | v;
    return 0;
}

static int ov9281_set_integration_time(struct tx_isp_subdev *sd, int value)
{

	int ret = 0;
	unsigned int expo = value;
	ret = ov9281_write(sd, 0x3502, (unsigned char)(expo & 0xff)<<4);
	ret += ov9281_write(sd, 0x3501, (unsigned char)((expo >> 4) & 0xff));
	ret += ov9281_write(sd, 0x3500, (unsigned char)((expo >> 12) & 0xf));
	if (ret < 0)
		return ret;
	return 0;
}

static int ov9281_set_analog_gain(struct tx_isp_subdev *sd, int value)
{
	int ret = -1;
	ret = ov9281_write(sd, 0x3509, (unsigned char)(value & 0xff));
    ret = ov9281_write(sd, 0x3508, (unsigned char)((value>>8) & 0xff));
	if (ret < 0)
		return ret;

	return 0;
}

static int ov9281_set_digital_gain(struct tx_isp_subdev *sd, int value)
{
	return 0;
}

static int ov9281_get_black_pedestal(struct tx_isp_subdev *sd, int value)
{
	return 0;
}

static int ov9281_init(struct tx_isp_subdev *sd, struct tx_isp_initarg *init)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	int ret = 0;

	if(!init->enable)
		return ISP_SUCCESS;
	sensor->video.mbus.width = wsize->width;
	sensor->video.mbus.height = wsize->height;
	sensor->video.mbus.code = wsize->mbus_code;
	sensor->video.mbus.field = TISP_FIELD_NONE;
	sensor->video.mbus.colorspace = wsize->colorspace;
	sensor->video.fps = wsize->fps;
	sensor->video.state = TX_ISP_MODULE_DEINIT;
	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	sensor->priv = wsize;

	return 0;
}
static int ov9281_s_stream(struct tx_isp_subdev *sd, struct tx_isp_initarg *init)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	int ret = 0;

	if (init->enable)
	{
		if (sensor->video.state == TX_ISP_MODULE_DEINIT)
		{
			ret = ov9281_write_array(sd, wsize->regs);
			if (ret)
				return ret;
			sensor->video.state = TX_ISP_MODULE_INIT;
		}
		if (sensor->video.state == TX_ISP_MODULE_INIT)
		{
			ret = ov9281_write_array(sd, ov9281_stream_on_mipi);
			sensor->video.state = TX_ISP_MODULE_RUNNING;
			ISP_WARNING("ov9281 stream on\n");
		}
	}
	else
	{
		ret = ov9281_write_array(sd, ov9281_stream_off_mipi);
		sensor->video.state = TX_ISP_MODULE_INIT;
		ISP_WARNING("ov9281 stream off\n");
	}

	return ret;
}
static int ov9281_set_fps(struct tx_isp_subdev *sd, int fps)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	int ret = 0;
	unsigned int sclk = 0;
	unsigned int hts = 0;
	unsigned int vts = 0;
	unsigned char val = 0;
	struct tx_isp_sensor_register_info *info = &sensor->info;
	unsigned int newformat = 0; //the format is 24.8
	/* the format of fps is 16/16. for example 25 << 16 | 2, the value is 25/2 fps. */

    switch(info->default_boot){
        case 0:
	    sclk = OV9281_SUPPORT_SCLK;
	break;
        case 1:
	    sclk = 0x208*0x2d8*210;
	break;
        default:
        ISP_ERROR("warn:nodefault_boot\n");
        return -1;
        }
    newformat = (((fps >> 16) / (fps & 0xffff)) << 8) + ((((fps >> 16) % (fps & 0xffff)) << 8) / (fps & 0xffff));
	if(newformat > (SENSOR_OUTPUT_MAX_FPS << 8) || fps < (SENSOR_OUTPUT_MIN_FPS << 8)) {
		ISP_ERROR("warn: fps(%d) no in range\n", fps);
        return -1;
	}

    val = 0;
	ret += ov9281_read(sd, 0x380c, &val);
	hts = val<<8;
	val = 0;
	ret += ov9281_read(sd, 0x380d, &val);
	hts |= val;
	if (0 != ret) {
		pr_debug("err: ov9281 read err\n");
		return ret;
	}

	vts = sclk * (fps & 0xffff) / hts / ((fps & 0xffff0000) >> 16);
	ret += ov9281_write(sd, 0x380f, vts&0xff);
	ret += ov9281_write(sd, 0x380e, (vts>>8)&0xff);
	if (0 != ret) {
		pr_debug("err: ov9281_write err\n");
		return ret;
	}
	sensor->video.fps = fps;
	sensor->video.attr->max_integration_time_native = vts  - 25 ;
	sensor->video.attr->integration_time_limit = vts  - 25 ;
	sensor->video.attr->total_height = vts;
	sensor->video.attr->max_integration_time = vts  - 25 ;
	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);

	return ret;
}

static int ov9281_set_mode(struct tx_isp_subdev *sd, int value)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	int ret = ISP_SUCCESS;

	if(wsize){
		sensor->video.mbus.width = wsize->width;
		sensor->video.mbus.height = wsize->height;
		sensor->video.mbus.code = wsize->mbus_code;
		sensor->video.mbus.field = TISP_FIELD_NONE;
		sensor->video.mbus.colorspace = wsize->colorspace;
		sensor->video.fps = wsize->fps;
		ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	}

	return ret;
}

static int sensor_attr_check(struct tx_isp_subdev *sd)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	struct tx_isp_sensor_register_info *info = &sensor->info;
	unsigned long rate,ret;
	struct clk *sclka;
    struct i2c_client *client = tx_isp_get_subdevdata(sd);
	//unsigned long rate;
	switch(info->default_boot){
	case 0:
	    wsize = &ov9281_win_sizes[0];
		ov9281_attr.data_type = TX_SENSOR_DATA_TYPE_LINEAR;
		memcpy(&(ov9281_attr.mipi), &ov9281_attr.mipi, sizeof(ov9281_attr.mipi));
		ov9281_attr.mipi.clk = 800;
		ov9281_attr.mipi.lans = 2;
		ov9281_attr.mipi.image_twidth = 1280,
		ov9281_attr.mipi.image_theight = 800,
		ov9281_attr.mipi.mipi_sc.sensor_frame_mode = TX_SENSOR_DEFAULT_FRAME_MODE;
		ov9281_attr.mipi.mipi_sc.sensor_mode = TX_SENSOR_DEFAULT_MODE;
		ov9281_attr.max_integration_time_native = 885;
		ov9281_attr.integration_time_limit = 885;
		ov9281_attr.total_width = 0x2d8; // 5152
		ov9281_attr.total_height = 0x38e;	 // 2542
		ov9281_attr.max_integration_time = 885;
		ov9281_attr.again = 0x80;
		ov9281_attr.integration_time = 0x901;
		break;
    case 1:
	    wsize = &ov9281_win_sizes[1];
		ov9281_attr.data_type = TX_SENSOR_DATA_TYPE_LINEAR;
		memcpy(&(ov9281_attr.mipi), &ov9281_mipi_640, sizeof(ov9281_mipi_640));
		ov9281_attr.mipi.mipi_sc.sensor_frame_mode = TX_SENSOR_DEFAULT_FRAME_MODE;
		ov9281_attr.mipi.mipi_sc.sensor_mode = TX_SENSOR_DEFAULT_MODE;
		ov9281_attr.max_integration_time_native = 495;
		ov9281_attr.integration_time_limit = 495;
		ov9281_attr.total_width = 0x2d8; // 5152
		ov9281_attr.total_height = 0x208;	 // 2542
		ov9281_attr.max_integration_time = 495;
		ov9281_attr.again = 0x80;
		ov9281_attr.integration_time = 0x901;
		break;
	default:
		ISP_ERROR("Have no this setting!!!\n");
	}

	switch(info->video_interface){
	case TISP_SENSOR_VI_MIPI_CSI0:
		ov9281_attr.dbus_type = TX_SENSOR_DATA_INTERFACE_MIPI;
		ov9281_attr.mipi.index = 0;
		break;
	case TISP_SENSOR_VI_DVP:
		ov9281_attr.dbus_type = TX_SENSOR_DATA_INTERFACE_DVP;
		break;
	default:
		ISP_ERROR("Have no this Interface Source!!!\n");
	}

	switch(info->mclk){
	case TISP_SENSOR_MCLK0:
	case TISP_SENSOR_MCLK1:
	case TISP_SENSOR_MCLK2:
                sclka = private_devm_clk_get(&client->dev, SEN_MCLK);
                sensor->mclk = private_devm_clk_get(sensor->dev, SEN_BCLK);
		set_sensor_mclk_function(0);
		break;
	default:
		ISP_ERROR("Have no this MCLK Source!!!\n");
	}

	//rate = private_clk_get_rate(sensor->mclk);
	rate = private_clk_get_rate(sensor->mclk);
	if (IS_ERR(sensor->mclk)) {
		ISP_ERROR("Cannot get sensor input clock cgu_cim\n");
		goto err_get_mclk;
	}
	private_clk_set_rate(sensor->mclk, 24000000);
	private_clk_prepare_enable(sensor->mclk);

	reset_gpio = info->rst_gpio;
	pwdn_gpio = info->pwdn_gpio;
	sensor->video.vi_max_width = wsize->width;
	sensor->video.vi_max_height = wsize->height;
	sensor->video.mbus.width = wsize->width;
	sensor->video.mbus.height = wsize->height;
	sensor->video.mbus.code = wsize->mbus_code;
	sensor->video.mbus.field = TISP_FIELD_NONE;
	sensor->video.mbus.colorspace = wsize->colorspace;
	sensor->video.fps = wsize->fps;
	sensor->video.max_fps = wsize->fps;
	sensor->video.min_fps = SENSOR_OUTPUT_MIN_FPS << 16 | 1;
    sensor->priv = wsize;
    //ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	return 0;

err_get_mclk:
	return -1;
}

static int ov9281_g_chip_ident(struct tx_isp_subdev *sd,
			       struct tx_isp_chip_ident *chip)
{
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	unsigned int ident = 0;
	int ret = ISP_SUCCESS;
	sensor_attr_check(sd);
	if(reset_gpio != -1){
		ret = private_gpio_request(reset_gpio,"ov9281_reset");
		if(!ret){
			private_gpio_direction_output(reset_gpio, 1);
			private_msleep(5);
			private_gpio_direction_output(reset_gpio, 0);
			private_msleep(20);
			private_gpio_direction_output(reset_gpio, 1);
			private_msleep(20);
		}else{
			ISP_ERROR("gpio requrest fail %d\n",reset_gpio);
		}
	}
	if(pwdn_gpio != -1){
		ret = private_gpio_request(pwdn_gpio,"ov9281_pwdn");
		if(!ret){
			private_gpio_direction_output(pwdn_gpio, 1);
			private_msleep(150);
			private_gpio_direction_output(pwdn_gpio, 0);
			private_msleep(10);
		}else{
			ISP_ERROR("gpio requrest fail %d\n",pwdn_gpio);
		}
	}
	ret = ov9281_detect(sd, &ident);
	if (ret) {
		ISP_ERROR("chip found @ 0x%x (%s) is not an ov9281 chip.\n",
			  client->addr, client->adapter->name);
		return ret;
	}
	ISP_WARNING("ov9281 chip found @ 0x%02x (%s) version %s \n", client->addr, client->adapter->name, SENSOR_VERSION);
	if(chip){
		memcpy(chip->name, "ov9281", sizeof("ov9281"));
		chip->ident = ident;
		chip->revision = SENSOR_VERSION;
	}
	return 0;
}


static int ov9281_set_hvflip(struct tx_isp_subdev *sd, int enable)
{
	int ret = 0;
	uint8_t val_m;
	uint8_t val_f;
	//struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);

	/* 2'b01:mirror,2'b10:filp */
	ret=ov9281_read(sd, 0x3821, &val_m);
	ret=ov9281_read(sd, 0x3820, &val_f);

	switch(enable) {
	case 0:
        ret=ov9281_write(sd, 0x3821, 0x00);
	    ret=ov9281_write(sd, 0x3820, 0x40);
		break;
    case 1:
        ret=ov9281_write(sd, 0x3821, (val_m|0x04));
	    //ret=ov9281_write(sd, 0x3820, val_f);
		break;
	case 2:
        //ret=ov9281_write(sd, 0x3821,val_m);
	    ret=ov9281_write(sd, 0x3820,(val_f|0x04));
		break;
	case 3:
        ret=ov9281_write(sd, 0x3821, (val_m|0x04));
	    ret=ov9281_write(sd, 0x3820, (val_f|0x04));
		break;
	}
//ret += ov9281_write(sd, 0x00eb,0x01);
	//if(!ret)
		//ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);

	return ret;
}


static int ov9281_sensor_ops_ioctl(struct tx_isp_subdev *sd, unsigned int cmd, void *arg)
{
	long ret = 0;
	struct tx_isp_sensor_value *sensor_val = arg;
	if(IS_ERR_OR_NULL(sd)){
		ISP_ERROR("[%d]The pointer is invalid!\n", __LINE__);
		return -EINVAL;
	}
	switch(cmd){
	case TX_ISP_EVENT_SENSOR_INT_TIME:
		if(arg)
			ret = ov9281_set_integration_time(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_AGAIN:
		if(arg)
			ret = ov9281_set_analog_gain(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_DGAIN:
		if(arg)
			ret = ov9281_set_digital_gain(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_BLACK_LEVEL:
		if(arg)
			ret = ov9281_get_black_pedestal(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_RESIZE:
		if(arg)
		    ret = ov9281_set_mode(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_PREPARE_CHANGE:
		    ret = ov9281_write_array(sd, ov9281_stream_off_mipi);
		break;
	case TX_ISP_EVENT_SENSOR_FINISH_CHANGE:
		    ret = ov9281_write_array(sd, ov9281_stream_on_mipi);
		break;
	case TX_ISP_EVENT_SENSOR_FPS:
		if(arg)
		    ret = ov9281_set_fps(sd, sensor_val->value);
		break;
    case TX_ISP_EVENT_SENSOR_VFLIP:
		if(arg)
			ret = ov9281_set_hvflip(sd, sensor_val->value);
		break;
	default:
		break;
	}
	return 0;
}

static int ov9281_g_register(struct tx_isp_subdev *sd, struct tx_isp_dbg_register *reg)
{
	unsigned char val = 0;
	int len = 0;
	int ret = 0;

	len = strlen(sd->chip.name);
	if (len && strncmp(sd->chip.name, reg->name, len))
	{
		return -EINVAL;
	}
	if (!private_capable(CAP_SYS_ADMIN))
		return -EPERM;
	ret = ov9281_read(sd, reg->reg & 0xffff, &val);
	reg->val = val;
	reg->size = 2;

	return ret;
}

static int ov9281_s_register(struct tx_isp_subdev *sd, const struct tx_isp_dbg_register *reg)
{
	int len = 0;

	len = strlen(sd->chip.name);
	if (len && strncmp(sd->chip.name, reg->name, len))
	{
		return -EINVAL;
	}
	if (!private_capable(CAP_SYS_ADMIN))
		return -EPERM;
	ov9281_write(sd, reg->reg & 0xffff, reg->val & 0xff);

	return 0;
}

static struct tx_isp_subdev_core_ops ov9281_core_ops = {
	.g_chip_ident = ov9281_g_chip_ident,
	.reset = ov9281_reset,
	.init = ov9281_init,
	.g_register = ov9281_g_register,
	.s_register = ov9281_s_register,
};

static struct tx_isp_subdev_video_ops ov9281_video_ops = {
	.s_stream = ov9281_s_stream,
};

static struct tx_isp_subdev_sensor_ops ov9281_sensor_ops = {
	.ioctl = ov9281_sensor_ops_ioctl,
};

static struct tx_isp_subdev_ops ov9281_ops = {
	.core = &ov9281_core_ops,
	.video = &ov9281_video_ops,
	.sensor = &ov9281_sensor_ops,
};

/* It's the sensor device */
static u64 tx_isp_module_dma_mask = ~(u64)0;
struct platform_device sensor_platform_device = {
	.name = "ov9281",
	.id = -1,
	.dev = {
		.dma_mask = &tx_isp_module_dma_mask,
		.coherent_dma_mask = 0xffffffff,
		.platform_data = NULL,
	},
	.num_resources = 0,
};

static int ov9281_probe(struct i2c_client *client,
						 const struct i2c_device_id *id)
{
	struct tx_isp_subdev *sd;
	struct tx_isp_video_in *video;
	struct tx_isp_sensor *sensor;

	sensor = (struct tx_isp_sensor *)kzalloc(sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
	{
		ISP_ERROR("Failed to allocate sensor subdev.\n");
		return -ENOMEM;
	}
	memset(sensor, 0, sizeof(*sensor));

	sd = &sensor->sd;
	video = &sensor->video;
	sensor->dev = &client->dev;
	ov9281_attr.expo_fs = 1;
	sensor->video.attr = &ov9281_attr;
	sensor->video.vi_max_width = wsize->width;
	sensor->video.vi_max_height = wsize->height;
	sensor->video.mbus.width = wsize->width;
	sensor->video.mbus.height = wsize->height;
	sensor->video.mbus.code = wsize->mbus_code;
	sensor->video.mbus.field = TISP_FIELD_NONE;
	sensor->video.mbus.colorspace = wsize->colorspace;
	sensor->video.fps = wsize->fps;
    sensor->video.shvflip = 1;
	tx_isp_subdev_init(&sensor_platform_device, sd, &ov9281_ops);
	tx_isp_set_subdevdata(sd, client);
	tx_isp_set_subdev_hostdata(sd, sensor);
	private_i2c_set_clientdata(client, sd);

	pr_debug("probe ok ------->ov9281\n");

	return 0;
}

static int ov9281_remove(struct i2c_client *client)
{
	struct tx_isp_subdev *sd = private_i2c_get_clientdata(client);
	struct tx_isp_sensor *sensor = tx_isp_get_subdev_hostdata(sd);

	if (reset_gpio != -1)
		private_gpio_free(reset_gpio);
	if (pwdn_gpio != -1)
		private_gpio_free(pwdn_gpio);

	private_clk_disable_unprepare(sensor->mclk);
	tx_isp_subdev_deinit(sd);

	kfree(sensor);

	return 0;
}

static const struct i2c_device_id ov9281_id[] = {
	{"ov9281", 0},
	{}};
MODULE_DEVICE_TABLE(i2c, ov9281_id);

static struct i2c_driver ov9281_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "ov9281",
	},
	.probe = ov9281_probe,
	.remove = ov9281_remove,
	.id_table = ov9281_id,
};

static __init int init_ov9281(void)
{
	return private_i2c_add_driver(&ov9281_driver);
}

static __exit void exit_ov9281(void)
{
	private_i2c_del_driver(&ov9281_driver);
}

module_init(init_ov9281);
module_exit(exit_ov9281);

MODULE_DESCRIPTION("A low-level driver for OV ov9281 sensors");
MODULE_LICENSE("GPL");
