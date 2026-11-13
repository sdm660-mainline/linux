// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2024 Linaro Ltd.
 */

#include <linux/bitops.h>
#include <linux/build_bug.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/pm_runtime.h>
#include <linux/types.h>
#include <linux/unaligned.h>

#include "ipa_dma.h"
#include "ipa_trans.h"
#include "ipa.h"
#include "ipa_cmd.h"
#include "ipa_endpoint.h"
#include "ipa_mem.h"
#include "ipa_qmi_msg.h"
#include "ipa_reg.h"
#include "ipa_table.h"
#include "ipa_version.h"

static void ipa_ap_route_setup(struct ipa *ipa);

/**
 * DOC: IPA Filter and Route Tables
 *
 * The IPA has tables defined in its local (IPA-resident) memory that define
 * filter and routing rules.  An entry in either of these tables is a little
 * endian 64-bit "slot" that holds the address of a rule definition.  (The
 * size of these slots is 64 bits regardless of the host DMA address size.)
 *
 * Separate tables (both filter and route) are used for IPv4 and IPv6.  There
 * is normally another set of "hashed" filter and route tables, which are
 * used with a hash of message metadata.  Hashed operation is not supported
 * by all IPA hardware (IPA v4.2 doesn't support hashed tables).
 *
 * Rules can be in local memory or in DRAM (system memory).  The offset of
 * an object (such as a route or filter table) in IPA-resident memory must
 * 128-byte aligned.  An object in system memory (such as a route or filter
 * rule) must be at an 8-byte aligned address.  We currently only place
 * route or filter rules in system memory.
 *
 * A rule consists of a contiguous block of 32-bit values terminated with
 * 32 zero bits.  A special "zero entry" rule consisting of 64 zero bits
 * represents "no filtering" or "no routing," and is the reset value for
 * filter or route table rules.
 *
 * Each filter rule is associated with an AP or modem TX endpoint, though
 * not all TX endpoints support filtering.  The first 64-bit slot in a
 * filter table is a bitmap indicating which endpoints have entries in
 * the table.  Each set bit in this bitmap indicates the presence of the
 * address of a filter rule in the memory following the bitmap.  Until IPA
 * v5.0,  the low-order bit (bit 0) in this bitmap represents a special
 * global filter, which applies to all traffic.  Otherwise the position of
 * each set bit represents an endpoint for which a filter rule is defined.
 *
 * The global rule is not used in current code, and support for it is
 * removed starting at IPA v5.0.  For IPA v5.0+, the endpoint bitmap
 * position defines the endpoint ID--i.e. if bit 1 is set in the endpoint
 * bitmap, endpoint 1 has a filter rule.  Older versions of IPA represent
 * the presence of a filter rule for endpoint X by bit (X + 1) being set.
 * I.e., bit 1 set indicates the presence of a filter rule for endpoint 0,
 * and bit 3 set means there is a filter rule present for endpoint 2.
 *
 * Each filter table entry has the address of a set of equations that
 * implement a filter rule.  So following the endpoint bitmap there
 * will be such an address/entry for each endpoint with a set bit in
 * the bitmap.
 *
 * The AP initializes all entries in a filter table to refer to a "zero"
 * rule.  Once initialized, the modem and AP update the entries for
 * endpoints they "own" directly.  Currently the AP does not use the IPA
 * filtering functionality.
 *
 * This diagram shows an example of a filter table with an endpoint
 * bitmap as defined prior to IPA v5.0.
 *
 *                    IPA Filter Table
 *                 ----------------------
 * endpoint bitmap | 0x0000000000000048 | Bits 3 and 6 set (endpoints 2 and 5)
 *                 |--------------------|
 * 1st endpoint    | 0x000123456789abc0 | DMA address for modem endpoint 2 rule
 *                 |--------------------|
 * 2nd endpoint    | 0x000123456789abf0 | DMA address for AP endpoint 5 rule
 *                 |--------------------|
 * (unused)        |                    | (Unused space in filter table)
 *                 |--------------------|
 *                          . . .
 *                 |--------------------|
 * (unused)        |                    | (Unused space in filter table)
 *                 ----------------------
 *
 * The set of available route rules is divided about equally between the AP
 * and modem.  The AP initializes all entries in a route table to refer to
 * a "zero entry".  Once initialized, the modem and AP are responsible for
 * updating their own entries.  All entries in a route table are usable,
 * though the AP currently does not use the IPA routing functionality.
 *
 *                    IPA Route Table
 *                 ----------------------
 * 1st modem route | 0x0001234500001100 | DMA address for first route rule
 *                 |--------------------|
 * 2nd modem route | 0x0001234500001140 | DMA address for second route rule
 *                 |--------------------|
 *                          . . .
 *                 |--------------------|
 * Last modem route| 0x0001234500002280 | DMA address for Nth route rule
 *                 |--------------------|
 * 1st AP route    | 0x0001234500001100 | DMA address for route rule (N+1)
 *                 |--------------------|
 * 2nd AP route    | 0x0001234500001140 | DMA address for next route rule
 *                 |--------------------|
 *                          . . .
 *                 |--------------------|
 * Last AP route   | 0x0001234500002280 | DMA address for last route rule
 *                 ----------------------
 */

/* Filter or route rules consist of a set of 32-bit values followed by a
 * 32-bit all-zero rule list terminator.  The "zero rule" is simply an
 * all-zero rule followed by the list terminator.
 */
#define IPA_ZERO_RULE_SIZE(version)	\
	(IPA_IS_64BIT(version) ? 2 * sizeof(__le32) : sizeof(__le32))

/* Check things that can be validated at build time. */
static void ipa_table_validate_build(void)
{
	/* Filter and route tables contain DMA addresses that refer
	 * to filter or route rules.  But the size of a table entry
	 * is 64 bits regardless of what the size of an AP DMA address
	 * is.  A fixed constant defines the size of an entry, and
	 * code in ipa_table_init() uses a pointer to __le64 to
	 * initialize tables.
	 */
	BUILD_BUG_ON(sizeof(dma_addr_t) > sizeof(__le64));
}

static const struct ipa_mem *
ipa_table_mem(struct ipa *ipa, bool filter, bool hashed, bool ipv6)
{
	enum ipa_mem_id mem_id;

	mem_id = filter ? hashed ? ipv6 ? IPA_MEM_V6_FILTER_HASHED
					: IPA_MEM_V4_FILTER_HASHED
				 : ipv6 ? IPA_MEM_V6_FILTER
					: IPA_MEM_V4_FILTER
			: hashed ? ipv6 ? IPA_MEM_V6_ROUTE_HASHED
					: IPA_MEM_V4_ROUTE_HASHED
				 : ipv6 ? IPA_MEM_V6_ROUTE
					: IPA_MEM_V4_ROUTE;

	return ipa_mem_find(ipa, mem_id);
}

/* Return true if hashed tables are supported */
bool ipa_table_hash_support(struct ipa *ipa)
{
	return ipa->version != IPA_VERSION_4_2 && ipa->version > IPA_VERSION_2_6L;
}

bool ipa_filtered_valid(struct ipa *ipa, u64 filtered)
{
	struct device *dev = ipa->dev;
	u32 count;

	if (!filtered) {
		dev_err(dev, "at least one filtering endpoint is required\n");

		return false;
	}

	count = hweight64(filtered);
	if (count > ipa->filter_count) {
		dev_err(dev, "too many filtering endpoints (%u > %u)\n",
			count, ipa->filter_count);

		return false;
	}

	return true;
}

/* Zero entry count means no table, so just return a 0 address */
static dma_addr_t ipa_table_addr(struct ipa *ipa, bool filter_mask, u16 count)
{
	u32 skip;

	if (!count)
		return 0;

	/* filter_count excludes the filter map entry at the start */
	WARN_ON(count > max_t(u32, ipa->filter_count + 1, ipa->route_count));

	/* Skip over the zero rule and possibly the filter mask */
	skip = filter_mask ? 1 : 2;

	return ipa->table_addr + skip * IPA_TABLE_ENTRY_SIZE(ipa->version);
}

static void ipa_table_reset_add(struct ipa_trans *trans, bool filter,
				bool hashed, bool ipv6, u16 first, u16 count)
{
	struct ipa *ipa = container_of(trans->dma_subsys, struct ipa, dma_subsys);
	const struct ipa_mem *mem;
	dma_addr_t addr;
	u32 offset;
	u16 size;

	/* Nothing to do if the memory region is doesn't exist or is empty */
	mem = ipa_table_mem(ipa, filter, hashed, ipv6);
	if (!mem || !mem->size)
		return;

	if (filter)
		first++;	/* skip over bitmap */

	offset = mem->offset + first * IPA_TABLE_ENTRY_SIZE(ipa->version);
	size = count * IPA_TABLE_ENTRY_SIZE(ipa->version);
	addr = ipa_table_addr(ipa, false, count);

	ipa_cmd_dma_shared_mem_add(trans, offset, size, addr, true);
}

/* Reset entries in a single filter table belonging to either the AP or
 * modem to refer to the zero entry.  The memory region supplied will be
 * for the IPv4 and IPv6 non-hashed and hashed filter tables.
 */
static int
ipa_filter_reset_table(struct ipa *ipa, bool hashed, bool ipv6, bool modem)
{
	enum gsi_ee_id ee_id = modem ? GSI_EE_MODEM : GSI_EE_AP;
	u32 channel_id = ipa->name_map[IPA_ENDPOINT_AP_COMMAND_TX]->channel_id;
	u32 tre_max = min_t(u32,
			    ipa->dma_subsys.channel[channel_id].trans_tre_max,
			    IPA_COMMAND_TRANS_TRE_MAX);
	struct ipa_trans *trans = NULL;
	u64 ep_mask = ipa->filtered;
	u32 used = 0;

	/* A transaction can hold at most trans_tre_max commands, which can
	 * be fewer than the number of filtered endpoints, so reset the
	 * table in batches of that size.
	 */
	while (ep_mask) {
		u32 endpoint_id = __ffs(ep_mask);

		ep_mask ^= BIT(endpoint_id);

		if (ipa->endpoint[endpoint_id].ee_id != ee_id)
			continue;

		if (!trans) {
			trans = ipa_cmd_trans_alloc(ipa, tre_max);
			if (!trans) {
				dev_err(ipa->dev,
					"no transaction for %s filter reset\n",
					modem ? "modem" : "AP");
				return -EBUSY;
			}
			used = 0;
		}

		ipa_table_reset_add(trans, true, hashed, ipv6, endpoint_id, 1);

		if (++used == tre_max) {
			ipa_trans_commit_wait(trans);
			trans = NULL;
		}
	}

	if (trans)
		ipa_trans_commit_wait(trans);

	return 0;
}

/* Theoretically, each filter table could have more filter slots to
 * update than the maximum number of commands in a transaction.  So
 * we do each table separately.
 */
static int ipa_filter_reset(struct ipa *ipa, bool modem)
{
	int ret;

	ret = ipa_filter_reset_table(ipa, false, false, modem);
	if (ret)
		return ret;

	ret = ipa_filter_reset_table(ipa, false, true, modem);
	if (ret || !ipa_table_hash_support(ipa))
		return ret;

	ret = ipa_filter_reset_table(ipa, true, false, modem);
	if (ret)
		return ret;

	return ipa_filter_reset_table(ipa, true, true, modem);
}

/* The AP routes and modem routes are each contiguous within the
 * table.  We can update each table with a single command, and we
 * won't exceed the per-transaction command limit.
 * */
static int ipa_route_reset(struct ipa *ipa, bool modem)
{
	bool hash_support = ipa_table_hash_support(ipa);
	u32 modem_route_count = ipa->modem_route_count;
	struct ipa_trans *trans;
	u16 first;
	u16 count;

	trans = ipa_cmd_trans_alloc(ipa, hash_support ? 4 : 2);
	if (!trans) {
		dev_err(ipa->dev, "no transaction for %s route reset\n",
			modem ? "modem" : "AP");
		return -EBUSY;
	}

	if (modem) {
		first = 0;
		count = modem_route_count;
	} else {
		first = modem_route_count;
		count = ipa->route_count - modem_route_count;
	}

	ipa_table_reset_add(trans, false, false, false, first, count);
	ipa_table_reset_add(trans, false, false, true, first, count);

	if (hash_support) {
		ipa_table_reset_add(trans, false, true, false, first, count);
		ipa_table_reset_add(trans, false, true, true, first, count);
	}

	ipa_trans_commit_wait(trans);

	return 0;
}

void ipa_table_reset(struct ipa *ipa, bool modem)
{
	struct device *dev = ipa->dev;
	const char *ee_name;
	int ret;

	ee_name = modem ? "modem" : "AP";

	/* Report errors, but reset filter and route tables */
	ret = ipa_filter_reset(ipa, modem);
	if (ret)
		dev_err(dev, "error %d resetting filter table for %s\n",
				ret, ee_name);

	ret = ipa_route_reset(ipa, modem);
	if (ret)
		dev_err(dev, "error %d resetting route table for %s\n",
				ret, ee_name);
}

int ipa_table_hash_flush(struct ipa *ipa)
{
	struct ipa_trans *trans;
	const struct reg *reg;
	u32 val;

	if (!ipa_table_hash_support(ipa))
		return 0;

	trans = ipa_cmd_trans_alloc(ipa, 1);
	if (!trans) {
		dev_err(ipa->dev, "no transaction for hash flush\n");
		return -EBUSY;
	}

	if (ipa->version < IPA_VERSION_5_0) {
		reg = ipa_reg(ipa, FILT_ROUT_HASH_FLUSH);

		val = reg_bit(reg, IPV6_ROUTER_HASH);
		val |= reg_bit(reg, IPV6_FILTER_HASH);
		val |= reg_bit(reg, IPV4_ROUTER_HASH);
		val |= reg_bit(reg, IPV4_FILTER_HASH);
	} else {
		reg = ipa_reg(ipa, FILT_ROUT_CACHE_FLUSH);

		/* IPA v5.0+ uses a unified cache (both IPv4 and IPv6) */
		val = reg_bit(reg, ROUTER_CACHE);
		val |= reg_bit(reg, FILTER_CACHE);
	}

	ipa_cmd_register_write_add(trans, reg_offset(reg), val, val, false);

	ipa_trans_commit_wait(trans);

	return 0;
}

static void ipa_table_init_add(struct ipa_trans *trans, bool filter, bool ipv6)
{
	struct ipa *ipa = container_of(trans->dma_subsys, struct ipa, dma_subsys);
	const struct ipa_mem *hash_mem;
	enum ipa_cmd_opcode opcode;
	const struct ipa_mem *mem;
	dma_addr_t hash_addr;
	dma_addr_t addr;
	u32 hash_offset;
	u32 zero_offset;
	u16 hash_count;
	u32 zero_size;
	u16 hash_size;
	u16 count;
	u16 size;

	opcode = filter ? ipv6 ? IPA_CMD_IP_V6_FILTER_INIT
			       : IPA_CMD_IP_V4_FILTER_INIT
			: ipv6 ? IPA_CMD_IP_V6_ROUTING_INIT
			       : IPA_CMD_IP_V4_ROUTING_INIT;

	/* The non-hashed region will exist (see ipa_table_mem_valid()) */
	mem = ipa_table_mem(ipa, filter, false, ipv6);
	hash_mem = ipa_table_mem(ipa, filter, true, ipv6);
	hash_offset = hash_mem ? hash_mem->offset : 0;

	/* Compute the number of table entries to initialize */
	if (filter) {
		/* The number of filtering endpoints determines number of
		 * entries in the filter table; we also add one more "slot"
		 * to hold the bitmap itself.  The size of the hashed filter
		 * table is either the same as the non-hashed one, or zero.
		 */
		count = 1 + hweight64(ipa->filtered);
		hash_count = hash_mem && hash_mem->size ? count : 0;
	} else {
		/* The size of a route table region determines the number
		 * of entries it has.
		 */
		count = mem->size / IPA_TABLE_ENTRY_SIZE(ipa->version);
		hash_count = hash_mem ? hash_mem->size / IPA_TABLE_ENTRY_SIZE(ipa->version) : 0;
	}
	size = count * IPA_TABLE_ENTRY_SIZE(ipa->version);
	hash_size = hash_count * IPA_TABLE_ENTRY_SIZE(ipa->version);

	addr = ipa_table_addr(ipa, filter, count);
	if (filter && ipa->filter_table_virt) {
		addr = ipa->filter_table_addr;
		if (ipv6 && ipa->filter_table_virt_v6)
			addr = ipa->filter_table_addr_v6;
	}
	if (!filter && ipa->route_table_virt)
		addr = ipa->route_table_addr;
	hash_addr = ipa_table_addr(ipa, filter, hash_count);

	ipa_cmd_table_init_add(trans, opcode, size, mem->offset, addr,
			       hash_size, hash_offset, hash_addr);
	if (!filter)
		return;

	/* Zero the unused space in the filter table */
	zero_offset = mem->offset + size;
	zero_size = mem->size - size;
	if (zero_size)
		ipa_cmd_dma_shared_mem_add(trans, zero_offset, zero_size,
					   ipa->zero_addr, true);
	if (!hash_size)
		return;

	/* Zero the unused space in the hashed filter table */
	zero_offset = hash_offset + hash_size;
	zero_size = hash_mem->size - hash_size;
	if (zero_size)
		ipa_cmd_dma_shared_mem_add(trans, zero_offset, zero_size,
					   ipa->zero_addr, true);
}

int ipa_table_setup(struct ipa *ipa)
{
	struct ipa_trans *trans;

	/* We will need at most 8 TREs:
	 * - IPv4:
	 *     - One for route table initialization (non-hashed and hashed)
	 *     - One for filter table initialization (non-hashed and hashed)
	 *     - One to zero unused entries in the non-hashed filter table
	 *     - One to zero unused entries in the hashed filter table
	 * - IPv6:
	 *     - One for route table initialization (non-hashed and hashed)
	 *     - One for filter table initialization (non-hashed and hashed)
	 *     - One to zero unused entries in the non-hashed filter table
	 *     - One to zero unused entries in the hashed filter table
	 * All platforms support at least 8 TREs in a transaction.
	 */
	trans = ipa_cmd_trans_alloc(ipa, 8);
	if (!trans) {
		dev_err(ipa->dev, "no transaction for table setup\n");
		return -EBUSY;
	}

	ipa_table_init_add(trans, false, false);
	ipa_table_init_add(trans, false, true);
	ipa_table_init_add(trans, true, false);
	ipa_table_init_add(trans, true, true);

	ipa_trans_commit_wait(trans);

	if (ipa->version <= IPA_VERSION_2_6L)
		ipa_ap_route_setup(ipa);

	return 0;
}

/**
 * ipa_filter_tuple_zero() - Zero an endpoint's hashed filter tuple
 * @endpoint:	Endpoint whose filter hash tuple should be zeroed
 *
 * Endpoint must be for the AP (not modem) and support filtering. Updates
 * the filter hash values without changing route ones.
 */
static void ipa_filter_tuple_zero(struct ipa_endpoint *endpoint)
{
	u32 endpoint_id = endpoint->endpoint_id;
	struct ipa *ipa = endpoint->ipa;
	const struct reg *reg;
	u32 offset;
	u32 val;

	if (ipa->version < IPA_VERSION_5_0) {
		reg = ipa_reg(ipa, ENDP_FILTER_ROUTER_HSH_CFG);

		offset = reg_n_offset(reg, endpoint_id);
		val = ioread32(endpoint->ipa->reg_virt + offset);

		/* Zero all filter-related fields, preserving the rest */
		val &= ~reg_fmask(reg, FILTER_HASH_MSK_ALL);
	} else {
		/* IPA v5.0 separates filter and router cache configuration */
		reg = ipa_reg(ipa, ENDP_FILTER_CACHE_CFG);
		offset = reg_n_offset(reg, endpoint_id);

		/* Zero all filter-related fields */
		val = 0;
	}

	iowrite32(val, endpoint->ipa->reg_virt + offset);
}

/* Configure a hashed filter table; there is no ipa_filter_deconfig() */
static void ipa_filter_config(struct ipa *ipa, bool modem)
{
	enum gsi_ee_id ee_id = modem ? GSI_EE_MODEM : GSI_EE_AP;
	u64 ep_mask = ipa->filtered;

	if (!ipa_table_hash_support(ipa))
		return;

	while (ep_mask) {
		u32 endpoint_id = __ffs(ep_mask);
		struct ipa_endpoint *endpoint;

		ep_mask ^= BIT(endpoint_id);

		endpoint = &ipa->endpoint[endpoint_id];
		if (endpoint->ee_id == ee_id)
			ipa_filter_tuple_zero(endpoint);
	}
}

static bool ipa_route_id_modem(struct ipa *ipa, u32 route_id)
{
	return route_id < ipa->modem_route_count;
}

/**
 * ipa_route_tuple_zero() - Zero a hashed route table entry tuple
 * @ipa:	IPA pointer
 * @route_id:	Route table entry whose hash tuple should be zeroed
 *
 * Updates the route hash values without changing filter ones.
 */
static void ipa_route_tuple_zero(struct ipa *ipa, u32 route_id)
{
	const struct reg *reg;
	u32 offset;
	u32 val;

	if (ipa->version < IPA_VERSION_5_0) {
		reg = ipa_reg(ipa, ENDP_FILTER_ROUTER_HSH_CFG);
		offset = reg_n_offset(reg, route_id);

		val = ioread32(ipa->reg_virt + offset);

		/* Zero all route-related fields, preserving the rest */
		val &= ~reg_fmask(reg, ROUTER_HASH_MSK_ALL);
	} else {
		/* IPA v5.0 separates filter and router cache configuration */
		reg = ipa_reg(ipa, ENDP_ROUTER_CACHE_CFG);
		offset = reg_n_offset(reg, route_id);

		/* Zero all route-related fields */
		val = 0;
	}

	iowrite32(val, ipa->reg_virt + offset);
}

/* Configure a hashed route table; there is no ipa_route_deconfig() */
static void ipa_route_config(struct ipa *ipa, bool modem)
{
	u32 route_id;

	if (!ipa_table_hash_support(ipa))
		return;

	for (route_id = 0; route_id < ipa->route_count; route_id++)
		if (ipa_route_id_modem(ipa, route_id) == modem)
			ipa_route_tuple_zero(ipa, route_id);
}

/* Configure a filter and route tables; there is no ipa_table_deconfig() */
void ipa_table_config(struct ipa *ipa)
{
	ipa_filter_config(ipa, false);
	ipa_filter_config(ipa, true);
	ipa_route_config(ipa, false);
	ipa_route_config(ipa, true);
}

/* Verify the sizes of all IPA table filter or routing table memory regions
 * are valid.  If valid, this records the size of the routing table.
 */
bool ipa_table_mem_valid(struct ipa *ipa, bool filter)
{
	bool hash_support = ipa_table_hash_support(ipa);
	const struct ipa_mem *mem_hashed;
	const struct ipa_mem *mem_ipv4;
	const struct ipa_mem *mem_ipv6;
	u32 count;

	/* IPv4 and IPv6 non-hashed tables are expected to be defined and
	 * have the same size.  Both must have at least two entries (and
	 * would normally have more than that).
	 */
	mem_ipv4 = ipa_table_mem(ipa, filter, false, false);
	if (!mem_ipv4)
		return false;

	mem_ipv6 = ipa_table_mem(ipa, filter, false, true);
	if (!mem_ipv6)
		return false;

	if (mem_ipv4->size != mem_ipv6->size)
		return false;

	/* Compute and record the number of entries for each table type */
	count = mem_ipv4->size / IPA_TABLE_ENTRY_SIZE(ipa->version);
	if (count < 2)
		return false;
	if (filter)
		ipa->filter_count = count - 1;	/* Filter map in first entry */
	else
		ipa->route_count = count;

	/* Table offset and size must fit in TABLE_INIT command fields */
	if (!ipa_cmd_table_init_valid(ipa, mem_ipv4, !filter))
		return false;

	/* Make sure the regions are big enough */
	if (filter) {
		/* Filter tables must able to hold the endpoint bitmap plus
		 * an entry for each endpoint that supports filtering
		 */
		if (count < 1 + hweight64(ipa->filtered))
			return false;
	} else {
		/* Routing tables must be able to hold all modem entries,
		 * plus at least one entry for the AP.
		 */
		if (count < ipa->modem_route_count + 1)
			return false;
	}

	/* If hashing is supported, hashed tables are expected to be defined,
	 * and have the same size as non-hashed tables.  If hashing is not
	 * supported, hashed tables are expected to have zero size (or not
	 * be defined).
	 */
	mem_hashed = ipa_table_mem(ipa, filter, true, false);
	if (hash_support) {
		if (!mem_hashed || mem_hashed->size != mem_ipv4->size)
			return false;
	} else {
		if (mem_hashed && mem_hashed->size)
			return false;
	}

	/* Same check for IPv6 tables */
	mem_hashed = ipa_table_mem(ipa, filter, true, true);
	if (hash_support) {
		if (!mem_hashed || mem_hashed->size != mem_ipv6->size)
			return false;
	} else {
		if (mem_hashed && mem_hashed->size)
			return false;
	}

	return true;
}

/* Initialize a coherent DMA allocation containing initialized filter and
 * route table data.  This is used when initializing or resetting the IPA
 * filter or route table.
 *
 * The first entry in a filter table contains a bitmap indicating which
 * endpoints contain entries in the table.  In addition to that first entry,
 * there is a fixed maximum number of entries that follow.  Filter table
 * entries are 64 bits wide, and (other than the bitmap) contain the DMA
 * address of a filter rule.  A "zero rule" indicates no filtering, and
 * consists of 64 bits of zeroes.  When a filter table is initialized (or
 * reset) its entries are made to refer to the zero rule.
 *
 * Each entry in a route table is the DMA address of a routing rule.  For
 * routing there is also a 64-bit "zero rule" that means no routing, and
 * when a route table is initialized or reset, its entries are made to refer
 * to the zero rule.  The zero rule is shared for route and filter tables.
 *
 *	     +-------------------+
 *	 --> |     zero rule     |
 *	/    |-------------------|
 *	|    |     filter mask   |
 *	|\   |-------------------|
 *	| ---- zero rule address | \
 *	|\   |-------------------|  |
 *	| ---- zero rule address |  |	Max IPA filter count
 *	|    |-------------------|   >	or IPA route count,
 *	|	      ...	    |	whichever is greater
 *	 \   |-------------------|  |
 *	  ---- zero rule address | /
 *	     +-------------------+
 */

/* Write a value to the next entry in a table, and return the advanced
 * pointer.  Table entries are 64 bits wide on IPA v3.0+ and 32 bits wide
 * on IPA v2.x.
 */
static void *ipa_table_write(enum ipa_version version, void *virt, u64 value)
{
	if (IPA_IS_64BIT(version)) {
		__le64 *ptr = virt;

		*ptr = cpu_to_le64(value);
	} else {
		__le32 *ptr = virt;

		*ptr = cpu_to_le32(value);
	}

	return virt + IPA_TABLE_ENTRY_SIZE(version);
}

#define IPA_AP_RULE_SIZE		(sizeof(__le32) * 5)
#define IPA_AP_FILTER_RULE_SIZE		20
#define IPA_V2_ENDP_INIT_ROUTE_OFFSET	0x370

/* Install the match-all route used by the AP->modem TX endpoint.
 *
 * The route table is split between the modem (low entries) and the AP
 * (high entries); the split point is ipa->modem_route_count.  The
 * hardware hashes a packet into the AP's contiguous range, so every
 * entry in that range must refer to a rule (a single entry is not
 * enough), and the endpoint must be told where the range starts.
 */
static void ipa_ap_route_setup(struct ipa *ipa)
{
	const struct ipa_mem *mem;

	if (ipa->version > IPA_VERSION_2_6L)
		return;
	struct ipa_trans *trans;
	u32 endpoint_id;
	u32 first;
	u32 count;
	u32 offset;
	u32 i;

	if (!ipa->route_table_virt || !ipa->ap_route_rule_virt)
		return;

	first = ipa->modem_route_count;
	count = ipa->route_count - first;
	if (!count)
		return;

	/* v2.x hashes every packet over the whole route table, not just the
	 * AP's share, so a hash into the modem's range (entries 0..first-1)
	 * must also resolve to a valid rule.  Fill the entire table with the
	 * match-all rule; the modem later overwrites its own entries in the
	 * shared SRAM with its per-flow rules, leaving any entries it does
	 * not touch pointing at the AP match-all -> Q6_LAN_CONS.
	 */
	for (i = 0; i < ipa->route_count; i++)
		((__le32 *)ipa->route_table_virt)[i] =
			cpu_to_le32((u32)ipa->ap_route_rule_addr);

	trans = ipa_cmd_trans_alloc(ipa, 2);
	if (!trans) {
		dev_err(ipa->dev, "no transaction for AP route table\n");
		return;
	}

	mem = ipa_mem_find(ipa, IPA_MEM_V4_ROUTE);
	ipa_cmd_table_init_add(trans, IPA_CMD_IP_V4_ROUTING_INIT,
			       mem->size, mem->offset,
			       ipa->route_table_addr, 0, 0, 0);

	mem = ipa_mem_find(ipa, IPA_MEM_V6_ROUTE);
	ipa_cmd_table_init_add(trans, IPA_CMD_IP_V6_ROUTING_INIT,
			       mem->size, mem->offset,
			       ipa->route_table_addr, 0, 0, 0);

	ipa_trans_commit_wait(trans);

	endpoint_id = ipa->name_map[IPA_ENDPOINT_AP_MODEM_TX]->endpoint_id;
	offset = IPA_V2_ENDP_INIT_ROUTE_OFFSET + endpoint_id * sizeof(u32);
	iowrite32(first, (u8 *)ipa->reg_virt + offset);
}

/* === Legacy IPA v2.x filter rule installation ===
 *
 * A filter rule in IPA v2.x hardware is a 32-bit header followed by the
 * enabled equations, each padded to a 32-bit boundary.  The header holds
 * the equation bitmap (bits 0..15), the action (bits 16..20), and the
 * routing table index (bits 21..25).  Rules in a set are concatenated and
 * the set ends with a zero word.
 *
 * A route rule uses the same equation format; its header instead holds
 * the destination pipe (bits 16..20) and a "system memory" bit (21).
 */
#define IPA_V2_FLT_RULE_MAX_SIZE	256
#define IPA_V2_ROUTE_RULE_SIZE		32

static u8 *ipa_rule_pad(u8 *ptr)
{
	return (u8 *)ALIGN((unsigned long)ptr, sizeof(u32));
}

static u8 *ipa_flt_eq_meq32(u8 *ptr,
			    const struct ipa_ipfltr_mask_eq_32 *eq)
{
	*ptr++ = eq->offset;
	put_unaligned_le32(eq->mask, ptr);
	ptr += sizeof(u32);
	put_unaligned_le32(eq->value, ptr);
	ptr += sizeof(u32);

	return ipa_rule_pad(ptr);
}

/* Write the body of a filter rule, in the order the hardware expects */
static u8 *ipa_flt_rule_write(u8 *buf, const struct ipa_filter_spec *spec,
			      bool retain_hdr)
{
	const struct ipa_filter_rule *rule = &spec->filter_rule;
	u8 *ptr = buf;
	u32 hdr;
	u32 count;
	u32 i;

	hdr = rule->rule_eq_bitmap;
	hdr |= (spec->filter_action & 0x1f) << 16;
	hdr |= (spec->route_table_index & 0x1f) << 21;
	if (retain_hdr)
		hdr |= BIT(26);
	put_unaligned_le32(hdr, ptr);
	ptr += sizeof(u32);

	if (rule->tos_eq_present) {
		*ptr++ = rule->tos_eq;
		ptr = ipa_rule_pad(ptr);
	}
	if (rule->protocol_eq_present) {
		*ptr++ = rule->protocol_eq;
		ptr = ipa_rule_pad(ptr);
	}
	count = min_t(u32, rule->num_offset_meq_32, 2);
	for (i = 0; i < count; i++)
		ptr = ipa_flt_eq_meq32(ptr, &rule->offset_meq_32[i]);

	count = min_t(u32, rule->num_ihl_offset_range_16, 2);
	for (i = 0; i < count; i++) {
		const struct ipa_ipfltr_range_eq_16 *eq;

		eq = &rule->ihl_offset_range_16[i];
		*ptr++ = eq->offset;
		put_unaligned_le16(eq->range_high, ptr);
		ptr += sizeof(u16);
		put_unaligned_le16(eq->range_low, ptr);
		ptr += sizeof(u16);
		ptr = ipa_rule_pad(ptr);
	}
	if (rule->ihl_offset_eq_16_present) {
		*ptr++ = rule->ihl_offset_eq_16.offset;
		put_unaligned_le16(rule->ihl_offset_eq_16.value, ptr);
		ptr += sizeof(u16);
		ptr = ipa_rule_pad(ptr);
	}
	if (rule->ihl_offset_eq_32_present) {
		*ptr++ = rule->ihl_offset_eq_32.offset;
		put_unaligned_le32(rule->ihl_offset_eq_32.value, ptr);
		ptr += sizeof(u32);
		ptr = ipa_rule_pad(ptr);
	}
	if (rule->num_ihl_offset_meq_32)
		ptr = ipa_flt_eq_meq32(ptr, &rule->ihl_offset_meq_32[0]);

	count = min_t(u32, rule->num_offset_meq_128, 2);
	for (i = 0; i < count; i++) {
		const struct ipa_ipfltr_mask_eq_128 *eq;

		eq = &rule->offset_meq_128[i];
		*ptr++ = eq->offset;
		memcpy(ptr, eq->mask, sizeof(eq->mask));
		ptr += sizeof(eq->mask);
		memcpy(ptr, eq->value, sizeof(eq->value));
		ptr += sizeof(eq->value);
		ptr = ipa_rule_pad(ptr);
	}
	if (rule->tc_eq_present) {
		*ptr++ = rule->tc_eq;
		ptr = ipa_rule_pad(ptr);
	}
	if (rule->flow_eq_present) {
		put_unaligned_le32(rule->flow_eq, ptr);
		ptr += sizeof(u32);
		ptr = ipa_rule_pad(ptr);
	}
	if (rule->num_ihl_offset_meq_32 > 1)
		ptr = ipa_flt_eq_meq32(ptr, &rule->ihl_offset_meq_32[1]);
	if (rule->metadata_meq32_present)
		ptr = ipa_flt_eq_meq32(ptr, &rule->metadata_meq32);
	if (rule->ipv4_frag_eq_present)
		ptr = ipa_rule_pad(ptr);

	return ptr;
}

/* Match-all rule: a masked comparison with a zero mask matches anything */
static u8 *ipa_flt_rule_write_match_all(u8 *buf, u32 rt_index)
{
	u8 *ptr = buf;
	u32 hdr;

	hdr = 4;			/* one offset_meq_32 equation */
	hdr |= (rt_index & 0x1f) << 21;
	put_unaligned_le32(hdr, ptr);
	ptr += sizeof(u32);

	*ptr++ = 16;			/* offset (zero mask matches any) */
	ptr = ipa_rule_pad(ptr);
	put_unaligned_le32(0, ptr);	/* mask */
	ptr += sizeof(u32);
	put_unaligned_le32(0, ptr);	/* value */
	ptr += sizeof(u32);

	return ptr;
}

static u8 *ipa_rt_rule_write_match_all(u8 *buf, u32 pipe, bool proc_ctx)
{
	u8 *ptr = buf;
	u32 hdr;

	hdr = 4;			/* one offset_meq_32 equation */
	hdr |= (pipe & 0x1f) << 16;
	hdr |= BIT(21);			/* header/proc-ctx table in system memory */
	if (proc_ctx)
		hdr |= BIT(31);		/* use a header processing context */
	put_unaligned_le32(hdr, ptr);
	ptr += sizeof(u32);

	*ptr++ = 16;
	ptr = ipa_rule_pad(ptr);
	put_unaligned_le32(0, ptr);
	ptr += sizeof(u32);
	put_unaligned_le32(0, ptr);
	ptr += sizeof(u32);

	return ptr;
}

/* Return the filter table slot used for a filtering endpoint */
static u32 ipa_filter_slot(struct ipa *ipa, u32 endpoint_id)
{
	u64 below;

	below = ipa->filtered & (BIT_ULL(endpoint_id) - 1);

	return 1 + hweight64(below);
}

/* Build the system QMAP header table and its processing context.
 *
 * The WAN route toward the AP uses a header handle: a 4-byte QMAP header
 * template with the mux id, inserted through a processing context.  IPA
 * v2.5 keeps both tables in DRAM, so the header table is armed with
 * IPA_CMD_HDR_INIT_SYSTEM and the processing context base is written to
 * the system proc-ctx register.
 */
#define IPA_SYS_PKT_PROC_CNTXT_BASE	0x5d8
#define IPA_QMAP_HDR_LEN		4
#define IPA_PROC_CTX_ALIGN		32

static int ipa_hdr_proc_ctx_setup(struct ipa *ipa, struct ipa_trans *trans)
{
	u8 *hdr;
	u32 *ctx;

	if (ipa->hdr_table_virt)
		return 0;

	ipa->hdr_table_virt = dma_alloc_coherent(ipa->dev, PAGE_SIZE,
						 &ipa->hdr_table_addr,
						 GFP_KERNEL);
	if (!ipa->hdr_table_virt)
		return -ENOMEM;
	ipa->hdr_table_size = PAGE_SIZE;
	memset(ipa->hdr_table_virt, 0, ipa->hdr_table_size);

	/* QMAP header template: flags, mux_id, pkt_len (filled by IPA).
	 * The mux ID must match the rmnet link's mux ID.
	 */
	hdr = ipa->hdr_table_virt;
	hdr[1] = ipa->qmap_mux_id;	/* QMAP mux id */

	ipa->proc_ctx_virt = dma_alloc_coherent(ipa->dev, PAGE_SIZE,
						&ipa->proc_ctx_addr,
						GFP_KERNEL);
	if (!ipa->proc_ctx_virt) {
		dma_free_coherent(ipa->dev, ipa->hdr_table_size,
				  ipa->hdr_table_virt, ipa->hdr_table_addr);
		ipa->hdr_table_virt = NULL;
		return -ENOMEM;
	}
	ipa->proc_ctx_size = PAGE_SIZE;
	memset(ipa->proc_ctx_virt, 0, ipa->proc_ctx_size);

	ctx = ipa->proc_ctx_virt;
	ctx[0] = 1 | (1 << 8) | (IPA_QMAP_HDR_LEN << 16);	/* HDR_ADD */
	ctx[1] = (u32)ipa->hdr_table_addr;
	ctx[2] = 0;						/* END */

	ipa_cmd_hdr_init_system_add(trans, ipa->hdr_table_addr);
	ipa_cmd_register_write_add(trans, IPA_SYS_PKT_PROC_CNTXT_BASE,
				   (u32)ipa->proc_ctx_addr,
				   ~(IPA_PROC_CTX_ALIGN - 1), false);

	return 0;
}

/* Install the filter rules sent by the modem for the AP's uplink pipe,
 * and a match-all rule on the modem's TX pipe so downlink traffic is
 * delivered to the AP.  Route table entry 1 is pointed at a pipe-5 rule;
 * the modem's other route entries are left untouched.
 */

int ipa_table_install_filter_rules(struct ipa *ipa,
				   const struct ipa_install_fltr_rule_req *req)
{
	const struct ipa_filter_spec *spec;
	const struct ipa_mem *mem;
	struct ipa_endpoint *ul_endpoint;
	struct ipa_endpoint *dl_endpoint;
	struct ipa_trans *trans;
	dma_addr_t pipe5_addr;
	void *pipe5_base;
	void *old_virt[2] = { };
	dma_addr_t old_addr[2] = { };
	u32 old_size[2] = { };
	void *dl_old_virt = NULL;
	dma_addr_t dl_old_addr = 0;
	u32 dl_old_size = 0;
	u32 count[2] = { };
	bool any_mux = false;
	u32 ul_slot;
	u32 dl_slot;
	u32 wan_rt;
	u32 nspecs;
	u64 rt_mask;
	u32 i;
	u32 j;
	int ret;

	if (!req || !req->filter_spec_list_valid || !req->filter_spec_list_len)
		return 0;

	if (ipa->version > IPA_VERSION_2_6L ||
	    !ipa->filter_table_virt || !ipa->filter_table_virt_v6 ||
	    !ipa->route_table_virt)
		return -EOPNOTSUPP;
	if (!ipa->name_map[IPA_ENDPOINT_AP_COMMAND_TX])
		return -ENODEV;

	ret = pm_runtime_get_sync(ipa->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(ipa->dev);
		return ret;
	}

	ul_endpoint = ipa->name_map[IPA_ENDPOINT_AP_MODEM_TX];
	dl_endpoint = ipa->name_map[IPA_ENDPOINT_MODEM_LAN_TX];
	if (!ul_endpoint || !dl_endpoint) {
		ret = -ENODEV;
		goto out_power_put;
	}
	ul_slot = ipa_filter_slot(ipa, ul_endpoint->endpoint_id);
	wan_rt = 8;

	dl_slot = ipa_filter_slot(ipa, dl_endpoint->endpoint_id);
	if (ul_slot >= 1 + ipa->filter_count ||
	    dl_slot >= 1 + ipa->filter_count) {
		ret = -EINVAL;
		goto out_power_put;
	}

	nspecs = min_t(u32, req->filter_spec_list_len, IPA_QMI_MAX_FILTERS);
	for (i = 0; i < nspecs; i++) {
		if (req->filter_spec_list[i].is_mux_id_valid) {
			any_mux = true;
			break;
		}
	}

	/* Decode and select the specs to install.  A v2.x modem normally
	 * leaves mux_id invalid, in which case the whole request belongs
	 * to the bearer that triggered it and every spec applies.
	 */
	for (i = 0; i < nspecs; i++) {
		u32 ip_type;

		spec = &req->filter_spec_list[i];

		/* ModemManager's bearer mux is 0 (qmapmux0.0), but the
		 * modem's uplink filter specs have also been seen with the
		 * on-wire mux 1.  Accept either rather than dropping the
		 * uplink rule set entirely.
		 */
		if (any_mux && (!spec->is_mux_id_valid ||
				(spec->mux_id != 0 && spec->mux_id != 1)))
			continue;
		if (!spec->is_routing_table_index_valid ||
		    spec->route_table_index >= ipa->modem_route_count) {
			dev_warn(ipa->dev, "spec %u has no valid route table\n",
				 spec->filter_spec_identifier);
			continue;
		}
		ip_type = spec->ip_type;
		if (ip_type == IPA_QMI_IP_TYPE_V4)
			count[0]++;
		else if (ip_type == IPA_QMI_IP_TYPE_V6)
			count[1]++;
	}

	if (!count[0] && !count[1]) {
		dev_warn(ipa->dev, "no installable filter rules\n");
		ret = 0;
		goto out_power_put;
	}

	/* Allocate replacement rule sets before publishing their addresses */
	for (i = 0; i < 2; i++) {
		old_virt[i] = ipa->modem_flt_rule_virt[i];
		old_addr[i] = ipa->modem_flt_rule_addr[i];
		old_size[i] = ipa->modem_flt_rule_size[i];
	}
	for (i = 0; i < 2; i++) {
		size_t size;

		if (!count[i])
			continue;
		size = (count[i] + 1) * IPA_V2_FLT_RULE_MAX_SIZE +
		       sizeof(u32);
		ipa->modem_flt_rule_virt[i] =
			dma_alloc_coherent(ipa->dev, size,
					   &ipa->modem_flt_rule_addr[i],
					   GFP_KERNEL);
		if (!ipa->modem_flt_rule_virt[i]) {
			ipa->modem_flt_rule_virt[i] = old_virt[i];
			ipa->modem_flt_rule_addr[i] = old_addr[i];
			ipa->modem_flt_rule_size[i] = old_size[i];
			goto err_free;
		}
		ipa->modem_flt_rule_size[i] = size;
		memset(ipa->modem_flt_rule_virt[i], 0, size);
	}

	dl_old_virt = ipa->dl_flt_rule_virt;
	dl_old_addr = ipa->dl_flt_rule_addr;
	dl_old_size = ipa->dl_flt_rule_size;
	ipa->dl_flt_rule_virt = dma_alloc_coherent(ipa->dev,
						   IPA_V2_FLT_RULE_MAX_SIZE,
						   &ipa->dl_flt_rule_addr,
						   GFP_KERNEL);
	if (!ipa->dl_flt_rule_virt) {
		ipa->dl_flt_rule_virt = dl_old_virt;
		ipa->dl_flt_rule_addr = dl_old_addr;
		ipa->dl_flt_rule_size = dl_old_size;
		goto err_free;
	}
	ipa->dl_flt_rule_size = IPA_V2_FLT_RULE_MAX_SIZE;
	memset(ipa->dl_flt_rule_virt, 0, ipa->dl_flt_rule_size);

	if (!ipa->pipe_5_route_rule_virt) {
		u8 *ptr;

		pipe5_base = dma_alloc_coherent(ipa->dev,
						IPA_V2_ROUTE_RULE_SIZE,
						&pipe5_addr, GFP_KERNEL);
		if (!pipe5_base)
			goto err_free;
		memset(pipe5_base, 0, IPA_V2_ROUTE_RULE_SIZE);
		/* Stock uses a plain header-table entry for the WAN route
		 * (proc_ctx = 0, hdr_offset into the system header table),
		 * not a header processing context; follow it.
		 */
		ptr = ipa_rt_rule_write_match_all(pipe5_base, 5, false);
		put_unaligned_le32(0, ptr);
		ipa->pipe_5_route_rule_virt = pipe5_base;
		ipa->pipe_5_route_rule_addr = pipe5_addr;
	}

	/* Uplink rule sets: the modem's rules plus a catch-all that keeps
	 * normal traffic on its original route table (entry 2).
	 */
	for (i = 0; i < 2; i++) {
		u8 *ptr;

		if (!count[i])
			continue;
		ptr = ipa->modem_flt_rule_virt[i];
		for (j = 0; j < nspecs; j++) {
			spec = &req->filter_spec_list[j];
			if (any_mux &&
			    (!spec->is_mux_id_valid ||
			     (spec->mux_id != 0 && spec->mux_id != 1)))
				continue;
			if (!spec->is_routing_table_index_valid ||
			    spec->route_table_index >= ipa->modem_route_count)
				continue;
			if (spec->ip_type != (i ? IPA_QMI_IP_TYPE_V6
						: IPA_QMI_IP_TYPE_V4))
				continue;
			ptr = ipa_flt_rule_write(ptr, spec, true);
		}
		ptr = ipa_flt_rule_write_match_all(ptr, 2);
		put_unaligned_le32(0, ptr);
	}

	/* Downlink rule set for the modem TX pipe: everything to the AP.
	 * The route table is in the AP-owned range (stock uses table 8,
	 * ipa_dflt_wan_rt), not the modem-owned entries 0..6.
	 */
	{
		u8 *ptr = ipa->dl_flt_rule_virt;

		ptr = ipa_flt_rule_write_match_all(ptr, wan_rt);
		put_unaligned_le32(0, ptr);
	}

	trans = ipa_cmd_trans_alloc(ipa, 8);
	if (!trans) {
		dev_err(ipa->dev, "no transaction for filter table update\n");
		ret = -EBUSY;
		goto err_free;
	}

	ret = ipa_hdr_proc_ctx_setup(ipa, trans);
	if (ret) {
		ipa_trans_free(trans);
		goto err_free;
	}

	mem = ipa_mem_find(ipa, IPA_MEM_V4_FILTER);
	if (count[0]) {
		((__le32 *)ipa->filter_table_virt)[ul_slot] =
			cpu_to_le32((u32)ipa->modem_flt_rule_addr[0]);
		ipa_cmd_dma_shared_mem_add(trans,
					   mem->offset + ul_slot * sizeof(u32), sizeof(u32),
					   ipa->filter_table_addr + ul_slot * sizeof(u32), true);
	}
	if (count[1]) {
		((__le32 *)ipa->filter_table_virt_v6)[ul_slot] =
			cpu_to_le32((u32)ipa->modem_flt_rule_addr[1]);
		mem = ipa_mem_find(ipa, IPA_MEM_V6_FILTER);
		ipa_cmd_dma_shared_mem_add(trans,
					   mem->offset + ul_slot * sizeof(u32), sizeof(u32),
					   ipa->filter_table_addr_v6 + ul_slot * sizeof(u32), true);
	}

	mem = ipa_mem_find(ipa, IPA_MEM_V4_FILTER);
	((__le32 *)ipa->filter_table_virt)[dl_slot] =
		cpu_to_le32((u32)ipa->dl_flt_rule_addr);
	ipa_cmd_dma_shared_mem_add(trans,
				   mem->offset + dl_slot * sizeof(u32), sizeof(u32),
				   ipa->filter_table_addr + dl_slot * sizeof(u32), true);

	mem = ipa_mem_find(ipa, IPA_MEM_V6_FILTER);
	((__le32 *)ipa->filter_table_virt_v6)[dl_slot] =
		cpu_to_le32((u32)ipa->dl_flt_rule_addr);
	ipa_cmd_dma_shared_mem_add(trans,
				   mem->offset + dl_slot * sizeof(u32), sizeof(u32),
				   ipa->filter_table_addr_v6 + dl_slot * sizeof(u32), true);

	ipa_trans_commit_wait(trans);

	/* Point the selected modem route entries at the pipe-5 rule.
	 * The other modem entries keep whatever the modem installed.
	 */
	rt_mask = BIT(wan_rt);
	while (rt_mask) {
		u32 batch[4];
		u32 n = 0;
		u32 rt;

		while (rt_mask && n < ARRAY_SIZE(batch)) {
			rt = __ffs(rt_mask);
			rt_mask &= ~BIT(rt);
			batch[n++] = rt;
		}
		trans = ipa_cmd_trans_alloc(ipa, 2 * n);
		if (!trans) {
			dev_err(ipa->dev,
				"no transaction for route table update\n");
			ret = -EBUSY;
			goto out_power_put;
		}
		for (i = 0; i < n; i++) {
			rt = batch[i];
			((__le32 *)ipa->route_table_virt)[rt] =
				cpu_to_le32((u32)ipa->pipe_5_route_rule_addr);
			mem = ipa_mem_find(ipa, IPA_MEM_V4_ROUTE);
			ipa_cmd_dma_shared_mem_add(trans,
						   mem->offset + rt * sizeof(u32), sizeof(u32),
						   ipa->route_table_addr + rt * sizeof(u32),
						   true);
			mem = ipa_mem_find(ipa, IPA_MEM_V6_ROUTE);
			ipa_cmd_dma_shared_mem_add(trans,
						   mem->offset + rt * sizeof(u32), sizeof(u32),
						   ipa->route_table_addr + rt * sizeof(u32),
						   true);
		}
		ipa_trans_commit_wait(trans);
	}

	for (i = 0; i < 2; i++) {
		if (count[i] && old_virt[i])
			dma_free_coherent(ipa->dev, old_size[i], old_virt[i],
					  old_addr[i]);
	}
	if (dl_old_virt)
		dma_free_coherent(ipa->dev, dl_old_size, dl_old_virt,
				  dl_old_addr);

	ret = 0;
	goto out_power_put;

err_free:
	for (i = 0; i < 2; i++) {
		if (!count[i] || ipa->modem_flt_rule_virt[i] == old_virt[i])
			continue;
		dma_free_coherent(ipa->dev, ipa->modem_flt_rule_size[i],
				  ipa->modem_flt_rule_virt[i],
				  ipa->modem_flt_rule_addr[i]);
		ipa->modem_flt_rule_virt[i] = old_virt[i];
		ipa->modem_flt_rule_addr[i] = old_addr[i];
		ipa->modem_flt_rule_size[i] = old_size[i];
	}
	if (ipa->dl_flt_rule_virt != dl_old_virt) {
		dma_free_coherent(ipa->dev, ipa->dl_flt_rule_size,
				  ipa->dl_flt_rule_virt,
				  ipa->dl_flt_rule_addr);
		ipa->dl_flt_rule_virt = dl_old_virt;
		ipa->dl_flt_rule_addr = dl_old_addr;
		ipa->dl_flt_rule_size = dl_old_size;
	}
	(void)pm_runtime_put_autosuspend(ipa->dev);

	return ret;

out_power_put:
	(void)pm_runtime_put_autosuspend(ipa->dev);

	return ret;
}

int ipa_table_init(struct ipa *ipa)
{
	struct device *dev = ipa->dev;
	enum ipa_version version = ipa->version;
	u64 filtered = ipa->filtered;
	dma_addr_t addr;
	dma_addr_t filter_addr;
	dma_addr_t filter_rule_addr;
	dma_addr_t route_addr;
	dma_addr_t rule_addr;
	void *virt;
	void *filter_virt;
	void *filter_virt_v6;
	void *filter_rule_virt;
	void *route_virt;
	void *rule_virt;
	dma_addr_t filter_addr_v6;
	size_t size;
	size_t filter_size;
	size_t route_size;
	u32 count;
	u32 i;
	u32 val;

	ipa_table_validate_build();

	count = max_t(u32, ipa->filter_count, ipa->route_count);

	/* The IPA hardware requires route and filter table rules to be
	 * aligned on a 128-byte boundary.  We put the "zero rule" at the
	 * base of the table area allocated here.  The DMA address returned
	 * by dma_alloc_coherent() is guaranteed to be a power-of-2 number
	 * of pages, which satisfies the rule alignment requirement.
	 */
	size = IPA_ZERO_RULE_SIZE(version) +
	       (1 + count) * IPA_TABLE_ENTRY_SIZE(version);
	virt = dma_alloc_coherent(dev, size, &addr, GFP_KERNEL);
	if (!virt)
		return -ENOMEM;

	ipa->table_virt = virt;
	ipa->table_addr = addr;

	/* First slot is the zero rule */
	virt = ipa_table_write(version, virt, 0);

	/* Next is the filter table bitmap.  The "soft" bitmap value might
	 * need to be converted to the hardware representation by shifting
	 * it left one position.  Prior to IPA v5.0, bit 0 repesents global
	 * filtering, which is possible but not used.  IPA v5.0+ eliminated
	 * that option, so there's no shifting required.
	 */
	if (version < IPA_VERSION_5_0)
		filtered = filtered << 1;
	virt = ipa_table_write(version, virt, filtered);

	/* All the rest contain the DMA address of the zero rule */
	while (count--)
		virt = ipa_table_write(version, virt, addr);

	if (version > IPA_VERSION_2_6L)
		return 0;

	/* IPA v2.x route tables are installed from system memory.  Keep a
	 * separate route table buffer so filter and route entries cannot
	 * overlap, and so the route table can be re-installed as a whole.
	 */
	route_size = ipa->route_count * IPA_TABLE_ENTRY_SIZE(version);
	route_virt = dma_alloc_coherent(dev, route_size, &route_addr,
					GFP_KERNEL);
	if (!route_virt) {
		dma_free_coherent(dev, size, ipa->table_virt, ipa->table_addr);
		ipa->table_addr = 0;
		ipa->table_virt = NULL;
		return -ENOMEM;
	}
	for (i = 0; i < ipa->route_count; i++)
		((__le32 *)route_virt)[i] = cpu_to_le32((u32)addr);

	rule_virt = dma_alloc_coherent(dev, IPA_AP_RULE_SIZE, &rule_addr,
				       GFP_KERNEL);
	if (!rule_virt) {
		dma_free_coherent(dev, route_size, route_virt, route_addr);
		dma_free_coherent(dev, size, ipa->table_virt, ipa->table_addr);
		ipa->table_addr = 0;
		ipa->table_virt = NULL;
		return -ENOMEM;
	}

	/* Route rule: match-all, destination modem LAN RX pipe (Q6_LAN_CONS,
	 * endpoint 8).  The modem's own UL route rules (route table entries
	 * 1/2) also target Q6_LAN_CONS, so the AP default must match.
	 *
	 * The hardware does not treat an equation-less rule as a match-all,
	 * so use one meq32 equation with a zero mask; the offset selects
	 * the IPv4 destination address but the zero mask makes it match
	 * any packet (and any IP version).
	 */
	val = ipa->name_map[IPA_ENDPOINT_MODEM_LAN_RX]->endpoint_id << 16;
	val |= 1 << 21;			/* rule resides in system memory */
	val |= 4;			/* IPA_OFFSET_MEQ32_0 */
	((__le32 *)rule_virt)[0] = cpu_to_le32(val);
	((__le32 *)rule_virt)[1] = cpu_to_le32(16);	/* IPv4 dst offset */
	((__le32 *)rule_virt)[2] = 0;			/* mask: match any */
	((__le32 *)rule_virt)[3] = 0;			/* value */
	((__le32 *)rule_virt)[4] = 0;			/* rule terminator */

	ipa->route_table_addr = route_addr;
	ipa->route_table_virt = route_virt;
	ipa->route_table_size = route_size;
	ipa->ap_route_rule_addr = rule_addr;
	ipa->ap_route_rule_virt = rule_virt;

	/* AP filter rule: default match-all, pass to routing, route to the
	 * AP's share of the route table.
	 */
	filter_rule_virt = dma_alloc_coherent(dev, IPA_AP_FILTER_RULE_SIZE,
					      &filter_rule_addr, GFP_KERNEL);
	if (!filter_rule_virt) {
		dma_free_coherent(dev, IPA_AP_RULE_SIZE, rule_virt, rule_addr);
		dma_free_coherent(dev, route_size, route_virt, route_addr);
		dma_free_coherent(dev, size, ipa->table_virt, ipa->table_addr);
		ipa->table_addr = 0;
		ipa->table_virt = NULL;
		return -ENOMEM;
	}
	/* Match-all IPv4 filter rule using meq32 at offset 12 with a zero
	 * mask/value pair, then pass to routing with the AP route table.
	 */
	val = 4;					/* IPA_OFFSET_MEQ32_0 */
	val |= ipa->modem_route_count << 21;
	((__le32 *)filter_rule_virt)[0] = cpu_to_le32(val);
	((u8 *)filter_rule_virt)[4] = 12;		/* IPv4 source address */
	((u8 *)filter_rule_virt)[16] = 0;		/* rule terminator */

	/* Filter table: bitmap plus one entry per filtered endpoint. */
	filter_size = (1 + ipa->filter_count) *
		      IPA_TABLE_ENTRY_SIZE(version);
	filter_virt = dma_alloc_coherent(dev, filter_size, &filter_addr,
					 GFP_KERNEL);
	if (!filter_virt) {
		dma_free_coherent(dev, IPA_AP_FILTER_RULE_SIZE, filter_rule_virt,
				  filter_rule_addr);
		dma_free_coherent(dev, IPA_AP_RULE_SIZE, rule_virt, rule_addr);
		dma_free_coherent(dev, route_size, route_virt, route_addr);
		dma_free_coherent(dev, size, ipa->table_virt, ipa->table_addr);
		ipa->table_addr = 0;
		ipa->table_virt = NULL;
		return -ENOMEM;
	}
	val = (u32)ipa->filtered << 1;
	((__le32 *)filter_virt)[0] = cpu_to_le32(val);
	for (i = 1; i < 1 + ipa->filter_count; i++)
		((__le32 *)filter_virt)[i] = cpu_to_le32((u32)addr);

	/* Install the AP match-all filter rule on the AP->modem TX endpoint
	 * so unmatched uplink traffic routes through the AP's share of the
	 * route table (ENDP_INIT_ROUTE) instead of the zero rule.  This is
	 * the effective default when the modem's per-mux METADATA_COMPARE
	 * rules are not installed (QMI 0x23 is ACKed without programming).
	 */
	if (ipa->name_map[IPA_ENDPOINT_AP_MODEM_TX]) {
		u32 ul_slot = ipa_filter_slot(ipa,
			ipa->name_map[IPA_ENDPOINT_AP_MODEM_TX]->endpoint_id);

		((__le32 *)filter_virt)[ul_slot] =
			cpu_to_le32((u32)filter_rule_addr);
	}

	ipa->filter_table_addr = filter_addr;
	ipa->filter_table_virt = filter_virt;
	ipa->filter_table_size = filter_size;

	/* The v4 and v6 filter tables have different rules for the modem's
	 * TX pipe, so keep a separate copy for IPv6.
	 */
	filter_virt_v6 = dma_alloc_coherent(dev, filter_size,
					    &filter_addr_v6, GFP_KERNEL);
	if (!filter_virt_v6) {
		dma_free_coherent(dev, filter_size, filter_virt,
				  ipa->filter_table_addr);
		dma_free_coherent(dev, IPA_AP_FILTER_RULE_SIZE, filter_rule_virt,
				  filter_rule_addr);
		dma_free_coherent(dev, IPA_AP_RULE_SIZE, rule_virt, rule_addr);
		dma_free_coherent(dev, route_size, route_virt, route_addr);
		dma_free_coherent(dev, size, ipa->table_virt, ipa->table_addr);
		ipa->table_addr = 0;
		ipa->table_virt = NULL;
		ipa->filter_table_virt = NULL;
		return -ENOMEM;
	}
	memcpy(filter_virt_v6, filter_virt, filter_size);
	ipa->filter_table_addr_v6 = filter_addr_v6;
	ipa->filter_table_virt_v6 = filter_virt_v6;
	ipa->filter_table_size_v6 = filter_size;

	ipa->ap_filter_rule_addr = filter_rule_addr;
	ipa->ap_filter_rule_virt = filter_rule_virt;

	return 0;
}

void ipa_table_exit(struct ipa *ipa)
{
	u32 count = max_t(u32, 1 + ipa->filter_count, ipa->route_count);
	struct device *dev = ipa->dev;
	size_t size;
	u32 i;

	size = IPA_ZERO_RULE_SIZE(ipa->version) +
	       (1 + count) * IPA_TABLE_ENTRY_SIZE(ipa->version);

	if (ipa->ap_filter_rule_virt) {
		dma_free_coherent(dev, IPA_AP_FILTER_RULE_SIZE,
				  ipa->ap_filter_rule_virt,
				  ipa->ap_filter_rule_addr);
		ipa->ap_filter_rule_virt = NULL;
		ipa->ap_filter_rule_addr = 0;
	}
	if (ipa->filter_table_virt) {
		dma_free_coherent(dev, ipa->filter_table_size,
				  ipa->filter_table_virt,
				  ipa->filter_table_addr);
		ipa->filter_table_virt = NULL;
		ipa->filter_table_addr = 0;
		ipa->filter_table_size = 0;
	}
	if (ipa->filter_table_virt_v6) {
		dma_free_coherent(dev, ipa->filter_table_size_v6,
				  ipa->filter_table_virt_v6,
				  ipa->filter_table_addr_v6);
		ipa->filter_table_virt_v6 = NULL;
		ipa->filter_table_addr_v6 = 0;
		ipa->filter_table_size_v6 = 0;
	}
	for (i = 0; i < ARRAY_SIZE(ipa->modem_flt_rule_virt); i++) {
		if (!ipa->modem_flt_rule_virt[i])
			continue;
		dma_free_coherent(dev, ipa->modem_flt_rule_size[i],
				  ipa->modem_flt_rule_virt[i],
				  ipa->modem_flt_rule_addr[i]);
		ipa->modem_flt_rule_virt[i] = NULL;
		ipa->modem_flt_rule_addr[i] = 0;
		ipa->modem_flt_rule_size[i] = 0;
	}
	if (ipa->dl_flt_rule_virt) {
		dma_free_coherent(dev, ipa->dl_flt_rule_size,
				  ipa->dl_flt_rule_virt,
				  ipa->dl_flt_rule_addr);
		ipa->dl_flt_rule_virt = NULL;
		ipa->dl_flt_rule_addr = 0;
		ipa->dl_flt_rule_size = 0;
	}
	if (ipa->hdr_table_virt) {
		dma_free_coherent(dev, ipa->hdr_table_size,
				  ipa->hdr_table_virt, ipa->hdr_table_addr);
		ipa->hdr_table_virt = NULL;
		ipa->hdr_table_addr = 0;
		ipa->hdr_table_size = 0;
	}
	if (ipa->proc_ctx_virt) {
		dma_free_coherent(dev, ipa->proc_ctx_size,
				  ipa->proc_ctx_virt, ipa->proc_ctx_addr);
		ipa->proc_ctx_virt = NULL;
		ipa->proc_ctx_addr = 0;
		ipa->proc_ctx_size = 0;
	}
	if (ipa->pipe_5_route_rule_virt) {
		dma_free_coherent(dev, IPA_V2_ROUTE_RULE_SIZE,
				  ipa->pipe_5_route_rule_virt,
				  ipa->pipe_5_route_rule_addr);
		ipa->pipe_5_route_rule_virt = NULL;
		ipa->pipe_5_route_rule_addr = 0;
	}
	if (ipa->ap_route_rule_virt) {
		dma_free_coherent(dev, IPA_AP_RULE_SIZE,
				  ipa->ap_route_rule_virt,
				  ipa->ap_route_rule_addr);
		ipa->ap_route_rule_virt = NULL;
		ipa->ap_route_rule_addr = 0;
	}
	if (ipa->route_table_virt) {
		dma_free_coherent(dev, ipa->route_table_size,
				  ipa->route_table_virt,
				  ipa->route_table_addr);
		ipa->route_table_virt = NULL;
		ipa->route_table_addr = 0;
		ipa->route_table_size = 0;
	}

	dma_free_coherent(dev, size, ipa->table_virt, ipa->table_addr);
	ipa->table_addr = 0;
	ipa->table_virt = NULL;
}
