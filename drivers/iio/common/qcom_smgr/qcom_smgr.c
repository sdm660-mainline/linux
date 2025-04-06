// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm Sensor Manager core driver
 *
 * Copyright (c) 2021, Yassine Oudjana <y.oudjana@protonmail.com>
 */

#include <linux/iio/buffer.h>
#include <linux/iio/common/qcom_smgr.h>
#include <linux/iio/iio.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/remoteproc/qcom_rproc.h>
#include <linux/soc/qcom/qmi.h>
#include <linux/soc/qcom/qrtr.h>
#include <linux/types.h>
#include <net/sock.h>

#include "qmi/sns_smgr.h"

#define SMGR_TICKS_PER_SECOND		32768
#define SMGR_REPORT_RATE_HZ		(SMGR_TICKS_PER_SECOND * 2)
#define SMGR_VALUE_DIV			65536

struct qcom_smgr {
	struct device *dev;

	struct qmi_handle sns_smgr_hdl;
	struct sockaddr_qrtr sns_smgr_info;
	struct work_struct sns_smgr_work;

	u8 sensor_count;
	struct qcom_smgr_sensor *sensors;
};

static const char *const qcom_smgr_sensor_type_platform_names[] = {
	[SNS_SMGR_SENSOR_TYPE_ACCEL] = "qcom-smgr-accel",
	[SNS_SMGR_SENSOR_TYPE_GYRO] = "qcom-smgr-gyro",
	[SNS_SMGR_SENSOR_TYPE_MAG] = "qcom-smgr-mag",
	[SNS_SMGR_SENSOR_TYPE_PROX_LIGHT] = "qcom-smgr-prox-light",
	[SNS_SMGR_SENSOR_TYPE_PRESSURE] = "qcom-smgr-pressure",
	[SNS_SMGR_SENSOR_TYPE_HALL_EFFECT] = "qcom-smgr-hall-effect"
};

static void qcom_smgr_unregister_sensor(void *data)
{
	struct platform_device *pdev = data;

	platform_device_unregister(pdev);
}

static int qcom_smgr_register_sensor(struct qcom_smgr *smgr,
				     struct qcom_smgr_sensor *sensor)
{
	struct platform_device *pdev;
	const char *name = qcom_smgr_sensor_type_platform_names[sensor->type];

	pdev = platform_device_register_data(smgr->dev, name, sensor->id,
					     &sensor, sizeof(sensor));
	if (IS_ERR(pdev)) {
		dev_err(smgr->dev, "Failed to register %s: %pe\n", name, pdev);
		return PTR_ERR(pdev);
	}

	return devm_add_action_or_reset(smgr->dev, qcom_smgr_unregister_sensor,
					pdev);
}

static int qcom_smgr_request_all_sensor_info(struct qcom_smgr *smgr,
					     struct qcom_smgr_sensor **sensors)
{
	struct sns_smgr_all_sensor_info_resp resp = {};
	struct qmi_txn txn;
	u8 i;
	int ret;

	dev_dbg(smgr->dev, "Getting available sensors\n");

	ret = qmi_txn_init(&smgr->sns_smgr_hdl, &txn,
			   sns_smgr_all_sensor_info_resp_ei, &resp);
	if (ret < 0) {
		dev_err(smgr->dev, "Failed to initialize QMI TXN: %d\n", ret);
		return ret;
	}

	ret = qmi_send_request(&smgr->sns_smgr_hdl, &smgr->sns_smgr_info, &txn,
			       SNS_SMGR_ALL_SENSOR_INFO_MSG_ID,
			       SNS_SMGR_ALL_SENSOR_INFO_REQ_MAX_LEN, NULL,
			       NULL);
	if (ret) {
		dev_err(smgr->dev,
			"Failed to send available sensors request: %d\n", ret);
		qmi_txn_cancel(&txn);
		return ret;
	}

	ret = qmi_txn_wait(&txn, 5 * HZ);
	if (ret < 0)
		return ret;

	/* Check the response */
	if (resp.result) {
		dev_err(smgr->dev, "Available sensors request failed: 0x%x\n",
			resp.result);
		return -EREMOTEIO;
	}

	*sensors = devm_kzalloc(smgr->dev,
				sizeof(struct qcom_smgr_sensor) * resp.item_len,
				GFP_KERNEL);

	for (i = 0; i < resp.item_len; ++i) {
		(*sensors)[i].id = resp.items[i].id;
		(*sensors)[i].type =
			sns_smgr_sensor_type_from_str(resp.items[i].type);
	}

	return resp.item_len;
}

static int qcom_smgr_request_single_sensor_info(struct qcom_smgr *smgr,
						struct qcom_smgr_sensor *sensor)
{
	struct sns_smgr_single_sensor_info_req req = {
		.sensor_id = sensor->id,
	};
	struct sns_smgr_single_sensor_info_resp resp = {};
	struct qmi_txn txn;
	u8 i;
	int ret;

	dev_vdbg(smgr->dev, "Getting single sensor info for ID 0x%02x\n",
		 sensor->id);

	ret = qmi_txn_init(&smgr->sns_smgr_hdl, &txn,
			   sns_smgr_single_sensor_info_resp_ei, &resp);
	if (ret < 0) {
		dev_err(smgr->dev, "Failed to initialize QMI transaction: %d\n",
			ret);
		return ret;
	}

	ret = qmi_send_request(&smgr->sns_smgr_hdl, &smgr->sns_smgr_info, &txn,
			       SNS_SMGR_SINGLE_SENSOR_INFO_MSG_ID,
			       SNS_SMGR_SINGLE_SENSOR_INFO_REQ_MAX_LEN,
			       sns_smgr_single_sensor_info_req_ei, &req);
	if (ret < 0) {
		dev_err(smgr->dev, "Failed to send sensor data request: %d\n",
			ret);
		qmi_txn_cancel(&txn);
		return ret;
	}

	ret = qmi_txn_wait(&txn, 5 * HZ);
	if (ret < 0)
		return ret;

	/* Check the response */
	if (resp.result) {
		dev_err(smgr->dev, "Single sensor info request failed: 0x%x\n",
			resp.result);
		return -EREMOTEIO;
	}

	sensor->data_type_count = resp.data_type_len;
	sensor->data_types =
		devm_kzalloc(smgr->dev,
			     sizeof(struct qcom_smgr_data_type_item) *
				     sensor->data_type_count,
			     GFP_KERNEL);
	if (!sensor->data_types)
		return -ENOMEM;

	for (i = 0; i < sensor->data_type_count; ++i) {
		sensor->data_types[i].name = devm_kstrdup_const(
			smgr->dev, resp.data_types[i].name, GFP_KERNEL);
		sensor->data_types[i].vendor = devm_kstrdup_const(
			smgr->dev, resp.data_types[i].vendor, GFP_KERNEL);

		sensor->data_types[i].range = resp.data_types[i].range;

		sensor->data_types[i].native_sample_rate_count =
			resp.native_sample_rates[i].rate_count;
		if (sensor->data_types[i].native_sample_rate_count) {
			sensor->data_types[i]
				.native_sample_rates = devm_kmemdup_array(
				smgr->dev, resp.native_sample_rates[i].rates,
				sensor->data_types[i].native_sample_rate_count,
				sizeof(u16), GFP_KERNEL);
			if (!sensor->data_types[i].native_sample_rates)
				return -ENOMEM;
		}

		sensor->data_types[i].max_sample_rate =
			resp.data_types[i].max_sample_rate_hz;
	}

	return 0;
}

static int qcom_smgr_request_buffering(struct qcom_smgr *smgr,
				       struct qcom_smgr_sensor *sensor,
				       bool enable)
{
	struct sns_smgr_buffering_req req = {
		/*
		 * Reuse sensor ID as a report ID to avoid having to keep track
		 * of a separate set of IDs
		 */
		.report_id = sensor->id,
		.notify_suspend_valid = false
	};
	struct sns_smgr_buffering_resp resp = {};
	struct qmi_txn txn;
	u16 sample_rate = 0;
	int ret;

	if (enable) {
		req.action = SNS_SMGR_BUFFERING_ACTION_ADD;

		/*
		 * Report rate and sample rate can be configured separately.
		 * The former is the rate at which buffering report indications
		 * are sent, while the latter is the actual sample rate of the
		 * sensor. If report rate is set lower than sample rate,
		 * multiple samples can be bundled and sent in one report.
		 * A report cannot have 0 samples, therefore report rate cannot
		 * be higher than sample rate.
		 *
		 * Fow now we default to the maximum sample rate and set the
		 * report rate such that every report contains only 1 sample.
		 * This gives us the lowest latency.
		 */
		if (sensor->data_types[0].native_sample_rates)
			sample_rate = sensor->data_types[0].native_sample_rates
					[sensor->data_types[0]
						 .native_sample_rate_count - 1];

		/*
		 * SMGR may support a lower maximum sample rate than natively
		 * supported by the sensor.
		 */
		if (sample_rate == 0 ||
		    sample_rate > sensor->data_types[0].max_sample_rate)
			sample_rate = sensor->data_types[0].max_sample_rate;

		req.report_rate = sample_rate * SMGR_REPORT_RATE_HZ;

		req.item_len = 1;
		req.items[0].sensor_id = sensor->id;
		req.items[0].data_type = SNS_SMGR_DATA_TYPE_PRIMARY;

		req.items[0].sampling_rate = sample_rate;

		/*
		 * Unknown fields set to values frequently seen in dumps and
		 * known to be working (although many different random values
		 * appear to not cause any trouble).
		 */
		req.items[0].val1 = 3;
		req.items[0].val2 = 1;
	} else
		req.action = SNS_SMGR_BUFFERING_ACTION_DELETE;

	ret = qmi_txn_init(&smgr->sns_smgr_hdl, &txn,
			   sns_smgr_buffering_resp_ei, &resp);
	if (ret < 0) {
		dev_err(smgr->dev, "Failed to initialize QMI TXN: %d\n", ret);
		return ret;
	}

	ret = qmi_send_request(&smgr->sns_smgr_hdl, &smgr->sns_smgr_info, &txn,
			       SNS_SMGR_BUFFERING_MSG_ID,
			       SNS_SMGR_BUFFERING_REQ_MAX_LEN,
			       sns_smgr_buffering_req_ei, &req);
	if (ret < 0) {
		dev_err(smgr->dev, "Failed to send buffering request: %d\n",
			ret);
		qmi_txn_cancel(&txn);
		return ret;
	}

	ret = qmi_txn_wait(&txn, 5 * HZ);
	if (ret < 0)
		return ret;

	/* Check the response */
	if (resp.result) {
		dev_err(smgr->dev, "Buffering request failed: 0x%x\n",
			resp.result);
		return -EREMOTEIO;
	}

	/* Keep track of requested sample rate */
	sensor->data_types[0].cur_sample_rate = sample_rate;

	return 0;
}

static void qcom_smgr_buffering_report_handler(struct qmi_handle *hdl,
					       struct sockaddr_qrtr *sq,
					       struct qmi_txn *txn,
					       const void *data)
{
	struct qcom_smgr *smgr =
		container_of(hdl, struct qcom_smgr, sns_smgr_hdl);
	struct sns_smgr_buffering_report_ind *ind =
		(struct sns_smgr_buffering_report_ind *)data;
	struct qcom_smgr_sensor *sensor;
	u8 i;

	for (i = 0; i < smgr->sensor_count; ++i) {
		sensor = &smgr->sensors[i];

		/* Find sensor matching report */
		if (sensor->id != ind->report_id)
			continue;

		if (!sensor->iio_dev)
			/* Corresponding driver was unloaded. Ignore remaining reports. */
			return;

		/*
		 * Since we are matching report rate with sample rate, we only
		 * get a single sample in every report.
		 */
		iio_push_to_buffers_with_timestamp(sensor->iio_dev,
						   ind->samples[0].values,
						   ind->metadata.timestamp);

		break;
	}
}

static const struct qmi_msg_handler qcom_smgr_msg_handlers[] = {
	{
		.type = QMI_INDICATION,
		.msg_id = SNS_SMGR_BUFFERING_REPORT_MSG_ID,
		.ei = sns_smgr_buffering_report_ind_ei,
		.decoded_size = sizeof(struct sns_smgr_buffering_report_ind),
		.fn = qcom_smgr_buffering_report_handler,
	},
	{}
};

static int qcom_smgr_sensor_postenable(struct iio_dev *iio_dev)
{
	struct qcom_smgr *smgr = dev_get_drvdata(iio_dev->dev.parent->parent);
	struct qcom_smgr_iio_priv *priv = iio_priv(iio_dev);
	struct qcom_smgr_sensor *sensor = priv->sensor;

	return qcom_smgr_request_buffering(smgr, sensor, true);
}

static int qcom_smgr_sensor_postdisable(struct iio_dev *iio_dev)
{
	struct qcom_smgr *smgr = dev_get_drvdata(iio_dev->dev.parent->parent);
	struct qcom_smgr_iio_priv *priv = iio_priv(iio_dev);
	struct qcom_smgr_sensor *sensor = priv->sensor;

	return qcom_smgr_request_buffering(smgr, sensor, false);
}

const struct iio_buffer_setup_ops qcom_smgr_buffer_ops = {
	.postenable = &qcom_smgr_sensor_postenable,
	.postdisable = &qcom_smgr_sensor_postdisable
};
EXPORT_SYMBOL_GPL(qcom_smgr_buffer_ops);

static int qcom_smgr_iio_read_raw(struct iio_dev *iio_dev,
				  struct iio_chan_spec const *chan, int *val,
				  int *val2, long mask)
{
	struct qcom_smgr_iio_priv *priv = iio_priv(iio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = priv->sensor->data_types[0].cur_sample_rate;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_PROXIMITY:
			/*
			 * Proximity value is reported as (SMGR_VALUE_DIV - x)/SMGR_VALUE_DIV of
			 * the sensor range. As with sensor values, range is also reported as range
			 * in meters * SMGR_VALUE_DIV. Proximity in meters can be calculated as
			 * such:
			 *
			 * proximity = -value * range / SMGR_VALUE_DIV**2
			 *
			 * Since our denominator (val2) is an int, we cannot fit SMGR_VALUE_DIV**2.
			 * Without losing too much accuracy, we can instead divide by 2 in the
			 * numerator and denominator, and move the -1 coefficient to the
			 * denominator. This way we can exactly fit within the lower bound of int.
			 */
			*val = priv->sensor->data_types[0].range / 2;
			*val2 = -SMGR_VALUE_DIV / 2 * SMGR_VALUE_DIV;
			return IIO_VAL_FRACTIONAL;
		default:
			/*
			 * Sensor values are generally reported as 1/SMGR_VALUE_DIVths of the
			 * corresponding unit.
			 */
			*val = 1;
			*val2 = SMGR_VALUE_DIV;
			return IIO_VAL_FRACTIONAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		/*
		 * Proximity values are inverted and start from the upper bound as explained above.
		 * No other channel types have an offset.
		 */
		*val = priv->sensor->data_types[0].range;
		*val2 = SMGR_VALUE_DIV;
		return IIO_VAL_FRACTIONAL;
	}

	return -EINVAL;
}

static int qcom_smgr_iio_write_raw(struct iio_dev *iio_dev,
				   struct iio_chan_spec const *chan, int val,
				   int val2, long mask)
{
	struct qcom_smgr_iio_priv *priv = iio_priv(iio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		priv->sensor->data_types[0].cur_sample_rate = val;

		/*
		 * Send new SMGR buffering request with updated rates
		 * if buffer is enabled
		 */
		if (iio_buffer_enabled(iio_dev))
			return iio_dev->setup_ops->postenable(iio_dev);

		return 0;
	}

	return -EINVAL;
}

static int qcom_smgr_iio_read_avail(struct iio_dev *iio_dev,
				    struct iio_chan_spec const *chan,
				    const int **vals, int *type, int *length,
				    long mask)
{
	struct qcom_smgr_iio_priv *priv = iio_priv(iio_dev);
	const int samp_freq_vals[3] = {
		1, 1, priv->sensor->data_types[0].cur_sample_rate
	};

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*type = IIO_VAL_INT;
		*vals = samp_freq_vals;
		*length = ARRAY_SIZE(samp_freq_vals);
		return IIO_AVAIL_RANGE;
	}

	return -EINVAL;
}

const struct iio_info qcom_smgr_iio_info = {
	.read_raw = qcom_smgr_iio_read_raw,
	.write_raw = qcom_smgr_iio_write_raw,
	.read_avail = qcom_smgr_iio_read_avail,
};
EXPORT_SYMBOL_GPL(qcom_smgr_iio_info);

/* SMGR reports values for 3-axis sensors in north-east-down coordinates */
static const struct iio_mount_matrix qcom_smgr_iio_mount_matrix = {
	.rotation = {
		"0", "-1", "0",
		"-1", "0", "0",
		"0", "0", "1"
	}
};

static const struct iio_mount_matrix *
qcom_smgr_iio_get_mount_matrix(const struct iio_dev *iio_dev,
			       const struct iio_chan_spec *chan)
{
	return &qcom_smgr_iio_mount_matrix;
}

const struct iio_chan_spec_ext_info qcom_smgr_iio_ext_info[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_DIR, qcom_smgr_iio_get_mount_matrix),
	{}
};
EXPORT_SYMBOL_GPL(qcom_smgr_iio_ext_info);

static int qcom_smgr_probe(struct qrtr_device *qdev)
{
	struct qcom_smgr *smgr;
	int i, j;
	int ret;

	smgr = devm_kzalloc(&qdev->dev, sizeof(*smgr), GFP_KERNEL);
	if (!smgr)
		return -ENOMEM;

	smgr->dev = &qdev->dev;

	smgr->sns_smgr_info.sq_family = AF_QIPCRTR;
	smgr->sns_smgr_info.sq_node = qdev->node;
	smgr->sns_smgr_info.sq_port = qdev->port;

	dev_set_drvdata(&qdev->dev, smgr);

	ret = qmi_handle_init(&smgr->sns_smgr_hdl,
			      SNS_SMGR_SINGLE_SENSOR_INFO_RESP_MAX_LEN, NULL,
			      qcom_smgr_msg_handlers);
	if (ret < 0) {
		dev_err(smgr->dev,
			"Failed to initialize sensor manager handle: %d\n",
			ret);
		return ret;
	}

	ret = qcom_smgr_request_all_sensor_info(smgr, &smgr->sensors);
	if (ret < 0) {
		dev_err(smgr->dev, "Failed to get available sensors: %pe\n",
			ERR_PTR(ret));
		return ret;
	}
	smgr->sensor_count = ret;

	/* Get primary and secondary sensors from each sensor ID */
	for (i = 0; i < smgr->sensor_count; i++) {
		ret = qcom_smgr_request_single_sensor_info(smgr,
							   &smgr->sensors[i]);
		if (ret < 0) {
			dev_err(smgr->dev,
				"Failed to get sensors from ID 0x%02x: %pe\n",
				smgr->sensors[i].id, ERR_PTR(ret));
			return ret;
		}

		for (j = 0; j < smgr->sensors[i].data_type_count; j++) {
			/* Default to maximum sample rate */
			smgr->sensors[i].data_types->cur_sample_rate =
				smgr->sensors[i].data_types->max_sample_rate;

			dev_dbg(smgr->dev, "0x%02x,%d: %s %s\n",
				smgr->sensors[i].id, j,
				smgr->sensors[i].data_types[j].vendor,
				smgr->sensors[i].data_types[j].name);
		}

		qcom_smgr_register_sensor(smgr, &smgr->sensors[i]);
	}

	return 0;
}

static void qcom_smgr_remove(struct qrtr_device *qdev)
{
	struct qcom_smgr *smgr = dev_get_drvdata(&qdev->dev);

	qmi_handle_release(&smgr->sns_smgr_hdl);
}

static const struct qrtr_device_id qcom_smgr_qrtr_match[] = {
	{
		.service = SNS_SMGR_QMI_SVC_ID,
		.instance = QRTR_INSTANCE(SNS_SMGR_QMI_SVC_V1,
					  SNS_SMGR_QMI_INS_ID)
	},
	{},
};
MODULE_DEVICE_TABLE(qrtr, qcom_smgr_qrtr_match);

static struct qrtr_driver qcom_smgr_driver = {
	.probe = qcom_smgr_probe,
	.remove = qcom_smgr_remove,
	.id_table = qcom_smgr_qrtr_match,
	.driver	= {
		.name = "qcom_smgr",
	},
};
module_qrtr_driver(qcom_smgr_driver);

MODULE_AUTHOR("Yassine Oudjana <y.oudjana@protonmail.com>");
MODULE_DESCRIPTION("Qualcomm Sensor Manager driver");
MODULE_LICENSE("GPL");
