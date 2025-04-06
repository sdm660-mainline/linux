/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __QCOM_SMGR_H__
#define __QCOM_SMGR_H__

#include <linux/iio/iio.h>
#include <linux/iio/types.h>
#include <linux/types.h>

enum qcom_smgr_sensor_type {
	SNS_SMGR_SENSOR_TYPE_UNKNOWN,
	SNS_SMGR_SENSOR_TYPE_ACCEL,
	SNS_SMGR_SENSOR_TYPE_GYRO,
	SNS_SMGR_SENSOR_TYPE_MAG,
	SNS_SMGR_SENSOR_TYPE_PROX_LIGHT,
	SNS_SMGR_SENSOR_TYPE_PRESSURE,
	SNS_SMGR_SENSOR_TYPE_HALL_EFFECT,

	SNS_SMGR_SENSOR_TYPE_COUNT
};

enum qcom_smgr_data_type {
	SNS_SMGR_DATA_TYPE_PRIMARY,
	SNS_SMGR_DATA_TYPE_SECONDARY,

	SNS_SMGR_DATA_TYPE_COUNT
};

struct qcom_smgr_data_type_item {
	const char *name;
	const char *vendor;

	u32 range;

	size_t native_sample_rate_count;
	u16 *native_sample_rates;

	u16 max_sample_rate;
	u16 cur_sample_rate;
};

struct qcom_smgr_sensor {
	u8 id;
	enum qcom_smgr_sensor_type type;

	u8 data_type_count;
	/*
	 * Only SNS_SMGR_DATA_TYPE_PRIMARY is used at the moment, but we store
	 * SNS_SMGR_DATA_TYPE_SECONDARY when available as well for future use.
	 */
	struct qcom_smgr_data_type_item *data_types;

	struct iio_dev *iio_dev;
};

struct qcom_smgr_iio_priv {
	struct qcom_smgr_sensor *sensor;
};

extern const struct iio_buffer_setup_ops qcom_smgr_buffer_ops;
extern const struct iio_info qcom_smgr_iio_info;
extern const struct iio_chan_spec_ext_info qcom_smgr_iio_ext_info[];

#endif /* __QCOM_SMGR_H__ */
