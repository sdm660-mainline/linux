// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm Sensor Manager driver
 *
 * Copyright (c) 2021, Yassine Oudjana <y.oudjana@protonmail.com>
 */

/* TODO: Figure out sampling and report rate units and remove this everywhere */
#ifdef SMGR_PROFILE_SAMPLE_RATE
#include <linux/debugfs.h>
#include <linux/hrtimer.h>
#endif /* SMGR_PROFILE_SAMPLE_RATE */

#include <linux/iio/buffer.h>
#include <linux/iio/common/qcom_smgr.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/remoteproc/qcom_rproc.h>
#include <linux/soc/qcom/qmi.h>
#include <linux/soc/qcom/qrtr.h>
#include <linux/types.h>
#include <net/sock.h>

#include "qmi/sns_smgr.h"

#define SMGR_REPORT_RATE_IN_HZ		0xf000

struct smgr {
	struct device *dev;

	struct qmi_handle sns_smgr_hdl;
	struct sockaddr_qrtr sns_smgr_info;
	struct work_struct sns_smgr_work;

	u8 sensor_count;
	struct smgr_sensor *sensors;

#ifdef SMGR_PROFILE_SAMPLE_RATE
	struct dentry *dir;

	struct hrtimer timer;
	ktime_t time_last;
	u32 report_rate;
	u16 sampling_rate;
#endif /* SMGR_PROFILE_SAMPLE_RATE */
};

static const char *smgr_sensor_type_platform_names[] = {
	[SNS_SMGR_SENSOR_TYPE_ACCEL] = "qcom-smgr-accel",
	[SNS_SMGR_SENSOR_TYPE_GYRO] = "qcom-smgr-gyro",
	[SNS_SMGR_SENSOR_TYPE_MAG] = "qcom-smgr-mag",
	[SNS_SMGR_SENSOR_TYPE_PROX_LIGHT] = "qcom-smgr-prox-light",
	[SNS_SMGR_SENSOR_TYPE_PRESSURE] = "qcom-smgr-pressure",
	[SNS_SMGR_SENSOR_TYPE_HALL_EFFECT] = "qcom-smgr-hall-effect"
};

static void smgr_unregister_sensor(void *data)
{
	struct platform_device *pdev = data;

	platform_device_unregister(pdev);
}

static int smgr_register_sensor(struct smgr *smgr, struct smgr_sensor *sensor)
{
	struct platform_device *pdev;
	const char *name = smgr_sensor_type_platform_names[sensor->type];

	pdev = platform_device_register_data(smgr->dev, name, sensor->id,
					     &sensor, sizeof(sensor));
	if (IS_ERR(pdev)) {
		dev_err(smgr->dev, "Failed to register %s: %pe\n", name, pdev);
		return PTR_ERR(pdev);
	}

	return devm_add_action_or_reset(smgr->dev, smgr_unregister_sensor,
					pdev);
}

static int smgr_request_all_sensor_info(struct smgr *smgr,
					struct smgr_sensor **sensors)
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
				sizeof(struct smgr_sensor) * resp.item_len,
				GFP_KERNEL);

	for (i = 0; i < resp.item_len; ++i) {
		(*sensors)[i].id = resp.items[i].id;
		(*sensors)[i].type =
			sns_smgr_sensor_type_from_str(resp.items[i].type);
	}

	return resp.item_len;
}

static int smgr_request_single_sensor_info(struct smgr *smgr,
					   struct smgr_sensor *sensor)
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
	sensor->data_types = devm_kzalloc(smgr->dev,
					  sizeof(struct smgr_data_type_item) *
						  sensor->data_type_count,
					  GFP_KERNEL);
	if (!sensor->data_types)
		return -ENOMEM;

	for (i = 0; i < sensor->data_type_count; ++i) {
		sensor->data_types[i].name = devm_kstrdup_const(
			smgr->dev, resp.data_types[i].name, GFP_KERNEL);
		sensor->data_types[i].vendor = devm_kstrdup_const(
			smgr->dev, resp.data_types[i].vendor, GFP_KERNEL);

		sensor->data_types[i].max_sample_rate =
			resp.data_types[i].max_sample_rate;
	}

	return 0;
}

static int smgr_request_buffering(struct smgr *smgr, struct smgr_sensor *sensor,
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
	int ret;

	if (enable) {
		req.action = SNS_SMGR_BUFFERING_ACTION_ADD;
		/* TODO: Replace hardcoded values */
		req.item_len = 1;
		req.items[0].sensor_id = sensor->id;
		req.items[0].data_type = SNS_SMGR_DATA_TYPE_PRIMARY;
		req.items[0].decimation = 0x3;
		req.items[0].calibration = 0xf;
#ifdef SMGR_PROFILE_SAMPLE_RATE
		req.report_rate = smgr->report_rate;
		req.items[0].sampling_rate = smgr->sampling_rate;
#else
		req.report_rate = sensor->data_types[0].cur_sample_rate *
				  SMGR_REPORT_RATE_IN_HZ;
		req.items[0].sampling_rate =
			sensor->data_types[0].cur_sample_rate;
#endif /* SMGR_PROFILE_SAMPLE_RATE */

		dev_dbg(smgr->dev,
			"Requesting buffering for sensor 0x%02x, report rate: %d, sample_rate: %d",
			req.items[0].sensor_id, req.report_rate,
			req.items[0].sampling_rate);
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

#ifdef SMGR_PROFILE_SAMPLE_RATE
	if (enable)
		hrtimer_start(&smgr->timer, ULONG_MAX, HRTIMER_MODE_REL);
#endif /* SMGR_PROFILE_SAMPLE_RATE */

	dev_dbg(smgr->dev, "Buffering response ack_nak %d\n", resp.ack_nak);

	return 0;
}

static void smgr_buffering_report_handler(struct qmi_handle *hdl,
					  struct sockaddr_qrtr *sq,
					  struct qmi_txn *txn, const void *data)
{
	struct smgr *smgr = container_of(hdl, struct smgr, sns_smgr_hdl);
	struct sns_smgr_buffering_report_ind *ind =
		(struct sns_smgr_buffering_report_ind *)data;
	struct smgr_sensor *sensor;
	u8 i;
#ifdef SMGR_PROFILE_SAMPLE_RATE
	ktime_t time;

	time = hrtimer_cb_get_time(&smgr->timer);
	hrtimer_start(&smgr->timer, ULONG_MAX, HRTIMER_MODE_REL);

	dev_info(smgr->dev, "time: %llu, samples: %d\n", time - smgr->time_last,
		 ind->samples_len);
	smgr->time_last = time;
#endif /* SMGR_PROFILE_SAMPLE_RATE */

	for (i = 0; i < smgr->sensor_count; ++i) {
		sensor = &smgr->sensors[i];

		if (sensor->id != ind->report_id)
			continue;

		// TODO: handle multiple samples
		iio_push_to_buffers_with_timestamp(sensor->iio_dev,
						   ind->samples[0].values,
						   ind->metadata.timestamp);

		break;
	}
}

static const struct qmi_msg_handler smgr_msg_handlers[] = {
	{
		.type = QMI_INDICATION,
		.msg_id = SNS_SMGR_BUFFERING_REPORT_MSG_ID,
		.ei = sns_smgr_buffering_report_ind_ei,
		.decoded_size = sizeof(struct sns_smgr_buffering_report_ind),
		.fn = smgr_buffering_report_handler,
	},
	{}
};

static int smgr_sensor_postenable(struct iio_dev *iio_dev)
{
	struct smgr *smgr = dev_get_drvdata(iio_dev->dev.parent->parent);
	struct smgr_iio_priv *priv = iio_priv(iio_dev);
	struct smgr_sensor *sensor = priv->sensor;

	return smgr_request_buffering(smgr, sensor, true);
}

static int smgr_sensor_postdisable(struct iio_dev *iio_dev)
{
	struct smgr *smgr = dev_get_drvdata(iio_dev->dev.parent->parent);
	struct smgr_iio_priv *priv = iio_priv(iio_dev);
	struct smgr_sensor *sensor = priv->sensor;

	return smgr_request_buffering(smgr, sensor, false);
}

struct iio_buffer_setup_ops smgr_buffer_ops = {
	.postenable = &smgr_sensor_postenable,
	.postdisable = &smgr_sensor_postdisable
};
EXPORT_SYMBOL_GPL(smgr_buffer_ops);

static int smgr_probe(struct qrtr_device *qdev)
{
	struct smgr *smgr;
	int i, j;
	int ret;

	smgr = devm_kzalloc(&qdev->dev, sizeof(*smgr), GFP_KERNEL);
	if (!smgr)
		return -ENOMEM;

	smgr->dev = &qdev->dev;
#ifdef SMGR_PROFILE_SAMPLE_RATE
	hrtimer_init(&smgr->timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	smgr->report_rate = 0x0a0000;
	smgr->sampling_rate = 0x1;

	smgr->dir = debugfs_create_dir("smgr", NULL);
	if (IS_ERR(smgr->dir)) {
		dev_err(smgr->dev, "Failed to create debugfs directory %pe\n",
			smgr->dir);
		return PTR_ERR(smgr->dir);
	}

	debugfs_create_u32("report_rate", 0666, smgr->dir, &smgr->report_rate);
	debugfs_create_u16("sampling_rate", 0666, smgr->dir,
			   &smgr->sampling_rate);
#endif /* SMGR_PROFILE_SAMPLE_RATE */

	smgr->sns_smgr_info.sq_family = AF_QIPCRTR;
	smgr->sns_smgr_info.sq_node = qdev->node;
	smgr->sns_smgr_info.sq_port = qdev->port;

	dev_set_drvdata(&qdev->dev, smgr);

	ret = qmi_handle_init(&smgr->sns_smgr_hdl,
			      SNS_SMGR_SINGLE_SENSOR_INFO_RESP_MAX_LEN, NULL,
			      smgr_msg_handlers);
	if (ret < 0) {
		dev_err(smgr->dev,
			"Failed to initialize sensor manager handle: %d\n",
			ret);
		return ret;
	}

	ret = smgr_request_all_sensor_info(smgr, &smgr->sensors);
	if (ret < 0) {
		dev_err(smgr->dev, "Failed to get available sensors: %pe\n",
			ERR_PTR(ret));
		return ret;
	}
	smgr->sensor_count = ret;

	/* Get primary and secondary sensors from each sensor ID */
	for (i = 0; i < smgr->sensor_count; i++) {
		ret = smgr_request_single_sensor_info(smgr, &smgr->sensors[i]);
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

		smgr_register_sensor(smgr, &smgr->sensors[i]);
	}

	return 0;
}

static void smgr_remove(struct qrtr_device *qdev)
{
	struct smgr *smgr = dev_get_drvdata(&qdev->dev);

#ifdef SMGR_PROFILE_SAMPLE_RATE
	debugfs_remove_recursive(smgr->dir);
#endif /* SMGR_PROFILE_SAMPLE_RATE */

	qmi_handle_release(&smgr->sns_smgr_hdl);
}

static const struct qrtr_device_id smgr_qrtr_match[] = {
	{
		.service = SNS_SMGR_QMI_SVC_ID,
		.instance = QRTR_INSTANCE(SNS_SMGR_QMI_SVC_V1,
					  SNS_SMGR_QMI_INS_ID)
	},
	{},
};
MODULE_DEVICE_TABLE(qrtr, smgr_qrtr_match);

static struct qrtr_driver smgr_driver = {
	.probe = smgr_probe,
	.remove = smgr_remove,
	.id_table = smgr_qrtr_match,
	.driver	= {
		.name = "smgr",
	},
};
module_qrtr_driver(smgr_driver);

MODULE_AUTHOR("Yassine Oudjana <y.oudjana@protonmail.com>");
MODULE_DESCRIPTION("Qualcomm Sensor Manager driver");
MODULE_LICENSE("GPL");
