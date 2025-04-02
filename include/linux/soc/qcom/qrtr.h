/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __QCOM_QRTR_H__
#define __QCOM_QRTR_H__

struct qrtr_device {
	struct device dev;
	unsigned int node;
	unsigned int port;
	u16 service;
	u16 instance;
};

#define to_qrtr_device(d) container_of(d, struct qrtr_device, dev)

struct qrtr_driver {
	int (*probe)(struct qrtr_device *qdev);
	void (*remove)(struct qrtr_device *qdev);
	const struct qrtr_device_id *id_table;
	struct device_driver driver;
};

#define to_qrtr_driver(d) container_of(d, struct qrtr_driver, driver)

#define qrtr_driver_register(drv) __qrtr_driver_register(drv, THIS_MODULE)

int __qrtr_driver_register(struct qrtr_driver *drv, struct module *owner);
void qrtr_driver_unregister(struct qrtr_driver *drv);

#define module_qrtr_driver(__qrtr_driver) \
	module_driver(__qrtr_driver, qrtr_driver_register, \
			qrtr_driver_unregister)

#endif /* __QCOM_QRTR_H__ */
