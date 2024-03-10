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

#define CV8001_CHIP_ID_H	0x80
#define CV8001_CHIP_ID_L	0x01
#define CV8001_REG_END		0xffff
#define CV8001_REG_DELAY	0xfffe
#define SENSOR_OUTPUT_MAX_FPS 20
#define SENSOR_OUTPUT_MIN_FPS 5
#define SENSOR_VERSION	"H20230505a"
#define AGAIN_MAX_DB 	0xB4

static int reset_gpio = -1;
static int pwdn_gpio = -1;

struct regval_list {
	uint16_t reg_num;
	unsigned char value;
};

struct tx_isp_sensor_attribute cv8001_attr;


unsigned int cv8001_alloc_again(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_again)
{
	uint16_t again=(isp_gain*20)>>shift;
	if(again>AGAIN_MAX_DB) again=AGAIN_MAX_DB;
	*sensor_again = again;
	isp_gain= (((int32_t)again)<<shift)/20;

	return isp_gain;
}

unsigned int cv8001_alloc_dgain(unsigned int isp_gain, unsigned char shift, unsigned int *sensor_dgain)
{
	return 0;
}

unsigned int cv8001_alloc_integration_time(unsigned int it, unsigned char shift, unsigned int *sensor_it)
{
	it = ((it >> 2) << 2);
	*sensor_it = it;
	return it;
}

struct tx_isp_sensor_attribute cv8001_attr={
	.name = "cv8001",
	.chip_id = 0x8001,
	.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C,
	.cbus_mask = TISP_SBUS_MASK_SAMPLE_8BITS | TISP_SBUS_MASK_ADDR_16BITS,
	.cbus_device = 0x35,
	.dbus_type = TX_SENSOR_DATA_INTERFACE_MIPI,
	.max_again = 589824,
	.max_dgain = 0,
	.min_integration_time = 4,
	.max_integration_time = 6596, /* 6604 - 8 */
	.min_integration_time_native = 4,
	.max_integration_time_native = 6596,
	.integration_time_limit = 6596,
	.total_width = 608,
	.total_height = 6604,
	.integration_time_apply_delay = 2,
	.again_apply_delay = 2,
	.dgain_apply_delay = 0,
	.sensor_ctrl.alloc_again = cv8001_alloc_again,
	.sensor_ctrl.alloc_dgain = cv8001_alloc_dgain,
	.sensor_ctrl.alloc_integration_time = cv8001_alloc_integration_time,
};

struct tx_isp_mipi_bus cv8001_mipi={
	.mode = SENSOR_MIPI_OTHER_MODE,
	.clk = 1200,
	.lans = 4,
	.settle_time_apative_en = 0,
	.mipi_sc.sensor_csi_fmt = TX_SENSOR_RAW12,//RAW
	.mipi_sc.hcrop_diff_en = 0,
	.mipi_sc.mipi_vcomp_en = 0,
	.mipi_sc.mipi_hcomp_en = 0,
	.mipi_sc.line_sync_mode = 0,
	.mipi_sc.work_start_flag = 0,
	.image_twidth = 3840,
	.image_theight = 2160,
	.mipi_sc.mipi_crop_start0x = 0,
	.mipi_sc.mipi_crop_start0y = 0,
	.mipi_sc.mipi_crop_start1x = 0,
	.mipi_sc.mipi_crop_start1y = 0,
	.mipi_sc.mipi_crop_start2x = 0,
	.mipi_sc.mipi_crop_start2y = 0,
	.mipi_sc.mipi_crop_start3x = 0,
	.mipi_sc.mipi_crop_start3y = 0,
	.mipi_sc.data_type_en = 0,
	.mipi_sc.data_type_value = RAW12,//RAW
	.mipi_sc.del_start = 0,
	.mipi_sc.sensor_frame_mode = TX_SENSOR_DEFAULT_FRAME_MODE,
	.mipi_sc.sensor_fid_mode = 0,
	.mipi_sc.sensor_mode = TX_SENSOR_DEFAULT_MODE,
};

static struct regval_list cv8001_init_regs_mipi[] = {
	{0x302C, 0x60},
	{0x302D, 0x02},
	{0x302C, 0x60},
	{0x3035, 0x01},
	{0x3036, 0x01},
	{0x3908, 0x4B},
	{0x3909, 0x00},
	{0x343C, 0x03},
	{0x363A, 0x00},
	{0x3635, 0x01},
	{0x3641, 0x40},
	{0x35C4, 0x09},
	{0x35C8, 0x09},
	{0x35CE, 0x07},
	{0x35CF, 0x08},
	{0x3220, 0x08},
	{0x3185, 0x03},
	{0x318A, 0x03},
	{0x387D, 0x3F},
	{0x3220, 0x08},
	{0x397A, 0x08},
	{0x3804, 0x14},
	{0x3020, 0x04},
	{0x3040, 0x01},
	{0x3048, 0x08},
	{0x3049, 0x00},
	{0x304A, 0x00},
	{0x304B, 0x0F},
	{0x3044, 0x04},
	{0x3045, 0x00},
	{0x3046, 0x70},
	{0x3047, 0x08},
	{0x31AC, 0xC8},
	{0x3028, 0xCC},
	{0x3029, 0x19},
	{0x302a, 0x00},
	{0x3587, 0x2a},
	{0x3b68, 0x00},
	{0x3b69, 0x00},
	{0x3b6a, 0xff},
	{0x3b6b, 0x01},
	{0x316C, 0x01},
	{0x3348, 0x00},
	{0x35EC, 0x04},
	{CV8001_REG_END, 0x00},/* END MARKER */
};


static struct tx_isp_sensor_win_setting cv8001_win_sizes[] = {
	{
		.width		= 3840,
		.height		= 2160,
		.fps		= 20 << 16 | 1,
		.mbus_code	= TISP_VI_FMT_SRGGB10_1X10,//RAW
		.colorspace	= TISP_COLORSPACE_SRGB,
		.regs 		= cv8001_init_regs_mipi,
	},
};
struct tx_isp_sensor_win_setting *wsize = &cv8001_win_sizes[0];	//默认线性模式

static struct regval_list cv8001_stream_on_mipi[] = {
	{0x3000, 0x00},
	{CV8001_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list cv8001_stream_off_mipi[] = {
	{0x3000, 0x01},
	{CV8001_REG_END, 0x00},	/* END MARKER */
};

int cv8001_read(struct tx_isp_subdev *sd, uint16_t reg, unsigned char *value)
{
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	uint8_t buf[2] = {(reg >> 8) & 0xff, reg & 0xff};
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
	int ret;
	ret = private_i2c_transfer(client->adapter, msg, 2);
	if (ret > 0)
		ret = 0;

	return ret;
}

int cv8001_write(struct tx_isp_subdev *sd, uint16_t reg, unsigned char value)
{
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	uint8_t buf[3] = {(reg >> 8) & 0xff, reg & 0xff, value};
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
static int cv8001_read_array(struct tx_isp_subdev *sd, struct regval_list *vals)
{
	int ret;
	unsigned char val;
	while (vals->reg_num != CV8001_REG_END) {
		if (vals->reg_num == CV8001_REG_DELAY) {
			msleep(vals->value);
		} else {
			ret = cv8001_read(sd, vals->reg_num, &val);
			if (ret < 0)
				return ret;
		}
		vals++;
	}

	return 0;
}
#endif

static int cv8001_write_array(struct tx_isp_subdev *sd, struct regval_list *vals)
{
	int ret;
	while (vals->reg_num != CV8001_REG_END) {
		if (vals->reg_num == CV8001_REG_DELAY) {
			msleep(vals->value);
		} else {
			ret = cv8001_write(sd, vals->reg_num, vals->value);
			if (ret < 0)
				return ret;
		}
		vals++;
	}

	return 0;
}

static int cv8001_reset(struct tx_isp_subdev *sd, struct tx_isp_initarg *init)
{
	return 0;
}

static int cv8001_detect(struct tx_isp_subdev *sd, unsigned int *ident)
{
	int ret;
	unsigned char v;

	ret = cv8001_read(sd, 0x3002, &v);
	ISP_WARNING("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != CV8001_CHIP_ID_L)
		return -ENODEV;
	*ident = v;

	ret = cv8001_read(sd, 0x3003, &v);
	ISP_WARNING("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != CV8001_CHIP_ID_H)
		return -ENODEV;
	*ident = (*ident << 8) | v;

	return ret;
}

#if 0
static int cv8001_set_expo(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;
	int it = value & 0xffff;
	unsigned short exp;
	unsigned int vmax = 1125;
	int again = (value >> 16) & 0x0000ffff;

	vmax = cv8001_attr.total_height;
	exp = vmax - it;
	exp = ((exp >> 2) << 2);
	ret = cv8001_write(sd, 0x3060, (unsigned char)(exp & 0xff));
	ret += cv8001_write(sd, 0x3061, (unsigned char)((exp >> 8) & 0xff));
	ret += cv8001_write(sd, 0x3164, (unsigned char)(again & 0xff));

	ISP_WARNING("cv8001 set exp=0x%04x(%4d line) gain=0x%02x\n", exp, it, again);

	return ret;
}
#endif

static int cv8001_set_integration_time(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;
	int it = value & 0xffff;
	unsigned short exp0;
	unsigned int vmax = 1125;

	vmax = cv8001_attr.total_height;
	exp0 = vmax - it;
	exp0 = (exp0 >> 1) << 1;
	ret = cv8001_write(sd, 0x3060, (unsigned char)(exp0 & 0xff));
	ret += cv8001_write(sd, 0x3061, (unsigned char)((exp0 >> 8) & 0xff));

	//ISP_WARNING("cv8001 set exp=0x%04x(%4d line)\n", exp0, it);

	return ret;
}

static int cv8001_set_analog_gain(struct tx_isp_subdev *sd, int value)
{
	int ret = 0;
	unsigned char again = value & 0xff;

	ret = cv8001_write(sd, 0x3164, again);

	//ISP_WARNING("cv8001 set gain0=0x%02x(%03d)\n", again, again);

	return ret;
}

static int cv8001_set_digital_gain(struct tx_isp_subdev *sd, int value)
{
	return 0;
}

static int cv8001_get_black_pedestal(struct tx_isp_subdev *sd, int value)
{
	return 0;
}

#if 0
static int sensor_set_attr(struct tx_isp_subdev *sd, struct tx_isp_sensor_win_setting *wise)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);

	sensor->video.vi_max_width = wsize->width;
	sensor->video.vi_max_height = wsize->height;
	sensor->video.mbus.width = wsize->width;
	sensor->video.mbus.height = wsize->height;
	sensor->video.mbus.code = wsize->mbus_code;
	sensor->video.mbus.field = TISP_FIELD_NONE;
	sensor->video.mbus.colorspace = wsize->colorspace;
	sensor->video.fps = wsize->fps;
	sensor->video.state = TX_ISP_MODULE_DEINIT;

	return 0;
}
#endif

static int cv8001_init(struct tx_isp_subdev *sd, struct tx_isp_initarg *init)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	int ret = 0;

	if(!init->enable)
		return ISP_SUCCESS;

	sensor->video.vi_max_width = wsize->width;
	sensor->video.vi_max_height = wsize->height;
	sensor->video.mbus.width = wsize->width;
	sensor->video.mbus.height = wsize->height;
	sensor->video.mbus.code = wsize->mbus_code;
	sensor->video.mbus.field = TISP_FIELD_NONE;
	sensor->video.mbus.colorspace = wsize->colorspace;
	sensor->video.fps = wsize->fps;
	sensor->video.state = TX_ISP_MODULE_INIT;
	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	sensor->priv = wsize;

	return 0;
}

static int cv8001_s_stream(struct tx_isp_subdev *sd, struct tx_isp_initarg *init)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	int ret = 0;

	if (init->enable)
	{
		if(sensor->video.state == TX_ISP_MODULE_INIT)
		{
			ret = cv8001_write_array(sd, wsize->regs);
			if (ret)
				return ret;
			sensor->video.state = TX_ISP_MODULE_RUNNING;
		}
		if(sensor->video.state == TX_ISP_MODULE_RUNNING)
		{
			ret = cv8001_write_array(sd, cv8001_stream_on_mipi);
			ISP_WARNING("cv8001 stream on\n");
		}
	}
	else
	{
		ret = cv8001_write_array(sd, cv8001_stream_off_mipi);
		ISP_WARNING("cv8001 stream off\n");
	}

	return ret;
}

static int cv8001_set_mode(struct tx_isp_subdev *sd, int value)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	int ret = ISP_SUCCESS;

	if (wsize)
	{
		sensor->video.vi_max_width = wsize->width;
		sensor->video.vi_max_height = wsize->height;
		sensor->video.mbus.width = wsize->width;
		sensor->video.mbus.height = wsize->height;
		sensor->video.mbus.code = wsize->mbus_code;
		sensor->video.mbus.field = TISP_FIELD_NONE;
		sensor->video.mbus.colorspace = wsize->colorspace;
		sensor->video.fps = wsize->fps;
		sensor->video.state = TX_ISP_MODULE_DEINIT;
		ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	}

	return ret;
}

static int cv8001_set_fps(struct tx_isp_subdev *sd, int fps)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	unsigned int sclk = 0;
	unsigned int hts = 0;
	unsigned int vts = 0;
	unsigned int max_fps;
	unsigned char val = 0;
	unsigned int newformat = 0; //the format is 24.8
	int ret = 0;

	switch(sensor->info.default_boot){
	case 0:
		sclk = 80304640; /* 608 * 6604 * 20 */
		max_fps = TX_SENSOR_MAX_FPS_30;
		break;
	default:
		ISP_ERROR("Now we do not support this framerate!!!\n");
	}

	newformat = (((fps >> 16) / (fps & 0xffff)) << 8) + ((((fps >> 16) % (fps & 0xffff)) << 8) / (fps & 0xffff));
	if(newformat > (max_fps<< 8) || newformat < (SENSOR_OUTPUT_MIN_FPS << 8)) {
		ISP_ERROR("warn: fps(%x) no in range\n", fps);
		return -1;
	}

	ret += cv8001_read(sd, 0x302D, &val);
	hts = val;
	val = 0;
	ret += cv8001_read(sd, 0x302C, &val);
	hts = ((hts << 8) | val);

	if (0 != ret) {
		ISP_ERROR("err: cv8001 read err\n");
		return -1;
	}

	vts = sclk * (fps & 0xffff) / hts / ((fps & 0xffff0000) >> 16);

	ret = cv8001_write(sd, 0x3028, (unsigned char)(vts & 0xff));
	ret += cv8001_write(sd, 0x3029, (unsigned char)((vts >> 8)& 0xff));
	ret += cv8001_write(sd, 0x302A, (unsigned char)((vts >> 16)& 0x0f));

	if (0 != ret) {
		ISP_ERROR("err: cv8001_write err\n");
		return ret;
	}
	sensor->video.fps = fps;
	sensor->video.attr->max_integration_time_native = vts - 8;
	sensor->video.attr->integration_time_limit = vts - 8;
	sensor->video.attr->total_height = vts;
	sensor->video.attr->max_integration_time = vts - 8;
	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);

	return ret;
}

static int cv8001_set_vflip(struct tx_isp_subdev *sd, int enable)
{
	int ret = 0;
	uint8_t val;
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	/* 2'b01:mirror,2'b10:filp */
	val = cv8001_read(sd, 0x3034, &val);
	switch(enable) {
	case 0:
		val &= 0xFC;
		sensor->video.mbus.code	= TISP_VI_FMT_SRGGB10_1X10;
		break;
	case 1:
		val &= 0xFD;
		val |= 0x01;
		sensor->video.mbus.code	= TISP_VI_FMT_SGRBG10_1X10;
		break;
	case 2:
		val &= 0xFC;
		val |= 0x02;
		sensor->video.mbus.code	= TISP_VI_FMT_SGBRG10_1X10;
		break;
	case 3:
		val |= 0x03;
		sensor->video.mbus.code	= TISP_VI_FMT_SBGGR10_1X10;
		break;
	};
	sensor->video.mbus_change = 1;
	ret = cv8001_write(sd, 0x3034, val);
	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);
	//if(!ret)
	//	ret = tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR, &sensor->video);

	return ret;
}

static int sensor_attr_check(struct tx_isp_subdev *sd)
{
	struct tx_isp_sensor *sensor = sd_to_sensor_device(sd);
	struct tx_isp_sensor_register_info *info = &sensor->info;
	unsigned long rate;

	wsize = &cv8001_win_sizes[0];
	memcpy(&(cv8001_attr.mipi), &cv8001_mipi, sizeof(cv8001_mipi));
	cv8001_attr.data_type = TX_SENSOR_DATA_TYPE_LINEAR;

	switch (info->video_interface)
	{
	case TISP_SENSOR_VI_MIPI_CSI0:
		cv8001_attr.dbus_type = TX_SENSOR_DATA_INTERFACE_MIPI;
		cv8001_attr.mipi.index = 0;
		break;
	case TISP_SENSOR_VI_MIPI_CSI1:
		cv8001_attr.dbus_type = TX_SENSOR_DATA_INTERFACE_MIPI;
		cv8001_attr.mipi.index = 1;
		break;
	default:
		ISP_ERROR("Error interface(%d)!!!\n", info->video_interface);
	}

	switch (info->mclk)
	{
	case TISP_SENSOR_MCLK0:
		sensor->mclk = private_devm_clk_get(sensor->dev, "div_cim0");
		set_sensor_mclk_function(0);
		break;
	case TISP_SENSOR_MCLK1:
		sensor->mclk = private_devm_clk_get(sensor->dev, "div_cim1");
		set_sensor_mclk_function(1);
		break;
	case TISP_SENSOR_MCLK2:
		sensor->mclk = private_devm_clk_get(sensor->dev, "div_cim2");
		set_sensor_mclk_function(2);
		break;
	default:
		ISP_ERROR("Have no this MCLK Source(%d)!!!\n", info->mclk);
	}

	rate = private_clk_get_rate(sensor->mclk);
	if (IS_ERR(sensor->mclk))
	{
		ISP_ERROR("Cannot get sensor input clock cgu_cim\n");
		ISP_ERROR("Cannot get sensor input clock cgu_cim\n");
		goto err_get_mclk;
	}
	private_clk_set_rate(sensor->mclk, 24000000);
	private_clk_prepare_enable(sensor->mclk);

	reset_gpio = info->rst_gpio;
	pwdn_gpio = info->pwdn_gpio;

	sensor->priv = wsize;
	sensor->video.vi_max_width = wsize->width;
	sensor->video.vi_max_height = wsize->height;
	sensor->video.mbus.width = wsize->width;
	sensor->video.mbus.height = wsize->height;
	sensor->video.mbus.code = wsize->mbus_code;
	sensor->video.mbus.field = TISP_FIELD_NONE;
	sensor->video.mbus.colorspace = wsize->colorspace;
	sensor->video.fps = wsize->fps;
	sensor->video.state = TX_ISP_MODULE_INIT;

	return 0;

err_get_mclk:
	return -1;
}

static int cv8001_g_chip_ident(struct tx_isp_subdev *sd, struct tx_isp_chip_ident *chip)
{
	struct i2c_client *client = tx_isp_get_subdevdata(sd);
	unsigned int ident = 0;
	int ret = ISP_SUCCESS;

	sensor_attr_check(sd);
	if (reset_gpio != -1)
	{
		ret = private_gpio_request(reset_gpio, "cv8001_reset");
		if (!ret)
		{
			private_gpio_direction_output(reset_gpio, 1);
			private_msleep(5);
			private_gpio_direction_output(reset_gpio, 0);
			private_msleep(5);
			private_gpio_direction_output(reset_gpio, 1);
			private_msleep(5);
		}
		else
		{
			ISP_ERROR("gpio requrest fail %d\n", reset_gpio);
		}
	}
	if (pwdn_gpio != -1)
	{
		if (!ret)
		{
			private_gpio_direction_output(pwdn_gpio, 0);
			private_msleep(5);
			private_gpio_direction_output(pwdn_gpio, 1);
			private_msleep(5);
		}
		else
		{
			ISP_ERROR("gpio requrest fail %d\n", pwdn_gpio);
		}
	}
	ret = cv8001_detect(sd, &ident);	//检查sensor id
	if (ret)
	{
		ISP_ERROR("chip found @ 0x%x (%s) is not an cv8001 chip.\n", client->addr, client->adapter->name);
		return ret;
	}
	ISP_WARNING("cv8001 chip found @ 0x%02x (%s)\n", client->addr, client->adapter->name);
	ISP_WARNING("sensor driver version %s\n", SENSOR_VERSION);
	if (chip)
	{
		memcpy(chip->name, "cv8001", sizeof("cv8001"));
		chip->ident = ident;
		chip->revision = SENSOR_VERSION;
	}

	return 0;
}

static int cv8001_sensor_ops_ioctl(struct tx_isp_subdev *sd, unsigned int cmd, void *arg)
{
	long ret = 0;
	struct tx_isp_sensor_value *sensor_val = arg;
	if(IS_ERR_OR_NULL(sd)){
		ISP_ERROR("[%d]The pointer is invalid!\n", __LINE__);
		return -EINVAL;
	}
	switch(cmd){
	case TX_ISP_EVENT_SENSOR_EXPO:
		// if(arg)
		// 	ret = cv8001_set_expo(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_INT_TIME:
		if(arg)
			ret = cv8001_set_integration_time(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_AGAIN:
		if(arg)
			ret = cv8001_set_analog_gain(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_DGAIN:
		if(arg)
			ret = cv8001_set_digital_gain(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_BLACK_LEVEL:
		if(arg)
			ret = cv8001_get_black_pedestal(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_RESIZE:
		if(arg)
			ret = cv8001_set_mode(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_PREPARE_CHANGE:
		ret = cv8001_write_array(sd, cv8001_stream_off_mipi);
		break;
	case TX_ISP_EVENT_SENSOR_FINISH_CHANGE:
		ret = cv8001_write_array(sd, cv8001_stream_on_mipi);
		break;
	case TX_ISP_EVENT_SENSOR_FPS:
		if(arg)
			ret = cv8001_set_fps(sd, sensor_val->value);
		break;
	case TX_ISP_EVENT_SENSOR_VFLIP:
		if(arg)
			ret = cv8001_set_vflip(sd, sensor_val->value);
		break;
	default:
		break;
	}

	return ret;
}

static int cv8001_g_register(struct tx_isp_subdev *sd, struct tx_isp_dbg_register *reg)
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
	ret = cv8001_read(sd, reg->reg & 0xffff, &val);
	reg->val = val;
	reg->size = 2;

	return ret;
}

static int cv8001_s_register(struct tx_isp_subdev *sd, const struct tx_isp_dbg_register *reg)
{
	int len = 0;

	len = strlen(sd->chip.name);
	if (len && strncmp(sd->chip.name, reg->name, len))
	{
		return -EINVAL;
	}
	if (!private_capable(CAP_SYS_ADMIN))
		return -EPERM;
	cv8001_write(sd, reg->reg & 0xffff, reg->val & 0xff);

	return 0;
}

static struct tx_isp_subdev_core_ops cv8001_core_ops = {
	.reset = cv8001_reset,
	.init = cv8001_init,
	.g_chip_ident = cv8001_g_chip_ident,
	.g_register = cv8001_g_register,
	.s_register = cv8001_s_register,
};

static struct tx_isp_subdev_video_ops cv8001_video_ops = {
	.s_stream = cv8001_s_stream,
};

static struct tx_isp_subdev_sensor_ops	cv8001_sensor_ops = {
	.ioctl	= cv8001_sensor_ops_ioctl,
};

static struct tx_isp_subdev_ops cv8001_ops = {
	.core = &cv8001_core_ops,
	.video = &cv8001_video_ops,
	.sensor = &cv8001_sensor_ops,
};

static u64 tx_isp_module_dma_mask = ~(u64)0;
struct platform_device sensor_platform_device = {
	.name = "cv8001",
	.id = -1,
	.dev = {
		.dma_mask = &tx_isp_module_dma_mask,
		.coherent_dma_mask = 0xffffffff,
		.platform_data = NULL,
	},
	.num_resources = 0,
};

static int cv8001_probe(struct i2c_client *client,
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

	memset(sensor, 0 ,sizeof(*sensor));
    sd = &sensor->sd;
	video = &sensor->video;
	sensor->dev = &client->dev;
	cv8001_attr.expo_fs = 1;
	sensor->video.shvflip = 1;
	sensor->video.attr = &cv8001_attr;
	sensor->video.vi_max_width = wsize->width;
	sensor->video.vi_max_height = wsize->height;
	sensor->video.mbus.width = wsize->width;
	sensor->video.mbus.height = wsize->height;
	sensor->video.mbus.code = wsize->mbus_code;
	sensor->video.mbus.field = TISP_FIELD_NONE;
	sensor->video.mbus.colorspace = wsize->colorspace;
	sensor->video.fps = wsize->fps;
	tx_isp_subdev_init(&sensor_platform_device, sd, &cv8001_ops);
	tx_isp_set_subdevdata(sd, client);
	tx_isp_set_subdev_hostdata(sd, sensor);
	private_i2c_set_clientdata(client, sd);

	ISP_WARNING("---- cv8001 probe ok ----\n");

	return 0;
}

static int cv8001_remove(struct i2c_client *client)
{
	struct tx_isp_subdev *sd = private_i2c_get_clientdata(client);
	struct tx_isp_sensor *sensor = tx_isp_get_subdev_hostdata(sd);

	if(reset_gpio != -1)
		private_gpio_free(reset_gpio);
	if(pwdn_gpio != -1)
		private_gpio_free(pwdn_gpio);

	private_clk_disable_unprepare(sensor->mclk);
	private_devm_clk_put(&client->dev, sensor->mclk);
	tx_isp_subdev_deinit(sd);
	kfree(sensor);

	return 0;
}

static const struct i2c_device_id cv8001_id[] = {
	{ "cv8001", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cv8001_id);

static struct i2c_driver cv8001_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "cv8001",
	},
	.probe		= cv8001_probe,
	.remove		= cv8001_remove,
	.id_table	= cv8001_id,
};

static __init int init_cv8001(void)
{
	return private_i2c_add_driver(&cv8001_driver);
}

static __exit void exit_cv8001(void)
{
	private_i2c_del_driver(&cv8001_driver);
}

module_init(init_cv8001);
module_exit(exit_cv8001);

MODULE_DESCRIPTION("A low-level driver for cv8001 sensors");
MODULE_LICENSE("GPL");
