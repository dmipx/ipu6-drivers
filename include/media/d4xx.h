/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2022 Intel Corporation */

#ifndef D4XX_H
#define D4XX_H

#define D457_NAME "d4xx"
#define MAX9296_NAME "MAX9296"

struct serdes_pdata {
	uint32_t reg;
	uint32_t def_addr;
	uint32_t dst_csi_port;
	uint32_t src_csi_port;
	uint32_t csi_mode;
	uint32_t serdes_csi_link;
	uint32_t st_vc;
	uint32_t vc_id;
	uint32_t num_lanes;
};

struct d4xx_pdata {
	unsigned int port;
	unsigned int nlanes;
	struct serdes_pdata gmsl_link;
	char suffix;
	union {	
		struct i2c_client *ser_i2c;
		uint32_t ser_addr;
	};
	union {	
		struct i2c_client *dser_i2c;
		uint32_t des_addr;
	};
};

#endif
