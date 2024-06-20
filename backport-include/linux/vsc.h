/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_VSC_H_
#define _LINUX_VSC_H_

#include <linux/types.h>
#include <linux/module.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0))
#include <linux/kprobes.h>
#ifndef CONFIG_KPROBES
# error "You need kprobes :("
#endif
#endif


/**
 * @brief VSC camera ownership definition
 */
enum vsc_camera_owner {
	VSC_CAMERA_NONE = 0,
	VSC_CAMERA_CVF,
	VSC_CAMERA_IPU,
};

/**
 * @brief VSC privacy status definition
 */
enum vsc_privacy_status {
	VSC_PRIVACY_ON = 0,
	VSC_PRIVACY_OFF,
};

/**
 * @brief VSC MIPI configuration definition
 */
struct vsc_mipi_config {
	uint32_t freq;
	uint32_t lane_num;
};

/**
 * @brief VSC camera status definition
 */
struct vsc_camera_status {
	enum vsc_camera_owner owner;
	enum vsc_privacy_status status;
	uint32_t exposure_level;
};

/**
 * @brief VSC privacy callback type definition
 *
 * @param context Privacy callback handle
 * @param status Current privacy status
 */
typedef void (*vsc_privacy_callback_t)(void *handle,
				       enum vsc_privacy_status status);


typedef int (*vsc_acquire_camera_sensor_t)(struct vsc_mipi_config *,
					   vsc_privacy_callback_t,
					   void *,
					   struct vsc_camera_status *);
typedef int (*vsc_release_camera_sensor_t)(struct vsc_camera_status *);

static vsc_acquire_camera_sensor_t __p_vsc_acquire_camera_sensor = NULL;
static vsc_release_camera_sensor_t __p_vsc_release_camera_sensor = NULL;

static int init_vsc_symbols(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0))
	typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);
	struct kprobe probe;
	int ret;
	kallsyms_lookup_name_t kallsyms_lookup_name;

	memset(&probe, 0, sizeof(probe));
	probe.symbol_name = "kallsyms_lookup_name";
	ret = register_kprobe(&probe);
	if (ret)
		return ret;

	kallsyms_lookup_name = (kallsyms_lookup_name_t) probe.addr;
	unregister_kprobe(&probe);
#endif

	__p_vsc_acquire_camera_sensor =
		(vsc_acquire_camera_sensor_t) kallsyms_lookup_name("vsc_acquire_camera_sensor");
	__p_vsc_release_camera_sensor =
		(vsc_release_camera_sensor_t) kallsyms_lookup_name("vsc_release_camera_sensor");

	return 0;
}

/**
 * @brief Acquire camera sensor ownership to IPU
 *
 * @param config[IN] The pointer of MIPI configuration going to set
 * @param callback[IN] The pointer of privacy callback function
 * @param handle[IN] Privacy callback function runtime handle from IPU driver
 * @param status[OUT] The pointer of camera status after the acquire
 *
 * @retval 0 If success
 * @retval -EIO IO error
 * @retval -EINVAL Invalid argument
 * @retval -EAGAIN VSC device not ready
 * @retval negative values for other errors
 */
static int vsc_acquire_camera_sensor(struct vsc_mipi_config *config,
				      vsc_privacy_callback_t callback,
				      void *handle,
				      struct vsc_camera_status *status)
{
	return __p_vsc_acquire_camera_sensor(config, callback, handle, status);
}

/**
 * @brief Release camera sensor ownership
 *
 * @param status[OUT] Camera status after the release
 *
 * @retval 0 If success
 * @retval -EIO IO error
 * @retval -EINVAL Invalid argument
 * @retval -EAGAIN VSC device not ready
 * @retval negative values for other errors
 */
static int vsc_release_camera_sensor(struct vsc_camera_status *status)
{
	return __p_vsc_release_camera_sensor(status);
}

MODULE_SOFTDEP("pre: intel_vsc");

#endif
