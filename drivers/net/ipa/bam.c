// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2024 Linaro Ltd.
 */

#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/refcount.h>
#include <linux/types.h>

#include "ipa.h"
#include "ipa_cmd.h"
#include "ipa_data.h"
#include "ipa_dma.h"
#include "ipa_dma_private.h"
#include "ipa_endpoint.h"
#include "ipa_gsi.h"
#include "ipa_trans.h"

/**
 * DOC: The IPA Smart Peripheral System Interface
 *
 * IPA v2.x uses BAM rather than GSI as its DMA transport.  Each channel
 * here corresponds to one BAM pipe configured in BAM2BAM mode.  IPA
 * immediate commands are transferred one at a time, each in one BAM
 * transfer.
 */

/* Consult hardware, move any newly completed transactions to completed state */
static void bam_channel_update(struct ipa_channel *channel)
{
	struct ipa_trans_info *trans_info = &channel->trans_info;
	struct ipa_trans *trans;
	enum dma_status status;
	u16 trans_id;

	/* Nothing to do if there are no pending transactions */
	if (trans_info->pending_id == trans_info->committed_id)
		return;

	trans_id = trans_info->pending_id % channel->tre_count;
	trans = &trans_info->trans[trans_id];

	status = dma_async_is_tx_complete(channel->dma_chan, trans->cookie,
					  NULL, NULL);
	if (status != DMA_COMPLETE)
		return;

	/* Take a reference to keep it from completing before we give the
	 * completed transactions back to the transaction layer.
	 */
	refcount_inc(&trans->refcount);

	/* For RX channels the received byte count has already been recorded
	 * in trans->len by DMA metadata.  For TX channels report the number
	 * of transactions and bytes this completion represents up the
	 * network stack.
	 */
	if (channel->toward_ipa)
		gsi_trans_tx_completed(trans);

	ipa_trans_move_complete(trans);

	ipa_trans_free(trans);
}

/**
 * bam_channel_poll_one() - Return a single completed transaction on a channel
 * @channel:	Channel to be polled
 *
 * Return:	Transaction pointer, or null if none are available
 */
static struct ipa_trans *bam_channel_poll_one(struct ipa_channel *channel)
{
	struct ipa_trans *trans;

	/* Get the first completed transaction */
	trans = ipa_channel_trans_complete(channel);
	if (trans)
		ipa_trans_move_polled(trans);

	return trans;
}

/**
 * bam_channel_poll() - NAPI poll function for a channel
 * @napi:	NAPI structure for the channel
 * @budget:	Budget supplied by NAPI core
 *
 * Return:	Number of items polled (<= budget)
 */
static int bam_channel_poll(struct napi_struct *napi, int budget)
{
	struct ipa_channel *channel;
	int count;

	channel = container_of(napi, struct ipa_channel, napi);
	for (count = 0; count < budget; count++) {
		struct ipa_trans *trans;

		trans = bam_channel_poll_one(channel);
		if (!trans)
			break;
		ipa_trans_complete(trans);
	}

	if (count < budget)
		napi_complete(napi);

	return count;
}

/* Get and configure the BAM DMA channel */
static int bam_channel_init_one(struct ipa_dma *bam,
				const struct ipa_gsi_endpoint_data *data,
				bool command)
{
	struct dma_slave_config bam_config;
	u32 channel_id = data->channel_id;
	struct ipa_channel *channel = &bam->channel[channel_id];
	u32 tre_max;
	int ret;

	channel->dma_subsys = bam;
	channel->dma_chan = dma_request_chan(bam->dev, data->channel_name);
	if (IS_ERR(channel->dma_chan)) {
		ret = PTR_ERR(channel->dma_chan);
		dev_err(bam->dev, "failed to request BAM channel %s: %d\n",
			data->channel_name, ret);
		goto err_clear_bam;
	}
	channel->toward_ipa = data->toward_ipa;
	channel->command = command;
	channel->trans_tre_max = data->channel.tlv_count;
	channel->tre_count = data->channel.tre_count;

	ret = ipa_channel_trans_init(bam, channel_id);
	if (ret)
		goto err_dma_chan_free;

	if (data->toward_ipa) {
		bam_config.direction = DMA_MEM_TO_DEV;
		bam_config.dst_maxburst = channel->trans_tre_max;
	} else {
		bam_config.direction = DMA_DEV_TO_MEM;
		bam_config.src_maxburst = channel->trans_tre_max;
	}

	ret = dmaengine_slave_config(channel->dma_chan, &bam_config);
	if (ret) {
		dev_err(bam->dev, "error %d configuring BAM channel %s\n",
			ret, data->channel_name);
		goto err_trans_exit;
	}

	if (command) {
		tre_max = ipa_channel_tre_max(bam, channel_id);

		ret = ipa_cmd_pool_init(channel, tre_max);
		if (ret)
			goto err_trans_exit;
	}

	return 0;

err_trans_exit:
	ipa_channel_trans_exit(channel);
err_dma_chan_free:
	dma_release_channel(channel->dma_chan);
err_clear_bam:
	channel->dma_subsys = NULL;	/* Mark it not (fully) initialized */

	return ret;
}

/* Inverse of bam_channel_init_one() */
static void bam_channel_exit_one(struct ipa_channel *channel)
{
	if (!channel->dma_subsys)
		return;

	if (channel->command)
		ipa_cmd_pool_exit(channel);
	ipa_channel_trans_exit(channel);
	dmaengine_terminate_sync(channel->dma_chan);
	dma_release_channel(channel->dma_chan);
	channel->dma_subsys = NULL;
}

/* Get channels from BAM_DMA */
static int bam_channel_init(struct ipa_dma *bam, u32 count,
			    const struct ipa_gsi_endpoint_data *data)
{
	int ret = 0;
	u32 i;

	for (i = 0; i < count; ++i) {
		bool command = i == IPA_ENDPOINT_AP_COMMAND_TX;

		if (ipa_gsi_endpoint_data_empty(&data[i]) ||
		    data[i].ee_id == GSI_EE_MODEM)
			continue;

		ret = bam_channel_init_one(bam, &data[i], command);
		if (ret)
			goto err_unwind;
	}

	return ret;

err_unwind:
	while (i--) {
		if (ipa_gsi_endpoint_data_empty(&data[i]) ||
		    data[i].ee_id == GSI_EE_MODEM)
			continue;

		bam_channel_exit_one(&bam->channel[data[i].channel_id]);
	}

	return ret;
}

/* Inverse of bam_channel_init() */
static void bam_channel_exit(struct ipa_dma *bam)
{
	u32 channel_id = BAM_CHANNEL_COUNT_MAX - 1;

	do
		bam_channel_exit_one(&bam->channel[channel_id]);
	while (channel_id--);
}

/* Inverse of bam_init() */
static void bam_exit(struct ipa_dma *bam)
{
	mutex_destroy(&bam->mutex);
	bam_channel_exit(bam);
	free_netdev(bam->dummy_dev);
}

/* Setup function for a single channel */
static void bam_channel_setup_one(struct ipa_dma *bam, u32 channel_id)
{
	struct ipa_channel *channel = &bam->channel[channel_id];

	if (!channel->dma_subsys)
		return;		/* Ignore uninitialized channels */

	if (channel->toward_ipa)
		netif_napi_add_tx(bam->dummy_dev, &channel->napi,
				  bam_channel_poll);
	else
		netif_napi_add(bam->dummy_dev, &channel->napi,
			       bam_channel_poll);
	napi_enable(&channel->napi);
}

static void bam_channel_teardown_one(struct ipa_dma *bam, u32 channel_id)
{
	struct ipa_channel *channel = &bam->channel[channel_id];

	if (!channel->dma_subsys)
		return;		/* Ignore uninitialized channels */

	netif_napi_del(&channel->napi);
}

/* Setup function for channels */
static int bam_channel_setup(struct ipa_dma *bam)
{
	u32 channel_id = 0;

	mutex_lock(&bam->mutex);

	do
		bam_channel_setup_one(bam, channel_id);
	while (++channel_id < BAM_CHANNEL_COUNT_MAX);

	mutex_unlock(&bam->mutex);

	return 0;
}

/* Inverse of bam_channel_setup() */
static void bam_channel_teardown(struct ipa_dma *bam)
{
	u32 channel_id;

	mutex_lock(&bam->mutex);

	channel_id = BAM_CHANNEL_COUNT_MAX;
	do
		bam_channel_teardown_one(bam, channel_id);
	while (channel_id--);

	mutex_unlock(&bam->mutex);
}

static int bam_setup(struct ipa_dma *bam)
{
	return bam_channel_setup(bam);
}

static void bam_teardown(struct ipa_dma *bam)
{
	bam_channel_teardown(bam);
}

static u32 bam_channel_tre_max(struct ipa_dma *bam, u32 channel_id)
{
	struct ipa_channel *channel = &bam->channel[channel_id];

	/* Hardware limit is channel->tre_count - 1 */
	return channel->tre_count - (channel->trans_tre_max - 1);
}

static int bam_channel_start(struct ipa_dma *bam, u32 channel_id)
{
	return 0;
}

static int bam_channel_stop(struct ipa_dma *bam, u32 channel_id)
{
	struct ipa_channel *channel = &bam->channel[channel_id];

	return dmaengine_terminate_sync(channel->dma_chan);
}

static void bam_channel_reset(struct ipa_dma *bam, u32 channel_id, bool doorbell)
{
	bam_channel_stop(bam, channel_id);
}

static int bam_channel_suspend(struct ipa_dma *bam, u32 channel_id)
{
	struct ipa_channel *channel = &bam->channel[channel_id];

	return dmaengine_pause(channel->dma_chan);
}

static int bam_channel_resume(struct ipa_dma *bam, u32 channel_id)
{
	struct ipa_channel *channel = &bam->channel[channel_id];

	return dmaengine_resume(channel->dma_chan);
}

static void bam_suspend(struct ipa_dma *bam)
{
	/* No-op for now */
}

static void bam_resume(struct ipa_dma *bam)
{
	/* No-op for now */
}

static void bam_trans_callback(void *arg)
{
	ipa_trans_complete(arg);
}

static void bam_trans_commit(struct ipa_trans *trans, bool unused)
{
	struct ipa_channel *channel =
		&trans->dma_subsys->channel[trans->channel_id];
	enum ipa_cmd_opcode opcode = IPA_CMD_NONE;
	enum dma_transfer_direction direction;
	struct scatterlist *sg;
	u32 byte_count = 0;
	u8 *cmd_opcode;
	u32 i;

	if (channel->toward_ipa)
		direction = DMA_MEM_TO_DEV;
	else
		direction = DMA_DEV_TO_MEM;

	/* A command channel's transaction supplies a command opcode in the
	 * transfer length field, and requires the immediate command flag.
	 */
	cmd_opcode = channel->command ? &trans->cmd_opcode[0] : NULL;
	for_each_sg(trans->sgl, sg, trans->used_count, i) {
		bool last_tre = i == trans->used_count - 1;
		dma_addr_t addr = sg_dma_address(sg);
		u32 len = sg_dma_len(sg);
		unsigned long dma_flags = 0;
		struct dma_async_tx_descriptor *desc;

		byte_count += len;
		if (cmd_opcode)
			opcode = *cmd_opcode++;

		if (opcode != IPA_CMD_NONE) {
			len = opcode;
			dma_flags |= DMA_PREP_IMM_CMD;
		}

		if (last_tre)
			dma_flags |= DMA_PREP_INTERRUPT;

		desc = dmaengine_prep_slave_single(channel->dma_chan, addr, len,
						   direction, dma_flags);
		if (!desc)
			return;

		if (last_tre) {
			desc->callback = bam_trans_callback;
			desc->callback_param = trans;
		}

		desc->cookie = dmaengine_submit(desc);

		if (last_tre)
			trans->cookie = desc->cookie;

		/* For RX, the DMA metadata records the received byte count */
		if (direction == DMA_DEV_TO_MEM)
			dmaengine_desc_attach_metadata(desc, &trans->len,
						       sizeof(trans->len));
	}

	trans->len = byte_count;
	if (channel->toward_ipa)
		gsi_trans_tx_committed(trans);

	ipa_trans_move_pending(trans);

	dma_async_issue_pending(channel->dma_chan);
}

/* Initialize the BAM DMA channels.
 * Actual hardware initialization is handled by the BAM_DMA driver.
 */
int bam_init(struct ipa_dma *bam, struct platform_device *pdev,
	     enum ipa_version version, u32 count,
	     const struct ipa_gsi_endpoint_data *data)
{
	struct device *dev = &pdev->dev;
	int ret;

	bam->dev = dev;
	bam->version = version;
	bam->channel_count = BAM_CHANNEL_COUNT_MAX;

	bam->setup = bam_setup;
	bam->teardown = bam_teardown;
	bam->exit = bam_exit;
	bam->suspend = bam_suspend;
	bam->resume = bam_resume;
	bam->channel_tre_max = bam_channel_tre_max;
	bam->channel_start = bam_channel_start;
	bam->channel_stop = bam_channel_stop;
	bam->channel_reset = bam_channel_reset;
	bam->channel_suspend = bam_channel_suspend;
	bam->channel_resume = bam_channel_resume;
	bam->channel_update = bam_channel_update;
	bam->trans_commit = bam_trans_commit;

	/* BAM uses NAPI on all channels.  Create a dummy network device
	 * for the channel NAPI contexts to be associated with.
	 */
	bam->dummy_dev = alloc_netdev_dummy(0);
	if (!bam->dummy_dev)
		return -ENOMEM;

	ret = bam_channel_init(bam, count, data);
	if (ret)
		goto err_free_netdev;

	mutex_init(&bam->mutex);

	return 0;

err_free_netdev:
	free_netdev(bam->dummy_dev);

	return ret;
}
