/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2024 Linaro Ltd.
 */
#ifndef _GSI_PRIVATE_H_
#define _GSI_PRIVATE_H_

/* === Only "gsi.c" and "ipa_trans.c" should include this file === */

#include <linux/types.h>

struct ipa_dma;
struct ipa_channel;
struct gsi_ring;
struct ipa_trans;

#define GSI_RING_ELEMENT_SIZE	16	/* bytes; must be a power of 2 */

/* GSI-specific transaction commit, dispatched by ipa_trans_commit() */
void gsi_trans_commit(struct ipa_trans *trans, bool ring_db);

/* Move a transaction to the pending list (used by the BAM transport) */
void ipa_trans_move_pending(struct ipa_trans *trans);

/**
 * ipa_trans_move_complete() - Mark a GSI transaction completed
 * @trans:	Transaction whose state is to be updated
 */
void ipa_trans_move_complete(struct ipa_trans *trans);

/**
 * ipa_trans_move_polled() - Mark a transaction polled
 * @trans:	Transaction whose state is to be updated
 */
void ipa_trans_move_polled(struct ipa_trans *trans);

/**
 * ipa_trans_complete() - Complete a GSI transaction
 * @trans:	Transaction to complete
 *
 * Marks a transaction complete (including freeing it).
 */
void ipa_trans_complete(struct ipa_trans *trans);

/**
 * gsi_channel_trans_mapped() - Return a transaction mapped to a TRE index
 * @channel:	Channel associated with the transaction
 * @index:	Index of the TRE having a transaction
 *
 * Return:	The GSI transaction pointer associated with the TRE index
 */
struct ipa_trans *gsi_channel_trans_mapped(struct ipa_channel *channel,
					   u32 index);

/**
 * ipa_channel_trans_complete() - Return a channel's next completed transaction
 * @channel:	Channel whose next transaction is to be returned
 *
 * Return:	The next completed transaction, or NULL if nothing new
 */
struct ipa_trans *ipa_channel_trans_complete(struct ipa_channel *channel);

/**
 * ipa_channel_trans_cancel_pending() - Cancel pending transactions
 * @channel:	Channel whose pending transactions should be cancelled
 *
 * Cancel all pending transactions on a channel.  These are transactions
 * that have been committed but not yet completed.  This is required when
 * the channel gets reset.  At that time all pending transactions will be
 * marked as cancelled.
 *
 * NOTE:  Transactions already complete at the time of this call are
 *	  unaffected.
 */
void ipa_channel_trans_cancel_pending(struct ipa_channel *channel);

/**
 * ipa_channel_trans_init() - Initialize a channel's GSI transaction info
 * @gsi:	GSI pointer
 * @channel_id:	Channel number
 *
 * Return:	0 if successful, or -ENOMEM on allocation failure
 *
 * Creates and sets up information for managing transactions on a channel
 */
int ipa_channel_trans_init(struct ipa_dma *gsi, u32 channel_id);

/**
 * ipa_channel_trans_exit() - Inverse of ipa_channel_trans_init()
 * @channel:	Channel whose transaction information is to be cleaned up
 */
void ipa_channel_trans_exit(struct ipa_channel *channel);

/**
 * gsi_channel_doorbell() - Ring a channel's doorbell
 * @channel:	Channel whose doorbell should be rung
 *
 * Rings a channel's doorbell to inform the GSI hardware that new
 * transactions (TREs, really) are available for it to process.
 */
void gsi_channel_doorbell(struct ipa_channel *channel);

/* gsi_channel_update() - Update knowledge of channel hardware state
 * @channel:	Channel to be updated
 *
 * Consult hardware, change the state of any newly-completed transactions
 * on a channel.
 */
void gsi_channel_update(struct ipa_channel *channel);

/**
 * gsi_ring_virt() - Return virtual address for a ring entry
 * @ring:	Ring whose address is to be translated
 * @index:	Index (slot number) of entry
 */
void *gsi_ring_virt(struct gsi_ring *ring, u32 index);

/**
 * gsi_trans_tx_committed() - Record bytes committed for transmit
 * @trans:	TX endpoint transaction being committed
 *
 * Report that a TX transaction has been committed.  It updates some
 * statistics used to manage transmit rates.
 */
void gsi_trans_tx_committed(struct ipa_trans *trans);

/**
 * gsi_trans_tx_queued() - Report a queued TX channel transaction
 * @trans:	Transaction being passed to hardware
 *
 * Report to the network stack that a TX transaction is being supplied
 * to the hardware.
 */
void gsi_trans_tx_queued(struct ipa_trans *trans);

/**
 * gsi_trans_tx_completed() - Report completed TX transactions
 * @trans:	TX channel transaction that has completed
 *
 * Report to the network stack that a TX transaction has completed,
 * used to manage transmit rates.
 */
void gsi_trans_tx_completed(struct ipa_trans *trans);

#endif /* _GSI_PRIVATE_H_ */
