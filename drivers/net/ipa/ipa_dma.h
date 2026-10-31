/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2024 Linaro Ltd.
 */
#ifndef _GSI_H_
#define _GSI_H_

#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/types.h>

#include "ipa_version.h"

/* Maximum number of channels and event rings supported by the driver */
#define GSI_CHANNEL_COUNT_MAX	28
#define GSI_EVT_RING_COUNT_MAX	28
#define BAM_CHANNEL_COUNT_MAX	20

/* Maximum TLV FIFO size for a channel; 64 here is arbitrary (and high) */
#define GSI_TLV_MAX		64

struct device;
struct platform_device;
struct dma_chan;

struct ipa_dma;
struct ipa_trans;
struct ipa_gsi_endpoint_data;

struct gsi_ring {
	void *virt;			/* ring array base address */
	dma_addr_t addr;		/* primarily low 32 bits used */
	u32 count;			/* number of elements in ring */

	/* The ring index value indicates the next "open" entry in the ring.
	 *
	 * A channel ring consists of TRE entries filled by the AP and passed
	 * to the hardware for processing.  For a channel ring, the ring index
	 * identifies the next unused entry to be filled by the AP.  In this
	 * case the initial value is assumed by hardware to be 0.
	 *
	 * An event ring consists of event structures filled by the hardware
	 * and passed to the AP.  For event rings, the ring index identifies
	 * the next ring entry that is not known to have been filled by the
	 * hardware.  The initial value used is arbitrary (so we use 0).
	 */
	u32 index;
};

/* Transactions use several resources that can be allocated dynamically
 * but taken from a fixed-size pool.  The number of elements required for
 * the pool is limited by the total number of TREs that can be outstanding.
 *
 * If sufficient TREs are available to reserve for a transaction,
 * allocation from these pools is guaranteed to succeed.  Furthermore,
 * these resources are implicitly freed whenever the TREs in the
 * transaction they're associated with are released.
 *
 * The result of a pool allocation of multiple elements is always
 * contiguous.
 */
struct ipa_trans_pool {
	void *base;			/* base address of element pool */
	u32 count;			/* # elements in the pool */
	u32 free;			/* next free element in pool (modulo) */
	u32 size;			/* size (bytes) of an element */
	u32 max_alloc;			/* max allocation request */
	dma_addr_t addr;		/* DMA address if DMA pool (or 0) */
};

struct ipa_trans_info {
	atomic_t tre_avail;		/* TREs available for allocation */

	u16 free_id;			/* first free trans in array */
	u16 allocated_id;		/* first allocated transaction */
	u16 committed_id;		/* first committed transaction */
	u16 pending_id;			/* first pending transaction */
	u16 completed_id;		/* first completed transaction */
	u16 polled_id;			/* first polled transaction */
	struct ipa_trans *trans;	/* transaction array */
	struct ipa_trans **map;		/* TRE -> transaction map */

	struct ipa_trans_pool sg_pool;	/* scatterlist pool */
	struct ipa_trans_pool cmd_pool;	/* command payload DMA pool */
};

/* Hardware values signifying the state of a channel */
enum gsi_channel_state {
	GSI_CHANNEL_STATE_NOT_ALLOCATED		= 0x0,
	GSI_CHANNEL_STATE_ALLOCATED		= 0x1,
	GSI_CHANNEL_STATE_STARTED		= 0x2,
	GSI_CHANNEL_STATE_STOPPED		= 0x3,
	GSI_CHANNEL_STATE_STOP_IN_PROC		= 0x4,
	GSI_CHANNEL_STATE_FLOW_CONTROLLED	= 0x5,	/* IPA v4.2-v4.9 */
	GSI_CHANNEL_STATE_ERROR			= 0xf,
};

/* We only care about channels between IPA and AP */
struct ipa_channel {
	struct ipa_dma *dma_subsys;
	bool toward_ipa;
	bool command;			/* AP command TX channel or not */

	u8 trans_tre_max;		/* max TREs in a transaction */
	u16 tre_count;
	u16 event_count;

	struct gsi_ring tre_ring;
	u32 evt_ring_id;

	struct dma_chan *dma_chan;

	/* The following counts are used only for TX endpoints */
	u64 byte_count;			/* total # bytes transferred */
	u64 trans_count;		/* total # transactions */
	u64 queued_byte_count;		/* last reported queued byte count */
	u64 queued_trans_count;		/* ...and queued trans count */
	u64 compl_byte_count;		/* last reported completed byte count */
	u64 compl_trans_count;		/* ...and completed trans count */

	struct ipa_trans_info trans_info;

	struct napi_struct napi;
};

/* Hardware values signifying the state of an event ring */
enum gsi_evt_ring_state {
	GSI_EVT_RING_STATE_NOT_ALLOCATED	= 0x0,
	GSI_EVT_RING_STATE_ALLOCATED		= 0x1,
	GSI_EVT_RING_STATE_ERROR		= 0xf,
};

struct gsi_evt_ring {
	struct ipa_channel *channel;
	struct gsi_ring ring;
};

struct ipa_dma {
	struct device *dev;		/* Same as IPA device */
	enum ipa_version version;
	void __iomem *virt;		/* I/O mapped registers */
	const struct regs *regs;

	u32 irq;
	u32 channel_count;
	u32 evt_ring_count;
	u32 event_bitmap;		/* allocated event rings */
	u32 modem_channel_bitmap;	/* modem channels to allocate */
	u32 type_enabled_bitmap;	/* GSI IRQ types enabled */
	u32 ieob_enabled_bitmap;	/* IEOB IRQ enabled (event rings) */
	int result;			/* Negative errno (generic commands) */
	struct completion completion;	/* Signals GSI command completion */
	struct mutex mutex;		/* protects commands, programming */

	int (*setup)(struct ipa_dma *dma_subsys);
	void (*teardown)(struct ipa_dma *dma_subsys);
	void (*exit)(struct ipa_dma *dma_subsys);
	void (*suspend)(struct ipa_dma *dma_subsys);
	void (*resume)(struct ipa_dma *dma_subsys);
	u32 (*channel_tre_max)(struct ipa_dma *dma_subsys, u32 channel_id);
	int (*channel_start)(struct ipa_dma *dma_subsys, u32 channel_id);
	int (*channel_stop)(struct ipa_dma *dma_subsys, u32 channel_id);
	void (*channel_reset)(struct ipa_dma *dma_subsys, u32 channel_id,
			      bool doorbell);
	int (*channel_suspend)(struct ipa_dma *dma_subsys, u32 channel_id);
	int (*channel_resume)(struct ipa_dma *dma_subsys, u32 channel_id);
	void (*channel_update)(struct ipa_channel *channel);
	void (*trans_commit)(struct ipa_trans *trans, bool ring_db);

	struct ipa_channel channel[GSI_CHANNEL_COUNT_MAX];
	struct gsi_evt_ring evt_ring[GSI_EVT_RING_COUNT_MAX];
	struct net_device *dummy_dev;	/* needed for NAPI */
};

/**
 * ipa_dma_setup() - Set up the DMA subsystem
 * @dma_subsys:	Address of ipa_dma structure embedded in an IPA structure
 *
 * Return:	0 if successful, or a negative error code
 *
 * Performs initialization that must wait until the GSI/BAM hardware is
 * ready (including firmware loaded).
 */
static inline int ipa_dma_setup(struct ipa_dma *dma_subsys)
{
	return dma_subsys->setup(dma_subsys);
}

/**
 * ipa_dma_teardown() - Tear down DMA subsystem
 * @dma_subsys:	ipa_dma address previously passed to a successful
 *		ipa_dma_setup() call
 */
static inline void ipa_dma_teardown(struct ipa_dma *dma_subsys)
{
	dma_subsys->teardown(dma_subsys);
}

/**
 * ipa_channel_tre_max() - Channel maximum number of in-flight TREs
 * @dma_subsys:	Pointer to ipa_dma structure
 * @channel_id:	Channel whose limit is to be returned
 *
 * Return:	 The maximum number of TREs outstanding on the channel
 */
static inline u32 ipa_channel_tre_max(struct ipa_dma *dma_subsys, u32 channel_id)
{
	return dma_subsys->channel_tre_max(dma_subsys, channel_id);
}

/**
 * ipa_channel_start() - Start an allocated DMA channel
 * @dma_subsys:	Pointer to ipa_dma structure
 * @channel_id:	Channel to start
 *
 * Return:	0 if successful, or a negative error code
 */
static inline int ipa_channel_start(struct ipa_dma *dma_subsys, u32 channel_id)
{
	return dma_subsys->channel_start(dma_subsys, channel_id);
}

/**
 * ipa_channel_stop() - Stop a started DMA channel
 * @dma_subsys:	Pointer to ipa_dma structure returned by ipa_dma_setup()
 * @channel_id:	Channel to stop
 *
 * Return:	0 if successful, or a negative error code
 */
static inline int ipa_channel_stop(struct ipa_dma *dma_subsys, u32 channel_id)
{
	return dma_subsys->channel_stop(dma_subsys, channel_id);
}

/**
 * gsi_modem_channel_flow_control() - Set channel flow control state (IPA v4.2+)
 * @dma_subsys:	Pointer to ipa_dma structure returned by ipa_dma_setup()
 * @channel_id:	Modem TX channel to control
 * @enable:	Whether to enable flow control (i.e., prevent flow)
 */
void gsi_modem_channel_flow_control(struct ipa_dma *dma_subsys, u32 channel_id,
				    bool enable);

/**
 * ipa_channel_reset() - Reset an allocated DMA channel
 * @dma_subsys:	Pointer to ipa_dma structure
 * @channel_id:	Channel to be reset
 * @doorbell:	Whether to (possibly) enable the doorbell engine
 *
 * Reset a channel and reconfigure it.  The @doorbell flag indicates
 * that the doorbell engine should be enabled if needed.
 *
 * GSI hardware relinquishes ownership of all pending receive buffer
 * transactions and they will complete with their cancelled flag set.
 */
static inline void ipa_channel_reset(struct ipa_dma *dma_subsys, u32 channel_id,
				     bool doorbell)
{
	dma_subsys->channel_reset(dma_subsys, channel_id, doorbell);
}

/**
 * ipa_dma_suspend() - Prepare the DMA subsystem for suspend
 * @dma_subsys:	Pointer to ipa_dma structure
 */
static inline void ipa_dma_suspend(struct ipa_dma *dma_subsys)
{
	dma_subsys->suspend(dma_subsys);
}

/**
 * ipa_dma_resume() - Resume the DMA subsystem following suspend
 * @dma_subsys:	Pointer to ipa_dma structure
 */
static inline void ipa_dma_resume(struct ipa_dma *dma_subsys)
{
	dma_subsys->resume(dma_subsys);
}

/**
 * ipa_channel_suspend() - Suspend a DMA channel
 * @dma_subsys:	Pointer to ipa_dma structure
 * @channel_id:	Channel to suspend
 *
 * For IPA v4.0+, suspend is implemented by stopping the channel.
 */
static inline int ipa_channel_suspend(struct ipa_dma *dma_subsys,
				      u32 channel_id)
{
	return dma_subsys->channel_suspend(dma_subsys, channel_id);
}

/**
 * ipa_channel_resume() - Resume a suspended DMA channel
 * @dma_subsys:	Pointer to ipa_dma structure
 * @channel_id:	Channel to resume
 *
 * For IPA v4.0+, the stopped channel is started again.
 */
static inline int ipa_channel_resume(struct ipa_dma *dma_subsys,
				     u32 channel_id)
{
	return dma_subsys->channel_resume(dma_subsys, channel_id);
}

/**
 * gsi_init() - Initialize the GSI subsystem
 * @dma_subsys:	Address of ipa_dma structure embedded in an IPA structure
 * @pdev:	IPA platform device
 * @version:	IPA hardware version (implies GSI version)
 * @count:	Number of entries in the configuration data array
 * @data:	Endpoint and channel configuration data
 *
 * Return:	0 if successful, or a negative error code
 *
 * Early stage initialization of the GSI subsystem, performing tasks
 * that can be done before the GSI hardware is ready to use.
 */
int gsi_init(struct ipa_dma *dma_subsys, struct platform_device *pdev,
	     enum ipa_version version, u32 count,
	     const struct ipa_gsi_endpoint_data *data);

/**
 * bam_init() - Initialize the BAM subsystem
 * @dma_subsys:	Address of ipa_dma structure embedded in an IPA structure
 * @pdev:	IPA platform device
 * @version:	IPA hardware version (implies BAM version)
 * @count:	Number of entries in the configuration data array
 * @data:	Endpoint and channel configuration data
 *
 * Return:	0 if successful, or a negative error code
 *
 * Early stage initialization of the BAM subsystem, performing tasks
 * that can be done before the BAM hardware is ready to use.
 */
int bam_init(struct ipa_dma *dma_subsys, struct platform_device *pdev,
	     enum ipa_version version, u32 count,
	     const struct ipa_gsi_endpoint_data *data);

/**
 * ipa_dma_exit() - Exit the DMA subsystem
 * @dma_subsys:	ipa_dma address previously passed to a successful gsi_init()
 *		or bam_init() call
 */
static inline void ipa_dma_exit(struct ipa_dma *dma_subsys)
{
	if (dma_subsys)
		dma_subsys->exit(dma_subsys);
}

#endif /* _GSI_H_ */
