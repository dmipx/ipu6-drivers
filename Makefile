# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2021 Intel Corporation.

export EXTERNAL_BUILD = 1
export CONFIG_INTEL_VSC = m

export CONFIG_VIDEO_INTEL_IPU6 = m
export CONFIG_VIDEO_INTEL_IPU_SOC = y
export CONFIG_VIDEO_INTEL_IPU_USE_PLATFORMDATA = y
export CONFIG_INTEL_SKL_INT3472 = m
export CONFIG_INTEL_IPU6_ACPI = m
obj-y += drivers/media/pci/intel/

export CONFIG_VIDEO_IMX390 = m
export CONFIG_VIDEO_AR0234 = m
export CONFIG_VIDEO_LT6911UXC = m
export CONFIG_I2C_IOEXPANDER_SER_MAX9295 = m
export CONFIG_I2C_IOEXPANDER_DESER_MAX9296 = m
export CONFIG_VIDEO_D4XX = m

obj-y += drivers/media/i2c/
obj-y += drivers/media/platform/intel/

KERNEL_SRC := /lib/modules/$(shell uname -r)/build
MODSRC := $(shell pwd)
ccflags-y += -I$(MODSRC)/include/

NOSTDINC_FLAGS += \
	-I$(M)/backport-include/ \
	-I$(M)/include/

subdir-ccflags-$(CONFIG_INTEL_VSC) += \
        -DCONFIG_INTEL_VSC_MODULE=1
subdir-ccflags-$(CONFIG_IPU_ISYS_BRIDGE) += \
	-DCONFIG_IPU_ISYS_BRIDGE=1
subdir-ccflags-$(CONFIG_INTEL_IPU6_ACPI) += \
        -DCONFIG_VIDEO_INTEL_IPU_USE_PLATFORMDATA=1 -DCONFIG_VIDEO_INTEL_IPU_PDATA_DYNAMIC_LOADING=1 -DCONFIG_INTEL_IPU6_ACPI=1
subdir-ccflags-$(CONFIG_VIDEO_INTEL_IPU6) += \
        -DCONFIG_DEBUG_FS=1 -DCONFIG_VIDEO_INTEL_IPU6=1 -DCONFIG_VIDEO_V4L2_SUBDEV_API=1
# subdir-ccflags-$(CONFIG_POWER_CTRL_LOGIC) += \
# 	-DCONFIG_POWER_CTRL_LOGIC_MODULE=1
subdir-ccflags-y += $(subdir-ccflags-m)

all:
	$(MAKE) V=1 -C $(KERNEL_SRC) M=$(MODSRC) modules
modules_install:
	$(MAKE) V=1 INSTALL_MOD_DIR=/extra -C $(KERNEL_SRC) M=$(MODSRC) modules_install
clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(MODSRC) clean
