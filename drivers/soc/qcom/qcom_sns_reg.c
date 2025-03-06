// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm Sensor Registry service
 *
 * Based on sns-reg userspace daemon code by Yassine Oudjana, 2023
 * Copyright (c) 2025 Alexey Minnekhanov
 */

#include <linux/auxiliary_bus.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/refcount.h>
#include <linux/remoteproc/qcom_rproc.h>
#include <linux/soc/qcom/qmi.h>
#include <linux/workqueue.h>

#define SNS_REG_QMI_SVC_ID		0x010f /* 271 */
#define SNS_REG_QMI_SVC_V1		2
#define SNS_REG_QMI_INS_ID		0

#define SNS_REG_GROUP_MSG_ID		0x4
#define SNS_REG_GROUP_DATA_MAX_LEN	0x100

/* Maximum size of group response packet = size of all TLVs + size of data */
#define SNS_REG_GROUP_RESP_MAX_LEN	5 + 5 + 5 + SNS_REG_GROUP_DATA_MAX_LEN /* 271 */

struct sns_reg_group_req {
	unsigned short id;
};

struct sns_reg_group_resp {
	unsigned short result;
	unsigned short id;
	unsigned short data_len;
	unsigned char data[SNS_REG_GROUP_DATA_MAX_LEN];
};

struct qmi_elem_info sns_reg_group_req_ei[] = {
	{
		.data_type  = QMI_UNSIGNED_2_BYTE,
		.elem_len   = 1,
		.elem_size  = sizeof(unsigned short),
		.array_type = NO_ARRAY,
		.tlv_type   = 0x01,
		.offset     = offsetof(struct sns_reg_group_req, id),
	},
	{
		.data_type = QMI_EOTI,
	},
};

struct qmi_elem_info sns_reg_group_resp_ei[] = {
	{
		.data_type  = QMI_UNSIGNED_2_BYTE,
		.elem_len   = 1,
		.elem_size  = sizeof(unsigned short),
		.array_type = NO_ARRAY,
		.tlv_type   = 0x02,
		.offset     = offsetof(struct sns_reg_group_resp, result),
	},
	{
		.data_type  = QMI_UNSIGNED_2_BYTE,
		.elem_len   = 1,
		.elem_size  = sizeof(unsigned short),
		.array_type = NO_ARRAY,
		.tlv_type   = 0x03,
		.offset     = offsetof(struct sns_reg_group_resp, id),
	},
	{
		.data_type  = QMI_DATA_LEN,
		.elem_len   = 1,
		.elem_size  = sizeof(unsigned short),
		.array_type = NO_ARRAY,
		.tlv_type   = 0x04,
		.offset     = offsetof(struct sns_reg_group_resp, data_len),
	},
	{
		.data_type  = QMI_UNSIGNED_1_BYTE,
		.elem_len   = SNS_REG_GROUP_DATA_MAX_LEN,
		.elem_size  = sizeof(unsigned char),
		.array_type = VAR_LEN_ARRAY,
		.tlv_type   = 0x04,
		.offset     = offsetof(struct sns_reg_group_resp, data),
	},
	{
		.data_type = QMI_EOTI,
	},
};

/*
 * Mapping of groups to binary registry (sns.reg).
 * This data appears to be static.
 *
 * @group_id - group id
 * @offset - offset to group's first key in sns.reg file
 * @size - size of group keys combined, in bytes
 */
 struct group_map_entry {
	unsigned short group_id;
	unsigned short offset;
	size_t size;
};

 static const struct group_map_entry group_map[] = {
	{ 0, 0x0000, 0x018 },
	{ 10, 0x0800, 0x018 },
	{ 1000, 0x0a00, 0x003 },
	{ 1010, 0x0c00, 0x003 },
	{ 1020, 0x0d00, 0x003 },
	{ 1040, 0x0100, 0x080 },
	{ 2000, 0x0200, 0x010 },
	{ 2002, 0x0400, 0x018 },
	{ 2050, 0x1100, 0x00c },
	{ 2620, 0x0e00, 0x024 },
	{ 2630, 0x0f00, 0x018 },
	{ 2640, 0x1000, 0x00a },
	{ 2670, 0x1500, 0x010 },
	{ 2690, 0x1700, 0x100 },
	{ 2692, 0x1800, 0x100 },
	{ 2693, 0x1900, 0x100 },
	{ 2694, 0x1a00, 0x100 },
	{ 2695, 0x1b00, 0x100 },
	{ 2696, 0x1c00, 0x100 },
	{ 2698, 0x2700, 0x100 },
	{ 2699, 0x2d00, 0x100 },
	{ 2700, 0x1d00, 0x0e0 },
	{ 2800, 0x1f00, 0x022 },
	{ 2900, 0x2000, 0x004 },
	{ 2910, 0x2100, 0x004 },
	{ 2920, 0x2200, 0x004 },
	{ 2930, 0x2300, 0x004 },
	{ 2940, 0x2400, 0x024 },
	{ 2950, 0x2500, 0x008 },
	{ 2960, 0x2800, 0x004 },
	{ 3000, 0x2e00, 0x100 },
	{ 3010, 0x3100, 0x100 },
	{ 3020, 0x3500, 0x100 },
	{ 3040, 0x3a00, 0x014 },
	{ 3070, 0x3c00, 0x00c },
	{ 3080, 0x3f00, 0x05a },
	{ 3090, 0x6000, 0x014 },
	{ 3300, 0x4200, 0x00e },
	{ 3301, 0x4300, 0x00e },
	{ 3302, 0x4400, 0x00e },
	{ 3303, 0x4500, 0x00e },
	{ 3304, 0x4600, 0x00e },
	{ 3305, 0x4700, 0x00e },
	{ 3306, 0x4800, 0x00e },
	{ 3307, 0x4900, 0x00e },
	{ 3308, 0x4a00, 0x00e },
	{ 3309, 0x4b00, 0x00e },
	{ 3310, 0x4c00, 0x00e },
	{ 3311, 0x4d00, 0x00e },
	{ 3312, 0x4e00, 0x00e },
	{ 3313, 0x4f00, 0x00e },
	{ 3314, 0x5000, 0x00e },
	{ 3315, 0x5100, 0x00e },
	{ 3316, 0x5200, 0x00e },
	{ 3317, 0x5300, 0x00e },
	{ 3318, 0x5400, 0x00e },
	{ 3319, 0x5500, 0x00e },
	{ 3320, 0x5600, 0x00e },
	{ 3321, 0x5700, 0x00e },
	{ 3322, 0x5800, 0x00e },
	{ 3323, 0x5900, 0x00e },
	{ 3324, 0x5a00, 0x00e },
	{ 3325, 0x5b00, 0x00e },
	{ 3326, 0x5c00, 0x00e },
	{ 3327, 0x5d00, 0x00e },
	{ 3328, 0x5e00, 0x00e },
	{ 3329, 0x5f00, 0x00e },
	{ 3400, 0x6100, 0x01c },
	{ /* sentinel */}
};

static const struct group_map_entry *group_map_entry_by_id(short gid)
{
	const struct group_map_entry *entry;
	int index = 0;

	while ((entry = &group_map[index++])->size)
		if (entry->group_id == gid)
			return entry;
	return NULL;
}

enum sns_reg_target_rproc {
	NOT_SUPPORTED,
	RPROC_ADSP,
	RPROC_SLPI
};

struct sns_reg_of_match_data {
	enum sns_reg_target_rproc target_rproc;
};

static const struct sns_reg_of_match_data sns_reg_target_adsp = { .target_rproc = RPROC_ADSP };
static const struct sns_reg_of_match_data sns_reg_target_slpi = { .target_rproc = RPROC_SLPI };

static const struct of_device_id sns_reg_supported_socs[] __maybe_unused = {
	{ .compatible = "qcom,apq8096", .data = &sns_reg_target_slpi, },
	{ .compatible = "qcom,msm8226", .data = &sns_reg_target_adsp, }, /* untested */
	{ .compatible = "qcom,msm8917", .data = &sns_reg_target_adsp, },
	{ .compatible = "qcom,msm8920", .data = &sns_reg_target_adsp, },
	{ .compatible = "qcom,msm8937", .data = &sns_reg_target_adsp, },
	{ .compatible = "qcom,msm8940", .data = &sns_reg_target_adsp, },
	{ .compatible = "qcom,msm8953", .data = &sns_reg_target_adsp, },
	{ .compatible = "qcom,msm8974", .data = &sns_reg_target_adsp, }, /* untested */
	{ .compatible = "qcom,msm8996", .data = &sns_reg_target_slpi, },
	{ .compatible = "qcom,msm8998", .data = &sns_reg_target_slpi, }, /* untested */
	{ .compatible = "qcom,sda660", .data = &sns_reg_target_adsp, },
	{ .compatible = "qcom,sdm429", .data = &sns_reg_target_adsp, },
	{ .compatible = "qcom,sdm439", .data = &sns_reg_target_adsp, },
	{ .compatible = "qcom,sdm450", .data = &sns_reg_target_adsp, },
	{ .compatible = "qcom,sdm630", .data = &sns_reg_target_adsp, },
	{ .compatible = "qcom,sdm632", .data = &sns_reg_target_adsp, },
	{ .compatible = "qcom,sdm636", .data = &sns_reg_target_adsp, },
	{ .compatible = "qcom,sdm660", .data = &sns_reg_target_adsp, },
	{}
};

static enum sns_reg_target_rproc get_target_rproc(void)
{
	const struct of_device_id *match;
	const struct sns_reg_of_match_data *mdata;

	match = of_match_node(sns_reg_supported_socs, of_root);
	if (!match)
		return NOT_SUPPORTED;

	mdata = match->data;
	return mdata->target_rproc;
}

struct qcom_sns_reg_data {
	refcount_t refcnt;

	/*
	 * This driver is not bound to specific device; however for logging
	 * purposes we can use the first one that triggered our probe.
	 */
	struct auxiliary_device *first_auxdev;

	struct qmi_handle svc_handle;
	bool qmi_svc_started;

	const struct firmware *fw;

	enum sns_reg_target_rproc target_rproc;
	struct notifier_block ssr_nb;
	void *ssr_cookie;

	struct work_struct work;
};

static DEFINE_MUTEX(qcom_sns_reg_mutex); /* protects __qcom_sns_reg_data */
static struct qcom_sns_reg_data *__qcom_sns_reg_data;

/* little helpers to save from typing too long lines */
#define sns_reg_info(fmt, ...) \
	dev_info(&__qcom_sns_reg_data->first_auxdev->dev, fmt, ##__VA_ARGS__)
#define sns_reg_err(fmt, ...) \
	dev_err(&__qcom_sns_reg_data->first_auxdev->dev, fmt, ##__VA_ARGS__)
#define sns_reg_warn(fmt, ...) \
	dev_warn(&__qcom_sns_reg_data->first_auxdev->dev, fmt, ##__VA_ARGS__)
#define sns_reg_dbg(fmt, ...) \
	dev_dbg(&__qcom_sns_reg_data->first_auxdev->dev, fmt, ##__VA_ARGS__)

/*
 * SSC firmware will ask us for a group of configs keys stored
 * in sns.reg file in binary form. Each group consists of several
 * keys of various length, but they're stored sequentially and
 * we know starting offset and size of the group block from the
 * group_map struct data. This byte blob is what's being sent in
 * response in "data" field.
 * We're basically serving the partial contents of sns.reg file
 * in each request->response interaction.
 */
static void qcom_sns_reg_get_group_req_handler(
	struct qmi_handle *qmi,
	struct sockaddr_qrtr *sq,
	struct qmi_txn *txn,
	const void *decoded)
{
	struct sns_reg_group_resp *rsp;
	const struct sns_reg_group_req *req = decoded;
	const struct group_map_entry *group;
	int ret;

	group = group_map_entry_by_id(req->id);

	rsp = kzalloc(sizeof(*rsp), GFP_KERNEL);
	if (!rsp)
		return;

	mutex_lock(&qcom_sns_reg_mutex);

	/* assume by default everything goes wrong */
	rsp->result = QMI_RESULT_FAILURE_V01;
	rsp->data_len = 0;
	rsp->id = req->id; /* fill in group ID for both error and OK cases */

	if (!group) {
		/*
		 * This might be totally normal, depending on SoC.
		 * Not all SoCs have all the groups.
		 */
		sns_reg_warn("got request for unmapped group id=%u", req->id);
	} else {
		/* bounds check */
		if (!__qcom_sns_reg_data->fw
			|| group->offset >= __qcom_sns_reg_data->fw->size
			|| (group->offset + group->size) >= __qcom_sns_reg_data->fw->size)
		{
			sns_reg_err("no firmware or it is buggy!\n");
		} else {
			/* Success, fill in the response struct fields */
			rsp->result = QMI_RESULT_SUCCESS_V01;
			rsp->data_len = group->size;
			memcpy(&rsp->data, __qcom_sns_reg_data->fw->data + group->offset, group->size);
		}
	}

	ret = qmi_send_response(qmi, sq, txn, SNS_REG_GROUP_MSG_ID,
		SNS_REG_GROUP_RESP_MAX_LEN, sns_reg_group_resp_ei, rsp);
	if (ret)
		sns_reg_err("failed to send group response: %d\n", ret);

	mutex_unlock(&qcom_sns_reg_mutex);

	kfree(rsp);
}

static const struct qmi_msg_handler qcom_sns_reg_msg_handlers[] = {
	{
		.type = QMI_REQUEST,
		.msg_id = SNS_REG_GROUP_MSG_ID,
		.ei = sns_reg_group_req_ei,
		.decoded_size = sizeof(struct sns_reg_group_req),
		.fn = qcom_sns_reg_get_group_req_handler,
	},
	{ },
};

static int qcom_sns_reg_qmi_service_start(void)
{
	int ret = 0;

	if (__qcom_sns_reg_data->qmi_svc_started)
		return 0;

	ret = qmi_add_server(&__qcom_sns_reg_data->svc_handle,
		SNS_REG_QMI_SVC_ID, SNS_REG_QMI_SVC_V1, SNS_REG_QMI_INS_ID);
	if (ret) {
		sns_reg_err("error adding QMI server %d\n", ret);
	} else {
		__qcom_sns_reg_data->qmi_svc_started = true;
	}

	return ret;
}

static int qcom_sns_reg_load_firmware(struct auxiliary_device *auxdev)
{
	int ret;
	char *fwname[2];
	const char *top_compatible;
	int i;

	ret = of_property_read_string(of_root, "compatible", &top_compatible);
	if (ret == 0) {
		/* First try to load specific fw, then fallback one */
		fwname[0] = kasprintf(GFP_KERNEL, "qcom/sensors/sns.reg-%s", top_compatible);
		fwname[1] = kasprintf(GFP_KERNEL, "qcom/sensors/sns.reg");
	} else {
		fwname[0] = kasprintf(GFP_KERNEL, "qcom/sensors/sns.reg");
		fwname[1] = NULL;
	}

	if (!fwname[0] || !fwname[1])
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(fwname); i++) {
		if (!fwname[i])
			continue;

		ret = request_firmware(&__qcom_sns_reg_data->fw, fwname[i], &auxdev->dev);
		if (ret == 0) {
			if (i > 0)
				dev_info(&auxdev->dev, "firmware loaded, error above can be ignored.\n");
			goto fw_free_ret;
		}
	}

	dev_err(&auxdev->dev, "Failed to load fw from: qcom/sensors/sns.reg*\n");

fw_free_ret:
	kfree(fwname[1]);
	kfree(fwname[0]);
	return ret;
}

static void qcom_sns_reg_maybe_start(void)
{
	bool do_start = false;
	const bool adsp_started = qcom_ssr_last_status("lpass") == QCOM_SSR_AFTER_POWERUP;
	const bool slpi_started = qcom_ssr_last_status("dsps") == QCOM_SSR_AFTER_POWERUP;

	/* Start our service only if target rproc is up */
	switch(__qcom_sns_reg_data->target_rproc)
	{
	case RPROC_ADSP:
		sns_reg_dbg("maybe_start: waiting for: ADSP\n");
		if (adsp_started)
			do_start = true;
		break;
	case RPROC_SLPI:
		sns_reg_dbg("maybe_start: waiting for: SLPI\n");
		if (slpi_started)
			do_start = true;
		break;
	default:
		break;
	}

	if (do_start) {
		sns_reg_dbg("   ... and the wait is over\n");
		qcom_sns_reg_qmi_service_start();
	} else
		sns_reg_dbg("   ... not time to start yet\n");
}

static void qcom_sns_reg_ssr_notifier_work(struct work_struct *work)
{
	mutex_lock(&qcom_sns_reg_mutex);

	qcom_sns_reg_maybe_start();

	mutex_unlock(&qcom_sns_reg_mutex);
}

static int qcom_sns_reg_ssr_notify_handler(struct notifier_block *nb, unsigned long action, void *data)
{
	switch (action) {
	case QCOM_SSR_AFTER_POWERUP:
		schedule_work(&__qcom_sns_reg_data->work);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static int qcom_sns_reg_probe_once(struct auxiliary_device *auxdev)
{
	void *cookie;
	int ret;
	const char *target_ssr_name;

	__qcom_sns_reg_data->first_auxdev = auxdev;

	/*
	 * Determine if current SoC supports Sensor Manager (SMGR) at all.
	 * And if supports, on which remote processor it should be running.
	 */
	__qcom_sns_reg_data->target_rproc = get_target_rproc();
	switch (__qcom_sns_reg_data->target_rproc)
	{
	case RPROC_ADSP:
		target_ssr_name = "lpass";
		break;
	case RPROC_SLPI:
		target_ssr_name = "dsps";
		break;
	case NOT_SUPPORTED:
		dev_warn_once(&auxdev->dev, "Sensor Manager is not supported on current SoC.");
		return -ENODEV;
	}

	/*
	 * Load firmware (sensors registry config) first.
	 * Without firmware we cannot do anything useful.
	 */
	ret = qcom_sns_reg_load_firmware(auxdev);
	if (ret)
		return ret;

	__qcom_sns_reg_data->ssr_nb.notifier_call = qcom_sns_reg_ssr_notify_handler;
	cookie = qcom_register_ssr_notifier(target_ssr_name, &__qcom_sns_reg_data->ssr_nb);
	if (IS_ERR(cookie)) {
		ret = PTR_ERR(cookie);
		dev_err(&auxdev->dev, "failed to register remoteproc status notifier: %d\n", ret);
		goto err_cleanup;
	}
	__qcom_sns_reg_data->ssr_cookie = cookie;

	ret = qmi_handle_init(&__qcom_sns_reg_data->svc_handle,
		SNS_REG_GROUP_RESP_MAX_LEN, NULL, qcom_sns_reg_msg_handlers);
	if (ret) {
		dev_err(&auxdev->dev, "failed to init QMI handle %d\n", ret);
		goto err_cleanup;
	}

	INIT_WORK(&__qcom_sns_reg_data->work, qcom_sns_reg_ssr_notifier_work);
	return 0;

err_cleanup:
	if (__qcom_sns_reg_data->fw) {
		release_firmware(__qcom_sns_reg_data->fw);
		__qcom_sns_reg_data->fw = NULL;
	}
	if (__qcom_sns_reg_data->ssr_cookie) {
		qcom_unregister_ssr_notifier(__qcom_sns_reg_data->ssr_cookie, &__qcom_sns_reg_data->ssr_nb);
		__qcom_sns_reg_data->ssr_cookie = NULL;
	}
	return ret;
}

static int qcom_sns_reg_probe(struct auxiliary_device *auxdev,
			      const struct auxiliary_device_id *id)
{
	struct qcom_sns_reg_data *data;
	int ret = 0;

	/*
	 * This probe() function might be called from multiple remoteproc
	 * startup procedures, but we need to initialize only once
	 */

	mutex_lock(&qcom_sns_reg_mutex);

	if (!__qcom_sns_reg_data) {
		data = kzalloc(sizeof(*data), GFP_KERNEL);
		if (!data) {
			ret = -ENOMEM;
		} else {
			__qcom_sns_reg_data = data;
			refcount_set(&__qcom_sns_reg_data->refcnt, 1);

			ret = qcom_sns_reg_probe_once(auxdev);
		}
	} else {
		/* For 2nd, 3rd,.. init just increase reference count */
		refcount_inc(&__qcom_sns_reg_data->refcnt);
	}

	if (__qcom_sns_reg_data) {
		auxiliary_set_drvdata(auxdev, __qcom_sns_reg_data);

		/*
		 * Maybe we can already start our QMI service? For example,
		 * we were started by ADSP startup and our target rproc is ADSP.
		 * There might be different situation: we were started by ADSP,
		 * but need to wait for SLPI.
		 */
		qcom_sns_reg_maybe_start();
	}

	mutex_unlock(&qcom_sns_reg_mutex);
	return ret;
}

static void qcom_sns_reg_remove(struct auxiliary_device *auxdev)
{
	struct qcom_sns_reg_data *data;

	data = auxiliary_get_drvdata(auxdev);
	if (!data)
		return;

	if (refcount_dec_and_mutex_lock(&data->refcnt, &qcom_sns_reg_mutex)) {
		__qcom_sns_reg_data = NULL;

		qcom_unregister_ssr_notifier(data->ssr_cookie, &data->ssr_nb);

		/* The server is removed automatically */
		qmi_handle_release(&data->svc_handle);

		release_firmware(data->fw);

		kfree(data);

		mutex_unlock(&qcom_sns_reg_mutex);
	}
}

static const struct auxiliary_device_id qcom_sns_reg_table[] = {
	{ .name = "qcom_common.sns-reg" },
	{},
};

MODULE_DEVICE_TABLE(auxiliary, qcom_sns_reg_table);
 
static struct auxiliary_driver qcom_sns_reg_drv = {
	.name = "qcom-sns-reg",
	.id_table = qcom_sns_reg_table,
	.probe = qcom_sns_reg_probe,
	.remove = qcom_sns_reg_remove,
};

module_auxiliary_driver(qcom_sns_reg_drv);

MODULE_DESCRIPTION("Qualcomm Sensors Registry");
MODULE_LICENSE("GPL");
