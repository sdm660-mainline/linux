// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimal Dynamic Heap Memory Share service for the OPPO R11T modem.
 */

#include <linux/bitops.h>
#include <linux/dma-mapping.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/soc/qcom/pdr.h>
#include <linux/soc/qcom/qmi.h>

#include <dt-bindings/firmware/qcom,scm.h>

#include "qcom_r11t_memshare_qmi.h"

#define R11T_MEMSHARE_SERVICE_ID	0x34
#define R11T_MEMSHARE_VERSION		1
#define R11T_MEMSHARE_INSTANCE		1
#define R11T_MEMSHARE_MAX_MSG_LEN	1024
#define R11T_MEMSHARE_GUARD_SIZE	SZ_4K

#define R11T_RFSA_SERVICE_ID		0x1c
#define R11T_RFSA_VERSION		1
#define R11T_RFSA_INSTANCE		1
#define R11T_RFSA_GET_BUFF_ADDR		0x23
#define R11T_RFSA_RESP_MAX_LEN		18
#define R11T_RFSA_CLIENT_RMTFS		1
#define R11T_RFSA_CLIENT_OEMBACK	4
#define R11T_RFSA_RMTFS_ADDR		0x85e00000ULL
#define R11T_RFSA_RMTFS_SIZE		SZ_2M
#define R11T_RFSA_OEMBACK_ADDR		0xf6b00000ULL
#define R11T_RFSA_OEMBACK_SIZE		SZ_1M

struct r11t_rfsa_get_buffer_req {
	u32 client_id;
	u32 size;
};

struct r11t_rfsa_get_buffer_resp {
	struct qmi_response_type_v01 resp;
	u8 address_valid;
	u64 address;
};

static const struct qmi_elem_info r11t_rfsa_get_buffer_req_ei[] = {
	{
		.data_type = QMI_UNSIGNED_4_BYTE,
		.elem_len = 1,
		.elem_size = sizeof(u32),
		.array_type = NO_ARRAY,
		.tlv_type = 0x01,
		.offset = offsetof(struct r11t_rfsa_get_buffer_req, client_id),
	}, {
		.data_type = QMI_UNSIGNED_4_BYTE,
		.elem_len = 1,
		.elem_size = sizeof(u32),
		.array_type = NO_ARRAY,
		.tlv_type = 0x02,
		.offset = offsetof(struct r11t_rfsa_get_buffer_req, size),
	}, {
		.data_type = QMI_EOTI,
		.array_type = NO_ARRAY,
		.tlv_type = QMI_COMMON_TLV_TYPE,
	},
};

static const struct qmi_elem_info r11t_rfsa_get_buffer_resp_ei[] = {
	{
		.data_type = QMI_STRUCT,
		.elem_len = 1,
		.elem_size = sizeof(struct qmi_response_type_v01),
		.array_type = NO_ARRAY,
		.tlv_type = 0x02,
		.offset = offsetof(struct r11t_rfsa_get_buffer_resp, resp),
		.ei_array = qmi_response_type_v01_ei,
	}, {
		.data_type = QMI_OPT_FLAG,
		.elem_len = 1,
		.elem_size = sizeof(u8),
		.array_type = NO_ARRAY,
		.tlv_type = 0x10,
		.offset = offsetof(struct r11t_rfsa_get_buffer_resp,
				  address_valid),
	}, {
		.data_type = QMI_UNSIGNED_8_BYTE,
		.elem_len = 1,
		.elem_size = sizeof(u64),
		.array_type = NO_ARRAY,
		.tlv_type = 0x10,
		.offset = offsetof(struct r11t_rfsa_get_buffer_resp, address),
	}, {
		.data_type = QMI_EOTI,
		.array_type = NO_ARRAY,
		.tlv_type = QMI_COMMON_TLV_TYPE,
	},
};

struct r11t_memshare_client {
	u32 id;
	u32 size;
	u32 alloc_size;
	void *vaddr;
	dma_addr_t dma_addr;
	u64 perms;
};

struct r11t_memshare {
	struct device *dev;
	struct qmi_handle qmi;
	struct qmi_handle rfsa_qmi;
	struct pdr_handle *pdr;
	struct pdr_service *wlan_pd;
	struct mutex lock;
	struct r11t_memshare_client clients[3];
};

static void r11t_rfsa_get_buffer_req(struct qmi_handle *qmi,
				     struct sockaddr_qrtr *sq,
				     struct qmi_txn *txn,
				     const void *decoded)
{
	const struct r11t_rfsa_get_buffer_req *req = decoded;
	struct r11t_memshare *memshare = container_of(qmi, struct r11t_memshare,
						     rfsa_qmi);
	struct r11t_rfsa_get_buffer_resp resp = {};
	u64 address = 0;
	u32 capacity = 0;
	int ret;

	switch (req->client_id) {
	case R11T_RFSA_CLIENT_RMTFS:
		address = R11T_RFSA_RMTFS_ADDR;
		capacity = R11T_RFSA_RMTFS_SIZE;
		break;
	case R11T_RFSA_CLIENT_OEMBACK:
		address = R11T_RFSA_OEMBACK_ADDR;
		capacity = R11T_RFSA_OEMBACK_SIZE;
		break;
	}

	if (!address || !req->size || req->size > capacity) {
		resp.resp.result = QMI_RESULT_FAILURE_V01;
		resp.resp.error = QMI_ERR_NO_MEMORY_V01;
	} else {
		resp.resp.result = QMI_RESULT_SUCCESS_V01;
		resp.resp.error = QMI_ERR_NONE_V01;
		resp.address_valid = 1;
		resp.address = address;
		dev_info(memshare->dev,
			 "RFSA request: client=%u size=%u address=0x%llx\n",
			 req->client_id, req->size, address);
	}

	ret = qmi_send_response(qmi, sq, txn, R11T_RFSA_GET_BUFF_ADDR,
				R11T_RFSA_RESP_MAX_LEN,
				r11t_rfsa_get_buffer_resp_ei, &resp);
	if (ret)
		dev_err(memshare->dev, "failed to send RFSA response: %d\n", ret);
}

static const struct qmi_msg_handler r11t_rfsa_handlers[] = {
	{
		.type = QMI_REQUEST,
		.msg_id = R11T_RFSA_GET_BUFF_ADDR,
		.ei = r11t_rfsa_get_buffer_req_ei,
		.decoded_size = sizeof(struct r11t_rfsa_get_buffer_req),
		.fn = r11t_rfsa_get_buffer_req,
	}, {}
};

static void r11t_wlan_pd_status(int state, char *service_path, void *priv)
{
	struct r11t_memshare *memshare = priv;

	dev_info(memshare->dev, "WLAN PD %s state 0x%x\n",
		 service_path, state);
}

static struct r11t_memshare_client *
r11t_memshare_find_client(struct r11t_memshare *memshare, u32 id, u32 proc)
{
	int i;

	if (proc != DHMS_MEM_PROC_MPSS_V01)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(memshare->clients); i++)
		if (memshare->clients[i].id == id)
			return &memshare->clients[i];

	return NULL;
}

static int r11t_memshare_alloc(struct r11t_memshare *memshare,
			       struct r11t_memshare_client *client)
{
	struct qcom_scm_vmperm dst[] = {
		{ QCOM_SCM_VMID_HLOS, QCOM_SCM_PERM_RW },
		{ QCOM_SCM_VMID_MSS_MSA, QCOM_SCM_PERM_RW },
	};
	u64 src = BIT(QCOM_SCM_VMID_HLOS);
	int ret;

	if (client->vaddr)
		return 0;

	client->vaddr = dma_alloc_coherent(memshare->dev, client->alloc_size,
					   &client->dma_addr, GFP_KERNEL);
	if (!client->vaddr)
		return -ENOMEM;

	if (upper_32_bits(client->dma_addr)) {
		ret = -ERANGE;
		goto free;
	}

	ret = qcom_scm_assign_mem(client->dma_addr, client->size, &src,
				  dst, ARRAY_SIZE(dst));
	if (ret)
		goto free;

	client->perms = src;
	dev_info(memshare->dev, "client %u: shared %u bytes at %pad\n",
		 client->id, client->size, &client->dma_addr);
	return 0;

free:
	dma_free_coherent(memshare->dev, client->alloc_size,
			  client->vaddr, client->dma_addr);
	client->vaddr = NULL;
	return ret;
}

static void r11t_memshare_release(struct r11t_memshare *memshare,
				  struct r11t_memshare_client *client)
{
	struct qcom_scm_vmperm dst = {
		.vmid = QCOM_SCM_VMID_HLOS,
		.perm = QCOM_SCM_PERM_RW,
	};

	if (!client->vaddr)
		return;

	if (client->perms &&
	    qcom_scm_assign_mem(client->dma_addr, client->size,
				&client->perms, &dst, 1)) {
		dev_err(memshare->dev, "client %u: failed to restore ownership\n",
			client->id);
		return;
	}

	dma_free_coherent(memshare->dev, client->alloc_size,
			  client->vaddr, client->dma_addr);
	client->vaddr = NULL;
}

static void r11t_memshare_alloc_req(struct qmi_handle *qmi,
				    struct sockaddr_qrtr *sq,
				    struct qmi_txn *txn,
				    const void *decoded)
{
	const struct mem_alloc_generic_req_msg_v01 *req = decoded;
	struct r11t_memshare *memshare = container_of(qmi, struct r11t_memshare,
						      qmi);
	struct mem_alloc_generic_resp_msg_v01 resp = {};
	struct r11t_memshare_client *client;
	int ret;

	mutex_lock(&memshare->lock);
	client = r11t_memshare_find_client(memshare, req->client_id, req->proc_id);
	if (!client) {
		resp.resp.result = QMI_RESULT_FAILURE_V01;
		resp.resp.error = QMI_ERR_INVALID_ID_V01;
	} else if (r11t_memshare_alloc(memshare, client)) {
		resp.resp.result = QMI_RESULT_FAILURE_V01;
		resp.resp.error = QMI_ERR_NO_MEMORY_V01;
	} else {
		resp.resp.result = QMI_RESULT_SUCCESS_V01;
		resp.resp.error = QMI_ERR_NONE_V01;
		resp.sequence_id_valid = 1;
		resp.sequence_id = req->sequence_id;
		resp.dhms_mem_alloc_addr_info_valid = 1;
		resp.dhms_mem_alloc_addr_info_len = 1;
		resp.dhms_mem_alloc_addr_info[0].phy_addr = client->dma_addr;
		resp.dhms_mem_alloc_addr_info[0].num_bytes = client->size;
		dev_info(memshare->dev,
			 "alloc request: client=%u size=%u sequence=%u\n",
			 req->client_id, req->num_bytes, req->sequence_id);
	}
	mutex_unlock(&memshare->lock);

	ret = qmi_send_response(qmi, sq, txn, MEM_ALLOC_GENERIC_RESP_MSG_V01,
				R11T_MEMSHARE_MAX_MSG_LEN,
				mem_alloc_generic_resp_msg_data_v01_ei,
				&resp);
	if (ret)
		dev_err(memshare->dev, "failed to send alloc response: %d\n", ret);
}

static void r11t_memshare_free_req(struct qmi_handle *qmi,
				   struct sockaddr_qrtr *sq,
				   struct qmi_txn *txn,
				   const void *decoded)
{
	struct mem_free_generic_resp_msg_v01 resp = {
		.resp = {
			.result = QMI_RESULT_SUCCESS_V01,
			.error = QMI_ERR_NONE_V01,
		},
	};
	int ret;

	/* Keep all allocations until modem shutdown; acknowledge the request. */
	ret = qmi_send_response(qmi, sq, txn, MEM_FREE_GENERIC_RESP_MSG_V01,
				R11T_MEMSHARE_MAX_MSG_LEN,
				mem_free_generic_resp_msg_data_v01_ei,
				&resp);
	if (ret)
		dev_err(container_of(qmi, struct r11t_memshare, qmi)->dev,
			"failed to send free response: %d\n", ret);
}

static void r11t_memshare_query_req(struct qmi_handle *qmi,
				    struct sockaddr_qrtr *sq,
				    struct qmi_txn *txn,
				    const void *decoded)
{
	const struct mem_query_size_req_msg_v01 *req = decoded;
	struct r11t_memshare *memshare = container_of(qmi, struct r11t_memshare,
						      qmi);
	struct mem_query_size_rsp_msg_v01 resp = {};
	struct r11t_memshare_client *client;
	int ret;

	client = r11t_memshare_find_client(memshare, req->client_id,
					   req->proc_id_valid ? req->proc_id : 0);
	if (!client) {
		resp.resp.result = QMI_RESULT_FAILURE_V01;
		resp.resp.error = QMI_ERR_INVALID_ID_V01;
	} else {
		resp.resp.result = QMI_RESULT_SUCCESS_V01;
		resp.resp.error = QMI_ERR_NONE_V01;
		resp.size_valid = 1;
		resp.size = client->size;
	}

	ret = qmi_send_response(qmi, sq, txn, MEM_QUERY_SIZE_RESP_MSG_V01,
				R11T_MEMSHARE_MAX_MSG_LEN,
				mem_query_size_resp_msg_data_v01_ei,
				&resp);
	if (ret)
		dev_err(memshare->dev, "failed to send query response: %d\n", ret);
}

static const struct qmi_msg_handler r11t_memshare_handlers[] = {
	{
		.type = QMI_REQUEST,
		.msg_id = MEM_ALLOC_GENERIC_REQ_MSG_V01,
		.ei = mem_alloc_generic_req_msg_data_v01_ei,
		.decoded_size = sizeof(struct mem_alloc_generic_req_msg_v01),
		.fn = r11t_memshare_alloc_req,
	}, {
		.type = QMI_REQUEST,
		.msg_id = MEM_FREE_GENERIC_REQ_MSG_V01,
		.ei = mem_free_generic_req_msg_data_v01_ei,
		.decoded_size = sizeof(struct mem_free_generic_req_msg_v01),
		.fn = r11t_memshare_free_req,
	}, {
		.type = QMI_REQUEST,
		.msg_id = MEM_QUERY_SIZE_REQ_MSG_V01,
		.ei = mem_query_size_req_msg_data_v01_ei,
		.decoded_size = sizeof(struct mem_query_size_req_msg_v01),
		.fn = r11t_memshare_query_req,
	}, {}
};

static int r11t_memshare_probe(struct platform_device *pdev)
{
	struct r11t_memshare *memshare;
	int ret;

	if (!qcom_scm_is_available())
		return -EPROBE_DEFER;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	memshare = devm_kzalloc(&pdev->dev, sizeof(*memshare), GFP_KERNEL);
	if (!memshare)
		return -ENOMEM;

	memshare->dev = &pdev->dev;
	mutex_init(&memshare->lock);
	memshare->clients[0] = (struct r11t_memshare_client) {
		.id = 0, .size = SZ_2M, .alloc_size = SZ_2M,
	};
	memshare->clients[1] = (struct r11t_memshare_client) {
		.id = 1, .size = 15 * SZ_1M,
		.alloc_size = 15 * SZ_1M + R11T_MEMSHARE_GUARD_SIZE,
	};
	memshare->clients[2] = (struct r11t_memshare_client) {
		.id = 2, .size = 3 * SZ_1M, .alloc_size = 3 * SZ_1M,
	};

	ret = r11t_memshare_alloc(memshare, &memshare->clients[0]);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to allocate client 0\n");

	ret = r11t_memshare_alloc(memshare, &memshare->clients[1]);
	if (ret)
		goto release_client0;

	ret = qmi_handle_init(&memshare->qmi, R11T_MEMSHARE_MAX_MSG_LEN,
			      NULL, r11t_memshare_handlers);
	if (ret)
		goto release_client1;

	ret = qmi_add_server(&memshare->qmi, R11T_MEMSHARE_SERVICE_ID,
			     R11T_MEMSHARE_VERSION, R11T_MEMSHARE_INSTANCE);
	if (ret)
		goto release_qmi;

	ret = qmi_handle_init(&memshare->rfsa_qmi, R11T_RFSA_RESP_MAX_LEN, NULL,
			      r11t_rfsa_handlers);
	if (ret)
		goto release_qmi;

	ret = qmi_add_server(&memshare->rfsa_qmi, R11T_RFSA_SERVICE_ID,
			     R11T_RFSA_VERSION, R11T_RFSA_INSTANCE);
	if (ret)
		goto release_rfsa_qmi;

	memshare->pdr = pdr_handle_alloc(r11t_wlan_pd_status, memshare);
	if (IS_ERR(memshare->pdr)) {
		ret = PTR_ERR(memshare->pdr);
		goto release_rfsa_qmi;
	}

	memshare->wlan_pd = pdr_add_lookup(memshare->pdr, "wlan/fw",
					   "msm/modem/wlan_pd");
	if (IS_ERR(memshare->wlan_pd)) {
		ret = PTR_ERR(memshare->wlan_pd);
		goto release_pdr;
	}

	platform_set_drvdata(pdev, memshare);
	dev_info(&pdev->dev, "registered QMI service 0x34 version 1 instance 1\n");
	dev_info(&pdev->dev, "registered RFSA service 0x1c version 1 instance 1\n");
	dev_info(&pdev->dev, "registered wlan/fw PDR lookup\n");
	return 0;

release_pdr:
	pdr_handle_release(memshare->pdr);
release_rfsa_qmi:
	qmi_handle_release(&memshare->rfsa_qmi);
release_qmi:
	qmi_handle_release(&memshare->qmi);
release_client1:
	r11t_memshare_release(memshare, &memshare->clients[1]);
release_client0:
	r11t_memshare_release(memshare, &memshare->clients[0]);
	return dev_err_probe(&pdev->dev, ret, "probe failed\n");
}

static void r11t_memshare_remove(struct platform_device *pdev)
{
	struct r11t_memshare *memshare = platform_get_drvdata(pdev);
	int i;

	pdr_handle_release(memshare->pdr);
	qmi_handle_release(&memshare->rfsa_qmi);
	qmi_handle_release(&memshare->qmi);
	for (i = ARRAY_SIZE(memshare->clients) - 1; i >= 0; i--)
		r11t_memshare_release(memshare, &memshare->clients[i]);
}

static const struct of_device_id r11t_memshare_of_match[] = {
	{ .compatible = "oppo,r11t-memshare" },
	{}
};
MODULE_DEVICE_TABLE(of, r11t_memshare_of_match);

static struct platform_driver r11t_memshare_driver = {
	.probe = r11t_memshare_probe,
	.remove = r11t_memshare_remove,
	.driver = {
		.name = "qcom-r11t-memshare",
		.of_match_table = r11t_memshare_of_match,
	},
};
module_platform_driver(r11t_memshare_driver);

MODULE_DESCRIPTION("OPPO R11T Qualcomm modem memory share service");
MODULE_LICENSE("GPL");
