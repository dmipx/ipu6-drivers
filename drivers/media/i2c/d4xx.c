// SPDX-License-Identifier: GPL-2.0
/*
 * ds5.c - Intel(R) RealSense(TM) D4XX camera driver
 *
 * Copyright (c) 2017-2023, INTEL CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/media.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/videodev2.h>
#include <linux/version.h>
#ifdef CONFIG_VIDEO_INTEL_IPU6
#include <uapi/linux/ipu-isys.h>
#include <media/d4xx_pdata.h>
#endif
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-mediabus.h>

#ifdef CONFIG_VIDEO_D4XX_SERDES
#include <media/max9295.h>
#include <media/max9296.h>
#else
#include <media/gmsl-link.h>
#define GMSL_CSI_DT_YUV422_8 0x1E
#define GMSL_CSI_DT_RGB_888 0x24
#define GMSL_CSI_DT_RAW_8 0x2A
#define GMSL_CSI_DT_EMBED 0x12
#endif

//#define DS5_DRIVER_NAME "DS5 RealSense camera driver"
#define DS5_DRIVER_NAME "d4xx"
#define DS5_DRIVER_NAME_AWG "d4xx-awg"
#define DS5_DRIVER_NAME_ASR "d4xx-asr"
#define DS5_DRIVER_NAME_CLASS "d4xx-class"
#define DS5_DRIVER_NAME_DFU "d4xx-dfu"

#define DS5_MIPI_SUPPORT_LINES		0x0300
#define DS5_MIPI_SUPPORT_PHY		0x0304
#define DS5_MIPI_DATARATE_MIN		0x0308
#define DS5_MIPI_DATARATE_MAX		0x030A
#define DS5_FW_VERSION			0x030C
#define DS5_FW_BUILD			0x030E
#define DS5_DEVICE_TYPE			0x0310
#define DS5_DEVICE_TYPE_D45X		6
#define DS5_DEVICE_TYPE_D43X		5
#define DS5_DEVICE_TYPE_D46X		4

#define DS5_MIPI_LANE_NUMS		0x0400
#define DS5_MIPI_LANE_DATARATE		0x0402
#define DS5_MIPI_CONF_STATUS		0x0500

#define DS5_START_STOP_STREAM		0x1000
#define DS5_DEPTH_STREAM_STATUS		0x1004
#define DS5_RGB_STREAM_STATUS		0x1008
#define DS5_IMU_STREAM_STATUS		0x100C
#define DS5_IR_STREAM_STATUS		0x1014

#define DS5_STREAM_DEPTH		0x0
#define DS5_STREAM_RGB			0x1
#define DS5_STREAM_IMU			0x2
#define DS5_STREAM_IR			0x4
#define DS5_STREAM_STOP			0x100
#define DS5_STREAM_START		0x200
#define DS5_STREAM_IDLE			0x1
#define DS5_STREAM_STREAMING		0x2

#define DS5_DEPTH_STREAM_DT		0x4000
#define DS5_DEPTH_STREAM_MD		0x4002
#define DS5_DEPTH_RES_WIDTH		0x4004
#define DS5_DEPTH_RES_HEIGHT		0x4008
#define DS5_DEPTH_FPS			0x400C
#define DS5_DEPTH_OVERRIDE		0x401C

#define DS5_RGB_STREAM_DT		0x4020
#define DS5_RGB_STREAM_MD		0x4022
#define DS5_RGB_RES_WIDTH		0x4024
#define DS5_RGB_RES_HEIGHT		0x4028
#define DS5_RGB_FPS			0x402C

#define DS5_IMU_STREAM_DT		0x4040
#define DS5_IMU_STREAM_MD		0x4042
#define DS5_IMU_RES_WIDTH		0x4044
#define DS5_IMU_RES_HEIGHT		0x4048
#define DS5_IMU_FPS			0x404C

#define DS5_IR_STREAM_DT		0x4080
#define DS5_IR_STREAM_MD		0x4082
#define DS5_IR_RES_WIDTH		0x4084
#define DS5_IR_RES_HEIGHT		0x4088
#define DS5_IR_FPS			0x408C
#define DS5_IR_OVERRIDE			0x409C

#define DS5_DEPTH_CONTROL_BASE		0x4100
#define DS5_RGB_CONTROL_BASE		0x4200
#define DS5_MANUAL_EXPOSURE_LSB		0x0000
#define DS5_MANUAL_EXPOSURE_MSB		0x0002
#define DS5_MANUAL_GAIN			0x0004
#define DS5_LASER_POWER			0x0008
#define DS5_AUTO_EXPOSURE_MODE		0x000C
#define DS5_EXPOSURE_ROI_TOP		0x0010
#define DS5_EXPOSURE_ROI_LEFT		0x0014
#define DS5_EXPOSURE_ROI_BOTTOM		0x0018
#define DS5_EXPOSURE_ROI_RIGHT		0x001C
#define DS5_MANUAL_LASER_POWER		0x0024
#define DS5_PWM_FREQUENCY		0x0028

#define DS5_DEPTH_CONFIG_STATUS		0x4800
#define DS5_RGB_CONFIG_STATUS		0x4802
#define DS5_IMU_CONFIG_STATUS		0x4804
#define DS5_IR_CONFIG_STATUS		0x4808

#define DS5_STATUS_STREAMING		0x1
#define DS5_STATUS_INVALID_DT		0x2
#define DS5_STATUS_INVALID_RES		0x4
#define DS5_STATUS_INVALID_FPS		0x8

#define MIPI_LANE_RATE			1000

#define MAX_DEPTH_EXP			200000
#define MAX_RGB_EXP			10000
#define DEF_DEPTH_EXP			33000
#define DEF_RGB_EXP			1660
#ifdef CONFIG_VIDEO_INTEL_IPU6

#define MAX9295_REG0	0x0000
#define MAX9295_I2C_4	0x0044
#define MAX9295_I2C_5	0x0045

#define MAX9296_CTRL0	0x0010
#define RESET_LINK	(0x1 << 6)
#define RESET_ONESHOT	(0x1 << 5)
#define AUTO_LINK	(0x1 << 4)
#define DUAL_LINK	(0x0)
#define LINK_A		(0x1)
#define LINK_B		(0x2)
#define SPLITTER	(0x3)
#define MAX9296_NUM	(4)

#define MAX9295_I2C_ADDR_DEF	0x40
#define D457_I2C_ADDR	0x10
#endif
enum ds5_mux_pad {
	DS5_MUX_PAD_EXTERNAL,
	DS5_MUX_PAD_DEPTH,
	DS5_MUX_PAD_RGB,
	DS5_MUX_PAD_IR,
	DS5_MUX_PAD_IMU,
	DS5_MUX_PAD_COUNT,
};

#define DS5_N_CONTROLS			8

#define CSI2_MAX_VIRTUAL_CHANNELS	4

#define DFU_WAIT_RET_LEN 6

#define DS5_START_POLL_TIME	10
#define DS5_START_MAX_TIME	1000
#define DS5_START_MAX_COUNT	(DS5_START_MAX_TIME / DS5_START_POLL_TIME)

/* DFU definition section */
#define DFU_MAGIC_NUMBER "/0x01/0x02/0x03/0x04"
#define DFU_BLOCK_SIZE 1024
#ifdef CONFIG_TEGRA_CAMERA_PLATFORM
#define DFU_I2C_STANDARD_MODE		100000
#define DFU_I2C_FAST_MODE			400000
#define DFU_I2C_BUS_CLK_RATE		DFU_I2C_FAST_MODE
#endif
#define ds5_read_with_check(state, addr, val) {\
	if (ds5_read(state, addr, val))	\
		return -EINVAL; }
#define ds5_raw_read_with_check(state, addr, buf, size)	{\
	if (ds5_raw_read(state, addr, buf, size))	\
		return -EINVAL; }
#define ds5_write_with_check(state, addr, val) {\
	if (ds5_write(state, addr, val))	\
		return -EINVAL; }
#define ds5_raw_write_with_check(state, addr, buf, size) {\
	if (ds5_raw_write(state, addr, buf, size)) \
		return -EINVAL; }
#ifdef CONFIG_VIDEO_INTEL_IPU6
#define max9296_write_8_with_check(state, addr, buf) {\
	if (max9296_write_8(state, addr, buf)) \
		return -EINVAL; \
	}
#define max9295_write_8_with_check(state, addr, buf) {\
	if (max9295_write_8(state, addr, buf)) \
		return -EINVAL; \
	}
#define D4XX_LINK_FREQ_750MHZ		750000000ULL
#define D4XX_LINK_FREQ_360MHZ		360000000ULL
#define D4XX_LINK_FREQ_300MHZ		300000000ULL
#define D4XX_LINK_FREQ_288MHZ		288000000ULL
#define D4XX_LINK_FREQ_240MHZ		240000000ULL
#define D4XX_LINK_FREQ_225MHZ		22500000ULL
#endif
enum dfu_fw_state {
	appIDLE                = 0x0000,
	appDETACH              = 0x0001,
	dfuIDLE                = 0x0002,
	dfuDNLOAD_SYNC         = 0x0003,
	dfuDNBUSY              = 0x0004,
	dfuDNLOAD_IDLE         = 0x0005,
	dfuMANIFEST_SYNC       = 0x0006,
	dfuMANIFEST            = 0x0007,
	dfuMANIFEST_WAIT_RESET = 0x0008,
	dfuUPLOAD_IDLE         = 0x0009,
	dfuERROR               = 0x000a
};

enum dfu_state {
	DS5_DFU_IDLE = 0,
	DS5_DFU_RECOVERY,
	DS5_DFU_OPEN,
	DS5_DFU_IN_PROGRESS,
	DS5_DFU_DONE,
	DS5_DFU_ERROR
} dfu_state_t;

struct hwm_cmd {
	u16 header;
	u16 magic_word;
	u32 opcode;
	u32 param1;
	u32 param2;
	u32 param3;
	u32 param4;
	unsigned char Data[0];
};

static const struct hwm_cmd cmd_switch_to_dfu = {
	.header = 0x14,
	.magic_word = 0xCDAB,
	.opcode = 0x1e,
	.param1 = 0x01,
};

enum table_id {
	COEF_CALIBRATION_ID = 0x19,
	DEPTH_CALIBRATION_ID = 0x1f,
	RGB_CALIBRATION_ID = 0x20,
	IMU_CALIBRATION_ID = 0x22
} table_id_t;

static const struct hwm_cmd get_calib_data = {
	.header = 0x14,
	.magic_word = 0xCDAB,
	.opcode = 0x15,
	.param1 = 0x00,	//table_id
};

static const struct hwm_cmd set_calib_data = {
	.header = 0x0114,
	.magic_word = 0xCDAB,
	.opcode = 0x62,
	.param1 = 0x00,	//table_id
	.param2 = 0x02,	//region
};

static const struct hwm_cmd gvd = {
	.header = 0x14,
	.magic_word = 0xCDAB,
	.opcode = 0x10,
};

static const struct hwm_cmd set_ae_roi = {
	.header = 0x14,
	.magic_word = 0xCDAB,
	.opcode = 0x44,
};

static const struct hwm_cmd get_ae_roi = {
	.header = 0x014,
	.magic_word = 0xCDAB,
	.opcode = 0x45,
};

static const struct hwm_cmd set_ae_setpoint = {
	.header = 0x18,
	.magic_word = 0xCDAB,
	.opcode = 0x2B,
	.param1 = 0xa, // AE control
};

static const struct hwm_cmd get_ae_setpoint = {
	.header = 0x014,
	.magic_word = 0xCDAB,
	.opcode = 0x2C,
	.param1 = 0xa, // AE control
	.param2 = 0, // get current
};

static const struct hwm_cmd erb = {
	.header = 0x14,
	.magic_word = 0xCDAB,
	.opcode = 0x17,
};

static const struct hwm_cmd ewb = {
	.header = 0x14,
	.magic_word = 0xCDAB,
	.opcode = 0x18,
};
#ifdef CONFIG_VIDEO_INTEL_IPU6
static const s64 link_freq_menu_items[] = {
	D4XX_LINK_FREQ_750MHZ,
	D4XX_LINK_FREQ_360MHZ,
	D4XX_LINK_FREQ_300MHZ,
	D4XX_LINK_FREQ_288MHZ,
	D4XX_LINK_FREQ_240MHZ,
	D4XX_LINK_FREQ_225MHZ,
};
#endif
struct __fw_status {
	uint32_t	spare1;
	uint32_t	FW_lastVersion;
	uint32_t	FW_highestVersion;
	uint16_t	FW_DownloadStatus;
	uint16_t	DFU_isLocked;
	uint16_t	DFU_version;
	uint8_t		ivcamSerialNum[8];
	uint8_t		spare2[42];
};

/*************************/

struct ds5_ctrls {
	struct v4l2_ctrl_handler handler;
	struct v4l2_ctrl_handler handler_depth;
	struct v4l2_ctrl_handler handler_rgb;
	struct v4l2_ctrl_handler handler_y8;
	struct v4l2_ctrl_handler handler_imu;
	struct {
		struct v4l2_ctrl *log;
		struct v4l2_ctrl *fw_version;
		struct v4l2_ctrl *gvd;
		struct v4l2_ctrl *get_depth_calib;
		struct v4l2_ctrl *set_depth_calib;
		struct v4l2_ctrl *get_coeff_calib;
		struct v4l2_ctrl *set_coeff_calib;
		struct v4l2_ctrl *ae_roi_get;
		struct v4l2_ctrl *ae_roi_set;
		struct v4l2_ctrl *ae_setpoint_get;
		struct v4l2_ctrl *ae_setpoint_set;
		struct v4l2_ctrl *erb;
		struct v4l2_ctrl *ewb;
		struct v4l2_ctrl *hwmc;
		struct v4l2_ctrl *laser_power;
		struct v4l2_ctrl *manual_laser_power;
		struct v4l2_ctrl *auto_exp;
		struct v4l2_ctrl *exposure;
		/* in DS5 manual gain only works with manual exposure */
		struct v4l2_ctrl *gain;
		struct v4l2_ctrl *link_freq;
		struct v4l2_ctrl *query_sub_stream;
		struct v4l2_ctrl *set_sub_stream;
	};
};

struct ds5_resolution {
	u16 width;
	u16 height;
	u8 n_framerates;
	const u16 *framerates;
};

struct ds5_format {
	unsigned int n_resolutions;
	const struct ds5_resolution *resolutions;
	u32 mbus_code;
	u8 data_type;
};

struct ds5_sensor {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_mbus_framefmt format;
	u16 mux_pad;
	struct {
		const struct ds5_format *format;
		const struct ds5_resolution *resolution;
		u16 framerate;
	} config;
	bool streaming;
	/*struct ds5_vchan *vchan;*/
	const struct ds5_format *formats;
	unsigned int n_formats;
	int pipe_id;
};

#ifdef CONFIG_TEGRA_CAMERA_PLATFORM
#include <media/camera_common.h>
#define ds5_mux_subdev camera_common_data
#else
struct ds5_mux_subdev {
	struct v4l2_subdev subdev;
};
#endif

struct ds5_variant {
	const struct ds5_format *formats;
	unsigned int n_formats;
};

struct ds5_dfu_dev {
	struct cdev ds5_cdev;
	struct class *ds5_class;
	int device_open_count;
	enum dfu_state dfu_state_flag;
	unsigned char *dfu_msg;
	u16 msg_write_once;
	// unsigned char init_v4l_f; // need refactoring
	u32 bus_clk_rate;
};

enum {
	DS5_DS5U,
	DS5_ASR,
	DS5_AWG,
};

#ifdef CONFIG_VIDEO_INTEL_IPU6
#define NR_OF_DS5_PADS 7
#define NR_OF_DS5_STREAMS 4
struct v4l2_mbus_framefmt ds5_ffmts[NR_OF_DS5_PADS];
#endif

struct ds5 {
	struct { struct ds5_sensor sensor; } depth;
	struct { struct ds5_sensor sensor; } ir;
	struct { struct ds5_sensor sensor; } rgb;
	struct { struct ds5_sensor sensor; } imu;
	struct {
		struct ds5_mux_subdev sd;
		struct media_pad pads[DS5_MUX_PAD_COUNT];
		struct ds5_sensor *last_set;
	} mux;
	struct ds5_ctrls ctrls;
	struct ds5_dfu_dev dfu_dev;
	bool power;
	struct i2c_client *client;
	/*struct ds5_vchan virtual_channels[CSI2_MAX_VIRTUAL_CHANNELS];*/
	/* All below pointers are used for writing, cannot be const */
	struct mutex lock;
	struct regmap *regmap;
#ifndef CONFIG_VIDEO_D4XX_SERDES
	struct regmap *regmap_max9296;
	struct regmap *regmap_max9295;
#endif
	struct regulator *vcc;
	const struct ds5_variant *variant;
	int is_depth, is_y8, is_rgb, is_imu;
	int aggregated;
	u16 fw_version;
	u16 fw_build;
#ifdef CONFIG_VIDEO_D4XX_SERDES
	struct gmsl_link_ctx g_ctx;
	struct device *ser_dev;
	struct device *dser_dev;
	struct i2c_client *ser_i2c;
	struct i2c_client *dser_i2c;
#endif
#ifdef CONFIG_VIDEO_INTEL_IPU6
#define NR_OF_CSI2_BE_SOC_STREAMS	16
#define NR_OF_DS5_SUB_STREAMS	6 /*d+d.md,c+c.md,ir,imu*/
	int pad_to_vc[DS5_MUX_PAD_COUNT];
	int pad_to_substream[NR_OF_CSI2_BE_SOC_STREAMS];
#endif
};

struct ds5_counters {
	unsigned int n_res;
	unsigned int n_fmt;
	unsigned int n_ctrl;
};

#define ds5_from_depth_sd(sd) container_of(sd, struct ds5, depth.sd)
#define ds5_from_ir_sd(sd) container_of(sd, struct ds5, ir.sd)
#define ds5_from_rgb_sd(sd) container_of(sd, struct ds5, rgb.sd)
#ifdef CONFIG_VIDEO_INTEL_IPU6
static inline void msleep_range(unsigned int delay_base)
{
	usleep_range(delay_base * 1000, delay_base * 1000 + 500);
}
#ifndef CONFIG_VIDEO_D4XX_SERDES
static int max9296_write_8(struct ds5 *state, u16 reg, u8 val)
{
	int ret;

	ret = regmap_raw_write(state->regmap_max9296, reg, &val, 1);
	if (ret < 0)
		dev_err(&state->client->dev, "%s(): i2c write failed %d, 0x%04x = 0x%x\n",
			__func__, ret, reg, val);
	else
		if (state->dfu_dev.dfu_state_flag == DS5_DFU_IDLE)
			dev_dbg(&state->client->dev, "%s(): i2c write 0x%04x: 0x%x\n",
				 __func__, reg, val);

	dev_dbg(&state->client->dev, "%s(): (%d), 0x%02x 0x%04x = 0x%02x\n",
		__func__, ret, state->client->addr, reg, val);

	return ret;
}

static int max9296_read_8(struct ds5 *state, u16 reg, u8 *val)
{
	int ret;

	ret = regmap_raw_read(state->regmap_max9296, reg, val, 1);
	if (ret < 0)
		dev_err(&state->client->dev, "%s(): i2c read failed %d, 0x%04x\n",
			__func__, ret, reg);
	else
		if (state->dfu_dev.dfu_state_flag == DS5_DFU_IDLE)
			dev_info(&state->client->dev, "%s(): i2c read 0x%04x = 0x%x\n",
				 __func__, reg, *val);

	dev_dbg(&state->client->dev, "%s(): (%d), 0x%02x 0x%04x = 0x%02x\n",
		__func__, ret, state->client->addr, reg, *val);

	return ret;
}
static int max9295_write_8(struct ds5 *state, u16 reg, u8 val)
{
	int ret;

	ret = regmap_raw_write(state->regmap_max9295, reg, &val, 1);
	if (ret < 0)
		dev_err(&state->client->dev, "%s(): i2c write failed %d, 0x%04x = 0x%x\n",
			__func__, ret, reg, val);
	else
		if (state->dfu_dev.dfu_state_flag == DS5_DFU_IDLE)
			dev_info(&state->client->dev, "%s(): i2c write 0x%04x: 0x%x\n",
				 __func__, reg, val);

	dev_dbg(&state->client->dev, "%s(): (%d), 0x%02x 0x%04x = 0x%02x\n",
		__func__, ret, state->client->addr, reg, val);

	return ret;
}

static int max9295_read_8(struct ds5 *state, u16 reg, u8 *val)
{
	int ret;

	ret = regmap_raw_read(state->regmap_max9295, reg, val, 1);
	if (ret < 0)
		dev_err(&state->client->dev, "%s(): i2c read failed %d, 0x%04x\n",
			__func__, ret, reg);
	else
		if (state->dfu_dev.dfu_state_flag == DS5_DFU_IDLE)
			dev_info(&state->client->dev, "%s(): i2c read 0x%04x = 0x%x\n",
				 __func__, reg, *val);

	dev_dbg(&state->client->dev, "%s(): (%d), 0x%02x 0x%04x = 0x%02x\n",
		__func__, ret, state->client->addr, reg, *val);

	return ret;
}

#else
static int ds5_write_8(struct ds5 *state, u16 reg, u8 val)
{
	int ret;

	ret = regmap_raw_write(state->regmap, reg, &val, 1);
	if (ret < 0)
		dev_err(&state->client->dev, "%s(): i2c write failed %d, 0x%04x = 0x%x\n",
			__func__, ret, reg, val);
	else
		if (state->dfu_dev.dfu_state_flag == DS5_DFU_IDLE)
			dev_dbg(&state->client->dev, "%s(): i2c write 0x%04x: 0x%x\n",
				 __func__, reg, val);

	return ret;
}
#endif
#endif

static int ds5_write(struct ds5 *state, u16 reg, u16 val)
{
	int ret;
	u8 value[2];

	value[1] = val >> 8;
	value[0] = val & 0x00FF;

	dev_dbg(&state->client->dev,
			"%s(): writing to register: 0x%04x, value1: 0x%x, value2:0x%x\n",
			__func__, reg, value[1], value[0]);

	ret = regmap_raw_write(state->regmap, reg, value, sizeof(value));
	if (ret < 0)
		dev_err(&state->client->dev,
				"%s(): i2c write failed %d, 0x%04x = 0x%x\n",
				__func__, ret, reg, val);
	else
		if (state->dfu_dev.dfu_state_flag == DS5_DFU_IDLE)
			dev_dbg(&state->client->dev, "%s(): i2c write 0x%04x: 0x%x\n",
				__func__, reg, val);

	return ret;
}

static int ds5_raw_write(struct ds5 *state, u16 reg,
		const void *val, size_t val_len)
{
	int ret = regmap_raw_write(state->regmap, reg, val, val_len);
	if (ret < 0)
		dev_err(&state->client->dev,
				"%s(): i2c raw write failed %d, %04x size(%d) bytes\n",
				__func__, ret, reg, (int)val_len);
	else
		if (state->dfu_dev.dfu_state_flag == DS5_DFU_IDLE)
			dev_dbg(&state->client->dev,
					"%s(): i2c raw write 0x%04x: %d bytes\n",
					__func__, reg, (int)val_len);

	return ret;
}

static int ds5_read(struct ds5 *state, u16 reg, u16 *val)
{
	int ret = regmap_raw_read(state->regmap, reg, val, 2);
	if (ret < 0)
		dev_err(&state->client->dev, "%s(): i2c read failed %d, 0x%04x\n",
				__func__, ret, reg);
	else {
		if (state->dfu_dev.dfu_state_flag == DS5_DFU_IDLE)
			dev_dbg(&state->client->dev, "%s(): i2c read 0x%04x: 0x%x\n",
					__func__, reg, *val);
	}

	return ret;
}

static int ds5_raw_read(struct ds5 *state, u16 reg, void *val, size_t val_len)
{
	int ret = regmap_raw_read(state->regmap, reg, val, val_len);
	if (ret < 0)
		dev_err(&state->client->dev, "%s(): i2c read failed %d, 0x%04x\n",
			__func__, ret, reg);

	return ret;
}

#ifdef CONFIG_VIDEO_INTEL_IPU6
static s64 d4xx_query_sub_stream[NR_OF_CSI2_BE_SOC_STREAMS];
static u8 d4xx_set_sub_stream[NR_OF_CSI2_BE_SOC_STREAMS];
static void set_sub_stream_fmt(int index, u32 code)
{
	d4xx_query_sub_stream[index] &= 0xFFFFFFFFFFFF0000;
	d4xx_query_sub_stream[index] |= code;
}

static void set_sub_stream_h(int index, u32 height)
{
	s64 val = height;

	val &= 0xFFFF;
	d4xx_query_sub_stream[index] &= 0xFFFFFFFF0000FFFF;
	d4xx_query_sub_stream[index] |= val << 16;
}

static void set_sub_stream_w(int index, u32 width)
{
	s64 val = width;

	val &= 0xFFFF;
	d4xx_query_sub_stream[index] &= 0xFFFF0000FFFFFFFF;
	d4xx_query_sub_stream[index] |= val << 32;
}

static void set_sub_stream_dt(int index, u32 dt)
{
	s64 val = dt;

	val &= 0xFF;
	d4xx_query_sub_stream[index] &= 0xFF00FFFFFFFFFFFF;
	d4xx_query_sub_stream[index] |= val << 48;
}

static void set_sub_stream_vc_id(int index, u32 vc_id)
{
	s64 val = vc_id;

	val &= 0xFF;
	d4xx_query_sub_stream[index] &= 0x00FFFFFFFFFFFFFF;
	d4xx_query_sub_stream[index] |= val << 56;
}

static int get_sub_stream_vc_id(int index)
{
	s64 val = 0;

	val = d4xx_query_sub_stream[index] >> 56;
	val &= 0xFF;
	return (int)val;
}
#endif

/* Pad ops */

static const u16 ds5_default_framerate = 30;

// **********************
// FIXME: D16 width must be doubled, because an 8-bit format is used. Check how
// the Tegra driver propagates resolutions and formats.
// **********************

//TODO: keep 6, till 5 is supported by FW
static const u16 ds5_framerates[] = {5, 30};

#define DS5_FRAMERATE_DEFAULT_IDX 1

static const u16 ds5_framerate_30 = 30;

static const u16 ds5_framerate_15_30[] = {15, 30};

static const u16 ds5_framerate_25 = 25;

static const u16 ds5_depth_framerate_to_30[] = {5, 15, 30};
static const u16 ds5_framerate_to_30[] = {5, 10, 15, 30};
static const u16 ds5_framerate_to_60[] = {5, 15, 30, 60};
static const u16 ds5_framerate_to_90[] = {5, 15, 30, 60, 90};
static const u16 ds5_framerate_100[] = {100};
static const u16 ds5_framerate_90[] = {90};
static const u16 ds5_imu_framerates[] = {50, 100, 200, 400};

static const struct ds5_resolution d43x_depth_sizes[] = {
	{
		.width = 1280,
		.height = 720,
		.framerates = ds5_depth_framerate_to_30,
		.n_framerates = ARRAY_SIZE(ds5_depth_framerate_to_30),
	}, {
		.width =  848,
		.height = 480,
		.framerates = ds5_framerate_to_90,
		.n_framerates = ARRAY_SIZE(ds5_framerate_to_90),
	}, {
		.width =  848,
		.height = 100,
		.framerates = ds5_framerate_100,
		.n_framerates = ARRAY_SIZE(ds5_framerate_100),
	}, {
		.width =  640,
		.height = 480,
		.framerates = ds5_framerate_to_90,
		.n_framerates = ARRAY_SIZE(ds5_framerate_to_90),
	}, {
		.width =  640,
		.height = 360,
		.framerates = ds5_framerate_to_90,
		.n_framerates = ARRAY_SIZE(ds5_framerate_to_90),
	}, {
		.width =  480,
		.height = 270,
		.framerates = ds5_framerate_to_90,
		.n_framerates = ARRAY_SIZE(ds5_framerate_to_90),
	}, {
		.width =  424,
		.height = 240,
		.framerates = ds5_framerate_to_90,
		.n_framerates = ARRAY_SIZE(ds5_framerate_to_90),
	}, {
		.width =  256,
		.height = 144,
		.framerates = ds5_framerate_90,
		.n_framerates = ARRAY_SIZE(ds5_framerate_90),
	},
};

static const struct ds5_resolution d46x_depth_sizes[] = {
	{
		.width = 1280,
		.height = 960,
		.framerates = ds5_framerates,
		.n_framerates = ARRAY_SIZE(ds5_framerates),
	}, {
		.width =  640,
		.height = 480,
		.framerates = ds5_framerates,
		.n_framerates = ARRAY_SIZE(ds5_framerates),
	},
};

static const struct ds5_resolution y8_sizes[] = {
	{
		.width = 1280,
		.height = 720,
		.framerates = ds5_depth_framerate_to_30,
		.n_framerates = ARRAY_SIZE(ds5_depth_framerate_to_30),
	}, {
		.width =  848,
		.height = 480,
		.framerates = ds5_framerate_to_90,
		.n_framerates = ARRAY_SIZE(ds5_framerate_to_90),
	}, {
		.width =  640,
		.height = 480,
		.framerates = ds5_framerate_to_90,
		.n_framerates = ARRAY_SIZE(ds5_framerate_to_90),
	}, {
		.width =  640,
		.height = 360,
		.framerates = ds5_framerate_to_90,
		.n_framerates = ARRAY_SIZE(ds5_framerate_to_90),
	}, {
		.width =  480,
		.height = 270,
		.framerates = ds5_framerate_to_90,
		.n_framerates = ARRAY_SIZE(ds5_framerate_to_90),
	}, {
		.width =  424,
		.height = 240,
		.framerates = ds5_framerate_to_90,
		.n_framerates = ARRAY_SIZE(ds5_framerate_to_90),
	}
};

static const struct ds5_resolution ds5_rlt_rgb_sizes[] = {
	{
		.width = 1280,
		.height = 800,
		.framerates = ds5_framerate_to_30,
		.n_framerates = ARRAY_SIZE(ds5_framerate_to_30),
	}, {
		.width = 1280,
		.height = 720,
		.framerates = ds5_framerate_to_30,
		.n_framerates = ARRAY_SIZE(ds5_framerate_to_30),
	}, {
		.width = 848,
		.height = 480,
		.framerates = ds5_framerate_to_60,
		.n_framerates = ARRAY_SIZE(ds5_framerate_to_60),
	}, {
		.width = 640,
		.height = 480,
		.framerates = ds5_framerate_to_60,
		.n_framerates = ARRAY_SIZE(ds5_framerate_to_60),
	}, {
		.width = 640,
		.height = 360,
		.framerates = ds5_framerate_to_90,
		.n_framerates = ARRAY_SIZE(ds5_framerate_to_90),
	}, {
		.width = 480,
		.height = 270,
		.framerates = ds5_framerate_to_90,
		.n_framerates = ARRAY_SIZE(ds5_framerate_to_90),
	}, {
		.width = 424,
		.height = 240,
		.framerates = ds5_framerate_to_90,
		.n_framerates = ARRAY_SIZE(ds5_framerate_to_90),
	},
};

static const struct ds5_resolution ds5_onsemi_rgb_sizes[] = {
	{
		.width = 640,
		.height = 480,
		.framerates = ds5_framerate_to_90,
		.n_framerates = ARRAY_SIZE(ds5_framerate_to_90),
	}, {
		.width = 960,
		.height = 720,
		.framerates = ds5_framerate_to_60,
		.n_framerates = ARRAY_SIZE(ds5_framerate_to_60),
	}, {
		.width = 1280,
		.height = 720,
		.framerates = ds5_framerates,
		.n_framerates = ARRAY_SIZE(ds5_framerates),
	}, {
		.width = 1920,
		.height = 1080,
		.framerates = ds5_framerates,
		.n_framerates = ARRAY_SIZE(ds5_framerates),
	}, {
		.width = 2048,
		.height = 1536,
		.framerates = ds5_framerates,
		.n_framerates = ARRAY_SIZE(ds5_framerates),
	},
};

static const struct ds5_resolution ds5_size_w10 = {
	.width =  1920,
	.height = 1080,
	.framerates = &ds5_framerate_30,
	.n_framerates = 1,
};

static const struct ds5_resolution d43x_calibration_sizes[] = {
	{
		.width =  1280,
		.height = 800,
		.framerates = ds5_framerate_15_30,
		.n_framerates = ARRAY_SIZE(ds5_framerate_15_30),
	},
};

static const struct ds5_resolution d46x_calibration_sizes[] = {
	{
		.width =  1600,
		.height = 1300,
		.framerates = ds5_framerate_15_30,
		.n_framerates = ARRAY_SIZE(ds5_framerate_15_30),
	},
};

static const struct ds5_resolution ds5_size_imu[] = {
	{
	.width = 32,
	.height = 1,
	.framerates = ds5_imu_framerates,
	.n_framerates = ARRAY_SIZE(ds5_imu_framerates),
	},
};

// 32 bit IMU introduced with IMU sensitivity attribute Firmware
static const struct ds5_resolution ds5_size_imu_extended[] = {
	{
	.width = 38,
	.height = 1,
	.framerates = ds5_imu_framerates,
	.n_framerates = ARRAY_SIZE(ds5_imu_framerates),
	},
};

static const struct ds5_format ds5_depth_formats_d43x[] = {
	{
		// TODO: 0x31 is replaced with 0x1e since it caused low FPS in Jetson.
		.data_type = GMSL_CSI_DT_YUV422_8,	/* Z16 */
		.mbus_code = MEDIA_BUS_FMT_UYVY8_1X16,
		.n_resolutions = ARRAY_SIZE(d43x_depth_sizes),
		.resolutions = d43x_depth_sizes,
	}, {
		.data_type = GMSL_CSI_DT_RAW_8,	/* Y8 */
		.mbus_code = MEDIA_BUS_FMT_Y8_1X8,
		.n_resolutions = ARRAY_SIZE(d43x_depth_sizes),
		.resolutions = d43x_depth_sizes,
	}, {
		.data_type = GMSL_CSI_DT_RGB_888,	/* 24-bit Calibration */
		.mbus_code = MEDIA_BUS_FMT_RGB888_1X24,	/* FIXME */
		.n_resolutions = ARRAY_SIZE(d43x_calibration_sizes),
		.resolutions = d43x_calibration_sizes,
	},
};

static const struct ds5_format ds5_depth_formats_d46x[] = {
	{
		// TODO: 0x31 is replaced with 0x1e since it caused low FPS in Jetson.
		.data_type = GMSL_CSI_DT_YUV422_8,	/* Z16 */
		.mbus_code = MEDIA_BUS_FMT_UYVY8_1X16,
		.n_resolutions = ARRAY_SIZE(d46x_depth_sizes),
		.resolutions = d46x_depth_sizes,
	}, {
		/* First format: default */
		.data_type = GMSL_CSI_DT_RAW_8,	/* Y8 */
		.mbus_code = MEDIA_BUS_FMT_Y8_1X8,
		.n_resolutions = ARRAY_SIZE(d46x_depth_sizes),
		.resolutions = d46x_depth_sizes,
	}, {
		.data_type = GMSL_CSI_DT_RGB_888,	/* 24-bit Calibration */
		.mbus_code = MEDIA_BUS_FMT_RGB888_1X24,	/* FIXME */
		.n_resolutions = ARRAY_SIZE(d46x_calibration_sizes),
		.resolutions = d46x_calibration_sizes,
	},
};

#define DS5_DEPTH_N_FORMATS 1

static const struct ds5_format ds5_y_formats_ds5u[] = {
	{
		/* First format: default */
		.data_type = GMSL_CSI_DT_RAW_8,	/* Y8 */
		.mbus_code = MEDIA_BUS_FMT_Y8_1X8,
		.n_resolutions = ARRAY_SIZE(y8_sizes),
		.resolutions = y8_sizes,
	}, {
		.data_type = GMSL_CSI_DT_YUV422_8,	/* Y8I */
		.mbus_code = MEDIA_BUS_FMT_VYUY8_1X16,
		.n_resolutions = ARRAY_SIZE(y8_sizes),
		.resolutions = y8_sizes,
	}, {
		.data_type = GMSL_CSI_DT_RGB_888,	/* 24-bit Calibration */
		.mbus_code = MEDIA_BUS_FMT_RGB888_1X24,	/* FIXME */
		.n_resolutions = ARRAY_SIZE(d43x_calibration_sizes),
		.resolutions = d43x_calibration_sizes,
	},
};

static const struct ds5_format ds5_rlt_rgb_format = {
	.data_type = GMSL_CSI_DT_YUV422_8,	/* UYVY */
	.mbus_code = MEDIA_BUS_FMT_YUYV8_1X16,
	.n_resolutions = ARRAY_SIZE(ds5_rlt_rgb_sizes),
	.resolutions = ds5_rlt_rgb_sizes,
};
#define DS5_RLT_RGB_N_FORMATS 1

static const struct ds5_format ds5_onsemi_rgb_format = {
	.data_type = GMSL_CSI_DT_YUV422_8,	/* UYVY */
	.mbus_code = MEDIA_BUS_FMT_YUYV8_1X16,
	.n_resolutions = ARRAY_SIZE(ds5_onsemi_rgb_sizes),
	.resolutions = ds5_onsemi_rgb_sizes,
};
#define DS5_ONSEMI_RGB_N_FORMATS 1

static const struct ds5_variant ds5_variants[] = {
	[DS5_DS5U] = {
		.formats = ds5_y_formats_ds5u,
		.n_formats = ARRAY_SIZE(ds5_y_formats_ds5u),
	},
};

static const struct ds5_format ds5_imu_formats[] = {
	{
		/* First format: default */
		.data_type = GMSL_CSI_DT_RAW_8,	/* IMU DT */
		.mbus_code = MEDIA_BUS_FMT_Y8_1X8,
		.n_resolutions = ARRAY_SIZE(ds5_size_imu),
		.resolutions = ds5_size_imu,
	},
};

static const struct ds5_format ds5_imu_formats_extended[] = {
	{
		/* First format: default */
		.data_type = GMSL_CSI_DT_RAW_8,	/* IMU DT */
		.mbus_code = MEDIA_BUS_FMT_Y8_1X8,
		.n_resolutions = ARRAY_SIZE(ds5_size_imu_extended),
		.resolutions = ds5_size_imu_extended,
	},
};

static const struct v4l2_mbus_framefmt ds5_mbus_framefmt_template = {
	.width = 0,
	.height = 0,
	.code = MEDIA_BUS_FMT_FIXED,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_DEFAULT,
	.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT,
	.quantization = V4L2_QUANTIZATION_DEFAULT,
	.xfer_func = V4L2_XFER_FUNC_DEFAULT,
};

/* Get readable sensor name */
static const char *ds5_get_sensor_name(struct ds5 *state)
{
	static const char *sensor_name[] = {"unknown", "RGB", "DEPTH", "Y8", "IMU"};
	int sensor_id = state->is_rgb * 1 + state->is_depth * 2 + \
			state->is_y8 * 3 + state->is_imu * 4;
	if (sensor_id >= (sizeof(sensor_name)/sizeof(*sensor_name)))
		sensor_id = 0;

	return sensor_name[sensor_id];
}

static void ds5_set_state_last_set(struct ds5 *state)
{
	 dev_dbg(&state->client->dev, "%s(): %s\n",
		__func__, ds5_get_sensor_name(state));

	if (state->is_depth)
		state->mux.last_set = &state->depth.sensor;
	else if (state->is_rgb)
		state->mux.last_set = &state->rgb.sensor;
	else if (state->is_y8)
		state->mux.last_set = &state->ir.sensor;
	else
		state->mux.last_set = &state->imu.sensor;
}

/* This is needed for .get_fmt()
 * and if streaming is started without .set_fmt()
 */
static void ds5_sensor_format_init(struct ds5_sensor *sensor)
{
	const struct ds5_format *fmt;
	struct v4l2_mbus_framefmt *ffmt;
	unsigned int i;

	if (sensor->config.format)
		return;

	dev_dbg(sensor->sd.dev, "%s(): on pad %u\n", __func__, sensor->mux_pad);

	ffmt = &sensor->format;
	*ffmt = ds5_mbus_framefmt_template;
	/* Use the first format */
	fmt = sensor->formats;
	ffmt->code = fmt->mbus_code;
	/* and the first resolution */
	ffmt->width = fmt->resolutions->width;
	ffmt->height = fmt->resolutions->height;

	sensor->config.format = fmt;
	sensor->config.resolution = fmt->resolutions;
	/* Set default framerate to 30, or to 1st one if not supported */
	for (i = 0; i < fmt->resolutions->n_framerates; i++) {
		if (fmt->resolutions->framerates[i] == ds5_framerate_30 /* fps */) {
			sensor->config.framerate = ds5_framerate_30;
			return;
		}
	}
	sensor->config.framerate = fmt->resolutions->framerates[0];
}

/* No locking needed for enumeration methods */
static int ds5_sensor_enum_mbus_code(struct v4l2_subdev *sd,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
				     struct v4l2_subdev_pad_config *cfg,
#else
				     struct v4l2_subdev_state *v4l2_state,
#endif
				     struct v4l2_subdev_mbus_code_enum *mce)
{
	struct ds5_sensor *sensor = container_of(sd, struct ds5_sensor, sd);

	dev_dbg(sensor->sd.dev, "%s(): sensor %s pad: %d index: %d\n",
		__func__, sensor->sd.name, mce->pad, mce->index);
	if (mce->pad)
		return -EINVAL;

	if (mce->index >= sensor->n_formats)
		return -EINVAL;

	mce->code = sensor->formats[mce->index].mbus_code;

	return 0;
}

static int ds5_sensor_enum_frame_size(struct v4l2_subdev *sd,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
				     struct v4l2_subdev_pad_config *cfg,
#else
				     struct v4l2_subdev_state *v4l2_state,
#endif
				      struct v4l2_subdev_frame_size_enum *fse)
{
	struct ds5_sensor *sensor = container_of(sd, struct ds5_sensor, sd);
	struct ds5 *state = v4l2_get_subdevdata(sd);
	const struct ds5_format *fmt;
	unsigned int i;

	dev_dbg(sensor->sd.dev, "%s(): sensor %s is %s\n",
		__func__, sensor->sd.name, ds5_get_sensor_name(state));

	for (i = 0, fmt = sensor->formats; i < sensor->n_formats; i++, fmt++)
		if (fse->code == fmt->mbus_code)
			break;

	if (i == sensor->n_formats)
		return -EINVAL;

	if (fse->index >= fmt->n_resolutions)
		return -EINVAL;

	fse->min_width = fse->max_width = fmt->resolutions[fse->index].width;
	fse->min_height = fse->max_height = fmt->resolutions[fse->index].height;

	return 0;
}

static int ds5_sensor_enum_frame_interval(struct v4l2_subdev *sd,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
				     struct v4l2_subdev_pad_config *cfg,
#else
				     struct v4l2_subdev_state *v4l2_state,
#endif
					  struct v4l2_subdev_frame_interval_enum *fie)
{
	struct ds5_sensor *sensor = container_of(sd, struct ds5_sensor, sd);
	const struct ds5_format *fmt;
	const struct ds5_resolution *res;
	unsigned int i;

	for (i = 0, fmt = sensor->formats; i < sensor->n_formats; i++, fmt++)
		if (fie->code == fmt->mbus_code)
			break;

	if (i == sensor->n_formats)
		return -EINVAL;

	for (i = 0, res = fmt->resolutions; i < fmt->n_resolutions; i++, res++)
		if (res->width == fie->width && res->height == fie->height)
			break;

	if (i == fmt->n_resolutions)
		return -EINVAL;

	if (fie->index >= res->n_framerates)
		return -EINVAL;

	fie->interval.numerator = 1;
	fie->interval.denominator = res->framerates[fie->index];

	return 0;
}

static int ds5_sensor_get_fmt(struct v4l2_subdev *sd,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
				     struct v4l2_subdev_pad_config *cfg,
#else
				     struct v4l2_subdev_state *v4l2_state,
#endif
			      struct v4l2_subdev_format *fmt)
{
	struct ds5_sensor *sensor = container_of(sd, struct ds5_sensor, sd);
	struct ds5 *state = v4l2_get_subdevdata(sd);

	if (fmt->pad)
		return -EINVAL;

	mutex_lock(&state->lock);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
		fmt->format = *v4l2_subdev_get_try_format(sd, v4l2_state, fmt->pad);
#else
		fmt->format = *v4l2_subdev_state_get_format(v4l2_state, fmt->pad);
#endif
	else
		fmt->format = sensor->format;

	mutex_unlock(&state->lock);

	dev_dbg(sd->dev, "%s(): pad %x, code %x, res %ux%u\n",
			__func__, fmt->pad, fmt->format.code,
			fmt->format.width, fmt->format.height);

	return 0;
}

/* Called with lock held */
static const struct ds5_format *ds5_sensor_find_format(
		struct ds5_sensor *sensor,
		struct v4l2_mbus_framefmt *ffmt,
		const struct ds5_resolution **best)
{
	const struct ds5_resolution *res;
	const struct ds5_format *fmt;
	unsigned long best_delta = ~0;
	unsigned int i;

	for (i = 0, fmt = sensor->formats; i < sensor->n_formats; i++, fmt++) {
		if (fmt->mbus_code == ffmt->code)
			break;
	}
	dev_dbg(sensor->sd.dev, "%s(): mbus_code = %x, code = %x \n",
		__func__, fmt->mbus_code, ffmt->code);

	if (i == sensor->n_formats) {
		/* Not found, use default */
		dev_dbg(sensor->sd.dev, "%s:%d Not found, use default\n",
			__func__, __LINE__);
		fmt = sensor->formats;
	}
	for (i = 0, res = fmt->resolutions; i < fmt->n_resolutions; i++, res++) {
		unsigned long delta = abs(ffmt->width * ffmt->height -
				res->width * res->height);
		if (delta < best_delta) {
			best_delta = delta;
			*best = res;
		}
	}

	ffmt->code = fmt->mbus_code;
	ffmt->width = (*best)->width;
	ffmt->height = (*best)->height;

	ffmt->field = V4L2_FIELD_NONE;
	/* Should we use V4L2_COLORSPACE_RAW for Y12I? */
	ffmt->colorspace = V4L2_COLORSPACE_SRGB;

	return fmt;
}

#define MIPI_CSI2_TYPE_NULL	0x10
#define MIPI_CSI2_TYPE_BLANKING		0x11
#define MIPI_CSI2_TYPE_EMBEDDED8	0x12
#define MIPI_CSI2_TYPE_YUV422_8		0x1e
#define MIPI_CSI2_TYPE_YUV422_10	0x1f
#define MIPI_CSI2_TYPE_RGB565	0x22
#define MIPI_CSI2_TYPE_RGB888	0x24
#define MIPI_CSI2_TYPE_RAW6	0x28
#define MIPI_CSI2_TYPE_RAW7	0x29
#define MIPI_CSI2_TYPE_RAW8	0x2a
#define MIPI_CSI2_TYPE_RAW10	0x2b
#define MIPI_CSI2_TYPE_RAW12	0x2c
#define MIPI_CSI2_TYPE_RAW14	0x2d
/* 1-8 */
#define MIPI_CSI2_TYPE_USER_DEF(i)	(0x30 + (i) - 1)
#ifdef CONFIG_VIDEO_INTEL_IPU6
static unsigned int mbus_code_to_mipi(u32 code)
{
	switch (code) {
	case MEDIA_BUS_FMT_RGB565_1X16:
		return MIPI_CSI2_TYPE_RGB565;
	case MEDIA_BUS_FMT_RGB888_1X24:
		return MIPI_CSI2_TYPE_RGB888;
	case MEDIA_BUS_FMT_YUYV10_1X20:
		return MIPI_CSI2_TYPE_YUV422_10;
	case MEDIA_BUS_FMT_UYVY8_1X16:
	case MEDIA_BUS_FMT_YUYV8_1X16:
	case MEDIA_BUS_FMT_VYUY8_1X16:
		return MIPI_CSI2_TYPE_YUV422_8;
	case MEDIA_BUS_FMT_SBGGR12_1X12:
	case MEDIA_BUS_FMT_SGBRG12_1X12:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
	case MEDIA_BUS_FMT_SRGGB12_1X12:
		return MIPI_CSI2_TYPE_RAW12;
	case MEDIA_BUS_FMT_Y10_1X10:
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
		return MIPI_CSI2_TYPE_RAW10;
	case MEDIA_BUS_FMT_Y8_1X8:
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SRGGB8_1X8:
		return MIPI_CSI2_TYPE_RAW8;
	case MEDIA_BUS_FMT_SBGGR10_DPCM8_1X8:
	case MEDIA_BUS_FMT_SGBRG10_DPCM8_1X8:
	case MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8:
	case MEDIA_BUS_FMT_SRGGB10_DPCM8_1X8:
		return MIPI_CSI2_TYPE_USER_DEF(1);
	default:
		WARN_ON(1);
		return -EINVAL;
	}
}
#endif

#ifdef CONFIG_VIDEO_INTEL_IPU6
static int ds5_s_state_pad(struct ds5 *state, int pad)
{
	int ret = 0;

	dev_dbg(&state->client->dev, "%s(): set state for pad: %d\n", __func__, pad);

	switch (pad) {
	case DS5_MUX_PAD_DEPTH:
		state->is_depth = 1;
		state->is_rgb = 0;
		state->is_y8 = 0;
		state->is_imu = 0;
		break;
	case DS5_MUX_PAD_RGB:
		state->is_depth = 0;
		state->is_rgb = 1;
		state->is_y8 = 0;
		state->is_imu = 0;
		break;
	case DS5_MUX_PAD_IR:
		state->is_depth = 0;
		state->is_rgb = 0;
		state->is_y8 = 1;
		state->is_imu = 0;
		break;
	case DS5_MUX_PAD_IMU:
		state->is_depth = 0;
		state->is_rgb = 0;
		state->is_y8 = 0;
		state->is_imu = 1;
		break;
	default:
		dev_warn(&state->client->dev, "%s(): unknown pad: %d\n", __func__, pad);
		ret = -EINVAL;
		break;
	}
	ds5_set_state_last_set(state);
	return ret;
}

static int ds5_s_state(struct ds5 *state, int vc)
{
	int ret = 0;
	int i = 0;
	int pad = 0;
	for (i = 0; i < ARRAY_SIZE(state->pad_to_vc); i++) {
		if (state->pad_to_vc[i] == vc) {
			pad = i;
			break;
		}
	}

	dev_info(&state->client->dev, "%s(): set state for vc: %d on pad: %d\n", __func__, vc, pad);

	ret = ds5_s_state_pad(state, pad);
	return ret;
}

#endif

static int __ds5_sensor_set_fmt(struct ds5 *state, struct ds5_sensor *sensor,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
				     struct v4l2_subdev_pad_config *cfg,
#else
				     struct v4l2_subdev_state *v4l2_state,
#endif
				struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *mf;// = &fmt->format;
#ifdef CONFIG_VIDEO_INTEL_IPU6
	int substream = -1;
#endif
	//unsigned r;

	dev_dbg(sensor->sd.dev, "%s(): state %p, "
		"sensor %p, fmt %p, fmt->format %p\n",
		__func__, state, sensor, fmt,  &fmt->format);

	mf = &fmt->format;

	if (fmt->pad)
		return -EINVAL;

	mutex_lock(&state->lock);

	sensor->config.format = ds5_sensor_find_format(sensor, mf,
						&sensor->config.resolution);
	//r = DS5_FRAMERATE_DEFAULT_IDX < sensor->config.resolution->n_framerates ?
	//	DS5_FRAMERATE_DEFAULT_IDX : 0;
	/* FIXME: check if a framerate has been set */
	//sensor->config.framerate = sensor->config.resolution->framerates[r];

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
	if (cfg && fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		*v4l2_subdev_get_try_format(&sensor->sd, cfg, fmt->pad) = *mf;
#elif LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
	if (v4l2_state && fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		*v4l2_subdev_get_try_format(&sensor->sd, v4l2_state, fmt->pad) = *mf;
#else
	if (v4l2_state && fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		*v4l2_subdev_state_get_format(v4l2_state, fmt->pad) = *mf;
#endif

	else
// FIXME: use this format in .s_stream()
		sensor->format = *mf;

	state->mux.last_set = sensor;

	mutex_unlock(&state->lock);
#ifdef CONFIG_VIDEO_INTEL_IPU6
	substream = state->pad_to_substream[sensor->mux_pad];

	if (substream != -1) {
		set_sub_stream_fmt(substream, mf->code);
		set_sub_stream_h(substream, mf->height);
		set_sub_stream_w(substream, mf->width);
		set_sub_stream_dt(substream, mbus_code_to_mipi(mf->code));
	}

	dev_dbg(sensor->sd.dev, "%s(): fmt->pad: %d, sensor->mux_pad: %d, code: 0x%x, %ux%u substream:%d\n", __func__,
		fmt->pad, sensor->mux_pad, fmt->format.code,
		fmt->format.width, fmt->format.height, substream);
#endif
	return 0;
}

static int ds5_sensor_set_fmt(struct v4l2_subdev *sd,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
				     struct v4l2_subdev_pad_config *cfg,
#else
				     struct v4l2_subdev_state *v4l2_state,
#endif
			      struct v4l2_subdev_format *fmt)
{
	struct ds5_sensor *sensor = container_of(sd, struct ds5_sensor, sd);
	struct ds5 *state = v4l2_get_subdevdata(sd);
#ifdef CONFIG_VIDEO_INTEL_IPU6
	/* set state by vc */
	ds5_s_state_pad(state, sensor->mux_pad);
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
	return __ds5_sensor_set_fmt(state, sensor, cfg, fmt);
#else
	return __ds5_sensor_set_fmt(state, sensor, v4l2_state, fmt);
#endif
}

#ifdef CONFIG_VIDEO_D4XX_SERDES
static int ds5_setup_pipeline(struct ds5 *state, u8 data_type1, u8 data_type2,
			      int pipe_id, u32 vc_id)
{
	int ret = 0;
	dev_dbg(&state->client->dev,
			 "set pipe %d, data_type1: 0x%x, \
			 data_type2: 0x%x, vc_id: %u\n",
			 pipe_id, data_type1, data_type2, vc_id);
	ret |= max9295_set_pipe(state->ser_dev, pipe_id,
				data_type1, data_type2, vc_id);
	ret |= max9296_set_pipe(state->dser_dev, pipe_id,
				data_type1, data_type2, vc_id);
	if (ret)
		dev_warn(&state->client->dev,
			 "failed to set pipe %d, data_type1: 0x%x, \
			 data_type2: 0x%x, vc_id: %u\n",
			 pipe_id, data_type1, data_type2, vc_id);

	return ret;
}
#endif

static int ds5_configure(struct ds5 *state)
{
	struct ds5_sensor *sensor;
	u16 fmt, md_fmt, vc_id;
#ifdef CONFIG_VIDEO_D4XX_SERDES
	u16 data_type1, data_type2;
#endif
	u16 dt_addr, md_addr, override_addr, fps_addr, width_addr, height_addr;
	int ret;

	if (state->is_depth) {
		sensor = &state->depth.sensor;
		dt_addr = DS5_DEPTH_STREAM_DT;
		md_addr = DS5_DEPTH_STREAM_MD;
		override_addr = DS5_DEPTH_OVERRIDE;
		fps_addr = DS5_DEPTH_FPS;
		width_addr = DS5_DEPTH_RES_WIDTH;
		height_addr = DS5_DEPTH_RES_HEIGHT;
		md_fmt = GMSL_CSI_DT_EMBED;
		vc_id = 0;
	} else if (state->is_rgb) {
		sensor = &state->rgb.sensor;
		dt_addr = DS5_RGB_STREAM_DT;
		md_addr = DS5_RGB_STREAM_MD;
		override_addr = 0;
		fps_addr = DS5_RGB_FPS;
		width_addr = DS5_RGB_RES_WIDTH;
		height_addr = DS5_RGB_RES_HEIGHT;
		md_fmt = GMSL_CSI_DT_EMBED;
		vc_id = 1;
	} else if (state->is_y8) {
		sensor = &state->ir.sensor;
		dt_addr = DS5_IR_STREAM_DT;
		md_addr = DS5_IR_STREAM_MD;
		override_addr = DS5_IR_OVERRIDE;
		fps_addr = DS5_IR_FPS;
		width_addr = DS5_IR_RES_WIDTH;
		height_addr = DS5_IR_RES_HEIGHT;
		md_fmt = GMSL_CSI_DT_EMBED;
		vc_id = 2;
	} else if (state->is_imu) {
		sensor = &state->imu.sensor;
		dt_addr = DS5_IMU_STREAM_DT;
		md_addr = DS5_IMU_STREAM_MD;
		override_addr = 0;
		fps_addr = DS5_IMU_FPS;
		width_addr = DS5_IMU_RES_WIDTH;
		height_addr = DS5_IMU_RES_HEIGHT;
		md_fmt = 0x0;
		vc_id = 3;
	} else {
		return -EINVAL;
	}

#ifdef CONFIG_VIDEO_D4XX_SERDES
	data_type1 = sensor->config.format->data_type;
	data_type2 = state->is_y8 ? 0x00 : md_fmt;

	vc_id = state->g_ctx.dst_vc;

	ret = ds5_setup_pipeline(state, data_type1, data_type2, sensor->pipe_id,
				 vc_id);
	// reset data path when switching to Y12I
	if (state->is_y8 && data_type1 == GMSL_CSI_DT_RGB_888)
		max9296_reset_oneshot(state->dser_dev);
	if (ret < 0)
		return ret;
#endif

	fmt = sensor->streaming ? sensor->config.format->data_type : 0;

	/*
	 * Set depth stream Z16 data type as 0x31
	 * Set IR stream Y8I data type as 0x32
	 */
	if (state->is_depth && fmt != 0)
		ret = ds5_write(state, dt_addr, 0x31);
	else if (state->is_y8 && fmt != 0 &&
		 sensor->config.format->data_type == GMSL_CSI_DT_YUV422_8)
		ret = ds5_write(state, dt_addr, 0x32);
	else
		ret = ds5_write(state, dt_addr, fmt);
	if (ret < 0)
		return ret;

	ret = ds5_write(state, md_addr, (vc_id << 8) | md_fmt);
	if (ret < 0)
		return ret;

	if (!sensor->streaming)
		return ret;

	if (override_addr != 0) {
		ret = ds5_write(state, override_addr, fmt);
		if (ret < 0)
			return ret;
	}

	ret = ds5_write(state, fps_addr, sensor->config.framerate);
	if (ret < 0)
		return ret;

	ret = ds5_write(state, width_addr, sensor->config.resolution->width);
	if (ret < 0)
		return ret;

	ret = ds5_write(state, height_addr, sensor->config.resolution->height);
	if (ret < 0)
		return ret;

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
/* pad ops */
static int ds5_sensor_g_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *sd_state,
		struct v4l2_subdev_frame_interval *fi)
#else
/* Video ops */
static int ds5_sensor_g_frame_interval(struct v4l2_subdev *sd,
		struct v4l2_subdev_frame_interval *fi)
#endif
{
	struct ds5_sensor *sensor = container_of(sd, struct ds5_sensor, sd);

	if (NULL == sd || NULL == fi)
		return -EINVAL;

	fi->interval.numerator = 1;
	fi->interval.denominator = sensor->config.framerate;

	dev_dbg(sd->dev, "%s(): %s %u\n", __func__, sd->name,
			fi->interval.denominator);

	return 0;
}
static u16 __ds5_probe_framerate(const struct ds5_resolution *res, u16 target);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
/* pad ops */
static int ds5_sensor_s_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *sd_state,
		struct v4l2_subdev_frame_interval *fi)
#else
/* Video ops */
static int ds5_sensor_s_frame_interval(struct v4l2_subdev *sd,
		struct v4l2_subdev_frame_interval *fi)
#endif
{
	struct ds5_sensor *sensor = container_of(sd, struct ds5_sensor, sd);
	u16 framerate = 1;

	if (NULL == sd || NULL == fi || fi->interval.numerator == 0)
		return -EINVAL;

	framerate = fi->interval.denominator / fi->interval.numerator;
	framerate = __ds5_probe_framerate(sensor->config.resolution, framerate);
	sensor->config.framerate = framerate;
	fi->interval.numerator = 1;
	fi->interval.denominator = framerate;

	dev_dbg(sd->dev, "%s(): %s %u\n", __func__, sd->name, framerate);

	return 0;
}

static int ds5_sensor_s_stream(struct v4l2_subdev *sd, int on)
{
	struct ds5_sensor *sensor = container_of(sd, struct ds5_sensor, sd);

	dev_dbg(sensor->sd.dev, "%s(): sensor: name=%s state=%d\n",
		__func__, sensor->sd.name, on);

	sensor->streaming = on;

	return 0;
}

static const struct v4l2_subdev_pad_ops ds5_depth_pad_ops = {
	.enum_mbus_code		= ds5_sensor_enum_mbus_code,
	.enum_frame_size	= ds5_sensor_enum_frame_size,
	.enum_frame_interval	= ds5_sensor_enum_frame_interval,
	.get_fmt		= ds5_sensor_get_fmt,
	.set_fmt		= ds5_sensor_set_fmt,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
	.get_frame_interval	= ds5_sensor_g_frame_interval,
	.set_frame_interval	= ds5_sensor_s_frame_interval,
#endif
};

static const struct v4l2_subdev_video_ops ds5_sensor_video_ops = {
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
	.g_frame_interval	= ds5_sensor_g_frame_interval,
	.s_frame_interval	= ds5_sensor_s_frame_interval,
#endif
	.s_stream		= ds5_sensor_s_stream,
};

static const struct v4l2_subdev_ops ds5_depth_subdev_ops = {
	.pad = &ds5_depth_pad_ops,
	.video = &ds5_sensor_video_ops,
};

/* InfraRed stream Y8/Y16 */

/* FIXME: identical to ds5_depth_pad_ops, use one for both */
static const struct v4l2_subdev_pad_ops ds5_ir_pad_ops = {
	.enum_mbus_code		= ds5_sensor_enum_mbus_code,
	.enum_frame_size	= ds5_sensor_enum_frame_size,
	.enum_frame_interval	= ds5_sensor_enum_frame_interval,
	.get_fmt		= ds5_sensor_get_fmt,
	.set_fmt		= ds5_sensor_set_fmt,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
	.get_frame_interval	= ds5_sensor_g_frame_interval,
	.set_frame_interval	= ds5_sensor_s_frame_interval,
#endif
};

static const struct v4l2_subdev_ops ds5_ir_subdev_ops = {
	.pad = &ds5_ir_pad_ops,
	.video = &ds5_sensor_video_ops,
};

/* FIXME: identical to ds5_depth_pad_ops, use one for both? */
static const struct v4l2_subdev_pad_ops ds5_rgb_pad_ops = {
	.enum_mbus_code		= ds5_sensor_enum_mbus_code,
	.enum_frame_size	= ds5_sensor_enum_frame_size,
	.enum_frame_interval	= ds5_sensor_enum_frame_interval,
	.get_fmt		= ds5_sensor_get_fmt,
	.set_fmt		= ds5_sensor_set_fmt,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
	.get_frame_interval	= ds5_sensor_g_frame_interval,
	.set_frame_interval	= ds5_sensor_s_frame_interval,
#endif
};

static const struct v4l2_subdev_ops ds5_rgb_subdev_ops = {
	.pad = &ds5_rgb_pad_ops,
	.video = &ds5_sensor_video_ops,
};

static const struct v4l2_subdev_pad_ops ds5_imu_pad_ops = {
	.enum_mbus_code		= ds5_sensor_enum_mbus_code,
	.enum_frame_size	= ds5_sensor_enum_frame_size,
	.enum_frame_interval	= ds5_sensor_enum_frame_interval,
	.get_fmt		= ds5_sensor_get_fmt,
	.set_fmt		= ds5_sensor_set_fmt,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
	.get_frame_interval	= ds5_sensor_g_frame_interval,
	.set_frame_interval	= ds5_sensor_s_frame_interval,
#endif
};

static const struct v4l2_subdev_ops ds5_imu_subdev_ops = {
	.pad = &ds5_imu_pad_ops,
	.video = &ds5_sensor_video_ops,
};

static int ds5_hw_set_auto_exposure(struct ds5 *state, u32 base, s32 val)
{
	if (val != V4L2_EXPOSURE_APERTURE_PRIORITY &&
		val != V4L2_EXPOSURE_MANUAL)
		return -EINVAL;

	/*
	 * In firmware color auto exposure setting follow the uvc_menu_info
	 * exposure_auto_controls numbers, in drivers/media/usb/uvc/uvc_ctrl.c.
	 */
	if (state->is_rgb && val == V4L2_EXPOSURE_APERTURE_PRIORITY)
		val = 8;

	/*
	 * In firmware depth auto exposure on: 1, off: 0.
	 */
	if (!state->is_rgb) {
		if (val == V4L2_EXPOSURE_APERTURE_PRIORITY)
			val = 1;
		else if (val == V4L2_EXPOSURE_MANUAL)
			val = 0;
	}

	return ds5_write(state, base | DS5_AUTO_EXPOSURE_MODE, (u16)val);
}

/*
 * Manual exposure in us
 * Depth/Y8: between 100 and 200000 (200ms)
 * Color: between 100 and 1000000 (1s)
 */
static int ds5_hw_set_exposure(struct ds5 *state, u32 base, s32 val)
{
	int ret = -1;

	if (val < 1)
		val = 1;
	if ((state->is_depth || state->is_y8) && val > MAX_DEPTH_EXP)
		val = MAX_DEPTH_EXP;
	if (state->is_rgb && val > MAX_RGB_EXP)
		val = MAX_RGB_EXP;

	/*
	 * Color and depth uses different unit:
	 *	Color: 1 is 100 us
	 *	Depth: 1 is 1 us
	 */

	ret = ds5_write(state, base | DS5_MANUAL_EXPOSURE_MSB, (u16)(val >> 16));
	if (!ret)
		ret = ds5_write(state, base | DS5_MANUAL_EXPOSURE_LSB,
				(u16)(val & 0xffff));

	return ret;
}

#define DS5_MAX_LOG_WAIT 200
#define DS5_MAX_LOG_SLEEP 10
#define DS5_MAX_LOG_POLL (DS5_MAX_LOG_WAIT / DS5_MAX_LOG_SLEEP)

// TODO: why to use DS5_DEPTH_Y_STREAMS_DT?
#define DS5_CAMERA_CID_BASE	(V4L2_CTRL_CLASS_CAMERA | DS5_DEPTH_STREAM_DT)

#define DS5_CAMERA_CID_LOG			(DS5_CAMERA_CID_BASE+0)
#define DS5_CAMERA_CID_LASER_POWER		(DS5_CAMERA_CID_BASE+1)
#define DS5_CAMERA_CID_MANUAL_LASER_POWER	(DS5_CAMERA_CID_BASE+2)
#define DS5_CAMERA_DEPTH_CALIBRATION_TABLE_GET	(DS5_CAMERA_CID_BASE+3)
#define DS5_CAMERA_DEPTH_CALIBRATION_TABLE_SET	(DS5_CAMERA_CID_BASE+4)
#define DS5_CAMERA_COEFF_CALIBRATION_TABLE_GET	(DS5_CAMERA_CID_BASE+5)
#define DS5_CAMERA_COEFF_CALIBRATION_TABLE_SET	(DS5_CAMERA_CID_BASE+6)
#define DS5_CAMERA_CID_FW_VERSION		(DS5_CAMERA_CID_BASE+7)
#define DS5_CAMERA_CID_GVD			(DS5_CAMERA_CID_BASE+8)
#define DS5_CAMERA_CID_AE_ROI_GET		(DS5_CAMERA_CID_BASE+9)
#define DS5_CAMERA_CID_AE_ROI_SET		(DS5_CAMERA_CID_BASE+10)
#define DS5_CAMERA_CID_AE_SETPOINT_GET		(DS5_CAMERA_CID_BASE+11)
#define DS5_CAMERA_CID_AE_SETPOINT_SET		(DS5_CAMERA_CID_BASE+12)
#define DS5_CAMERA_CID_ERB			(DS5_CAMERA_CID_BASE+13)
#define DS5_CAMERA_CID_EWB			(DS5_CAMERA_CID_BASE+14)
#define DS5_CAMERA_CID_HWMC			(DS5_CAMERA_CID_BASE+15)

#define DS5_CAMERA_CID_PWM			(DS5_CAMERA_CID_BASE+22)

/* the HWMC will remain for legacy tools compatibility,
 * HWMC_RW used for UVC compatibility
 */
#define DS5_CAMERA_CID_HWMC_RW		(DS5_CAMERA_CID_BASE+32)

#define DS5_HWMC_DATA			0x4900
#define DS5_HWMC_STATUS			0x4904
#define DS5_HWMC_RESP_LEN		0x4908
#define DS5_HWMC_EXEC			0x490C

#define DS5_HWMC_STATUS_OK		0
#define DS5_HWMC_STATUS_ERR		1
#define DS5_HWMC_STATUS_WIP		2
#define DS5_HWMC_BUFFER_SIZE	1024

enum DS5_HWMC_ERR {
	DS5_HWMC_ERR_SUCCESS = 0,
	DS5_HWMC_ERR_CMD     = -1,
	DS5_HWMC_ERR_PARAM   = -6,
	DS5_HWMC_ERR_NODATA  = -21,
	DS5_HWMC_ERR_UNKNOWN = -64,
	DS5_HWMC_ERR_LAST,
};

static int ds5_get_hwmc_status(struct ds5 *state)
{
	int ret = 0;
	u16 status = DS5_HWMC_STATUS_WIP;
	int retries = 100;
	int errorCode;
	do {
		if (retries != 100)
			msleep_range(1);
		ret = ds5_read(state, DS5_HWMC_STATUS, &status);
	} while (!ret && retries-- && status == DS5_HWMC_STATUS_WIP);
	dev_dbg(&state->client->dev,
			"%s(): ret: 0x%x, status: 0x%x\n",
			__func__, ret, status);
	if (ret || status != DS5_HWMC_STATUS_OK) {
		if (status == DS5_HWMC_STATUS_ERR) {
			ds5_raw_read(state, DS5_HWMC_DATA, &errorCode, sizeof(errorCode));
			return errorCode;
		}
	}
	if (!ret && (status != DS5_HWMC_STATUS_OK))
		ret = DS5_HWMC_ERR_LAST;

	return ret;
}

static int ds5_get_hwmc(struct ds5 *state, unsigned char *data,
		u16 cmdDataLen, u16 *dataLen)
{
	int ret = 0;
	u16 tmp_len = 0;

	if (!data)
		return -ENOBUFS;

	memset(data, 0, cmdDataLen);
	ret = ds5_get_hwmc_status(state);
	if (ret) {
		dev_dbg(&state->client->dev,
			"%s(): HWMC status not clear, ret: %d\n",
			__func__, ret);
		if (ret != DS5_HWMC_ERR_LAST) {
			int *p = (int *)data;
			*p = ret;
			return 0;
		} else {
			return ret;
		}
	}

	ret = regmap_raw_read(state->regmap, DS5_HWMC_RESP_LEN,
			&tmp_len, sizeof(tmp_len));
	if (ret)
		return -EBADMSG;

	if (tmp_len > cmdDataLen)
		return -ENOBUFS;

	dev_dbg(&state->client->dev,
			"%s(): HWMC read len: %d, lrs_len: %d\n",
			__func__, tmp_len, tmp_len - 4);

	ds5_raw_read_with_check(state, DS5_HWMC_DATA, data, tmp_len);
	if (dataLen)
		*dataLen = tmp_len;
	return ret;
}

static int ds5_send_hwmc(struct ds5 *state,
			u16 cmdLen,
			struct hwm_cmd *cmd)
{
	dev_dbg(&state->client->dev,
			"%s(): HWMC header: 0x%x, magic: 0x%x, opcode: 0x%x, "
			"cmdLen: %d, param1: %d, param2: %d, param3: %d, param4: %d\n",
			__func__, cmd->header, cmd->magic_word, cmd->opcode,
			cmdLen,	cmd->param1, cmd->param2, cmd->param3, cmd->param4);

	ds5_raw_write_with_check(state, DS5_HWMC_DATA, cmd, cmdLen);
	
	ds5_write_with_check(state, DS5_HWMC_EXEC, 0x01); /* execute cmd */

	return 0;
}

static int ds5_set_calibration_data(struct ds5 *state,
		struct hwm_cmd *cmd, u16 length)
{
	int ret = -1;
	int retries = 10;
	u16 status = 2;

	ds5_raw_write_with_check(state, DS5_HWMC_DATA, cmd, length);

	ds5_write_with_check(state, DS5_HWMC_EXEC, 0x01); /* execute cmd */
	do {
		if (retries != 10)
			msleep_range(200);
		ret = ds5_read(state, DS5_HWMC_STATUS, &status);
	} while (retries-- && status != 0);

	if (ret || status != 0) {
		dev_err(&state->client->dev,
				"%s(): Failed to set calibration table %d,"
				"ret: %d, fw error: %x\n",
				__func__, cmd->param1, ret, status);
	}

	return ret;
}

static int ds5_mux_s_stream(struct v4l2_subdev *sd, int on);

static int ds5_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ds5 *state = container_of(ctrl->handler, struct ds5,
					 ctrls.handler);
	struct v4l2_subdev *sd = &state->mux.sd.subdev;
	struct ds5_sensor *sensor = (struct ds5_sensor *)ctrl->priv;
	int ret = -EINVAL;
	u16 base = DS5_DEPTH_CONTROL_BASE;

	if (sensor) {
		switch (sensor->mux_pad) {
		case DS5_MUX_PAD_DEPTH:
			state = container_of(ctrl->handler, struct ds5, ctrls.handler_depth);
			state->is_rgb = 0;
			state->is_depth = 1;
			state->is_y8 = 0;
			state->is_imu = 0;
		break;
		case DS5_MUX_PAD_RGB:
			state = container_of(ctrl->handler, struct ds5, ctrls.handler_rgb);
			state->is_rgb = 1;
			state->is_depth = 0;
			state->is_y8 = 0;
			state->is_imu = 0;
		break;
		case DS5_MUX_PAD_IR:
			state = container_of(ctrl->handler, struct ds5, ctrls.handler_y8);
			state->is_rgb = 0;
			state->is_depth = 0;
			state->is_y8 = 1;
			state->is_imu = 0;
		break;
		case DS5_MUX_PAD_IMU:
			state = container_of(ctrl->handler, struct ds5, ctrls.handler_imu);
			state->is_rgb = 0;
			state->is_depth = 0;
			state->is_y8 = 0;
			state->is_imu = 1;
		break;
		default:
			state->is_rgb = 0;
			state->is_depth = 0;
			state->is_y8 = 0;
			state->is_imu = 1;
		break;

		}
	}

	if (state->is_rgb)
		base = DS5_RGB_CONTROL_BASE;
#ifndef CONFIG_VIDEO_INTEL_IPU6
	else if (state->is_imu)
		return -EINVAL;
#endif
	v4l2_dbg(3, 1, sd, "ctrl: %s, value: %d\n", ctrl->name, ctrl->val);
	dev_dbg(&state->client->dev, "%s(): %s - ctrl: %s, value: %d\n",
		__func__, ds5_get_sensor_name(state), ctrl->name, ctrl->val);

	mutex_lock(&state->lock);

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ds5_write(state, base | DS5_MANUAL_GAIN, ctrl->val);
		break;

	case V4L2_CID_EXPOSURE_AUTO:
		ret = ds5_hw_set_auto_exposure(state, base, ctrl->val);
		break;

	case V4L2_CID_EXPOSURE_ABSOLUTE:
		ret = ds5_hw_set_exposure(state, base, ctrl->val);
		break;
	case DS5_CAMERA_CID_LASER_POWER:
		if (!state->is_rgb)
			ret = ds5_write(state, base | DS5_LASER_POWER,
					ctrl->val);
		break;
	case DS5_CAMERA_CID_MANUAL_LASER_POWER:
		if (!state->is_rgb)
			ret = ds5_write(state, base | DS5_MANUAL_LASER_POWER,
					ctrl->val);
		break;
	case DS5_CAMERA_DEPTH_CALIBRATION_TABLE_SET:
		dev_dbg(&state->client->dev,
			"%s(): DS5_CAMERA_DEPTH_CALIBRATION_TABLE_SET \n",	__func__);
		if (ctrl->p_new.p) {
			struct hwm_cmd *calib_cmd;
			dev_dbg(&state->client->dev,
				"%s(): table id: 0x%x\n",
				__func__, *((u8 *)ctrl->p_new.p + 2));
			if (DEPTH_CALIBRATION_ID == *((u8 *)ctrl->p_new.p + 2)) {
				calib_cmd = devm_kzalloc(&state->client->dev,
					sizeof(struct hwm_cmd) + 256, GFP_KERNEL);
				if (!calib_cmd) {
					dev_err(&state->client->dev,
						"%s(): Can't allocate memory for 0x%x\n",
						__func__, ctrl->id);
					ret = -ENOMEM;
					break;
				}
				memcpy(calib_cmd, &set_calib_data, sizeof(set_calib_data));
				calib_cmd->header = 276;
				calib_cmd->param1 = DEPTH_CALIBRATION_ID;
				memcpy(calib_cmd->Data, (u8 *)ctrl->p_new.p, 256);
				ret = ds5_set_calibration_data(state, calib_cmd,
					sizeof(struct hwm_cmd) + 256);
				devm_kfree(&state->client->dev, calib_cmd);
			}
		}
		break;
	case DS5_CAMERA_COEFF_CALIBRATION_TABLE_SET:
			dev_dbg(&state->client->dev,
				"%s(): DS5_CAMERA_COEFF_CALIBRATION_TABLE_SET \n",
				__func__);
			if (ctrl->p_new.p) {
				struct hwm_cmd *calib_cmd;
				dev_dbg(&state->client->dev,
					"%s(): table id %d\n",
					__func__, *((u8 *)ctrl->p_new.p + 2));
				if (COEF_CALIBRATION_ID == *((u8 *)ctrl->p_new.p + 2)) {
					calib_cmd = devm_kzalloc(&state->client->dev,
						sizeof(struct hwm_cmd) + 512, GFP_KERNEL);
					if (!calib_cmd) {
						dev_err(&state->client->dev,
							"%s(): Can't allocate memory for 0x%x\n",
							__func__, ctrl->id);
						ret = -ENOMEM;
						break;
					}
				memcpy(calib_cmd, &set_calib_data, sizeof (set_calib_data));
				calib_cmd->header = 532;
				calib_cmd->param1 = COEF_CALIBRATION_ID;
				memcpy(calib_cmd->Data, (u8 *)ctrl->p_new.p, 512);
				ret = ds5_set_calibration_data(state, calib_cmd,
						sizeof(struct hwm_cmd) + 512);
				devm_kfree(&state->client->dev, calib_cmd);
			}
		}
		break;
	case DS5_CAMERA_CID_AE_ROI_SET: 
		if (ctrl->p_new.p_u16) {
			struct hwm_cmd ae_roi_cmd;
			memcpy(&ae_roi_cmd, &set_ae_roi, sizeof(ae_roi_cmd));
			ae_roi_cmd.param1 = *((u16 *)ctrl->p_new.p_u16);
			ae_roi_cmd.param2 = *((u16 *)ctrl->p_new.p_u16 + 1);
			ae_roi_cmd.param3 = *((u16 *)ctrl->p_new.p_u16 + 2);
			ae_roi_cmd.param4 = *((u16 *)ctrl->p_new.p_u16 + 3);
			ret = ds5_send_hwmc(state, sizeof(struct hwm_cmd),
				&ae_roi_cmd);
			if (!ret)
				ret = ds5_get_hwmc_status(state);
		}
		break;
	case DS5_CAMERA_CID_AE_SETPOINT_SET:
		if (ctrl->p_new.p_s32) {
			struct hwm_cmd *ae_setpoint_cmd;
			dev_dbg(&state->client->dev, "%s():0x%x \n",
				__func__, *(ctrl->p_new.p_s32));
			ae_setpoint_cmd = devm_kzalloc(&state->client->dev,
					sizeof(struct hwm_cmd) + 4, GFP_KERNEL);
			if (!ae_setpoint_cmd) {
				dev_err(&state->client->dev,
					"%s(): Can't allocate memory for 0x%x\n",
					__func__, ctrl->id);
				ret = -ENOMEM;
				break;
			}
			memcpy(ae_setpoint_cmd, &set_ae_setpoint, sizeof (set_ae_setpoint));
			memcpy(ae_setpoint_cmd->Data, (u8 *)ctrl->p_new.p_s32, 4);
			ret = ds5_send_hwmc(state, sizeof(struct hwm_cmd) + 4,
					ae_setpoint_cmd);
			if (!ret)
				ret = ds5_get_hwmc_status(state);
			devm_kfree(&state->client->dev, ae_setpoint_cmd);
		}
		break;
	case DS5_CAMERA_CID_ERB:
		if (ctrl->p_new.p_u8) {
			u16 offset = 0;
			u16 size = 0;
			u16 len = 0;
			struct hwm_cmd *erb_cmd;

			offset = *(ctrl->p_new.p_u8) << 8;
			offset |= *(ctrl->p_new.p_u8 + 1);
			size = *(ctrl->p_new.p_u8 + 2) << 8;
			size |= *(ctrl->p_new.p_u8 + 3);

			dev_dbg(&state->client->dev, "%s(): offset %x, size: %x\n",
							__func__, offset, size);
			len = sizeof(struct hwm_cmd) + size;
			erb_cmd = devm_kzalloc(&state->client->dev,	len, GFP_KERNEL);
			if (!erb_cmd) {
				dev_err(&state->client->dev,
					"%s(): Can't allocate memory for 0x%x\n",
					__func__, ctrl->id);
				ret = -ENOMEM;
				break;
			}
			memcpy(erb_cmd, &erb, sizeof(struct hwm_cmd));
			erb_cmd->param1 = offset;
			erb_cmd->param2 = size;
			ret = ds5_send_hwmc(state, sizeof(struct hwm_cmd), erb_cmd);
			if (!ret)
				ret = ds5_get_hwmc(state, erb_cmd->Data, len, &size);
			if (ret) {
				dev_err(&state->client->dev,
					"%s(): ERB cmd failed, ret: %d,"
					"requested size: %d, actual size: %d\n",
					__func__, ret, erb_cmd->param2, size);
				devm_kfree(&state->client->dev, erb_cmd);
				return -EAGAIN;
			}

			// Actual size returned from FW
			*(ctrl->p_new.p_u8 + 2) = (size & 0xFF00) >> 8;
			*(ctrl->p_new.p_u8 + 3) = (size & 0x00FF);

			memcpy(ctrl->p_new.p_u8 + 4, erb_cmd->Data + 4, size - 4);
			dev_dbg(&state->client->dev, "%s(): 0x%x 0x%x 0x%x 0x%x \n",
				__func__,
				*(ctrl->p_new.p_u8),
				*(ctrl->p_new.p_u8+1),
				*(ctrl->p_new.p_u8+2),
				*(ctrl->p_new.p_u8+3));
			devm_kfree(&state->client->dev, erb_cmd);
		}
		break;
	case DS5_CAMERA_CID_EWB:
		if (ctrl->p_new.p_u8) {
			u16 offset = 0;
			u16 size = 0;
			struct hwm_cmd *ewb_cmd;

			offset = *((u8 *)ctrl->p_new.p_u8) << 8;
			offset |= *((u8 *)ctrl->p_new.p_u8 + 1);
			size = *((u8 *)ctrl->p_new.p_u8 + 2) << 8;
			size |= *((u8 *)ctrl->p_new.p_u8 + 3);

			dev_dbg(&state->client->dev, "%s():0x%x 0x%x 0x%x 0x%x\n",
					__func__,
					*((u8 *)ctrl->p_new.p_u8),
					*((u8 *)ctrl->p_new.p_u8 + 1),
					*((u8 *)ctrl->p_new.p_u8 + 2),
					*((u8 *)ctrl->p_new.p_u8 + 3));

			ewb_cmd = devm_kzalloc(&state->client->dev,
					sizeof(struct hwm_cmd) + size,
					GFP_KERNEL);
			if (!ewb_cmd) {
				dev_err(&state->client->dev,
					"%s(): Can't allocate memory for 0x%x\n",
					__func__, ctrl->id);
				ret = -ENOMEM;
				break;
			}
			memcpy(ewb_cmd, &ewb, sizeof(ewb));
			ewb_cmd->header = 0x14 + size;
			ewb_cmd->param1 = offset; // start index
			ewb_cmd->param2 = size; // size
			memcpy(ewb_cmd->Data, (u8 *)ctrl->p_new.p_u8 + 4, size);
			ret = ds5_send_hwmc(state, sizeof(struct hwm_cmd) + size, ewb_cmd);
			if (!ret)
				ret = ds5_get_hwmc_status(state);
			if (ret) {
				dev_err(&state->client->dev,
					"%s(): EWB cmd failed, ret: %d,"
					"requested size: %d, actual size: %d\n",
					__func__, ret, ewb_cmd->param2, size);
				devm_kfree(&state->client->dev, ewb_cmd);
				return -EAGAIN;
			}

			devm_kfree(&state->client->dev, ewb_cmd);
		}
		break;
	case DS5_CAMERA_CID_HWMC:
		if (ctrl->p_new.p_u8) {
			u16 size = 0;
			struct hwm_cmd *cmd = (struct hwm_cmd *)ctrl->p_new.p_u8;
			size = *((u8 *)ctrl->p_new.p_u8 + 1) << 8;
			size |= *((u8 *)ctrl->p_new.p_u8 + 0);
			ret = ds5_send_hwmc(state, size + 4, cmd);
			ret = ds5_get_hwmc(state, cmd->Data, ctrl->dims[0], &size);
			if (ctrl->dims[0] < DS5_HWMC_BUFFER_SIZE) {
				ret = -ENODATA;
				break;
			}
			/*This is needed for legacy hwmc */
			size += 4; // SIZE_OF_HW_MONITOR_HEADER
			cmd->Data[1000] = (unsigned char)((size) & 0x00FF);
			cmd->Data[1001] = (unsigned char)(((size) & 0xFF00) >> 8);
		}
		break;
	case DS5_CAMERA_CID_HWMC_RW:
		if (ctrl->p_new.p_u8) {
			u16 size = *((u8 *)ctrl->p_new.p_u8 + 1) << 8;
			size |= *((u8 *)ctrl->p_new.p_u8 + 0);
			ret = ds5_send_hwmc(state, size + 4,
					(struct hwm_cmd *)ctrl->p_new.p_u8);
		}
		break;
	case DS5_CAMERA_CID_PWM:
		if (state->is_depth)
			ret = ds5_write(state, base | DS5_PWM_FREQUENCY, ctrl->val);
		break;
#ifdef CONFIG_VIDEO_INTEL_IPU6
	case V4L2_CID_IPU_SET_SUB_STREAM:
	{
		u32 val = (*ctrl->p_new.p_s64 & 0xFFFF);
		u16 on = val & 0x00FF;
		u16 vc_id = (val >> 8) & 0x00FF;
		int substream = -1;
		if (vc_id < DS5_MUX_PAD_COUNT)
			ret = ds5_s_state(state, vc_id);
		substream = state->pad_to_substream[state->mux.last_set->mux_pad];
		dev_info(&state->client->dev, "V4L2_CID_IPU_SET_SUB_STREAM %x vc_id:%d, substream:%d, on:%d\n", val, vc_id, substream, on);
		if (on == 0xff)
			break;
		if (vc_id > NR_OF_DS5_STREAMS - 1)
			dev_err(&state->client->dev, "invalid vc %d\n", vc_id);
		else
			d4xx_set_sub_stream[substream] = on;
		ret = 0;
#ifndef CONFIG_VIDEO_D4XX_SERDES
		ret = ds5_mux_s_stream(sd, on);
#endif
	}
		break;
	case V4L2_CID_LINK_FREQ: {
		if ( sensor && ctrl->p_new.p_u8)
		{
		  if (*ctrl->p_new.p_u8 <= (ARRAY_SIZE(link_freq_menu_items) - 1)) {
			struct v4l2_ctrl *link_freq = state->ctrls.link_freq;
			dev_info(&state->client->dev,
				"V4L2_CID_LINK_FREQ modify index to val=%d",
				 (unsigned int) *ctrl->p_new.p_u8);
			ret = 0;
		  }
		}

	}
	  break;
#endif
	}

	mutex_unlock(&state->lock);

	return ret;
}

static int ds5_get_calibration_data(struct ds5 *state, enum table_id id,
		unsigned char *table, unsigned int length)
{
	struct hwm_cmd *cmd;
	int ret = -1;
	int retries = 3;
	u16 status = 2;
	u16 table_length;

	cmd = devm_kzalloc(&state->client->dev,
			sizeof(struct hwm_cmd) + length + 4, GFP_KERNEL);
	if (!cmd) {
		dev_err(&state->client->dev, "%s(): Can't allocate memory\n", __func__);
		return -ENOMEM;
	}

	memcpy(cmd, &get_calib_data, sizeof(get_calib_data));
	cmd->param1 = id;
	ds5_raw_write_with_check(state, 0x4900, cmd, sizeof(struct hwm_cmd));
	ds5_write_with_check(state, 0x490c, 0x01); /* execute cmd */
	do {
		if (retries != 3)
			msleep_range(10);
		ret = ds5_read(state, 0x4904, &status);
	} while (ret && retries-- && status != 0);

	if (ret || status != 0) {
		dev_err(&state->client->dev,
				"%s(): Failed to get calibration table %d, fw error: %x\n",
				__func__, id, status);
		devm_kfree(&state->client->dev, cmd);
		return status;
	}

	// get table length from fw
	ret = regmap_raw_read(state->regmap, 0x4908,
			&table_length, sizeof(table_length));

	// read table
	ds5_raw_read_with_check(state, 0x4900, cmd->Data, table_length);

	// first 4 bytes are opcode HWM, not part of calibration table
	memcpy(table, cmd->Data + 4, length);
	devm_kfree(&state->client->dev, cmd);
	return 0;
}

static int ds5_gvd(struct ds5 *state, unsigned char *data)
{
	struct hwm_cmd cmd;
	int ret = -1;
	u16 length = 0;
	u16 status = 2;
	u8 retries = 3;

	memcpy(&cmd, &gvd, sizeof(gvd));
	ds5_raw_write_with_check(state, 0x4900, &cmd, sizeof(cmd));
	ds5_write_with_check(state, 0x490c, 0x01); /* execute cmd */
	do {
		if (retries != 3)
			msleep_range(10);

		ret = ds5_read(state, 0x4904, &status);
	} while (ret && retries-- && status != 0);

	if (ret || status != 0) {
		dev_err(&state->client->dev,
				"%s(): Failed to read GVD, HWM cmd status: %x\n",
				__func__, status);
		return status;
	}

	ret = regmap_raw_read(state->regmap, 0x4908, &length, sizeof(length));
	ds5_raw_read_with_check(state, 0x4900, data, length);

	return ret;
}

static int ds5_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ds5 *state = container_of(ctrl->handler, struct ds5,
			ctrls.handler);
	u16 log_prepare[] = {0x0014, 0xcdab, 0x000f, 0x0000, 0x0400, 0x0000,
			0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000};
	u16 execute_cmd = 0x0001;
	unsigned int i;
	u32 data;
	int ret = 0;
	struct ds5_sensor *sensor = (struct ds5_sensor *)ctrl->priv;
	u16 base = (state->is_rgb) ? DS5_RGB_CONTROL_BASE : DS5_DEPTH_CONTROL_BASE;
	u16 reg;

	if (sensor) {
		switch (sensor->mux_pad) {
		case DS5_MUX_PAD_DEPTH:
			state = container_of(ctrl->handler, struct ds5, ctrls.handler_depth);
			state->is_rgb = 0;
			state->is_depth = 1;
			state->is_y8 = 0;
			state->is_imu = 0;
		break;
		case DS5_MUX_PAD_RGB:
			state = container_of(ctrl->handler, struct ds5, ctrls.handler_rgb);
			state->is_rgb = 1;
			state->is_depth = 0;
			state->is_y8 = 0;
			state->is_imu = 0;
		break;
		case DS5_MUX_PAD_IR:
			state = container_of(ctrl->handler, struct ds5, ctrls.handler_y8);
			state->is_rgb = 0;
			state->is_depth = 0;
			state->is_y8 = 1;
			state->is_imu = 0;
		break;
		case DS5_MUX_PAD_IMU:
			state = container_of(ctrl->handler, struct ds5, ctrls.handler_imu);
			state->is_rgb = 0;
			state->is_depth = 0;
			state->is_y8 = 0;
			state->is_imu = 1;
		break;
		default:
			state->is_rgb = 0;
			state->is_depth = 0;
			state->is_y8 = 0;
			state->is_imu = 1;
		break;

		}
	}
	base = (state->is_rgb) ? DS5_RGB_CONTROL_BASE : DS5_DEPTH_CONTROL_BASE;

	dev_dbg(&state->client->dev, "%s(): %s - ctrl: %s \n",
		__func__, ds5_get_sensor_name(state), ctrl->name);

	switch (ctrl->id) {

	case V4L2_CID_ANALOGUE_GAIN:
		if (state->is_imu)
			return -EINVAL;
		ret = ds5_read(state, base | DS5_MANUAL_GAIN, ctrl->p_new.p_u16);
		break;

	case V4L2_CID_EXPOSURE_AUTO:
		if (state->is_imu)
			return -EINVAL;
		ds5_read(state, base | DS5_AUTO_EXPOSURE_MODE, &reg);
		*ctrl->p_new.p_u16 = reg;
		/* see ds5_hw_set_auto_exposure */
		if (!state->is_rgb) {
			if (reg == 1)
				*ctrl->p_new.p_u16 = V4L2_EXPOSURE_APERTURE_PRIORITY;
			else if (reg == 0)
				*ctrl->p_new.p_u16 = V4L2_EXPOSURE_MANUAL;
		}

		if (state->is_rgb && reg == 8)
			*ctrl->p_new.p_u16 = V4L2_EXPOSURE_APERTURE_PRIORITY;

		break;

	case V4L2_CID_EXPOSURE_ABSOLUTE:
		if (state->is_imu)
			return -EINVAL;
		/* see ds5_hw_set_exposure */
		ds5_read(state, base | DS5_MANUAL_EXPOSURE_MSB, &reg);
		data = ((u32)reg << 16) & 0xffff0000;
		ds5_read(state, base | DS5_MANUAL_EXPOSURE_LSB, &reg);
		data |= reg;
		*ctrl->p_new.p_u32 = data;
		break;

	case DS5_CAMERA_CID_LASER_POWER:
		if (!state->is_rgb)
			ds5_read(state, base | DS5_LASER_POWER, ctrl->p_new.p_u16);
		break;

	case DS5_CAMERA_CID_MANUAL_LASER_POWER:
		if (!state->is_rgb)
			ds5_read(state, base | DS5_MANUAL_LASER_POWER, ctrl->p_new.p_u16);
		break;

	case DS5_CAMERA_CID_LOG:
		// TODO: wrap HWMonitor command
		//       1. prepare and send command
		//       2. send command
		//       3. execute command
		//       4. wait for ccompletion
		ret = regmap_raw_write(state->regmap, 0x4900,
				log_prepare, sizeof(log_prepare));
		if (ret < 0)
			return ret;

		ret = regmap_raw_write(state->regmap, 0x490C,
				&execute_cmd, sizeof(execute_cmd));
		if (ret < 0)
			return ret;

		for (i = 0; i < DS5_MAX_LOG_POLL; i++) {
			ret = regmap_raw_read(state->regmap, 0x4904,
					&data, sizeof(data));
			dev_dbg(&state->client->dev, "%s(): log ready 0x%x\n",
				 __func__, data);
			if (ret < 0)
				return ret;
			if (!data)
				break;
			msleep_range(5);
		}

//		if (i == DS5_MAX_LOG_POLL)
//			return -ETIMEDOUT;

		ret = regmap_raw_read(state->regmap, 0x4908, &data, sizeof(data));
		dev_dbg(&state->client->dev, "%s(): log size 0x%x\n", __func__, data);
		if (ret < 0)
			return ret;
		if (!data)
			return 0;
		if (data > 1024)
			return -ENOBUFS;
		ret = regmap_raw_read(state->regmap, 0x4900,
				ctrl->p_new.p_u8, data);
		break;
	case DS5_CAMERA_DEPTH_CALIBRATION_TABLE_GET:
		ret = ds5_get_calibration_data(state, DEPTH_CALIBRATION_ID,
				ctrl->p_new.p_u8, 256);
		break;
	case DS5_CAMERA_COEFF_CALIBRATION_TABLE_GET:
		ret = ds5_get_calibration_data(state, COEF_CALIBRATION_ID,
				ctrl->p_new.p_u8, 512);
		break;
	case DS5_CAMERA_CID_FW_VERSION:
		ret = ds5_read(state, DS5_FW_VERSION, &state->fw_version);
		ret = ds5_read(state, DS5_FW_BUILD, &state->fw_build);
		*ctrl->p_new.p_u32 = state->fw_version << 16;
		*ctrl->p_new.p_u32 |= state->fw_build;
		break;
	case DS5_CAMERA_CID_GVD:
		ret = ds5_gvd(state, ctrl->p_new.p_u8);
		break;
	case DS5_CAMERA_CID_AE_ROI_GET:
		if (ctrl->p_new.p_u16) {
			u16 len = sizeof(struct hwm_cmd) + 12;
			u16 dataLen = 0;
			struct hwm_cmd *ae_roi_cmd;
			ae_roi_cmd = devm_kzalloc(&state->client->dev, len, GFP_KERNEL);
			if (!ae_roi_cmd) {
				dev_err(&state->client->dev,
					"%s(): Can't allocate memory for 0x%x\n",
					__func__, ctrl->id);
				ret = -ENOMEM;
				break;
			}
			memcpy(ae_roi_cmd, &get_ae_roi, sizeof(struct hwm_cmd));
			ret = ds5_send_hwmc(state, sizeof(struct hwm_cmd), ae_roi_cmd);
			if (ret) {
				devm_kfree(&state->client->dev, ae_roi_cmd);
				return ret;
			}
			ret = ds5_get_hwmc(state, ae_roi_cmd->Data, len, &dataLen);
			if (!ret && dataLen <= ctrl->dims[0])
				memcpy(ctrl->p_new.p_u16, ae_roi_cmd->Data + 4, 8);
			devm_kfree(&state->client->dev, ae_roi_cmd);
		}
		break;
	case DS5_CAMERA_CID_AE_SETPOINT_GET:
	if (ctrl->p_new.p_s32) {
		u16 len = sizeof(struct hwm_cmd) + 8;
		u16 dataLen = 0;
		struct hwm_cmd *ae_setpoint_cmd;
		ae_setpoint_cmd = devm_kzalloc(&state->client->dev,	len, GFP_KERNEL);
		if (!ae_setpoint_cmd) {
			dev_err(&state->client->dev,
					"%s(): Can't allocate memory for 0x%x\n",
					__func__, ctrl->id);
			ret = -ENOMEM;
			break;
		}
		memcpy(ae_setpoint_cmd, &get_ae_setpoint, sizeof(struct hwm_cmd));
		ret = ds5_send_hwmc(state, sizeof(struct hwm_cmd), ae_setpoint_cmd);
		if (ret) {		
			devm_kfree(&state->client->dev, ae_setpoint_cmd);
			return ret;
		}
		ret = ds5_get_hwmc(state, ae_setpoint_cmd->Data, len, &dataLen);
		memcpy(ctrl->p_new.p_s32, ae_setpoint_cmd->Data + 4, 4);
		dev_dbg(&state->client->dev, "%s(): len: %d, 0x%x \n",
			__func__, dataLen, *(ctrl->p_new.p_s32));
		devm_kfree(&state->client->dev, ae_setpoint_cmd);
		}
		break;
	case DS5_CAMERA_CID_HWMC_RW: 
		if (ctrl->p_new.p_u8) {
			unsigned char *data = (unsigned char *)ctrl->p_new.p_u8;
			u16 dataLen = 0;
			u16 bufLen = ctrl->dims[0];
			ret = ds5_get_hwmc(state, data,	bufLen, &dataLen);
			/* This is needed for librealsense, to align there code with UVC,
		 	 * last word is length - 4 bytes header length */
			dataLen -= 4;
			data[bufLen - 4] = (unsigned char)(dataLen & 0x00FF);
			data[bufLen - 3] = (unsigned char)((dataLen & 0xFF00) >> 8);
			data[bufLen - 2] = 0;
			data[bufLen - 1] = 0;
		}
		break;
	case DS5_CAMERA_CID_PWM:
		if (state->is_depth)
			ds5_read(state, base | DS5_PWM_FREQUENCY, ctrl->p_new.p_u16);
		break;
#ifdef CONFIG_VIDEO_INTEL_IPU6
	case V4L2_CID_IPU_QUERY_SUB_STREAM: {
		if (sensor) {
			int substream = state->pad_to_substream[sensor->mux_pad];
			int vc_id = get_sub_stream_vc_id(substream);

			dev_dbg(sensor->sd.dev,
				"%s(): V4L2_CID_IPU_QUERY_SUB_STREAM sensor->mux_pad:%d"
				", vc:[%d] %d\n",
				__func__, sensor->mux_pad, vc_id, substream);
			*ctrl->p_new.p_s32 = substream;
			state->mux.last_set = sensor;
		} else {
				/* we are in DS5 MUX case */
				*ctrl->p_new.p_s32 = -1;
		}
	}
		break;
#endif
	}
	return ret;
}

static const struct v4l2_ctrl_ops ds5_ctrl_ops = {
	.s_ctrl	= ds5_s_ctrl,
	.g_volatile_ctrl = ds5_g_volatile_ctrl,
};

static const struct v4l2_ctrl_config ds5_ctrl_log = {
	.ops = &ds5_ctrl_ops,
	.id = DS5_CAMERA_CID_LOG,
	.name = "Logger",
	.type = V4L2_CTRL_TYPE_U8,
	.dims = {1024},
	.elem_size = sizeof(u8),
	.flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY,
	.step = 1,
};

static const struct v4l2_ctrl_config ds5_ctrl_laser_power = {
	.ops = &ds5_ctrl_ops,
	.id = DS5_CAMERA_CID_LASER_POWER,
	.name = "Laser power on/off",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 1,
	.flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE,
};

static const struct v4l2_ctrl_config ds5_ctrl_manual_laser_power = {
	.ops = &ds5_ctrl_ops,
	.id = DS5_CAMERA_CID_MANUAL_LASER_POWER,
	.name = "Manual laser power",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 360,
	.step = 30,
	.def = 150,
	.flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE,
};

static const struct v4l2_ctrl_config ds5_ctrl_fw_version = {
	.ops = &ds5_ctrl_ops,
	.id = DS5_CAMERA_CID_FW_VERSION,
	.name = "fw version",
	.type = V4L2_CTRL_TYPE_U32,
	.dims = {1},
	.elem_size = sizeof(u32),
	.flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY,
	.step = 1,
};

static const struct v4l2_ctrl_config ds5_ctrl_gvd = {
	.ops = &ds5_ctrl_ops,
	.id = DS5_CAMERA_CID_GVD,
	.name = "GVD",
	.type = V4L2_CTRL_TYPE_U8,
	.dims = {239},
	.elem_size = sizeof(u8),
	.flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY,
	.step = 1,
};

static const struct v4l2_ctrl_config ds5_ctrl_get_depth_calib = {
	.ops = &ds5_ctrl_ops,
	.id = DS5_CAMERA_DEPTH_CALIBRATION_TABLE_GET,
	.name = "get depth calib",
	.type = V4L2_CTRL_TYPE_U8,
	.dims = {256},
	.elem_size = sizeof(u8),
	.flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY,
	.step = 1,
};

static const struct v4l2_ctrl_config ds5_ctrl_set_depth_calib = {
	.ops = &ds5_ctrl_ops,
	.id = DS5_CAMERA_DEPTH_CALIBRATION_TABLE_SET,
	.name = "set depth calib",
	.type = V4L2_CTRL_TYPE_U8,
	.dims = {256},
	.elem_size = sizeof(u8),
	.min = 0,
	.max = 0xFFFFFFFF,
	.def = 240,
	.step = 1,
};

static const struct v4l2_ctrl_config ds5_ctrl_get_coeff_calib = {
	.ops = &ds5_ctrl_ops,
	.id = DS5_CAMERA_COEFF_CALIBRATION_TABLE_GET,
	.name = "get coeff calib",
	.type = V4L2_CTRL_TYPE_U8,
	.dims = {512},
	.elem_size = sizeof(u8),
	.flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY,
	.step = 1,
};

static const struct v4l2_ctrl_config ds5_ctrl_set_coeff_calib = {
	.ops = &ds5_ctrl_ops,
	.id = DS5_CAMERA_COEFF_CALIBRATION_TABLE_SET,
	.name = "set coeff calib",
	.type = V4L2_CTRL_TYPE_U8,
	.dims = {512},
	.elem_size = sizeof(u8),
	.min = 0,
	.max = 0xFFFFFFFF,
	.def = 240,
	.step = 1,
};

static const struct v4l2_ctrl_config ds5_ctrl_ae_roi_get = {
	.ops = &ds5_ctrl_ops,
	.id = DS5_CAMERA_CID_AE_ROI_GET,
	.name = "ae roi get",
	.type = V4L2_CTRL_TYPE_U8,
	.dims = {8},
	.elem_size = sizeof(u16),
	.flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY,
	.step = 1,
};

static const struct v4l2_ctrl_config ds5_ctrl_ae_roi_set = {
	.ops = &ds5_ctrl_ops,
	.id = DS5_CAMERA_CID_AE_ROI_SET,
	.name = "ae roi set",
	.type = V4L2_CTRL_TYPE_U8,
	.dims = {8},
	.elem_size = sizeof(u16),
	.min = 0,
	.max = 0xFFFFFFFF,
	.def = 240,
	.step = 1,
};

static const struct v4l2_ctrl_config ds5_ctrl_ae_setpoint_get = {
	.ops = &ds5_ctrl_ops,
	.id = DS5_CAMERA_CID_AE_SETPOINT_GET,
	.name = "ae setpoint get",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY,
	.step = 1,
};

static const struct v4l2_ctrl_config ds5_ctrl_ae_setpoint_set = {
	.ops = &ds5_ctrl_ops,
	.id = DS5_CAMERA_CID_AE_SETPOINT_SET,
	.name = "ae setpoint set",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 4095,
	.step = 1,
	.def = 0,
};

static const struct v4l2_ctrl_config ds5_ctrl_erb = {
	.ops = &ds5_ctrl_ops,
	.id = DS5_CAMERA_CID_ERB,
	.name = "ERB eeprom read",
	.type = V4L2_CTRL_TYPE_U8,
	.dims = {1020},
	.elem_size = sizeof(u8),
	.min = 0,
	.max = 0xFFFFFFFF,
	.def = 240,
	.step = 1,
	.step = 1,
};

static const struct v4l2_ctrl_config ds5_ctrl_ewb = {
	.ops = &ds5_ctrl_ops,
	.id = DS5_CAMERA_CID_EWB,
	.name = "EWB eeprom write",
	.type = V4L2_CTRL_TYPE_U8,
	.dims = {1020},
	.elem_size = sizeof(u8),
	.min = 0,
	.max = 0xFFFFFFFF,
	.def = 240,
	.step = 1,
	.step = 1,
};

static const struct v4l2_ctrl_config ds5_ctrl_hwmc = {
	.ops = &ds5_ctrl_ops,
	.id = DS5_CAMERA_CID_HWMC,
	.name = "HWMC",
	.type = V4L2_CTRL_TYPE_U8,
	.dims = {DS5_HWMC_BUFFER_SIZE + 4},
	.elem_size = sizeof(u8),
	.min = 0,
	.max = 0xFFFFFFFF,
	.def = 240,
	.step = 1,
	.step = 1,
};

static const struct v4l2_ctrl_config ds5_ctrl_hwmc_rw = {
	.ops = &ds5_ctrl_ops,
	.id = DS5_CAMERA_CID_HWMC_RW,
	.name = "HWMC_RW",
	.type = V4L2_CTRL_TYPE_U8,
	.dims = {DS5_HWMC_BUFFER_SIZE},
	.elem_size = sizeof(u8),
	.min = 0,
	.max = 0xFFFFFFFF,
	.def = 240,
	.step = 1,
	.flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE,
};

static const struct v4l2_ctrl_config ds5_ctrl_pwm = {
	.ops = &ds5_ctrl_ops,
	.id = DS5_CAMERA_CID_PWM,
	.name = "PWM Frequency Selector",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 1,
	.flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE,
};
#ifdef CONFIG_VIDEO_INTEL_IPU6
static const struct v4l2_ctrl_config d4xx_controls_link_freq = {
	.ops = &ds5_ctrl_ops,
	.id = V4L2_CID_LINK_FREQ,
	.name = "V4L2_CID_LINK_FREQ",
	.type = V4L2_CTRL_TYPE_INTEGER_MENU,
	.max = ARRAY_SIZE(link_freq_menu_items) - 1,
	.min =  0,
	.step  = 0,
	.def = 2,    // default D4XX_LINK_FREQ_300MHZ
	.qmenu_int = link_freq_menu_items,
};

static struct v4l2_ctrl_config d4xx_controls_q_sub_stream = {
	.ops = &ds5_ctrl_ops,
	.id = V4L2_CID_IPU_QUERY_SUB_STREAM,
	.name = "query virtual channel",
	.type = V4L2_CTRL_TYPE_INTEGER_MENU,
	.max = NR_OF_DS5_SUB_STREAMS - 1,
	.min = 0,
	.def = 0,
	.menu_skip_mask = 0,
	.qmenu_int = d4xx_query_sub_stream,
};

static const struct v4l2_ctrl_config d4xx_controls_s_sub_stream = {
	.ops = &ds5_ctrl_ops,
	.id = V4L2_CID_IPU_SET_SUB_STREAM,
	.name = "set virtual channel",
	.type = V4L2_CTRL_TYPE_INTEGER64,
	.max = 0xFFFF,
	.min = 0,
	.def = 0,
	.step = 1,
};
#endif
static int ds5_mux_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ds5 *state = v4l2_get_subdevdata(sd);

	dev_dbg(sd->dev, "%s(): %s (%p)\n", __func__, sd->name, fh);
	if (state->dfu_dev.dfu_state_flag)
		return -EBUSY;
	state->dfu_dev.device_open_count++;

	return 0;
};

static int ds5_mux_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ds5 *state = v4l2_get_subdevdata(sd);

	dev_dbg(sd->dev, "%s(): %s (%p)\n", __func__, sd->name, fh);
	state->dfu_dev.device_open_count--;
	return 0;
};

static const struct v4l2_subdev_internal_ops ds5_sensor_internal_ops = {
	.open = ds5_mux_open,
	.close = ds5_mux_close,
};

#ifdef CONFIG_VIDEO_D4XX_SERDES

/*
 * FIXME
 * temporary solution before changing GMSL data structure or merging all 4 D457
 * sensors into one i2c device. Only first sensor node per max9295 sets up the
 * link.
 *
 * max 24 number from this link:
 * https://docs.nvidia.com/jetson/archives/r35.1/DeveloperGuide/text/
 * SD/CameraDevelopment/JetsonVirtualChannelWithGmslCameraFramework.html
 * #jetson-agx-xavier-series
 */
#define MAX_DEV_NUM 24
static struct ds5 *serdes_inited[MAX_DEV_NUM];
#ifdef CONFIG_OF
static int ds5_board_setup(struct ds5 *state)
{
	struct device *dev = &state->client->dev;
	struct device_node *node = dev->of_node;
	struct device_node *ser_node;
	struct i2c_client *ser_i2c = NULL;
	struct device_node *dser_node;
	struct i2c_client *dser_i2c = NULL;
	struct device_node *gmsl;
	int value = 0xFFFF;
	const char *str_value;
	int err;
	int i;

	err = of_property_read_u32(node, "reg", &state->g_ctx.sdev_reg);
	if (err < 0) {
		dev_err(dev, "reg not found\n");
		goto error;
	}

	err = of_property_read_u32(node, "def-addr",
					&state->g_ctx.sdev_def);
	if (err < 0) {
		dev_err(dev, "def-addr not found\n");
		goto error;
	}

	ser_node = of_parse_phandle(node, "maxim,gmsl-ser-device", 0);
	if (ser_node == NULL) {
		/* check compatibility with jetpack */
		ser_node = of_parse_phandle(node, "nvidia,gmsl-ser-device", 0);
		if (ser_node == NULL) {
			dev_err(dev, "missing %s handle\n", "[maxim|nvidia],gmsl-ser-device");
			goto error;
		}
	}
	err = of_property_read_u32(ser_node, "reg", &state->g_ctx.ser_reg);
	dev_dbg(dev,  "serializer reg: 0x%x\n", state->g_ctx.ser_reg);
	if (err < 0) {
		dev_err(dev, "serializer reg not found\n");
		goto error;
	}

	ser_i2c = of_find_i2c_device_by_node(ser_node);
	of_node_put(ser_node);

	if (ser_i2c == NULL) {
		err = -EPROBE_DEFER;
		goto error;
	}
	if (ser_i2c->dev.driver == NULL) {
		dev_err(dev, "missing serializer driver\n");
		goto error;
	}

	state->ser_dev = &ser_i2c->dev;

	dser_node = of_parse_phandle(node, "maxim,gmsl-dser-device", 0);
	if (dser_node == NULL) {
		dser_node = of_parse_phandle(node, "nvidia,gmsl-dser-device", 0);
		if (dser_node == NULL) {
			dev_err(dev, "missing %s handle\n", "[maxim|nvidia],gmsl-dser-device");
			goto error;
		}
	}

	dser_i2c = of_find_i2c_device_by_node(dser_node);
	of_node_put(dser_node);

	if (dser_i2c == NULL) {
		err = -EPROBE_DEFER;
		goto error;
	}
	if (dser_i2c->dev.driver == NULL) {
		dev_err(dev, "missing deserializer driver\n");
		goto error;
	}

	state->dser_dev = &dser_i2c->dev;

	/* populate g_ctx from DT */
	gmsl = of_get_child_by_name(node, "gmsl-link");
	if (gmsl == NULL) {
		dev_err(dev, "missing gmsl-link device node\n");
		err = -EINVAL;
		goto error;
	}

	err = of_property_read_string(gmsl, "dst-csi-port", &str_value);
	if (err < 0) {
		dev_err(dev, "No dst-csi-port found\n");
		goto error;
	}
	state->g_ctx.dst_csi_port =
		(!strcmp(str_value, "a")) ? GMSL_CSI_PORT_A : GMSL_CSI_PORT_B;

	err = of_property_read_string(gmsl, "src-csi-port", &str_value);
	if (err < 0) {
		dev_err(dev, "No src-csi-port found\n");
		goto error;
	}
	state->g_ctx.src_csi_port =
		(!strcmp(str_value, "a")) ? GMSL_CSI_PORT_A : GMSL_CSI_PORT_B;

	err = of_property_read_string(gmsl, "csi-mode", &str_value);
	if (err < 0) {
		dev_err(dev, "No csi-mode found\n");
		goto error;
	}

	if (!strcmp(str_value, "1x4")) {
		state->g_ctx.csi_mode = GMSL_CSI_1X4_MODE;
	} else if (!strcmp(str_value, "2x4")) {
		state->g_ctx.csi_mode = GMSL_CSI_2X4_MODE;
	} else if (!strcmp(str_value, "4x2")) {
		state->g_ctx.csi_mode = GMSL_CSI_4X2_MODE;
	} else if (!strcmp(str_value, "2x2")) {
		state->g_ctx.csi_mode = GMSL_CSI_2X2_MODE;
	} else {
		dev_err(dev, "invalid csi mode\n");
		goto error;
	}

	err = of_property_read_string(gmsl, "serdes-csi-link", &str_value);
	if (err < 0) {
		dev_err(dev, "No serdes-csi-link found\n");
		goto error;
	}
	state->g_ctx.serdes_csi_link =
		(!strcmp(str_value, "a")) ?
			GMSL_SERDES_CSI_LINK_A : GMSL_SERDES_CSI_LINK_B;

	err = of_property_read_u32(gmsl, "st-vc", &value);
	if (err < 0) {
		dev_err(dev, "No st-vc info\n");
		goto error;
	}
	state->g_ctx.st_vc = value;

	err = of_property_read_u32(gmsl, "vc-id", &value);
	if (err < 0) {
		dev_err(dev, "No vc-id info\n");
		goto error;
	}
	state->g_ctx.dst_vc = value;

	err = of_property_read_u32(gmsl, "num-lanes", &value);
	if (err < 0) {
		dev_err(dev, "No num-lanes info\n");
		goto error;
	}
	state->g_ctx.num_csi_lanes = value;
	state->g_ctx.s_dev = dev;

	for (i = 0; i < MAX_DEV_NUM; i++) {
		if (!serdes_inited[i]) {
			serdes_inited[i] = state;
			return 0;
		} else if (serdes_inited[i]->ser_dev == state->ser_dev) {
			return -ENOTSUPP;
		}
	}
	err = -EINVAL;
	dev_err(dev, "cannot handle more than %d D457 cameras\n", MAX_DEV_NUM);

error:
	return err;
}
#else
// ds5mux i2c ser des
// mux a - 2 0x42 0x48
// mux b - 2 0x44 0x4a
// mux c - 4 0x42 0x48
// mux d - 4 0x44 0x4a
// axiomtek
// mux a - 2 0x42 0x48
// mux b - 2 0x44 0x4a
// mux c - 4 0x62 0x68
// mux d - 4 0x64 0x6a

static int ds5_board_setup(struct ds5 *state)
{
	struct device *dev = &state->client->dev;
	struct d4xx_pdata *pdata = dev->platform_data;
	struct i2c_adapter *adapter = state->client->adapter;
	int bus = adapter->nr;
	int err = 0;
	int i;
	char suffix = pdata->suffix;
	static struct max9295_pdata max9295_pdata = {
		.is_prim_ser = 1, // todo: configurable
		.def_addr = 0x40, // todo: configurable
	};
	static struct max9296_pdata max9296_pdata = {
		.max_src = 2,
		.csi_mode = GMSL_CSI_2X4_MODE,
	};
	static struct i2c_board_info i2c_info_des = {
		I2C_BOARD_INFO("max9296", 0x48),
		.platform_data = &max9296_pdata,
	};
	static struct i2c_board_info i2c_info_ser = {
		I2C_BOARD_INFO("max9295", 0x42),
		.platform_data = &max9295_pdata,
	};

	i2c_info_ser.addr = pdata->subdev_info[0].ser_alias; //0x42, 0x44, 0x62, 0x64
	state->ser_i2c = i2c_new_client_device(adapter, &i2c_info_ser);

	i2c_info_des.addr = pdata->subdev_info[0].board_info.addr; //0x48, 0x4a, 0x68, 0x6a

	/* look for already registered max9296, use same context if found */
	for (i = 0; i < MAX_DEV_NUM; i++) {
		if (serdes_inited[i] && serdes_inited[i]->dser_i2c) {
			dev_info(dev, "MAX9296 found device on %d@0x%x\n",
				serdes_inited[i]->dser_i2c->adapter->nr, serdes_inited[i]->dser_i2c->addr);
			if (bus == serdes_inited[i]->dser_i2c->adapter->nr
				&& serdes_inited[i]->dser_i2c->addr == i2c_info_des.addr) {
				dev_info(dev, "MAX9296 AGGREGATION found device on 0x%x\n", i2c_info_des.addr);
				state->dser_i2c = serdes_inited[i]->dser_i2c;
				state->aggregated = 1;
			}
		}
	}
	if (state->aggregated)
		suffix += 4;
	dev_info(dev, "Init SerDes %c on %d@0x%x<->%d@0x%x\n",
		suffix,
		bus, pdata->subdev_info[0].board_info.addr, //48
		bus, pdata->subdev_info[0].ser_alias); //42

	if (!state->dser_i2c)
		state->dser_i2c = i2c_new_client_device(adapter, &i2c_info_des);

	if (state->ser_i2c == NULL) {
		err = -EPROBE_DEFER;
		dev_err(dev, "missing serializer client\n");
		goto error;
	}
	if (state->ser_i2c->dev.driver == NULL) {
		err = -EPROBE_DEFER;
		dev_err(dev, "missing serializer driver\n");
		goto error;
	}
	if (state->dser_i2c == NULL) {
		err = -EPROBE_DEFER;
		dev_err(dev, "missing deserializer client\n");
		goto error;
	}
	if (state->dser_i2c->dev.driver == NULL) {
		err = -EPROBE_DEFER;
		dev_err(dev, "missing deserializer driver\n");
		goto error;
	}

	// reg

	state->g_ctx.sdev_reg = state->client->addr;
	state->g_ctx.sdev_def = 0x10;// def-addr TODO: configurable
	// Address reassignment for d4xx-a 0x10->0x12
	dev_info(dev, "Address reassignment for %s-%c 0x%x->0x%x\n",
		pdata->subdev_info[0].board_info.type, suffix,
		state->g_ctx.sdev_def, state->g_ctx.sdev_reg);
	//0x42, 0x44, 0x62, 0x64
	state->g_ctx.ser_reg = pdata->subdev_info[0].ser_alias;
	dev_info(dev,  "serializer: i2c-%d@0x%x\n",
		state->ser_i2c->adapter->nr, state->g_ctx.ser_reg);

	if (err < 0) {
		dev_err(dev, "serializer reg not found\n");
		goto error;
	}

	state->ser_dev = &state->ser_i2c->dev;

	dev_info(dev,  "deserializer: i2c-%d@0x%x\n",
		state->dser_i2c->adapter->nr, state->dser_i2c->addr);


	state->dser_dev = &state->dser_i2c->dev;

	/* populate g_ctx from pdata */
	state->g_ctx.dst_csi_port = GMSL_CSI_PORT_A;
	state->g_ctx.src_csi_port = GMSL_CSI_PORT_B;
	state->g_ctx.csi_mode = GMSL_CSI_1X4_MODE;
	if (state->aggregated) { // aggregation
		dev_info(dev,  "configure GMSL port B\n");
		state->g_ctx.serdes_csi_link = GMSL_SERDES_CSI_LINK_B;
	} else {
		dev_info(dev,  "configure GMSL port A\n");
		state->g_ctx.serdes_csi_link = GMSL_SERDES_CSI_LINK_A;
	}
	state->g_ctx.st_vc = 0;
	state->g_ctx.dst_vc = 0;

	state->g_ctx.num_csi_lanes = 2;
	state->g_ctx.s_dev = dev;

	for (i = 0; i < MAX_DEV_NUM; i++) {
		if (!serdes_inited[i]) {
			serdes_inited[i] = state;
			return 0;
		} else if (serdes_inited[i]->ser_dev == state->ser_dev) {
			return -ENOTSUPP;
		}
	}
	err = -EINVAL;
	dev_err(dev, "cannot handle more than %d D457 cameras\n", MAX_DEV_NUM);

error:
	return err;
}

#endif
static const struct regmap_config ds5_regmap_max9296 = {
	.reg_bits = 16,
	.val_bits = 8,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
};

static const struct regmap_config ds5_regmap_max9295 = {
	.reg_bits = 16,
	.val_bits = 8,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
};
static struct mutex serdes_lock__;

static int ds5_gmsl_serdes_setup(struct ds5 *state)
{
	int err = 0;
	int des_err = 0;
	struct device *dev;

	if (!state || !state->ser_dev || !state->dser_dev || !state->client)
		return -EINVAL;

	dev = &state->client->dev;

	mutex_lock(&serdes_lock__);

	max9296_power_off(state->dser_dev);
	/* For now no separate power on required for serializer device */
	max9296_power_on(state->dser_dev);

	dev_dbg(dev, "Setup SERDES addressing and control pipeline\n");
	/* setup serdes addressing and control pipeline */
	err = max9296_setup_link(state->dser_dev, &state->client->dev);
	if (err) {
		dev_err(dev, "gmsl deserializer link config failed\n");
		goto error;
	}
	msleep(100);
	err = max9295_setup_control(state->ser_dev);

	/* proceed even if ser setup failed, to setup deser correctly */
	if (err)
		dev_err(dev, "gmsl serializer setup failed\n");

	des_err = max9296_setup_control(state->dser_dev, &state->client->dev);
	if (des_err) {
		dev_err(dev, "gmsl deserializer setup failed\n");
		/* overwrite err only if deser setup also failed */
		err = des_err;
	}

error:
	mutex_unlock(&serdes_lock__);
	return err;
}

#ifdef CONFIG_VIDEO_INTEL_IPU6
static short sensor_vc[NR_OF_DS5_STREAMS * 2] = {0,1,2,3, 2,3,0,1};
module_param_array(sensor_vc, ushort, NULL, 0444);
MODULE_PARM_DESC(sensor_vc, "VC set for sensors\n"
		"\t\tsensor_vc=0,1,2,3,2,3,0,1");

//#define PLATFORM_AXIOMTEK 1
#ifdef PLATFORM_AXIOMTEK
static short serdes_bus[4] = {5, 5, 5, 5};
#else
static short serdes_bus[4] = {2, 2, 4, 4};
#endif
module_param_array(serdes_bus, ushort, NULL, 0444);
MODULE_PARM_DESC(serdes_bus, "max9295/6 deserializer i2c bus\n"
		"\t\tserdes_bus=2,2,4,4");

// Deserializer addresses can be 0x40 0x48 0x4a
#ifdef PLATFORM_AXIOMTEK
static unsigned short des_addr[4] = {0x48, 0x4a, 0x68, 0x6c};
#else
static unsigned short des_addr[4] = {0x48, 0x4a, 0x48, 0x4a};
#endif
module_param_array(des_addr, ushort, NULL, 0444);
MODULE_PARM_DESC(des_addr, "max9296 deserializer i2c address\n"
		"\t\tdes_addr=0x48,0x4a,0x48,0x4a");


static int ds5_i2c_addr_setting(struct i2c_client *c, struct ds5 *state)
{
	int i = 0;
	int c_addr_save = c->addr;
	int c_bus = c->adapter->nr;
	for (i = 0; i < 4; i++) {
		if (c_bus == serdes_bus[i]) {
			c->addr = des_addr[i];
			dev_info(&c->dev, "Set max9296@%d-0x%x Link reset\n",
					c_bus, c->addr);
			ds5_write_8(state, 0x1000, 0x40); // reset link
		}
	}
	// restore original slave address
	c->addr = c_addr_save;

	return 0;
}
#endif
static int ds5_serdes_setup(struct ds5 *state)
{
	int ret = 0;
	struct i2c_client *c = state->client;
#ifdef CONFIG_VIDEO_INTEL_IPU6
	int i = 0, c_bus = 0;
	int c_bus_new = c->adapter->nr;

	for (i = 0; i < MAX_DEV_NUM; i++) {
		if (serdes_inited[i] && serdes_inited[i]->dser_i2c) {
			c_bus = serdes_inited[i]->dser_i2c->adapter->nr;
			if (c_bus == c->adapter->nr) {
				dev_info(&c->dev, "Already configured multiple camera for bus %d\n", c_bus);
				c_bus_new = 0;
				break;
			}
		} else {
			break;
		}
	}

	if (c_bus_new) {
		dev_info(&c->dev, "Apply multiple camera i2c addr setting for bus %d\n", c_bus_new);
		ret = ds5_i2c_addr_setting(c, state);
		if (ret) {
			dev_err(&c->dev, "failed apply i2c addr setting\n");
			return ret;
		}
	}
#endif
	ret = ds5_board_setup(state);
	if (ret) {
		if (ret == -ENOTSUPP)
			return 0;
		dev_err(&c->dev, "board setup failed\n");
		return ret;
	}

	/* Pair sensor to serializer dev */
	ret = max9295_sdev_pair(state->ser_dev, &state->g_ctx);
	if (ret) {
		dev_err(&c->dev, "gmsl ser pairing failed\n");
		return ret;
	}

	/* Register sensor to deserializer dev */
	ret = max9296_sdev_register(state->dser_dev, &state->g_ctx);
	if (ret) {
		dev_err(&c->dev, "gmsl deserializer register failed\n");
		return ret;
	}

	ret = ds5_gmsl_serdes_setup(state);
	if (ret) {
		dev_err(&c->dev, "%s gmsl serdes setup failed\n", __func__);
		return ret;
	}

	ret = max9295_init_settings(state->ser_dev);
	if (ret) {
		dev_warn(&c->dev, "%s, failed to init max9295 settings\n",
			__func__);
		return ret;
	}

	ret = max9296_init_settings(state->dser_dev);
	if (ret) {
		dev_warn(&c->dev, "%s, failed to init max9296 settings\n",
			__func__);
		return ret;
	}

	return ret;
}
#endif
enum state_sid {
	DEPTH_SID = 0,
	RGB_SID,
	IR_SID,
	IMU_SID,
	MUX_SID = -1
};

static int ds5_ctrl_init(struct ds5 *state, int sid)
{
	const struct v4l2_ctrl_ops *ops = &ds5_ctrl_ops;
	struct ds5_ctrls *ctrls = &state->ctrls;
	struct v4l2_ctrl_handler *hdl = &ctrls->handler;
	struct v4l2_subdev *sd = &state->mux.sd.subdev;
	int ret = -1;
	struct ds5_sensor *sensor = NULL;

	switch (sid) {
	case DEPTH_SID:
		hdl = &ctrls->handler_depth;
		sensor = &state->depth.sensor;
		break;
	case RGB_SID:
		hdl = &ctrls->handler_rgb;
		sensor = &state->rgb.sensor;
		break;
	case IR_SID:
		hdl = &ctrls->handler_y8;
		sensor = &state->ir.sensor;
		break;
	case IMU_SID:
		hdl = &ctrls->handler_imu;
		sensor = &state->imu.sensor;
		break;
	default:
		/* control for MUX */
		hdl = &ctrls->handler;
		sensor = NULL;
		break;
	}

	dev_dbg(NULL, "%s():%d sid: %d\n", __func__, __LINE__, sid);
	ret = v4l2_ctrl_handler_init(hdl, DS5_N_CONTROLS);
	if (ret < 0) {
		v4l2_err(sd, "cannot init ctrl handler (%d)\n", ret);
		return ret;
	}

	if (sid == DEPTH_SID || sid == IR_SID) {
		ctrls->laser_power = v4l2_ctrl_new_custom(hdl,
						&ds5_ctrl_laser_power,
						sensor);
		ctrls->manual_laser_power = v4l2_ctrl_new_custom(hdl,
						&ds5_ctrl_manual_laser_power,
						sensor);
	}

	/* Total gain */
	if (sid == DEPTH_SID || sid == IR_SID) {
		ctrls->gain = v4l2_ctrl_new_std(hdl, ops,
						V4L2_CID_ANALOGUE_GAIN,
						16, 248, 1, 16);
	} else if (sid == RGB_SID) {
		ctrls->gain = v4l2_ctrl_new_std(hdl, ops,
						V4L2_CID_ANALOGUE_GAIN,
						0, 128, 1, 64);
	}

	if ((ctrls->gain) && (sid >= DEPTH_SID && sid < IMU_SID)) {
		ctrls->gain->priv = sensor;
		ctrls->gain->flags =
				V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
	}
	if (sid >= DEPTH_SID && sid < IMU_SID) {

		ctrls->auto_exp = v4l2_ctrl_new_std_menu(hdl, ops,
				V4L2_CID_EXPOSURE_AUTO,
				V4L2_EXPOSURE_APERTURE_PRIORITY,
				~((1 << V4L2_EXPOSURE_MANUAL) |
						(1 << V4L2_EXPOSURE_APERTURE_PRIORITY)),
						V4L2_EXPOSURE_APERTURE_PRIORITY);

		if (ctrls->auto_exp) {
			ctrls->auto_exp->flags |=
					V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
			ctrls->auto_exp->priv = sensor;
		}
	}

	/* Exposure time: V4L2_CID_EXPOSURE_ABSOLUTE default unit: 100 us. */
	if (sid == DEPTH_SID || sid == IR_SID) {
		ctrls->exposure = v4l2_ctrl_new_std(hdl, ops,
					V4L2_CID_EXPOSURE_ABSOLUTE,
					1, MAX_DEPTH_EXP, 1, DEF_DEPTH_EXP);
	} else if (sid == RGB_SID) {
		ctrls->exposure = v4l2_ctrl_new_std(hdl, ops,
					V4L2_CID_EXPOSURE_ABSOLUTE,
					1, MAX_RGB_EXP, 1, DEF_RGB_EXP);
	}

	if ((ctrls->exposure) && (sid >= DEPTH_SID && sid < IMU_SID)) {
		ctrls->exposure->priv = sensor;
		ctrls->exposure->flags |=
				V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;
		/* override default int type to u32 to match SKU & UVC */
		ctrls->exposure->type = V4L2_CTRL_TYPE_U32;
	}
#ifdef CONFIG_VIDEO_INTEL_IPU6
	ctrls->link_freq = v4l2_ctrl_new_custom(hdl, &d4xx_controls_link_freq, sensor);
	/* MTL and RPL/ADL IPU6 CSI-DPHY do NOT share
	 *  the same default link_freq.
	 * V4L2_CID_LINK_FREQ must be R/W for udev ot set DPHY platform specific link_freq
	 * at systemd boottime.
	if (ctrls->link_freq)
		ctrls->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	*/
	if (state->aggregated) {
		d4xx_controls_q_sub_stream.def = NR_OF_DS5_SUB_STREAMS;
		d4xx_controls_q_sub_stream.min = NR_OF_DS5_SUB_STREAMS;
		d4xx_controls_q_sub_stream.max = NR_OF_DS5_SUB_STREAMS * 2 - 1;
	}
	ctrls->query_sub_stream = v4l2_ctrl_new_custom(hdl, &d4xx_controls_q_sub_stream, sensor);

	if (ctrls->query_sub_stream)
		ctrls->query_sub_stream->flags |=
		V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE;

	ctrls->set_sub_stream = v4l2_ctrl_new_custom(hdl, &d4xx_controls_s_sub_stream, sensor);
#endif
	if (hdl->error) {
		v4l2_err(sd, "error creating controls (%d)\n", hdl->error);
		ret = hdl->error;
		v4l2_ctrl_handler_free(hdl);
		return ret;
	}

	// Add these after v4l2_ctrl_handler_setup so they won't be set up
	if (sid >= DEPTH_SID && sid < IMU_SID) {
		ctrls->log = v4l2_ctrl_new_custom(hdl, &ds5_ctrl_log, sensor);
		ctrls->fw_version = v4l2_ctrl_new_custom(hdl, &ds5_ctrl_fw_version, sensor);
		ctrls->gvd = v4l2_ctrl_new_custom(hdl, &ds5_ctrl_gvd, sensor);
		ctrls->get_depth_calib =
				v4l2_ctrl_new_custom(hdl, &ds5_ctrl_get_depth_calib, sensor);
		ctrls->set_depth_calib =
				v4l2_ctrl_new_custom(hdl, &ds5_ctrl_set_depth_calib, sensor);
		ctrls->get_coeff_calib =
				v4l2_ctrl_new_custom(hdl, &ds5_ctrl_get_coeff_calib, sensor);
		ctrls->set_coeff_calib =
				v4l2_ctrl_new_custom(hdl, &ds5_ctrl_set_coeff_calib, sensor);
		ctrls->ae_roi_get = v4l2_ctrl_new_custom(hdl, &ds5_ctrl_ae_roi_get, sensor);
		ctrls->ae_roi_set = v4l2_ctrl_new_custom(hdl, &ds5_ctrl_ae_roi_set, sensor);
		ctrls->ae_setpoint_get =
				v4l2_ctrl_new_custom(hdl, &ds5_ctrl_ae_setpoint_get, sensor);
		ctrls->ae_setpoint_set =
				v4l2_ctrl_new_custom(hdl, &ds5_ctrl_ae_setpoint_set, sensor);
		ctrls->erb = v4l2_ctrl_new_custom(hdl, &ds5_ctrl_erb, sensor);
		ctrls->ewb = v4l2_ctrl_new_custom(hdl, &ds5_ctrl_ewb, sensor);
		ctrls->hwmc = v4l2_ctrl_new_custom(hdl, &ds5_ctrl_hwmc, sensor);
		v4l2_ctrl_new_custom(hdl, &ds5_ctrl_hwmc_rw, sensor);
	}
	// DEPTH custom
	if (sid == DEPTH_SID)
		v4l2_ctrl_new_custom(hdl, &ds5_ctrl_pwm, sensor);
	// IMU custom
	if (sid == IMU_SID)
		ctrls->fw_version = v4l2_ctrl_new_custom(hdl, &ds5_ctrl_fw_version, sensor);

	switch (sid) {
	case DEPTH_SID:
		state->depth.sensor.sd.ctrl_handler = hdl;
		dev_dbg(state->depth.sensor.sd.dev,
			"%s():%d set ctrl_handler pad:%d\n",
			__func__, __LINE__, state->depth.sensor.mux_pad);
		break;
	case RGB_SID:
		state->rgb.sensor.sd.ctrl_handler = hdl;
		dev_dbg(state->rgb.sensor.sd.dev,
			"%s():%d set ctrl_handler pad:%d\n",
			__func__, __LINE__, state->rgb.sensor.mux_pad);
		break;
	case IR_SID:
		state->ir.sensor.sd.ctrl_handler = hdl;
		dev_dbg(state->ir.sensor.sd.dev,
			"%s():%d set ctrl_handler pad:%d\n",
			__func__, __LINE__, state->ir.sensor.mux_pad);
		break;
	case IMU_SID:
		state->imu.sensor.sd.ctrl_handler = hdl;
		dev_dbg(state->imu.sensor.sd.dev,
			"%s():%d set ctrl_handler pad:%d\n",
			__func__, __LINE__, state->imu.sensor.mux_pad);
		break;
	default:
		state->mux.sd.subdev.ctrl_handler = hdl;
		dev_dbg(state->mux.sd.subdev.dev,
			"%s():%d set ctrl_handler for MUX\n", __func__, __LINE__);
		break;
	}

	return 0;
}

static int ds5_sensor_init(struct i2c_client *c, struct ds5 *state,
		struct ds5_sensor *sensor, const struct v4l2_subdev_ops *ops,
		const char *name)
{
	struct v4l2_subdev *sd = &sensor->sd;
	struct media_entity *entity = &sensor->sd.entity;
	struct media_pad *pad = &sensor->pad;
	dev_t *dev_num = &state->client->dev.devt;
#ifndef CONFIG_OF
	struct d4xx_pdata *dpdata = c->dev.platform_data;
	char suffix = dpdata->suffix;
#endif
	v4l2_i2c_subdev_init(sd, c, ops);
	// See tegracam_v4l2.c tegracam_v4l2subdev_register()
	// Set owner to NULL so we can unload the driver module
	sd->owner = NULL;
	sd->internal_ops = &ds5_sensor_internal_ops;
	sd->grp_id = *dev_num;
	v4l2_set_subdevdata(sd, state);
#ifndef CONFIG_OF
	/*
	 * TODO: suffix for 2 D457 connected to 1 Deser
	 */
	if (state->aggregated & 1)
		suffix += 4;
	snprintf(sd->name, sizeof(sd->name), "D4XX %s %c", name, suffix);
#else
	snprintf(sd->name, sizeof(sd->name), "D4XX %s %d-%04x",
		 name, i2c_adapter_id(c->adapter), c->addr);
#endif

	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	pad->flags = MEDIA_PAD_FL_SOURCE;
	entity->obj_type = MEDIA_ENTITY_TYPE_V4L2_SUBDEV;
	entity->function = MEDIA_ENT_F_CAM_SENSOR;
	return media_entity_pads_init(entity, 1, pad);
}

static int ds5_sensor_register(struct ds5 *state, struct ds5_sensor *sensor)
{
	struct v4l2_subdev *sd = &sensor->sd;
	struct media_entity *entity = &sensor->sd.entity;
	int ret = -1;

	// FIXME: is async needed?
	ret = v4l2_device_register_subdev(state->mux.sd.subdev.v4l2_dev, sd);
	if (ret < 0) {
		dev_err(sd->dev, "%s(): %d: %d\n", __func__, __LINE__, ret);
		return ret;
	}

	ret = media_create_pad_link(entity, 0,
			&state->mux.sd.subdev.entity, sensor->mux_pad,
			MEDIA_LNK_FL_IMMUTABLE | MEDIA_LNK_FL_ENABLED);
	if (ret < 0) {
		dev_err(sd->dev, "%s(): %d: %d\n", __func__, __LINE__, ret);
		goto e_sd;
	}

	dev_dbg(sd->dev, "%s(): 0 -> %d\n", __func__, sensor->mux_pad);

	return 0;

e_sd:
	v4l2_device_unregister_subdev(sd);

	return ret;
}

static void ds5_sensor_remove(struct ds5_sensor *sensor)
{
	v4l2_device_unregister_subdev(&sensor->sd);

	media_entity_cleanup(&sensor->sd.entity);
}

static int ds5_depth_init(struct i2c_client *c, struct ds5 *state)
{
	/* Which mux pad we're connecting to */
	state->depth.sensor.mux_pad = DS5_MUX_PAD_DEPTH;
	return ds5_sensor_init(c, state, &state->depth.sensor,
		       &ds5_depth_subdev_ops, "depth");
}

static int ds5_ir_init(struct i2c_client *c, struct ds5 *state)
{
	state->ir.sensor.mux_pad = DS5_MUX_PAD_IR;
	return ds5_sensor_init(c, state, &state->ir.sensor,
		       &ds5_ir_subdev_ops, "ir");
}

static int ds5_rgb_init(struct i2c_client *c, struct ds5 *state)
{
	state->rgb.sensor.mux_pad = DS5_MUX_PAD_RGB;
	return ds5_sensor_init(c, state, &state->rgb.sensor,
		       &ds5_rgb_subdev_ops, "rgb");
}

static int ds5_imu_init(struct i2c_client *c, struct ds5 *state)
{
	state->imu.sensor.mux_pad = DS5_MUX_PAD_IMU;
	return ds5_sensor_init(c, state, &state->imu.sensor,
		       &ds5_imu_subdev_ops, "imu");
}

/* No locking needed */
static int ds5_mux_enum_mbus_code(struct v4l2_subdev *sd,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
				     struct v4l2_subdev_pad_config *cfg,
#else
				     struct v4l2_subdev_state *v4l2_state,
#endif
				  struct v4l2_subdev_mbus_code_enum *mce)
{
	struct ds5 *state = container_of(sd, struct ds5, mux.sd.subdev);
	struct v4l2_subdev_mbus_code_enum tmp = *mce;
	struct v4l2_subdev *remote_sd;
	int ret = -1;

	dev_dbg(&state->client->dev, "%s(): %s \n", __func__, sd->name);
	switch (mce->pad) {
	case DS5_MUX_PAD_IR:
		remote_sd = &state->ir.sensor.sd;
		break;
	case DS5_MUX_PAD_DEPTH:
		remote_sd = &state->depth.sensor.sd;
		break;
	case DS5_MUX_PAD_RGB:
		remote_sd = &state->rgb.sensor.sd;
		break;
	case DS5_MUX_PAD_IMU:
		remote_sd = &state->imu.sensor.sd;
		break;
	case DS5_MUX_PAD_EXTERNAL:
		if (mce->index >= state->ir.sensor.n_formats +
				state->depth.sensor.n_formats)
			return -EINVAL;

		/*
		 * First list Left node / Motion Tracker formats, then depth.
		 * This should also help because D16 doesn't have a direct
		 * analog in MIPI CSI-2.
		 */
		if (mce->index < state->ir.sensor.n_formats) {
			remote_sd = &state->ir.sensor.sd;
		} else {
			tmp.index = mce->index - state->ir.sensor.n_formats;
			remote_sd = &state->depth.sensor.sd;
		}

		break;
	default:
		return -EINVAL;
	}

	tmp.pad = 0;
	if (state->is_rgb)
		remote_sd = &state->rgb.sensor.sd;
	if (state->is_depth)
		remote_sd = &state->depth.sensor.sd;
	if (state->is_y8)
		remote_sd = &state->ir.sensor.sd;
	if (state->is_imu)
		remote_sd = &state->imu.sensor.sd;
	/* Locks internally */
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
	ret = ds5_sensor_enum_mbus_code(remote_sd, cfg, &tmp);
#else
	ret = ds5_sensor_enum_mbus_code(remote_sd, v4l2_state, &tmp);
#endif
	if (!ret)
		mce->code = tmp.code;

	return ret;
}
static int ds5_state_to_pad(struct ds5 *state) {
	int pad = -1;
	if (state->is_depth)
		pad = DS5_MUX_PAD_DEPTH;
	if (state->is_y8)
		pad = DS5_MUX_PAD_IR;
	if (state->is_rgb)
		pad = DS5_MUX_PAD_RGB;
	if (state->is_imu)
		pad = DS5_MUX_PAD_IMU;
	return pad;
}

/* No locking needed */
static int ds5_mux_enum_frame_size(struct v4l2_subdev *sd,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
				     struct v4l2_subdev_pad_config *cfg,
#else
				     struct v4l2_subdev_state *v4l2_state,
#endif
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct ds5 *state = container_of(sd, struct ds5, mux.sd.subdev);
	struct v4l2_subdev_frame_size_enum tmp = *fse;
	struct v4l2_subdev *remote_sd;
	u32 pad = fse->pad;
	int ret = -1;

	tmp.pad = 0;
	pad = ds5_state_to_pad(state);

	switch (pad) {
	case DS5_MUX_PAD_IR:
		remote_sd = &state->ir.sensor.sd;
		break;
	case DS5_MUX_PAD_DEPTH:
		remote_sd = &state->depth.sensor.sd;
		break;
	case DS5_MUX_PAD_RGB:
		remote_sd = &state->rgb.sensor.sd;
		break;
	case DS5_MUX_PAD_IMU:
		remote_sd = &state->imu.sensor.sd;
		break;
	case DS5_MUX_PAD_EXTERNAL:
		/*
		 * Assume, that different sensors don't support the same formats
		 * Try the Depth sensor first, then the Motion Tracker
		 */
		remote_sd = &state->depth.sensor.sd;
		ret = ds5_sensor_enum_frame_size(remote_sd, NULL, &tmp);
		if (!ret) {
			*fse = tmp;
			fse->pad = pad;
			return 0;
		}

		remote_sd = &state->ir.sensor.sd;
		break;
	default:
		return -EINVAL;
	}

	/* Locks internally */
	ret = ds5_sensor_enum_frame_size(remote_sd, NULL, &tmp);
	if (!ret) {
		*fse = tmp;
		fse->pad = pad;
	}

	return ret;
}

/* No locking needed */
static int ds5_mux_enum_frame_interval(struct v4l2_subdev *sd,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
				     struct v4l2_subdev_pad_config *cfg,
#else
				     struct v4l2_subdev_state *v4l2_state,
#endif
				     struct v4l2_subdev_frame_interval_enum *fie)
{
	struct ds5 *state = container_of(sd, struct ds5, mux.sd.subdev);
	struct v4l2_subdev_frame_interval_enum tmp = *fie;
	struct v4l2_subdev *remote_sd;
	u32 pad = fie->pad;
	int ret = -1;

	tmp.pad = 0;

	dev_dbg(state->depth.sensor.sd.dev,
			"%s(): pad %d code %x width %d height %d\n",
			__func__, pad, tmp.code, tmp.width, tmp.height);

	pad = ds5_state_to_pad(state);

	switch (pad) {
	case DS5_MUX_PAD_IR:
		remote_sd = &state->ir.sensor.sd;
		break;
	case DS5_MUX_PAD_DEPTH:
		remote_sd = &state->depth.sensor.sd;
		break;
	case DS5_MUX_PAD_RGB:
		remote_sd = &state->rgb.sensor.sd;
		break;
	case DS5_MUX_PAD_IMU:
		remote_sd = &state->imu.sensor.sd;
		break;
	case DS5_MUX_PAD_EXTERNAL:
		/* Similar to ds5_mux_enum_frame_size() above */
		if (state->is_rgb)
			remote_sd = &state->rgb.sensor.sd;
		else
			remote_sd = &state->ir.sensor.sd;
		ret = ds5_sensor_enum_frame_interval(remote_sd, NULL, &tmp);
		if (!ret) {
			*fie = tmp;
			fie->pad = pad;
			return 0;
		}

		remote_sd = &state->ir.sensor.sd;
		break;
	default:
		return -EINVAL;
	}

	/* Locks internally */
	ret = ds5_sensor_enum_frame_interval(remote_sd, NULL, &tmp);
	if (!ret) {
		*fie = tmp;
		fie->pad = pad;
	}

	return ret;
}

/* No locking needed */
static int ds5_mux_set_fmt(struct v4l2_subdev *sd,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
		struct v4l2_subdev_pad_config *cfg,
#else
		struct v4l2_subdev_state *v4l2_state,
#endif
		struct v4l2_subdev_format *fmt)
{
	struct ds5 *state = container_of(sd, struct ds5, mux.sd.subdev);
	struct v4l2_mbus_framefmt *ffmt;
	struct ds5_sensor *sensor = state->mux.last_set;
	u32 pad = sensor->mux_pad;
	int ret = 0;
#ifdef CONFIG_VIDEO_INTEL_IPU6
	int substream = -1;
		dev_dbg(sd->dev, "%s:%d: fmt->pad:%d, sensor->mux_pad: %d, \
		 for sensor: %s\n",
		__func__, __LINE__,
		fmt->pad, pad,
		sensor->sd.name);

#endif

	if (pad != DS5_MUX_PAD_EXTERNAL)
		ds5_s_state_pad(state, pad);
	sensor = state->mux.last_set;
	switch (pad) {
	case DS5_MUX_PAD_DEPTH:
	case DS5_MUX_PAD_IR:
	case DS5_MUX_PAD_RGB:
	case DS5_MUX_PAD_IMU:
		//ffmt = &ds5_ffmts[pad];
		ffmt = &sensor->format;//ds5_ffmts[pad];
		break;
	case DS5_MUX_PAD_EXTERNAL:
		ffmt = &ds5_ffmts[pad];
		break;
	default:
		return -EINVAL;
	}

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		ffmt->width = fmt->format.width;
		ffmt->height = fmt->format.height;
		ffmt->code = fmt->format.code;
	}
	fmt->format = *ffmt;
#ifdef CONFIG_VIDEO_INTEL_IPU6
	// substream = pad_to_substream[fmt->pad];
	substream = state->pad_to_substream[pad];

	if (substream != -1) {
		set_sub_stream_fmt(substream, ffmt->code);
		set_sub_stream_h(substream, ffmt->height);
		set_sub_stream_w(substream, ffmt->width);
		set_sub_stream_dt(substream, mbus_code_to_mipi(ffmt->code));
	}

	dev_dbg(sd->dev, "%s(): fmt->pad:%d, sensor->mux_pad: %d, \
		code: 0x%x: %ux%u substream:%d for sensor: %s\n",
		__func__,
		fmt->pad, pad, fmt->format.code,
		fmt->format.width, fmt->format.height,
		substream, sensor->sd.name);
#endif
	return ret;
}

/* No locking needed */
static int ds5_mux_get_fmt(struct v4l2_subdev *sd,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
				     struct v4l2_subdev_pad_config *cfg,
#else
				     struct v4l2_subdev_state *v4l2_state,
#endif
			   struct v4l2_subdev_format *fmt)
{
	struct ds5 *state = container_of(sd, struct ds5, mux.sd.subdev);
	u32 pad = fmt->pad;
	int ret = 0;
	struct ds5_sensor *sensor = state->mux.last_set;
#ifdef CONFIG_VIDEO_INTEL_IPU6
	pad = sensor->mux_pad;
	if (pad != DS5_MUX_PAD_EXTERNAL)
		ds5_s_state_pad(state, pad);
#else
	pad = ds5_state_to_pad(state);
#endif
	sensor = state->mux.last_set;
	dev_dbg(sd->dev, "%s(): %u %s %p\n", __func__, pad, ds5_get_sensor_name(state), state->mux.last_set);

	switch (pad) {
	case DS5_MUX_PAD_DEPTH:
	case DS5_MUX_PAD_IR:
	case DS5_MUX_PAD_RGB:
	case DS5_MUX_PAD_IMU:
		fmt->format = sensor->format;
		break;
	case DS5_MUX_PAD_EXTERNAL:
	fmt->format = ds5_ffmts[pad];
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(sd->dev, "%s(): fmt->pad:%d, sensor->mux_pad:%u size:%d-%d, code:0x%x field:%d, color:%d\n",
		__func__, fmt->pad, pad,
		fmt->format.width, fmt->format.height, fmt->format.code,
		fmt->format.field, fmt->format.colorspace);
	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
/* pad ops */
static int ds5_mux_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *sd_state,
				    struct v4l2_subdev_frame_interval *fi)
#else
/* Video ops */
static int ds5_mux_g_frame_interval(struct v4l2_subdev *sd,
		struct v4l2_subdev_frame_interval *fi)
#endif
{
	struct ds5 *state = container_of(sd, struct ds5, mux.sd.subdev);
	struct ds5_sensor *sensor = NULL;

	if (NULL == sd || NULL == fi)
		return -EINVAL;

	sensor = state->mux.last_set;

	fi->interval.numerator = 1;
	fi->interval.denominator = sensor->config.framerate;

	dev_dbg(sd->dev, "%s(): %s %u\n", __func__, sd->name,
			fi->interval.denominator);

	return 0;
}

static u16 __ds5_probe_framerate(const struct ds5_resolution *res, u16 target)
{
	int i;
	u16 framerate;

	for (i = 0; i < res->n_framerates; i++) {
		framerate = res->framerates[i];
		if (target <= framerate)
			return framerate;
	}

	return res->framerates[res->n_framerates - 1];
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
/* pad ops */
static int ds5_mux_s_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *sd_state,
				    struct v4l2_subdev_frame_interval *fi)
#else
/* Video ops */
static int ds5_mux_s_frame_interval(struct v4l2_subdev *sd,
		struct v4l2_subdev_frame_interval *fi)
#endif
{
	struct ds5 *state = container_of(sd, struct ds5, mux.sd.subdev);
	struct ds5_sensor *sensor = NULL;
	u16 framerate = 1;

	if (NULL == sd || NULL == fi || fi->interval.numerator == 0)
		return -EINVAL;

	sensor = state->mux.last_set;

	framerate = fi->interval.denominator / fi->interval.numerator;
	framerate = __ds5_probe_framerate(sensor->config.resolution, framerate);
	sensor->config.framerate = framerate;
	fi->interval.numerator = 1;
	fi->interval.denominator = framerate;

	dev_dbg(sd->dev, "%s(): %s %u\n", __func__, sd->name, framerate);

	return 0;
}

#ifdef CONFIG_VIDEO_INTEL_IPU6
#ifndef CONFIG_VIDEO_D4XX_SERDES
int d4xx_reset_oneshot(struct ds5 *state)
{
	struct d4xx_pdata *dpdata = state->client->dev.platform_data;
	struct i2c_board_info *deser = dpdata->deser_board_info;

	int s_addr = state->client->addr;
	int n_addr = deser->addr;
	int ret = 0;

	if (n_addr) {
		state->client->addr = n_addr;
		dev_warn(&state->client->dev, "One-shot reset 0x%x enable auto-link\n", n_addr);
		/* One-shot reset  enable auto-link */
		ret = max9296_write_8(state, MAX9296_CTRL0, RESET_ONESHOT | AUTO_LINK | LINK_A);
		state->client->addr = s_addr;
		/* delay to settle link */
		msleep(100);
	}

	return ret;
}
#endif
static int ds5_state_to_vc(struct ds5 *state) {
	int pad = 0;
	if (state->is_depth) {
		pad = DS5_MUX_PAD_DEPTH;
	}
	if (state->is_rgb) {
		pad = DS5_MUX_PAD_RGB;
	}
	if (state->is_y8) {
		pad = DS5_MUX_PAD_IR;
	}
	if (state->is_imu) {
		pad = DS5_MUX_PAD_IMU;
	}

	return state->pad_to_vc[pad];
}
#endif
static int ds5_mux_s_stream(struct v4l2_subdev *sd, int on)
{
	struct ds5 *state = container_of(sd, struct ds5, mux.sd.subdev);
	u16 streaming, status;
	int ret = 0;
	unsigned int i = 0;
	int restore_val = 0;
	u16 config_status_base, stream_status_base, stream_id, vc_id;
	struct ds5_sensor *sensor = state->mux.last_set;

	// spare duplicate calls
	if (sensor->streaming == on)
		return 0;
	if (state->is_depth) {
		config_status_base = DS5_DEPTH_CONFIG_STATUS;
		stream_status_base = DS5_DEPTH_STREAM_STATUS;
		stream_id = DS5_STREAM_DEPTH;
		vc_id = 0;
	} else if (state->is_rgb) {
		config_status_base = DS5_RGB_CONFIG_STATUS;
		stream_status_base = DS5_RGB_STREAM_STATUS;
		stream_id = DS5_STREAM_RGB;
		vc_id = 1;
	} else if (state->is_y8) {
		config_status_base = DS5_IR_CONFIG_STATUS;
		stream_status_base = DS5_IR_STREAM_STATUS;
		stream_id = DS5_STREAM_IR;
		vc_id = 2;
	} else if (state->is_imu) {
		config_status_base = DS5_IMU_CONFIG_STATUS;
		stream_status_base = DS5_IMU_STREAM_STATUS;
		stream_id = DS5_STREAM_IMU;
		vc_id = 3;
	} else {
		return -EINVAL;
	}
#ifdef CONFIG_VIDEO_INTEL_IPU6
	vc_id = ds5_state_to_vc(state);
#endif
#ifdef CONFIG_TEGRA_CAMERA_PLATFORM
#ifdef CONFIG_VIDEO_D4XX_SERDES
	vc_id = state->g_ctx.dst_vc;
#endif
#endif
	dev_dbg(&state->client->dev, "s_stream for stream %s, vc:%d, SENSOR=%s on = %d\n",
			sensor->sd.name, vc_id, ds5_get_sensor_name(state), on);

	restore_val = sensor->streaming;
	sensor->streaming = on;

	if (on) {
#ifdef CONFIG_VIDEO_D4XX_SERDES
#ifdef CONFIG_VIDEO_INTEL_IPU6
		// set manually, need to configure vc in pdata
		state->g_ctx.dst_vc = vc_id;
#endif
		sensor->pipe_id =
			max9296_get_available_pipe_id(state->dser_dev,
					(int)state->g_ctx.dst_vc);
		if (sensor->pipe_id < 0) {
			dev_err(&state->client->dev,
				"No free pipe in max9296\n");
			ret = -(ENOSR);
			goto restore_s_state;
		}
#endif

		ret = ds5_configure(state);
		if (ret)
			goto restore_s_state;

		ret = ds5_write(state, DS5_START_STOP_STREAM,
				DS5_STREAM_START | stream_id);
		if (ret < 0)
			goto restore_s_state;

		// check streaming status from FW
		for (i = 0; i < DS5_START_MAX_COUNT; i++) {
			ds5_read(state, stream_status_base, &streaming);
			ds5_read(state, config_status_base, &status);
			if ((status & DS5_STATUS_STREAMING) &&
					streaming == DS5_STREAM_STREAMING)
				break;

			msleep_range(DS5_START_POLL_TIME);
		}

		if (DS5_START_MAX_COUNT == i) {
			dev_err(&state->client->dev,
				"start streaming failed, exit on timeout\n");
			/* notify fw */
			ret = ds5_write(state, DS5_START_STOP_STREAM,
					DS5_STREAM_STOP | stream_id);
			ret = -EAGAIN;
			goto restore_s_state;
		} else {
			dev_dbg(&state->client->dev, "started after %dms\n",
				i * DS5_START_POLL_TIME);
		}
	} else { // off
		ret = ds5_write(state, DS5_START_STOP_STREAM,
				DS5_STREAM_STOP | stream_id);
		if (ret < 0)
			goto restore_s_state;

#ifdef CONFIG_VIDEO_D4XX_SERDES
		// reset data path when Y12I streaming is done
		if (state->is_y8 &&
			state->ir.sensor.config.format->data_type ==
			GMSL_CSI_DT_RGB_888) {
			max9296_reset_oneshot(state->dser_dev);
		}
#ifndef CONFIG_TEGRA_CAMERA_PLATFORM
		// reset for IPU6
		streaming = 0;
		for (i = 0; i < ARRAY_SIZE(d4xx_set_sub_stream); i++) {
			if (d4xx_set_sub_stream[i]) {
				streaming = 1;
				break;
			}
		}
		if (!streaming) {
			dev_warn(&state->client->dev, "max9296_reset_oneshot\n");
				max9296_reset_oneshot(state->dser_dev);
		}
#endif
		if (max9296_release_pipe(state->dser_dev, sensor->pipe_id) < 0)
			dev_warn(&state->client->dev, "release pipe failed\n");
		sensor->pipe_id = -1;
#else
#ifdef CONFIG_VIDEO_INTEL_IPU6
		d4xx_reset_oneshot(state);
#endif
#endif
	}

	ds5_read(state, config_status_base, &status);
	ds5_read(state, stream_status_base, &streaming);
	dev_dbg(&state->client->dev,
			"%s %s, stream_status 0x%x:%x, config_status 0x%x:%x ret=%d\n",
			ds5_get_sensor_name(state),
			(on)?"START":"STOP",
			stream_status_base, streaming,
			config_status_base, status, ret);

	return ret;

restore_s_state:
#ifdef CONFIG_VIDEO_D4XX_SERDES
	if (on && sensor->pipe_id >= 0) {
		if (max9296_release_pipe(state->dser_dev, sensor->pipe_id) < 0)
			dev_warn(&state->client->dev, "release pipe failed\n");
		sensor->pipe_id = -1;
	}
#endif

	ds5_read(state, config_status_base, &status);
	dev_err(&state->client->dev,
			"%s stream toggle failed! %x status 0x%04x\n",
			ds5_get_sensor_name(state), restore_val, status);

	sensor->streaming = restore_val;

	return ret;
}

static int ds5_mux_get_frame_desc(struct v4l2_subdev *sd,
	unsigned int pad, struct v4l2_mbus_frame_desc *desc)
{
	unsigned int i;

	desc->num_entries = V4L2_FRAME_DESC_ENTRY_MAX;

	for (i = 0; i < desc->num_entries; i++) {
		desc->entry[i].flags = 0;
		desc->entry[i].pixelcode = MEDIA_BUS_FMT_FIXED;
		desc->entry[i].length = 0;
		if (i == desc->num_entries - 1) {
			desc->entry[i].pixelcode = 0x12;
			desc->entry[i].length = 68;
		}
	}
	return 0;
}

static const struct v4l2_subdev_pad_ops ds5_mux_pad_ops = {
	.enum_mbus_code		= ds5_mux_enum_mbus_code,
	.enum_frame_size	= ds5_mux_enum_frame_size,
	.enum_frame_interval	= ds5_mux_enum_frame_interval,
	.get_fmt		= ds5_mux_get_fmt,
	.set_fmt		= ds5_mux_set_fmt,
	.get_frame_desc		= ds5_mux_get_frame_desc,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
	.get_frame_interval	= ds5_mux_g_frame_interval,
	.set_frame_interval	= ds5_mux_s_frame_interval,
#endif
};

static const struct v4l2_subdev_core_ops ds5_mux_core_ops = {
	//.s_power = ds5_mux_set_power,
	.log_status = v4l2_ctrl_subdev_log_status,
};

static const struct v4l2_subdev_video_ops ds5_mux_video_ops = {
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
	.g_frame_interval	= ds5_mux_g_frame_interval,
	.s_frame_interval	= ds5_mux_s_frame_interval,
#endif
	.s_stream		= ds5_mux_s_stream,
};

static const struct v4l2_subdev_ops ds5_mux_subdev_ops = {
	.core = &ds5_mux_core_ops,
	.pad = &ds5_mux_pad_ops,
	.video = &ds5_mux_video_ops,
};

static int ds5_mux_registered(struct v4l2_subdev *sd)
{
	struct ds5 *state = v4l2_get_subdevdata(sd);
	int ret = ds5_sensor_register(state, &state->depth.sensor);
	if (ret < 0)
		return ret;

	ret = ds5_sensor_register(state, &state->ir.sensor);
	if (ret < 0)
		goto e_depth;

	ret = ds5_sensor_register(state, &state->rgb.sensor);
	if (ret < 0)
		goto e_rgb;

	ret = ds5_sensor_register(state, &state->imu.sensor);
	if (ret < 0)
		goto e_imu;

	return 0;

e_imu:
	v4l2_device_unregister_subdev(&state->rgb.sensor.sd);

e_rgb:
	v4l2_device_unregister_subdev(&state->ir.sensor.sd);

e_depth:
	v4l2_device_unregister_subdev(&state->depth.sensor.sd);

	return ret;
}

static void ds5_mux_unregistered(struct v4l2_subdev *sd)
{
	struct ds5 *state = v4l2_get_subdevdata(sd);
	ds5_sensor_remove(&state->imu.sensor);
	ds5_sensor_remove(&state->rgb.sensor);
	ds5_sensor_remove(&state->ir.sensor);
	ds5_sensor_remove(&state->depth.sensor);
}

static const struct v4l2_subdev_internal_ops ds5_mux_internal_ops = {
	.open = ds5_mux_open,
	.close = ds5_mux_close,
	.registered = ds5_mux_registered,
	.unregistered = ds5_mux_unregistered,
};

static int ds5_mux_register(struct i2c_client *c, struct ds5 *state)
{
	return v4l2_async_register_subdev(&state->mux.sd.subdev);
}

static int ds5_hw_init(struct i2c_client *c, struct ds5 *state)
{
	struct v4l2_subdev *sd = &state->mux.sd.subdev;
	u16 mipi_status, n_lanes, phy, drate_min, drate_max;
	int ret = ds5_read(state, DS5_MIPI_SUPPORT_LINES, &n_lanes);
	if (!ret)
		ret = ds5_read(state, DS5_MIPI_SUPPORT_PHY, &phy);

	if (!ret)
		ret = ds5_read(state, DS5_MIPI_DATARATE_MIN, &drate_min);

	if (!ret)
		ret = ds5_read(state, DS5_MIPI_DATARATE_MAX, &drate_max);

	if (!ret)
		dev_dbg(sd->dev, "%s(): %d: %u lanes, phy %x, data rate %u-%u\n",
			 __func__, __LINE__, n_lanes, phy, drate_min, drate_max);

#ifdef CONFIG_TEGRA_CAMERA_PLATFORM
	n_lanes = state->mux.sd.numlanes;
#else
	n_lanes = 2;
#endif

	ret = ds5_write(state, DS5_MIPI_LANE_NUMS, n_lanes - 1);
	if (!ret)
		ret = ds5_write(state, DS5_MIPI_LANE_DATARATE, MIPI_LANE_RATE);

	if (!ret)
		ret = ds5_read(state, DS5_MIPI_CONF_STATUS, &mipi_status);

#ifdef CONFIG_TEGRA_CAMERA_PLATFORM
	dev_dbg(sd->dev, "%s(): %d phandle %x node %s status %x\n", __func__, __LINE__,
		 c->dev.of_node->phandle, c->dev.of_node->full_name, mipi_status);
#endif

	return ret;
}

static int ds5_mux_init(struct i2c_client *c, struct ds5 *state)
{
	struct v4l2_subdev *sd = &state->mux.sd.subdev;
	struct media_entity *entity = &state->mux.sd.subdev.entity;
	struct media_pad *pads = state->mux.pads, *pad;
	unsigned int i;
	int ret;
#ifndef CONFIG_OF
	struct d4xx_pdata *dpdata = c->dev.platform_data;
	char suffix = dpdata->suffix;
#endif
	v4l2_i2c_subdev_init(sd, c, &ds5_mux_subdev_ops);
	// See tegracam_v4l2.c tegracam_v4l2subdev_register()
	// Set owner to NULL so we can unload the driver module
	sd->owner = NULL;
	sd->internal_ops = &ds5_mux_internal_ops;
	v4l2_set_subdevdata(sd, state);
#ifdef CONFIG_OF
	snprintf(sd->name, sizeof(sd->name), "DS5 mux %d-%04x",
		 i2c_adapter_id(c->adapter), c->addr);
#else
	if (state->aggregated)
		suffix += 4;
	snprintf(sd->name, sizeof(sd->name), "DS5 mux %c", suffix);
#endif
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	entity->obj_type = MEDIA_ENTITY_TYPE_V4L2_SUBDEV;
	entity->function = MEDIA_ENT_F_CAM_SENSOR;

	pads[0].flags = MEDIA_PAD_FL_SOURCE;
	for (i = 1, pad = pads + 1; i < ARRAY_SIZE(state->mux.pads); i++, pad++)
		pad->flags = MEDIA_PAD_FL_SINK;

	ret = media_entity_pads_init(entity, ARRAY_SIZE(state->mux.pads), pads);
	if (ret < 0)
		return ret;

	/*set for mux*/
	ret = ds5_ctrl_init(state, MUX_SID);
	if (ret < 0)
		goto e_entity;

	/*set for depth*/
	ret = ds5_ctrl_init(state, DEPTH_SID);
	if (ret < 0)
		return ret;
	/*set for rgb*/
	ret = ds5_ctrl_init(state, RGB_SID);
	if (ret < 0)
		return ret;
	/*set for y8*/
	ret = ds5_ctrl_init(state, IR_SID);
	if (ret < 0)
		return ret;
	/*set for imu*/
	ret = ds5_ctrl_init(state, IMU_SID);
	if (ret < 0)
		return ret;

	ds5_set_state_last_set(state);

#ifdef CONFIG_TEGRA_CAMERA_PLATFORM
	if (state->is_depth) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
		v4l2_ctrl_add_handler(&state->ctrls.handler,
					&state->ctrls.handler_depth, NULL);
#else
		v4l2_ctrl_add_handler(&state->ctrls.handler,
					&state->ctrls.handler_depth, NULL, true);
#endif
		state->mux.last_set = &state->depth.sensor;
	}
	else if (state->is_rgb) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
		v4l2_ctrl_add_handler(&state->ctrls.handler,
					&state->ctrls.handler_rgb, NULL);
#else
		v4l2_ctrl_add_handler(&state->ctrls.handler,
					&state->ctrls.handler_rgb, NULL, true);
#endif
		state->mux.last_set = &state->rgb.sensor;
	}
	else if (state->is_y8) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
		v4l2_ctrl_add_handler(&state->ctrls.handler,
					&state->ctrls.handler_y8, NULL);
#else
		v4l2_ctrl_add_handler(&state->ctrls.handler,
					&state->ctrls.handler_y8, NULL, true);
#endif
		state->mux.last_set = &state->ir.sensor;
	}
	else
		state->mux.last_set = &state->imu.sensor;

	state->mux.sd.dev = &c->dev;
	ret = camera_common_initialize(&state->mux.sd, "d4xx");
	if (ret) {
		dev_err(&c->dev, "Failed to initialize d4xx.\n");
		goto e_ctrl;
	}
#endif

	return 0;

#ifdef CONFIG_TEGRA_CAMERA_PLATFORM
e_ctrl:
	v4l2_ctrl_handler_free(sd->ctrl_handler);
#endif
e_entity:
	media_entity_cleanup(entity);

	return ret;
}

#define USE_Y

static int ds5_fixed_configuration(struct i2c_client *client, struct ds5 *state)
{
	struct ds5_sensor *sensor;
	u16 cfg0 = 0, cfg0_md = 0, cfg1 = 0, cfg1_md = 0;
	u16 dw = 0, dh = 0, yw = 0, yh = 0, dev_type = 0;
	int ret;

	ret = ds5_read(state, DS5_DEPTH_STREAM_DT, &cfg0);
	if (!ret)
		ret = ds5_read(state, DS5_DEPTH_STREAM_MD, &cfg0_md);
	if (!ret)
		ret = ds5_read(state, DS5_DEPTH_RES_WIDTH, &dw);
	if (!ret)
		ret = ds5_read(state, DS5_DEPTH_RES_HEIGHT, &dh);
	if (!ret)
		ret = ds5_read(state, DS5_IR_STREAM_DT, &cfg1);
	if (!ret)
		ret = ds5_read(state, DS5_IR_STREAM_MD, &cfg1_md);
	if (!ret)
		ret = ds5_read(state, DS5_IR_RES_WIDTH, &yw);
	if (!ret)
		ret = ds5_read(state, DS5_IR_RES_HEIGHT, &yh);
	if (!ret)
		ret = ds5_read(state, DS5_DEVICE_TYPE, &dev_type);
	if (ret < 0)
		return ret;

	dev_dbg(&client->dev, "%s(): cfg0 %x %ux%u cfg0_md %x %ux%u\n", __func__,
		 cfg0, dw, dh, cfg0_md, yw, yh);

	dev_dbg(&client->dev, "%s(): cfg1 %x %ux%u cfg1_md %x %ux%u\n", __func__,
		 cfg1, dw, dh, cfg1_md, yw, yh);

	sensor = &state->depth.sensor;
	switch (dev_type) {
	case DS5_DEVICE_TYPE_D43X:
	case DS5_DEVICE_TYPE_D45X:
		sensor->formats = ds5_depth_formats_d43x;
		break;
	case DS5_DEVICE_TYPE_D46X:
		sensor->formats = ds5_depth_formats_d46x;
		break;
	default:
		sensor->formats = ds5_depth_formats_d46x;
	}
	sensor->n_formats = 1;
	sensor->mux_pad = DS5_MUX_PAD_DEPTH;

	sensor = &state->ir.sensor;
	sensor->formats = state->variant->formats;
	sensor->n_formats = state->variant->n_formats;
	sensor->mux_pad = DS5_MUX_PAD_IR;

	sensor = &state->rgb.sensor;
	switch (dev_type) {
	case DS5_DEVICE_TYPE_D43X:
	case DS5_DEVICE_TYPE_D46X:
		sensor->formats = &ds5_onsemi_rgb_format;
		sensor->n_formats = DS5_ONSEMI_RGB_N_FORMATS;
		break;
	case DS5_DEVICE_TYPE_D45X:
		sensor->formats = &ds5_rlt_rgb_format;
		sensor->n_formats = DS5_RLT_RGB_N_FORMATS;
		break;
	default:
		sensor->formats = &ds5_onsemi_rgb_format;
		sensor->n_formats = DS5_ONSEMI_RGB_N_FORMATS;
	}
	sensor->mux_pad = DS5_MUX_PAD_RGB;

	sensor = &state->imu.sensor;

	/* For fimware version starting from: 5.16,
	   IMU will have 32bit axis values.
 	   5.16.x.y = firmware version: 0x0510 */
	if (state->fw_version >= 0x510)
		sensor->formats = ds5_imu_formats_extended;
	else
		sensor->formats = ds5_imu_formats;
	
	sensor->n_formats = 1;
	sensor->mux_pad = DS5_MUX_PAD_IMU;

	/* Development: set a configuration during probing */
	if ((cfg0 & 0xff00) == 0x1800) {
		/* MIPI CSI-2 YUV420 isn't supported by V4L, reconfigure to Y8 */
		struct v4l2_subdev_format fmt = {
			.which = V4L2_SUBDEV_FORMAT_ACTIVE,
			.pad = 0,
			/* Use template to fill in .field, .colorspace etc. */
			.format = ds5_mbus_framefmt_template,
		};

//#undef USE_Y
#ifdef USE_Y
		/* Override .width, .height, .code */
		fmt.format.width = yw;
		fmt.format.height = yh;
		fmt.format.code = MEDIA_BUS_FMT_UYVY8_2X8;
#ifdef CONFIG_TEGRA_CAMERA_PLATFORM
		state->mux.sd.mode_prop_idx = 0;
#endif
		state->ir.sensor.streaming = true;
		state->depth.sensor.streaming = true;
		ret = __ds5_sensor_set_fmt(state, &state->ir.sensor, NULL, &fmt);
#else
		fmt.format.width = dw;
		fmt.format.height = dh;
		fmt.format.code = MEDIA_BUS_FMT_UYVY8_1X16;
#ifdef CONFIG_TEGRA_CAMERA_PLATFORM
		state->mux.sd.mode_prop_idx = 1;
#endif
		state->ir.sensor.streaming = false;
		state->depth.sensor.streaming = true;
		ret = __ds5_sensor_set_fmt(state, &state->depth.sensor, NULL, &fmt);
#endif
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int ds5_parse_cam(struct i2c_client *client, struct ds5 *state)
{
	int ret;

	ret = ds5_fixed_configuration(client, state);
	if (ret < 0)
		return ret;

	ds5_sensor_format_init(&state->depth.sensor);
	ds5_sensor_format_init(&state->ir.sensor);
	ds5_sensor_format_init(&state->rgb.sensor);
	ds5_sensor_format_init(&state->imu.sensor);

	return 0;
}

static void ds5_mux_remove(struct ds5 *state)
{
#ifdef CONFIG_TEGRA_CAMERA_PLATFORM
	camera_common_cleanup(&state->mux.sd);
#endif
	v4l2_async_unregister_subdev(&state->mux.sd.subdev);
	v4l2_ctrl_handler_free(state->mux.sd.subdev.ctrl_handler);
	media_entity_cleanup(&state->mux.sd.subdev.entity);
}

static const struct regmap_config ds5_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.reg_format_endian = REGMAP_ENDIAN_NATIVE,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
};

static int ds5_dfu_wait_for_status(struct ds5 *state)
{
	int i, ret = 0;
	u16 status;

	for (i = 0; i < DS5_START_MAX_COUNT; i++) {
		ds5_read(state, 0x5000, &status);
		if (status == 0x0001 || status == 0x0002) {
			dev_err(&state->client->dev,
					"%s(): dfu failed status(0x%4x)\n",
					__func__, status);
			ret = -EREMOTEIO;
			break;
		}
		if (!status)
			break;
		msleep_range(DS5_START_POLL_TIME);
	}

	return ret;
};

static int ds5_dfu_switch_to_dfu(struct ds5 *state)
{
	int ret;
	int i = DS5_START_MAX_COUNT;
	u16 status;

	ds5_raw_write_with_check(state, 0x4900,
			&cmd_switch_to_dfu, sizeof(cmd_switch_to_dfu));
	ds5_write_with_check(state, 0x490c, 0x01); /* execute cmd */
	/*Wait for DFU fw to boot*/
	do {
		msleep_range(DS5_START_POLL_TIME*10);
		ret = ds5_read(state, 0x5000, &status);
	} while (ret && i--);
	return ret;
};

static int ds5_dfu_wait_for_get_dfu_status(struct ds5 *state,
		enum dfu_fw_state exp_state)
{
	int ret = 0;
	u16 status, dfu_state_len = 0x0000;
	unsigned char dfu_asw_buf[DFU_WAIT_RET_LEN];
	unsigned int dfu_wr_wait_msec = 0;

	do {
		ds5_write_with_check(state, 0x5008, 0x0003); // Get Write state
		do {
			ds5_read_with_check(state, 0x5000, &status);
			if (status == 0x0001) {
				dev_err(&state->client->dev,
						"%s(): Write status error I2C_STATUS_ERROR(1)\n",
						__func__);
				return -EINVAL;
			} else
				if (status == 0x0002 && dfu_wr_wait_msec)
					msleep_range(dfu_wr_wait_msec);

		} while (status);

		ds5_read_with_check(state, 0x5004, &dfu_state_len);
		if (dfu_state_len != DFU_WAIT_RET_LEN) {
			dev_err(&state->client->dev,
					"%s(): Wrong answer len (%d)\n", __func__, dfu_state_len);
			return -EINVAL;
		}
		ds5_raw_read_with_check(state, 0x4e00, &dfu_asw_buf, DFU_WAIT_RET_LEN);
		if (dfu_asw_buf[0]) {
			dev_err(&state->client->dev,
					"%s(): Wrong dfu_status (%d)\n", __func__, dfu_asw_buf[0]);
			return -EINVAL;
		}
		dfu_wr_wait_msec = (((unsigned int)dfu_asw_buf[3]) << 16)
						| (((unsigned int)dfu_asw_buf[2]) << 8)
						| dfu_asw_buf[1];
	} while (dfu_asw_buf[4] == dfuDNBUSY && exp_state == dfuDNLOAD_IDLE);

	if (dfu_asw_buf[4] != exp_state) {
		dev_notice(&state->client->dev,
				"%s(): Wrong dfu_state (%d) while expected(%d)\n",
				__func__, dfu_asw_buf[4], exp_state);
		ret = -EINVAL;
	}
	return ret;
};

static int ds5_dfu_get_dev_info(struct ds5 *state, struct __fw_status *buf)
{
	int ret = 0;
	u16 len = 0;

	ret = ds5_write(state, 0x5008, 0x0002); //Upload DFU cmd
	if (!ret)
		ret = ds5_dfu_wait_for_status(state);
	if (!ret)
		ds5_read_with_check(state, 0x5004, &len);
	/*Sanity check*/
	if (len == sizeof(struct __fw_status)) {
		ds5_raw_read_with_check(state, 0x4e00, buf, len);
	} else {
		dev_err(&state->client->dev,
				"%s(): Wrong state size (%d)\n",
				__func__, len);
		ret = -EINVAL;
	}
	return ret;
};

static int ds5_dfu_detach(struct ds5 *state)
{
	int ret;
	struct __fw_status buf = {0};

	ds5_write_with_check(state, 0x500c, 0x00);
	ret = ds5_dfu_wait_for_get_dfu_status(state, dfuIDLE);
	if (!ret)
		ret = ds5_dfu_get_dev_info(state, &buf);
	dev_notice(&state->client->dev, "%s():DFU ver (0x%x) received\n",
			__func__, buf.DFU_version);
	dev_notice(&state->client->dev, "%s():FW last version (0x%x) received\n",
			__func__, buf.FW_lastVersion);
	dev_notice(&state->client->dev, "%s():FW status (%s)\n",
			__func__, buf.DFU_isLocked ? "locked" : "unlocked");
	return ret;
};

/* When a process reads from our device, this gets called. */
static ssize_t ds5_dfu_device_read(struct file *flip,
		char __user *buffer, size_t len, loff_t *offset)
{
	struct ds5 *state = flip->private_data;
	u16 fw_ver, fw_build;
	char msg[64];
	int ret = 0;
	struct __fw_status f = {0};

	if (mutex_lock_interruptible(&state->lock))
		return -ERESTARTSYS;
	if (state->dfu_dev.dfu_state_flag == DS5_DFU_RECOVERY) {
		/* Read device info in recovery mode */
		ret = ds5_dfu_detach(state);
		if (ret < 0)
			goto e_dfu_read_failed;
		ret = ds5_dfu_get_dev_info(state, &f);
		if (ret < 0)
			goto e_dfu_read_failed;
		snprintf(msg, sizeof(msg) ,
			 "DFU info: \trecovery:  %02x%02x%02x%02x%02x%02x\n",
			 f.ivcamSerialNum[0], f.ivcamSerialNum[1], f.ivcamSerialNum[2],
			 f.ivcamSerialNum[3], f.ivcamSerialNum[4], f.ivcamSerialNum[5] );
	} else {
		ret |= ds5_read(state, DS5_FW_VERSION, &fw_ver);
		ret |= ds5_read(state, DS5_FW_BUILD, &fw_build);
		if (ret < 0)
			goto e_dfu_read_failed;
		snprintf(msg, sizeof(msg) ,"DFU info: \tver:  %d.%d.%d.%d\n",
			(fw_ver >> 8) & 0xff, fw_ver & 0xff,
			(fw_build >> 8) & 0xff, fw_build & 0xff);
	}

	if (copy_to_user(buffer, msg, strlen(msg)))
		ret = -EFAULT;
	else {
		state->dfu_dev.msg_write_once = ~state->dfu_dev.msg_write_once;
		ret = strlen(msg) & state->dfu_dev.msg_write_once;
	}

e_dfu_read_failed:
	mutex_unlock(&state->lock);
	return ret;
};

static ssize_t ds5_dfu_device_write(struct file *flip,
		const char __user *buffer, size_t len, loff_t *offset)
{
	struct ds5 *state = flip->private_data;
	int ret = 0;
	(void)offset;

	if (mutex_lock_interruptible(&state->lock))
		return -ERESTARTSYS;
	switch (state->dfu_dev.dfu_state_flag) {

	case DS5_DFU_OPEN:
		ret = ds5_dfu_switch_to_dfu(state);
		if (ret < 0) {
			dev_err(&state->client->dev, "%s(): Switch to dfu failed (%d)\n",
					__func__, ret);
			goto dfu_write_error;
		}
	/* fallthrough - procceed to recovery */
	__attribute__((__fallthrough__));
	case DS5_DFU_RECOVERY:
		ret = ds5_dfu_detach(state);
		if (ret < 0) {
			dev_err(&state->client->dev, "%s(): Detach failed (%d)\n",
					__func__, ret);
			goto dfu_write_error;
		}
		state->dfu_dev.dfu_state_flag = DS5_DFU_IN_PROGRESS;
	/* find a better way to reinitialize driver from recovery to operational */
		// state->dfu_dev.init_v4l_f = 1;
	/* fallthrough - procceed to download */
	__attribute__((__fallthrough__));
	case DS5_DFU_IN_PROGRESS: {
		unsigned int dfu_full_blocks = len / DFU_BLOCK_SIZE;
		unsigned int dfu_part_blocks = len % DFU_BLOCK_SIZE;

		while (dfu_full_blocks--) {
			if (copy_from_user(state->dfu_dev.dfu_msg, buffer, DFU_BLOCK_SIZE)) {
				ret = -EFAULT;
				goto dfu_write_error;
			}
			ret = ds5_raw_write(state, 0x4a00,
					state->dfu_dev.dfu_msg, DFU_BLOCK_SIZE);
			if (ret < 0)
				goto dfu_write_error;
			ret = ds5_dfu_wait_for_get_dfu_status(state, dfuDNLOAD_IDLE);
			if (ret < 0)
				goto dfu_write_error;
			buffer += DFU_BLOCK_SIZE;
		}
		if (copy_from_user(state->dfu_dev.dfu_msg, buffer, dfu_part_blocks)) {
				ret = -EFAULT;
				goto dfu_write_error;
		}
		if (dfu_part_blocks) {
			ret = ds5_raw_write(state, 0x4a00,
					state->dfu_dev.dfu_msg, dfu_part_blocks);
			if (!ret)
				ret = ds5_dfu_wait_for_get_dfu_status(state, dfuDNLOAD_IDLE);
			if (!ret)
				ret = ds5_write(state, 0x4a04, 0x00); /*Download complete */
			if (!ret)
				ret = ds5_dfu_wait_for_get_dfu_status(state, dfuMANIFEST);
			if (ret < 0)
				goto dfu_write_error;
			state->dfu_dev.dfu_state_flag = DS5_DFU_DONE;
		}
		if (len)
			dev_notice(&state->client->dev, "%s(): DFU block (%d) bytes written\n",
				__func__, (int)len);
		break;
	}
	default:
		dev_err(&state->client->dev, "%s(): Wrong state (%d)\n",
				__func__, state->dfu_dev.dfu_state_flag);
		ret = -EINVAL;
		goto dfu_write_error;

	};
	mutex_unlock(&state->lock);
	return len;

dfu_write_error:
	state->dfu_dev.dfu_state_flag = DS5_DFU_ERROR;
	// Reset DFU device to IDLE states
	if (!ds5_write(state, 0x5010, 0x0))
		state->dfu_dev.dfu_state_flag = DS5_DFU_IDLE;
	mutex_unlock(&state->lock);
	return ret;
};

static int ds5_dfu_device_open(struct inode *inode, struct file *file)
{
	struct ds5 *state = container_of(inode->i_cdev, struct ds5,
			dfu_dev.ds5_cdev);
#ifdef CONFIG_TEGRA_CAMERA_PLATFORM
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
	struct i2c_adapter *parent = i2c_parent_is_i2c_adapter(
			state->client->adapter);
#endif
#endif
	if (state->dfu_dev.device_open_count)
		return -EBUSY;
	state->dfu_dev.device_open_count++;
	if (state->dfu_dev.dfu_state_flag != DS5_DFU_RECOVERY)
		state->dfu_dev.dfu_state_flag = DS5_DFU_OPEN;
	state->dfu_dev.dfu_msg = devm_kzalloc(&state->client->dev,
			DFU_BLOCK_SIZE, GFP_KERNEL);
	if (!state->dfu_dev.dfu_msg)
		return -ENOMEM;

	file->private_data = state;
#ifdef CONFIG_TEGRA_CAMERA_PLATFORM
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
	/* get i2c controller and set dfu bus clock rate */
	while (parent && i2c_parent_is_i2c_adapter(parent))
		parent = i2c_parent_is_i2c_adapter(state->client->adapter);

	if (!parent)
		return 0;

	dev_dbg(&state->client->dev, "%s(): i2c-%d bus_clk = %d, set %d\n",
			__func__,
			i2c_adapter_id(parent),
			i2c_get_adapter_bus_clk_rate(parent),
			DFU_I2C_BUS_CLK_RATE);

	state->dfu_dev.bus_clk_rate = i2c_get_adapter_bus_clk_rate(parent);
	i2c_set_adapter_bus_clk_rate(parent, DFU_I2C_BUS_CLK_RATE);
#endif
#endif
	return 0;
};

static int ds5_v4l_init(struct i2c_client *c, struct ds5 *state)
{
	int ret;

	ret = ds5_parse_cam(c, state);
	if (ret < 0)
		return ret;

	ret = ds5_depth_init(c, state);
	if (ret < 0)
		return ret;

	ret = ds5_ir_init(c, state);
	if (ret < 0)
		goto e_depth;

	ret = ds5_rgb_init(c, state);
	if (ret < 0)
		goto e_ir;

	ret = ds5_imu_init(c, state);
	if (ret < 0)
		goto e_rgb;

	ret = ds5_mux_init(c, state);
	if (ret < 0)
		goto e_imu;

	ret = ds5_hw_init(c, state);
	if (ret < 0)
		goto e_mux;

	ret = ds5_mux_register(c, state);
	if (ret < 0)
		goto e_mux;

	return 0;
e_mux:
	ds5_mux_remove(state);
e_imu:
	media_entity_cleanup(&state->imu.sensor.sd.entity);
e_rgb:
	media_entity_cleanup(&state->rgb.sensor.sd.entity);
e_ir:
	media_entity_cleanup(&state->ir.sensor.sd.entity);
e_depth:
	media_entity_cleanup(&state->depth.sensor.sd.entity);
	return ret;
}

static int ds5_dfu_device_release(struct inode *inode, struct file *file)
{
	struct ds5 *state = container_of(inode->i_cdev, struct ds5, dfu_dev.ds5_cdev);
#ifdef CONFIG_TEGRA_CAMERA_PLATFORM
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
	struct i2c_adapter *parent = i2c_parent_is_i2c_adapter(
			state->client->adapter);
#endif
#endif
	int ret = 0, retry = 10;
	state->dfu_dev.device_open_count--;
	if (state->dfu_dev.dfu_state_flag != DS5_DFU_RECOVERY)
		state->dfu_dev.dfu_state_flag = DS5_DFU_IDLE;
	/* We disable this section as it has no effect when device in operational
	   mode and has not enough effect when device in recovery mode */
	// if (state->dfu_dev.dfu_state_flag == DS5_DFU_DONE
	// 		&& state->dfu_dev.init_v4l_f)
	// 	ds5_v4l_init(state->client, state);
	// state->dfu_dev.init_v4l_f = 0;
	if (state->dfu_dev.dfu_msg)
		devm_kfree(&state->client->dev, state->dfu_dev.dfu_msg);
	state->dfu_dev.dfu_msg = NULL;
#ifdef CONFIG_TEGRA_CAMERA_PLATFORM
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 10)
	/* get i2c controller and restore bus clock rate */
	while (parent && i2c_parent_is_i2c_adapter(parent))
		parent = i2c_parent_is_i2c_adapter(state->client->adapter);
	if (!parent)
		return 0;
	dev_dbg(&state->client->dev, "%s(): i2c-%d bus_clk %d, restore to %d\n",
			__func__, i2c_adapter_id(parent),
			i2c_get_adapter_bus_clk_rate(parent),
			state->dfu_dev.bus_clk_rate);

	i2c_set_adapter_bus_clk_rate(parent, state->dfu_dev.bus_clk_rate);
#endif
#endif
	/* Verify communication */
	do {
		ret = ds5_read(state, DS5_FW_VERSION, &state->fw_version);
		if (ret)
			msleep_range(10);
	} while (retry-- && ret != 0 );
	if (ret) {
		dev_warn(&state->client->dev,
			"%s(): no communication with d4xx\n", __func__);
		return ret;
	}
	ret = ds5_read(state, DS5_FW_BUILD, &state->fw_build);
	return ret;
};

static const struct file_operations ds5_device_file_ops = {
	.owner = THIS_MODULE,
	.read = &ds5_dfu_device_read,
	.write = &ds5_dfu_device_write,
	.open = &ds5_dfu_device_open,
	.release = &ds5_dfu_device_release
};

struct class *g_ds5_class;
atomic_t primary_chardev = ATOMIC_INIT(0);

static int ds5_chrdev_init(struct i2c_client *c, struct ds5 *state)
{
	struct cdev *ds5_cdev = &state->dfu_dev.ds5_cdev;
	struct class **ds5_class = &state->dfu_dev.ds5_class;
#ifndef CONFIG_OF
	struct d4xx_pdata *pdata = c->dev.platform_data;
	char suffix = pdata->suffix;
#endif
	struct device *chr_dev;
	char dev_name[sizeof(DS5_DRIVER_NAME_DFU) + 8];
	dev_t *dev_num = &c->dev.devt;
	int ret;

	dev_dbg(&c->dev, "%s()\n", __func__);
	/* Request the kernel for N_MINOR devices */
	ret = alloc_chrdev_region(dev_num, 0, 1, DS5_DRIVER_NAME_DFU);
	if (ret < 0)
		return ret;

	if (!atomic_read(&primary_chardev)) {
		dev_dbg(&c->dev, "%s(): <Major, Minor>: <%d, %d>\n",
				__func__, MAJOR(*dev_num), MINOR(*dev_num));
		/* Create a class : appears at /sys/class */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 2, 0)
		*ds5_class = class_create(THIS_MODULE, DS5_DRIVER_NAME_CLASS);
#else
		*ds5_class = class_create(DS5_DRIVER_NAME_CLASS);
#endif
		dev_warn(&state->client->dev, "%s() class_create\n", __func__);
		if (IS_ERR(*ds5_class)) {
			dev_err(&c->dev, "Could not create class device\n");
			unregister_chrdev_region(0, 1);
			ret = PTR_ERR(*ds5_class);
			return ret;
		}
		g_ds5_class = *ds5_class;
	} else
		*ds5_class = g_ds5_class;
	/* Associate the cdev with a set of file_operations */
	cdev_init(ds5_cdev, &ds5_device_file_ops);
	/* Build up the current device number. To be used further */
	*dev_num = MKDEV(MAJOR(*dev_num), MINOR(*dev_num));
	/* Create a device node for this device. */
#ifndef CONFIG_OF
	if (state->aggregated)
		suffix += 4;
	snprintf(dev_name, sizeof(dev_name), "%s-%c",
		DS5_DRIVER_NAME_DFU, suffix);
#else
	snprintf (dev_name, sizeof(dev_name), "%s-%d-%04x",
			DS5_DRIVER_NAME_DFU, i2c_adapter_id(c->adapter), c->addr);
#endif
	chr_dev = device_create(*ds5_class, NULL, *dev_num, NULL, dev_name);
	if (IS_ERR(chr_dev)) {
		ret = PTR_ERR(chr_dev);
		dev_err(&c->dev, "Could not create device\n");
		class_destroy(*ds5_class);
		unregister_chrdev_region(0, 1);
		return ret;
	}
	cdev_add(ds5_cdev, *dev_num, 1);
	atomic_inc(&primary_chardev);
	return 0;
};

static int ds5_chrdev_remove(struct ds5 *state)
{
	struct class **ds5_class = &state->dfu_dev.ds5_class;
	dev_t *dev_num = &state->client->dev.devt;
	if (!ds5_class) {
		return 0;
	}
	dev_dbg(&state->client->dev, "%s()\n", __func__);
	unregister_chrdev_region(*dev_num, 1);
	device_destroy(*ds5_class, *dev_num);
	if (atomic_dec_and_test(&primary_chardev)) {
		dev_warn(&state->client->dev, "%s() class_destroy\n", __func__);
		class_destroy(*ds5_class);
	}
	return 0;
}

#ifdef CONFIG_VIDEO_INTEL_IPU6
static void ds5_substream_init(struct ds5 *state)
{
	int i;
	state->pad_to_vc[DS5_MUX_PAD_EXTERNAL]= -1;
	if (!state->aggregated) {
		state->pad_to_vc[DS5_MUX_PAD_DEPTH]   = sensor_vc[0];
		state->pad_to_vc[DS5_MUX_PAD_RGB]     = sensor_vc[1];
		state->pad_to_vc[DS5_MUX_PAD_IR]      = sensor_vc[2];
		state->pad_to_vc[DS5_MUX_PAD_IMU]     = sensor_vc[3];
	} else {
		state->pad_to_vc[DS5_MUX_PAD_DEPTH] = sensor_vc[4];
		state->pad_to_vc[DS5_MUX_PAD_RGB]   = sensor_vc[5];
		state->pad_to_vc[DS5_MUX_PAD_IR]    = sensor_vc[6];
		state->pad_to_vc[DS5_MUX_PAD_IMU]   = sensor_vc[7];
	}

	for (i = 0; i < ARRAY_SIZE(state->pad_to_substream); i++)
		state->pad_to_substream[i] = -1;
	/* match for IPU6 CSI2 BE SOC video capture pads */
	if (!state->aggregated) {
		state->pad_to_substream[DS5_MUX_PAD_DEPTH]   = 0;
		state->pad_to_substream[DS5_MUX_PAD_RGB]     = 2;
		state->pad_to_substream[DS5_MUX_PAD_IR]      = 4;
		state->pad_to_substream[DS5_MUX_PAD_IMU]     = 5;
	}
	else {
		state->pad_to_substream[DS5_MUX_PAD_DEPTH] = 6;
		state->pad_to_substream[DS5_MUX_PAD_RGB]   = 8;
		state->pad_to_substream[DS5_MUX_PAD_IR]    = 10;
		state->pad_to_substream[DS5_MUX_PAD_IMU]   = 11;
	}
	/*
	 * 0, vc 0, depth
	 * 1, vc 0, meta data
	 * 2, vc 1, RGB
	 * 3, vc 1, meta data
	 * 4, vc 2, IR
	 * 5, vc 3, IMU
	 */
	/* aggreagated */
	/*
	 * 6, vc 2, depth
	 * 7, vc 2, meta data
	 * 8, vc 3, RGB
	 * 9, vc 3, meta data
	 * 10, vc 0, IR
	 * 11, vc 1, IMU
	 */
	/*DEPTH*/
	set_sub_stream_fmt  (state->pad_to_substream[DS5_MUX_PAD_DEPTH], MEDIA_BUS_FMT_UYVY8_1X16);
	set_sub_stream_h    (state->pad_to_substream[DS5_MUX_PAD_DEPTH], 480);
	set_sub_stream_w    (state->pad_to_substream[DS5_MUX_PAD_DEPTH], 640);
	set_sub_stream_dt   (state->pad_to_substream[DS5_MUX_PAD_DEPTH], mbus_code_to_mipi(MEDIA_BUS_FMT_UYVY8_1X16));
	set_sub_stream_vc_id(state->pad_to_substream[DS5_MUX_PAD_DEPTH], state->pad_to_vc[DS5_MUX_PAD_DEPTH]);
	/*DEPTH MD*/
	set_sub_stream_fmt  (state->pad_to_substream[DS5_MUX_PAD_DEPTH] + 1, MEDIA_BUS_FMT_SGRBG8_1X8);
	set_sub_stream_h    (state->pad_to_substream[DS5_MUX_PAD_DEPTH] + 1, 1);
	set_sub_stream_w    (state->pad_to_substream[DS5_MUX_PAD_DEPTH] + 1, 68);
	set_sub_stream_dt   (state->pad_to_substream[DS5_MUX_PAD_DEPTH] + 1, MIPI_CSI2_TYPE_EMBEDDED8);
	set_sub_stream_vc_id(state->pad_to_substream[DS5_MUX_PAD_DEPTH] + 1, state->pad_to_vc[DS5_MUX_PAD_DEPTH]);

	/*RGB*/
	set_sub_stream_fmt  (state->pad_to_substream[DS5_MUX_PAD_RGB], MEDIA_BUS_FMT_YUYV8_1X16);
	set_sub_stream_h    (state->pad_to_substream[DS5_MUX_PAD_RGB], 640);
	set_sub_stream_w    (state->pad_to_substream[DS5_MUX_PAD_RGB], 480);
	set_sub_stream_dt   (state->pad_to_substream[DS5_MUX_PAD_RGB], mbus_code_to_mipi(MEDIA_BUS_FMT_UYVY8_1X16));
	set_sub_stream_vc_id(state->pad_to_substream[DS5_MUX_PAD_RGB], state->pad_to_vc[DS5_MUX_PAD_RGB]);
	/*RGB MD*/
	set_sub_stream_fmt  (state->pad_to_substream[DS5_MUX_PAD_RGB] + 1, MEDIA_BUS_FMT_SGRBG8_1X8);
	set_sub_stream_h    (state->pad_to_substream[DS5_MUX_PAD_RGB] + 1, 1);
	set_sub_stream_w    (state->pad_to_substream[DS5_MUX_PAD_RGB] + 1, 68);
	set_sub_stream_dt   (state->pad_to_substream[DS5_MUX_PAD_RGB] + 1, MIPI_CSI2_TYPE_EMBEDDED8);
	set_sub_stream_vc_id(state->pad_to_substream[DS5_MUX_PAD_RGB] + 1, state->pad_to_vc[DS5_MUX_PAD_RGB]);
	/*IR*/
	set_sub_stream_fmt  (state->pad_to_substream[DS5_MUX_PAD_IR], MEDIA_BUS_FMT_UYVY8_1X16);
	set_sub_stream_h    (state->pad_to_substream[DS5_MUX_PAD_IR], 640);
	set_sub_stream_w    (state->pad_to_substream[DS5_MUX_PAD_IR], 480);
	set_sub_stream_dt   (state->pad_to_substream[DS5_MUX_PAD_IR], mbus_code_to_mipi(MEDIA_BUS_FMT_UYVY8_1X16));
	set_sub_stream_vc_id(state->pad_to_substream[DS5_MUX_PAD_IR], state->pad_to_vc[DS5_MUX_PAD_IR]);
	/*IMU*/
	set_sub_stream_fmt  (state->pad_to_substream[DS5_MUX_PAD_IMU], MEDIA_BUS_FMT_UYVY8_1X16);
	set_sub_stream_h    (state->pad_to_substream[DS5_MUX_PAD_IMU], 640);
	set_sub_stream_w    (state->pad_to_substream[DS5_MUX_PAD_IMU], 480);
	set_sub_stream_dt   (state->pad_to_substream[DS5_MUX_PAD_IMU], mbus_code_to_mipi(MEDIA_BUS_FMT_UYVY8_1X16));
	set_sub_stream_vc_id(state->pad_to_substream[DS5_MUX_PAD_IMU], state->pad_to_vc[DS5_MUX_PAD_IMU]);
}
#endif

/* SYSFS attributes */
#ifdef CONFIG_SYSFS
static ssize_t ds5_fw_ver_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *c = to_i2c_client(dev);
	struct ds5 *state = container_of(i2c_get_clientdata(c),
			struct ds5, mux.sd.subdev);

	ds5_read(state, DS5_FW_VERSION, &state->fw_version);
	ds5_read(state, DS5_FW_BUILD, &state->fw_build);

	return snprintf(buf, PAGE_SIZE, "D4XX Sensor: %s, Version: %d.%d.%d.%d\n",
			ds5_get_sensor_name(state),
			(state->fw_version >> 8) & 0xff, state->fw_version & 0xff,
			(state->fw_build >> 8) & 0xff, state->fw_build & 0xff);
}

static DEVICE_ATTR_RO(ds5_fw_ver);

/* Derive 'device_attribute' structure for a read register's attribute */
struct dev_ds5_reg_attribute {
	struct device_attribute attr;
	u16 reg;	// register
	u8 valid;	// validity of above data
};

/** Read DS5 register.
 * ds5_read_reg_show will actually read register from ds5 while
 * ds5_read_reg_store will store register to read
 * Example:
 * echo -n "0xc03c" >ds5_read_reg
 * Read register result:
 * cat ds5_read_reg
 * Expected:
 * reg:0xc93c, result:0x11
 */
static ssize_t ds5_read_reg_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u16 rbuf;
	int n;
	struct i2c_client *c = to_i2c_client(dev);
	struct ds5 *state = container_of(i2c_get_clientdata(c),
			struct ds5, mux.sd.subdev);
	struct dev_ds5_reg_attribute *ds5_rw_attr = container_of(attr,
			struct dev_ds5_reg_attribute, attr);
	if (ds5_rw_attr->valid != 1)
		return -EINVAL;
	ds5_read(state, ds5_rw_attr->reg, &rbuf);

	n = snprintf(buf, PAGE_SIZE, "register:0x%4x, value:0x%02x\n",
			ds5_rw_attr->reg, rbuf);

	return n;
}

/** Read DS5 register - Store reg to attr struct pointer
 * ds5_read_reg_show will actually read register from ds5 while
 * ds5_read_reg_store will store module, offset and length
 */
static ssize_t ds5_read_reg_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct dev_ds5_reg_attribute *ds5_rw_attr = container_of(attr,
			struct dev_ds5_reg_attribute, attr);
	int rc = -1;
	u32 reg;
	ds5_rw_attr->valid = 0;
	/* Decode input */
	rc = sscanf(buf, "0x%04x", &reg);
	if (rc != 1)
		return -EINVAL;
	ds5_rw_attr->reg = reg;
	ds5_rw_attr->valid = 1;
	return count;
}

#define DS5_RW_REG_ATTR(_name) \
		struct dev_ds5_reg_attribute dev_attr_##_name = { \
			__ATTR(_name, S_IRUGO | S_IWUSR, \
			ds5_read_reg_show, ds5_read_reg_store), \
			0, 0 }

static DS5_RW_REG_ATTR(ds5_read_reg);

static ssize_t ds5_write_reg_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *c = to_i2c_client(dev);
	struct ds5 *state = container_of(i2c_get_clientdata(c),
			struct ds5, mux.sd.subdev);

	int rc = -1;
	u32 reg, w_val = 0;
	u16 val = -1;
	/* Decode input */
	rc = sscanf(buf, "0x%04x 0x%04x", &reg, &w_val);
	if (rc != 2)
		return -EINVAL;
	val = w_val & 0xffff;
	mutex_lock(&state->lock);
	ds5_write(state, reg, val);
	mutex_unlock(&state->lock);
	return count;
}

static DEVICE_ATTR_WO(ds5_write_reg);

static struct attribute *ds5_attributes[] = {
		&dev_attr_ds5_fw_ver.attr,
		&dev_attr_ds5_read_reg.attr.attr,
		&dev_attr_ds5_write_reg.attr,
		NULL
};

static const struct attribute_group ds5_attr_group = {
	.attrs = ds5_attributes,
};
#endif

#ifdef CONFIG_VIDEO_INTEL_IPU6
#define NR_DESER 4

#ifndef CONFIG_VIDEO_D4XX_SERDES
static const struct regmap_config ds5_regmap_max9296 = {
	.reg_bits = 16,
	.val_bits = 8,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
};

static const struct regmap_config ds5_regmap_max9295 = {
	.reg_bits = 16,
	.val_bits = 8,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
};

static int ds5_i2c_addr_setting(struct i2c_client *c, struct ds5 *state)
{
	int curr_max9296 = c->addr;
	int max9296_addrs[MAX9296_NUM] = {0x48, 0x4a, 0x68, 0x6c};
	int i;
	u8 val;
	int ret;
	struct d4xx_pdata *dpdata = c->dev.platform_data;
	unsigned short ser_alias;
	unsigned short sensor_alias;

	/* TODO: 2x D457 subdev connect to MAX9296 */
	if (dpdata->subdev_num >= 1) {
		curr_max9296 = c->addr;
		sensor_alias = dpdata->subdev_info[0].board_info.addr;
		ser_alias = dpdata->subdev_info[0].ser_alias;
	} else {
		dev_err(&c->dev, "no subdev found!\n");
		return -EINVAL;
	}

	dev_dbg(&c->dev, "curr_max9296 0x%02x, sensor_alias 0x%02x, ser_alias 0x%02x\n",
		curr_max9296, sensor_alias, ser_alias);

	/*
	 * don't reset link,
	 * check max9296 i2c addr + 1,
	 * max9295 i2c addr reassigned already.
	 */
	c->addr = curr_max9296 + 1;
	ret = max9295_read_8(state, MAX9295_REG0, &val);
	if (!ret) {
		max9295_write_8(state, MAX9295_REG0, ser_alias << 1);
		msleep_range(1000); /* need this? */
		c->addr = ser_alias;
		max9295_write_8(state, MAX9295_I2C_4, sensor_alias << 1);
		max9295_write_8(state, MAX9295_I2C_5, D457_I2C_ADDR << 1);
		c->addr = sensor_alias;
		return 0;
	}

	/* i2c addr reassignment for all max9295 */
	for (i = 0; i < MAX9296_NUM; i++) {
		c->addr = max9296_addrs[i];
		max9296_write_8(state, MAX9296_CTRL0, RESET_LINK);
	}

	for (i = 0; i < MAX9296_NUM; i++) {
		/* release reset */
		c->addr = max9296_addrs[i];
		max9296_write_8(state, MAX9296_CTRL0, AUTO_LINK | LINK_A);
		msleep_range(1000);

		if (curr_max9296 == max9296_addrs[i]) {
			c->addr = MAX9295_I2C_ADDR_DEF;
			ret = max9295_read_8(state, MAX9295_REG0, &val);
			if (ret < 0) {
				dev_err(&c->dev, "no max9295 found for max9296 %x\n", curr_max9296);
				continue;
			}
			max9295_write_8(state, MAX9295_REG0, ser_alias << 1);
			msleep_range(1000); // need this?
			c->addr = ser_alias;
			max9295_write_8(state, MAX9295_I2C_4, sensor_alias << 1);
			max9295_write_8(state, MAX9295_I2C_5, D457_I2C_ADDR << 1);
			continue;
		}

		c->addr = MAX9295_I2C_ADDR_DEF;
		ret = max9295_read_8(state, MAX9295_REG0, &val);
		if (ret < 0)
			continue;
		max9295_write_8(state, MAX9295_REG0, (max9296_addrs[i] + 1)  << 1);
	}

	c->addr = sensor_alias;

	return 0;
}
#endif
#endif //CONFIG_VIDEO_INTEL_IPU6

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
static int ds5_probe(struct i2c_client *c, const struct i2c_device_id *id)
#else
static int ds5_probe(struct i2c_client *c)
#endif
{
	struct ds5 *state = devm_kzalloc(&c->dev, sizeof(*state), GFP_KERNEL);
	u16 rec_state;
	int ret, retry, err = 0;
#ifdef CONFIG_OF
	const char *str;
#endif
	if (!state)
		return -ENOMEM;

	mutex_init(&state->lock);

	state->client = c;
	dev_warn(&c->dev, "Probing driver for D45x\n");
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
	state->variant = ds5_variants + id->driver_data;
#else
	state->variant = ds5_variants;
#endif
#ifdef CONFIG_OF
	state->vcc = devm_regulator_get(&c->dev, "vcc");
	if (IS_ERR(state->vcc)) {
		ret = PTR_ERR(state->vcc);
		dev_warn(&c->dev, "failed %d to get vcc regulator\n", ret);
		return ret;
	}

	if (state->vcc) {
		ret = regulator_enable(state->vcc);
		if (ret < 0) {
			dev_warn(&c->dev, "failed %d to enable the vcc regulator\n", ret);
			return ret;
		}
	}
#endif
	state->regmap = devm_regmap_init_i2c(c, &ds5_regmap_config);
	if (IS_ERR(state->regmap)) {
		ret = PTR_ERR(state->regmap);
		dev_err(&c->dev, "regmap init failed: %d\n", ret);
		goto e_regulator;
	}

#ifdef CONFIG_VIDEO_D4XX_SERDES
	ret = ds5_serdes_setup(state);
	if (ret < 0)
		goto e_regulator;
#endif
#ifdef CONFIG_VIDEO_INTEL_IPU6
#ifndef CONFIG_VIDEO_D4XX_SERDES
	state->regmap_max9296 = devm_regmap_init_i2c(c, &ds5_regmap_max9296);
	if (IS_ERR(state->regmap_max9296)) {
		ret = PTR_ERR(state->regmap_max9296);
		dev_err(&c->dev, "regmap max9296 init failed: %d\n", ret);
		return ret;
	}

	state->regmap_max9295 = devm_regmap_init_i2c(c, &ds5_regmap_max9295);
	if (IS_ERR(state->regmap_max9295)) {
		ret = PTR_ERR(state->regmap_max9295);
		dev_err(&c->dev, "regmap max9295 init failed: %d\n", ret);
		return ret;
	}

	ret = ds5_i2c_addr_setting(c, state);
	if (ret) {
		dev_err(&c->dev, "failed apply i2c addr setting\n");
		return ret;
	}
#endif
#endif //CONFIG_VIDEO_INTEL_IPU6
	// Verify communication
	retry = 5;
	do {
		ret = ds5_read(state, 0x5020, &rec_state);
	} while (retry-- && ret < 0);
	if (ret < 0) {
		dev_err(&c->dev,
			"%s(): cannot communicate with D4XX: %d on addr: 0x%x\n",
			__func__, ret, c->addr);
		goto e_regulator;
	}

	state->is_depth = 0;
	state->is_y8 = 0;
	state->is_rgb = 0;
	state->is_imu = 0;
#ifdef CONFIG_OF
	ret = of_property_read_string(c->dev.of_node, "cam-type", &str);
	if (!ret && !strncmp(str, "Depth", strlen("Depth"))) {
		state->is_depth = 1;
	}
	if (!ret && !strncmp(str, "Y8", strlen("Y8"))) {
		state->is_y8 = 1;
	}
	if (!ret && !strncmp(str, "RGB", strlen("RGB"))) {
		state->is_rgb = 1;
	}
	if (!ret && !strncmp(str, "IMU", strlen("IMU"))) {
		state->is_imu = 1;
	}
#else
	state->is_depth = 1;
#endif
	/* create DFU chardev once */
	if (state->is_depth) {
		ret = ds5_chrdev_init(c, state);
		if (ret < 0)
			goto e_regulator;
	}

	ret = ds5_read(state, 0x5020, &rec_state);
	if (ret < 0) {
		dev_err(&c->dev, "%s(): cannot communicate with D4XX: %d\n",
				__func__, ret);
		goto e_chardev;
	}

	if (rec_state == 0x201) {
		dev_info(&c->dev, "%s(): D4XX recovery state\n", __func__);
		state->dfu_dev.dfu_state_flag = DS5_DFU_RECOVERY;
		/* Override I2C drvdata with state for use in remove function */
		i2c_set_clientdata(c, state);
		return 0;
	}

	ds5_read_with_check(state, DS5_FW_VERSION, &state->fw_version);
	ds5_read_with_check(state, DS5_FW_BUILD, &state->fw_build);

	dev_info(&c->dev, "D4XX Sensor: %s, firmware build: %d.%d.%d.%d\n",
			ds5_get_sensor_name(state),
			(state->fw_version >> 8) & 0xff, state->fw_version & 0xff,
			(state->fw_build >> 8) & 0xff, state->fw_build & 0xff);

	ret = ds5_v4l_init(c, state);
	if (ret < 0)
		goto e_chardev;

/*	regulators? clocks?
 *	devm_regulator_bulk_get(&c->dev, DS5_N_SUPPLIES, state->supplies);
 *	state->clock = devm_clk_get(&c->dev, DS5_CLK_NAME);
 *	if (IS_ERR(state->clock)) {
 *		ret = -EPROBE_DEFER;
 *		goto err;
 *	}
 */
#ifdef CONFIG_SYSFS
	/* Custom sysfs attributes */
	/* create the sysfs file group */
	err = sysfs_create_group(&state->client->dev.kobj, &ds5_attr_group);
#endif
#ifdef CONFIG_VIDEO_INTEL_IPU6
	ds5_substream_init(state);
#endif
	return 0;

e_chardev:
	if (state->dfu_dev.ds5_class)
		ds5_chrdev_remove(state);
e_regulator:
	if (state->vcc)
		regulator_disable(state->vcc);
#ifdef CONFIG_VIDEO_D4XX_SERDES
	if (state->ser_i2c)
		i2c_unregister_device(state->ser_i2c);
	if (state->dser_i2c && !state->aggregated)
		i2c_unregister_device(state->dser_i2c);
#endif
	return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
static int ds5_remove(struct i2c_client *c)
#else
static void ds5_remove(struct i2c_client *c)
#endif
{
#ifdef CONFIG_VIDEO_D4XX_SERDES
	int i, ret;
#endif
	struct ds5 *state = container_of(i2c_get_clientdata(c), struct ds5, mux.sd.subdev);
	if (state && !state->mux.sd.subdev.v4l2_dev) {
		state = i2c_get_clientdata(c);
	}

#ifdef CONFIG_VIDEO_D4XX_SERDES
	for (i = 0; i < MAX_DEV_NUM; i++) {
		if (serdes_inited[i] && serdes_inited[i] == state) {
			serdes_inited[i] = NULL;
			mutex_lock(&serdes_lock__);

			ret = max9295_reset_control(state->ser_dev);
			if (ret)
				dev_warn(&c->dev,
				  "failed in 9295 reset control\n");
			ret = max9296_reset_control(state->dser_dev,
				state->g_ctx.s_dev);
			if (ret)
				dev_warn(&c->dev,
				  "failed in 9296 reset control\n");

			ret = max9295_sdev_unpair(state->ser_dev,
				state->g_ctx.s_dev);
			if (ret)
				dev_warn(&c->dev, "failed to unpair sdev\n");
			ret = max9296_sdev_unregister(state->dser_dev,
				state->g_ctx.s_dev);
			if (ret)
				dev_warn(&c->dev,
				  "failed to sdev unregister sdev\n");
			max9296_power_off(state->dser_dev);

			mutex_unlock(&serdes_lock__);
			break;
		}
	}
	if (state->ser_i2c)
		i2c_unregister_device(state->ser_i2c);
	if (state->dser_i2c)
		i2c_unregister_device(state->dser_i2c);
#endif
#ifndef CONFIG_TEGRA_CAMERA_PLATFORM
	state->is_depth = 1;
#endif
	dev_info(&c->dev, "D4XX remove %s\n",
			ds5_get_sensor_name(state));
	if (state->vcc)
		regulator_disable(state->vcc);
//	gpio_free(state->pwdn_gpio);

	if (state->dfu_dev.dfu_state_flag != DS5_DFU_RECOVERY && \
		 state->mux.sd.subdev.v4l2_dev) {
#ifdef CONFIG_SYSFS
		sysfs_remove_group(&c->dev.kobj, &ds5_attr_group);
#endif
		ds5_mux_remove(state);
	}

	if (state->is_depth && state->dfu_dev.ds5_class) {
		ds5_chrdev_remove(state);
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
	return 0;
#endif
}

static const struct i2c_device_id ds5_id[] = {
	{ DS5_DRIVER_NAME, DS5_DS5U },
	{ DS5_DRIVER_NAME_ASR, DS5_ASR },
	{ DS5_DRIVER_NAME_AWG, DS5_AWG },
	{ },
};
MODULE_DEVICE_TABLE(i2c, ds5_id);

static const struct of_device_id d4xx_of_match[] = {
	{ .compatible = "intel,d4xx", },
	{ },
};
MODULE_DEVICE_TABLE(of, d4xx_of_match);

static struct i2c_driver ds5_i2c_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = DS5_DRIVER_NAME
	},
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
        .probe          = ds5_probe,
#elif LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)
	.probe_new	= ds5_probe,
#else
	.probe		= ds5_probe,
#endif
	.remove		= ds5_remove,
	.id_table	= ds5_id,
};

module_i2c_driver(ds5_i2c_driver);

MODULE_DESCRIPTION("Intel RealSense D4XX Camera Driver");
MODULE_AUTHOR("Guennadi Liakhovetski <guennadi.liakhovetski@intel.com>,\n\
				Nael Masalha <nael.masalha@intel.com>,\n\
				Alexander Gantman <alexander.gantman@intel.com>,\n\
				Emil Jahshan <emil.jahshan@intel.com>,\n\
				Xin Zhang <xin.x.zhang@intel.com>,\n\
				Qingwu Zhang <qingwu.zhang@intel.com>,\n\
				Evgeni Raikhel <evgeni.raikhel@intel.com>,\n\
				Shikun Ding <shikun.ding@intel.com>");
MODULE_AUTHOR("Dmitry Perchanov <dmitry.perchanov@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0.1.23");
