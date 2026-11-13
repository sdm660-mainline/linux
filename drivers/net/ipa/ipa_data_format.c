// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2024-2025 Linaro Ltd. */

/**
 * DOC: Modem data-port mapping
 *
 * On IPA v2.x platforms the modem must be told which IPA endpoints carry
 * its data port.  The modem's Data Port Mapper (DPM) service stores that
 * mapping via OPEN_PORT.  Vendor Android userspace (netmgrd) sends this
 * request; here the AP's IPA driver does it because it is the natural
 * owner of the IPA endpoint IDs.
 *
 * The data *format* (WDA SET_DATA_FORMAT) is deliberately left to
 * userspace (ModemManager / netmgrd): it is userspace that knows the
 * bearer setup and the rmnet capabilities.  This file only configures
 * the data-port mapping, the one piece that is tied to the driver's
 * endpoint IDs.
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/soc/qcom/qmi.h>
#include <linux/workqueue.h>

#include "ipa.h"
#include "ipa_data_format.h"
#include "ipa_endpoint.h"

/* QMI service ID for the modem's Data Port Mapper */
#define IPA_DPM_SERVICE_ID	0x2f

/* DPM OPEN_PORT message ID */
#define IPA_DPM_OPEN_PORT	0x20

/* Values from the QMI DPM specification / vendor netmgrd */
#define IPA_DPM_EP_TYPE_HSUSB	4	/* HSUSB / IPA */
#define IPA_DPM_IFACE_ID	1

#define IPA_DPM_TIMEOUT_MS	5000
#define IPA_DPM_RETRY_MS	1000
#define IPA_DPM_MAX_RETRIES	20

/* Number of bytes needed to decode a DPM response */
#define IPA_DPM_MAX_RSP_LEN	512

/* Encoded size of the request; the QMI encoder needs the maximum size
 * of the message it will produce, not the size of the C structure.
 */
#define IPA_DPM_OPEN_PORT_REQ_SZ	(7 + 3 + 17)

/* Map the modem's data port to the AP's IPA endpoints. */
static bool open_port = true;
module_param(open_port, bool, 0644);
MODULE_PARM_DESC(open_port,
		 "Map the modem's data port via DPM OPEN_PORT");

/**
 * struct ipa_dpm_port_info - One entry in a DPM OPEN_PORT request
 * @count:		Number of entries that follow
 * @ep_type:		Endpoint type (4 is HSUSB/IPA)
 * @iface_id:		Interface identifier
 * @tx_endpoint:	IPA endpoint the modem transmits on (the AP's RX endpoint)
 * @rx_endpoint:	IPA endpoint the modem receives on (the AP's TX endpoint)
 */
struct ipa_dpm_port_info {
	u8 count;
	u32 ep_type;
	u32 iface_id;
	u32 tx_endpoint;
	u32 rx_endpoint;
};

struct ipa_dpm_open_port_req {
	struct ipa_dpm_port_info info;
};

struct ipa_dpm_open_port_rsp {
	struct qmi_response_type_v01 rsp;
};

static const struct qmi_elem_info ipa_dpm_port_info_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_dpm_port_info,
					       count),
		.offset		= offsetof(struct ipa_dpm_port_info, count),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_dpm_port_info,
					       ep_type),
		.offset		= offsetof(struct ipa_dpm_port_info, ep_type),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_dpm_port_info,
					       iface_id),
		.offset		= offsetof(struct ipa_dpm_port_info, iface_id),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_dpm_port_info,
					       tx_endpoint),
		.offset		= offsetof(struct ipa_dpm_port_info,
					   tx_endpoint),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_dpm_port_info,
					       rx_endpoint),
		.offset		= offsetof(struct ipa_dpm_port_info,
					   rx_endpoint),
	},
	{ .data_type = QMI_EOTI },
};

static const struct qmi_elem_info ipa_dpm_open_port_req_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct ipa_dpm_port_info),
		.tlv_type	= 0x11,
		.offset		= offsetof(struct ipa_dpm_open_port_req,
					   info),
		.ei_array	= ipa_dpm_port_info_ei,
	},
	{ .data_type = QMI_EOTI },
};

static const struct qmi_elem_info ipa_dpm_open_port_rsp_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_response_type_v01),
		.tlv_type	= 0x02,
		.offset		= offsetof(struct ipa_dpm_open_port_rsp, rsp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{ .data_type = QMI_EOTI },
};

/**
 * struct ipa_data_format - Data-port mapping state
 * @ipa:		IPA pointer
 * @qmi:		QMI handle used for DPM requests
 * @work:		Work used to retry the request
 * @dpm_node:		QRTR node of the modem's DPM service
 * @dpm_port:		QRTR port of the modem's DPM service
 * @modem_ready:	Whether the modem's IPA QMI handshake completed
 * @dpm_done:		Whether the DPM request was sent for this modem boot
 * @stopping:		Whether the state is being torn down
 * @retries:		Number of attempts made so far
 */
struct ipa_data_format {
	struct ipa *ipa;
	struct qmi_handle qmi;
	struct delayed_work work;

	u32 dpm_node;
	u32 dpm_port;

	bool modem_ready;
	bool dpm_done;
	bool stopping;
	unsigned int retries;
};

static int ipa_data_format_send(struct ipa_data_format *df,
				u32 node, u32 port, u16 msg_id,
				const struct qmi_elem_info *req_ei,
				const void *req, size_t req_len,
				const struct qmi_elem_info *rsp_ei,
				void *rsp, unsigned int timeout_ms)
{
	struct sockaddr_qrtr sq = {
		.sq_family	= AF_QIPCRTR,
		.sq_node	= node,
		.sq_port	= port,
	};
	struct qmi_txn txn;
	int ret;

	ret = qmi_txn_init(&df->qmi, &txn, rsp_ei, rsp);
	if (ret < 0)
		return ret;

	ret = qmi_send_request(&df->qmi, &sq, &txn, msg_id,
			       req_len, req_ei, req);
	if (ret < 0) {
		qmi_txn_cancel(&txn);
		return ret;
	}

	ret = qmi_txn_wait(&txn, msecs_to_jiffies(timeout_ms));
	if (ret < 0)
		return ret;

	return 0;
}

/* Tell the modem which IPA endpoints carry its data port */
static int ipa_data_format_dpm(struct ipa_data_format *df)
{
	struct ipa *ipa = df->ipa;
	struct ipa_dpm_open_port_req req = { };
	struct ipa_dpm_open_port_rsp rsp = { };
	int ret;

	req.info.count = 1;
	req.info.ep_type = IPA_DPM_EP_TYPE_HSUSB;
	req.info.iface_id = IPA_DPM_IFACE_ID;
	/* DPM names the endpoints from the modem's point of view: the modem
	 * transmits on the AP's RX endpoint and receives on the AP's TX
	 * endpoint.  ModemManager's dpm_open_port() swaps tx/rx the same way
	 * ("The modem TX endpoint connects with the IPA's RX port and the
	 * modem RX endpoint connects with the IPA's TX port").
	 */
	req.info.tx_endpoint =
		ipa->name_map[IPA_ENDPOINT_AP_MODEM_RX]->endpoint_id;
	req.info.rx_endpoint =
		ipa->name_map[IPA_ENDPOINT_AP_MODEM_TX]->endpoint_id;

	ret = ipa_data_format_send(df, df->dpm_node, df->dpm_port,
				   IPA_DPM_OPEN_PORT,
				   ipa_dpm_open_port_req_ei, &req,
				   IPA_DPM_OPEN_PORT_REQ_SZ,
				   ipa_dpm_open_port_rsp_ei, &rsp,
				   IPA_DPM_TIMEOUT_MS);
	if (ret)
		return ret;

	if (rsp.rsp.result != QMI_RESULT_SUCCESS_V01) {
		dev_warn(ipa->dev,
			 "DPM OPEN_PORT failed: result %u error %u\n",
			 rsp.rsp.result, rsp.rsp.error);
		/* The mapping may already exist (e.g. ModemManager set it) */
		return 0;
	}

	dev_dbg(ipa->dev, "DPM port mapping installed (tx %u rx %u)\n",
		req.info.tx_endpoint, req.info.rx_endpoint);

	return 0;
}

static void ipa_data_format_work(struct work_struct *work)
{
	struct ipa_data_format *df;
	struct ipa *ipa;
	int ret;

	df = container_of(work, struct ipa_data_format, work.work);
	ipa = df->ipa;

	if (!READ_ONCE(df->modem_ready))
		return;

	/* Wait for the modem's DPM service to announce itself */
	if (!df->dpm_port)
		goto retry;

	if (!df->dpm_done) {
		ret = ipa_data_format_dpm(df);
		if (ret) {
			dev_dbg(ipa->dev, "DPM request failed: %d\n", ret);
			goto retry;
		}
		df->dpm_done = true;
	}

	return;

retry:
	if (!READ_ONCE(df->modem_ready))
		return;

	if (df->retries++ >= IPA_DPM_MAX_RETRIES) {
		dev_warn(ipa->dev,
			 "giving up on modem data-port mapping after %u attempts\n",
			 df->retries);
		return;
	}

	schedule_delayed_work(&df->work, msecs_to_jiffies(IPA_DPM_RETRY_MS));
}

static int ipa_data_format_new_server(struct qmi_handle *qmi,
				      struct qmi_service *svc)
{
	struct ipa_data_format *df;

	df = container_of(qmi, struct ipa_data_format, qmi);

	if (READ_ONCE(df->stopping))
		return 0;

	if (svc->service == IPA_DPM_SERVICE_ID && !df->dpm_port) {
		df->dpm_node = svc->node;
		df->dpm_port = svc->port;
	}

	schedule_delayed_work(&df->work, 0);

	return 0;
}

static void ipa_data_format_del_server(struct qmi_handle *qmi,
				       struct qmi_service *svc)
{
	struct ipa_data_format *df;

	df = container_of(qmi, struct ipa_data_format, qmi);

	if (READ_ONCE(df->stopping))
		return;

	if (svc->service == IPA_DPM_SERVICE_ID &&
	    svc->node == df->dpm_node && svc->port == df->dpm_port) {
		df->dpm_node = 0;
		df->dpm_port = 0;
	}
}

static void ipa_data_format_net_reset(struct qmi_handle *qmi)
{
	struct ipa_data_format *df;

	df = container_of(qmi, struct ipa_data_format, qmi);

	if (READ_ONCE(df->stopping))
		return;

	df->dpm_node = 0;
	df->dpm_port = 0;
}

static const struct qmi_ops ipa_data_format_ops = {
	.new_server	= ipa_data_format_new_server,
	.del_server	= ipa_data_format_del_server,
	.net_reset	= ipa_data_format_net_reset,
};

int ipa_data_format_setup(struct ipa *ipa)
{
	struct ipa_data_format *df;
	int ret;

	/* Only IPA v2.x uses this handshake, and only when enabled */
	if (ipa->version > IPA_VERSION_2_6L || !open_port)
		return 0;

	df = kzalloc_obj(*df);
	if (!df)
		return -ENOMEM;

	df->ipa = ipa;
	INIT_DELAYED_WORK(&df->work, ipa_data_format_work);

	ret = qmi_handle_init(&df->qmi, IPA_DPM_MAX_RSP_LEN,
			      &ipa_data_format_ops, NULL);
	if (ret)
		goto err_free;

	/* Look up the modem's DPM service; version and instance 0 match any */
	ret = qmi_add_lookup(&df->qmi, IPA_DPM_SERVICE_ID, 0, 0);
	if (ret)
		goto err_release;

	ipa->data_format = df;

	return 0;

err_release:
	qmi_handle_release(&df->qmi);
err_free:
	kfree(df);

	return ret;
}

void ipa_data_format_teardown(struct ipa *ipa)
{
	struct ipa_data_format *df = ipa->data_format;

	if (!df)
		return;

	ipa->data_format = NULL;
	WRITE_ONCE(df->stopping, true);

	cancel_delayed_work_sync(&df->work);
	qmi_handle_release(&df->qmi);
	kfree(df);
}

void ipa_data_format_start(struct ipa *ipa)
{
	struct ipa_data_format *df = ipa->data_format;

	if (!df)
		return;

	/* ipa_qmi_ready() can run more than once per modem boot */
	if (READ_ONCE(df->modem_ready))
		return;

	df->dpm_done = false;
	df->retries = 0;
	WRITE_ONCE(df->modem_ready, true);

	schedule_delayed_work(&df->work, 0);
}

void ipa_data_format_stop(struct ipa *ipa)
{
	struct ipa_data_format *df = ipa->data_format;

	if (!df)
		return;

	/*
	 * This can be called from the QMI workqueue (via the modem "bye"
	 * callback), so don't wait for the work here: it may be blocked
	 * waiting for a response that this very workqueue delivers.
	 */
	WRITE_ONCE(df->modem_ready, false);
	cancel_delayed_work(&df->work);

	df->retries = 0;
	df->dpm_done = false;
	df->dpm_node = 0;
	df->dpm_port = 0;
}
