/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_QRTR_H
#define LINUX_DEVICE_ID_QRTR_H

#ifdef __KERNEL__
#include <linux/types.h>
#endif

#define QRTR_NAME_SIZE		32
#define QRTR_MODULE_PREFIX	"qrtr:"

struct qrtr_device_id {
	__u16 service;
	__u16 instance;
	kernel_ulong_t driver_data;	/* Data private to the driver */
};

#endif /* ifndef LINUX_DEVICE_ID_QRTR_H */
