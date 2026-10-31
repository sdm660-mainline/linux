// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2024 Linaro Ltd.
 */

#include <linux/array_size.h>
#include <linux/log2.h>

#include "../ipa_data.h"
#include "../ipa_endpoint.h"
#include "../ipa_mem.h"
#include "../ipa_version.h"

/* Endpoint configuration for the IPA v2.x hardware. */
static const struct ipa_gsi_endpoint_data ipa_gsi_endpoint_data[] = {
	[IPA_ENDPOINT_AP_COMMAND_TX] = {
		.ee_id		= GSI_EE_AP,
		.channel_id	= 3,
		.endpoint_id	= 3,
		.channel_name	= "cmd_tx",
		.toward_ipa	= true,
		.channel = {
			.tre_count	= 256,
			.event_count	= 256,
			.tlv_count	= 20,
		},
		.endpoint = {
			.config = {
				.dma_mode	= true,
				.dma_endpoint	= IPA_ENDPOINT_AP_LAN_RX,
			},
		},
	},
	[IPA_ENDPOINT_AP_LAN_RX] = {
		.ee_id		= GSI_EE_AP,
		.channel_id	= 2,
		.endpoint_id	= 2,
		.channel_name	= "ap_lan_rx",
		.channel = {
			.tre_count	= 256,
			.event_count	= 256,
			.tlv_count	= 8,
		},
		.endpoint	= {
			.config	= {
				.aggregation	= true,
				.status_enable	= true,
				.rx = {
					.buffer_size	= 8192,
					.pad_align	= ilog2(sizeof(u32)),
				},
			},
		},
	},
	[IPA_ENDPOINT_AP_MODEM_TX] = {
		.ee_id		= GSI_EE_AP,
		.channel_id	= 4,
		.endpoint_id	= 4,
		.channel_name	= "ap_modem_tx",
		.toward_ipa	= true,
		.channel = {
			.tre_count	= 256,
			.event_count	= 256,
			.tlv_count	= 8,
		},
		.endpoint	= {
			.config	= {
				.qmap		= true,
				.status_enable	= true,
				.tx = {
					.status_endpoint =
						IPA_ENDPOINT_AP_LAN_RX,
				},
			},
		},
	},
	[IPA_ENDPOINT_AP_MODEM_RX] = {
		.ee_id		= GSI_EE_AP,
		.channel_id	= 5,
		.endpoint_id	= 5,
		.channel_name	= "ap_modem_rx",
		.toward_ipa	= false,
		.channel = {
			.tre_count	= 256,
			.event_count	= 256,
			.tlv_count	= 8,
		},
		.endpoint	= {
			.config = {
				.aggregation	= true,
				.qmap		= true,
				.rx = {
					.buffer_size	= 8192,
				},
			},
		},
	},
	[IPA_ENDPOINT_MODEM_LAN_TX] = {
		.ee_id		= GSI_EE_MODEM,
		.channel_id	= 6,
		.endpoint_id	= 6,
		.toward_ipa	= true,
	},
	[IPA_ENDPOINT_MODEM_COMMAND_TX] = {
		.ee_id		= GSI_EE_MODEM,
		.channel_id	= 7,
		.endpoint_id	= 7,
		.toward_ipa	= true,
	},
	[IPA_ENDPOINT_MODEM_LAN_RX] = {
		.ee_id		= GSI_EE_MODEM,
		.channel_id	= 8,
		.endpoint_id	= 8,
		.toward_ipa	= false,
	},
	[IPA_ENDPOINT_MODEM_AP_RX] = {
		.ee_id		= GSI_EE_MODEM,
		.channel_id	= 9,
		.endpoint_id	= 9,
		.toward_ipa	= false,
	},
};

/* Interconnect bandwidths are in 1000 byte/second units */
static const struct ipa_interconnect_data ipa_interconnect_data[] = {
	{
		.name			= "memory",
		.peak_bandwidth		= 1200000,	/* 1200 MBps */
		.average_bandwidth	= 100000,	/* 100 MBps */
	},
	{
		.name			= "imem",
		.peak_bandwidth		= 350000,	/* 350 MBps */
		.average_bandwidth	= 0,		/* unused */
	},
	{
		.name			= "config",
		.peak_bandwidth		= 40000,	/* 40 MBps */
		.average_bandwidth	= 0,		/* unused */
	},
};

/* Clock and interconnect configuration data for IPA v2.x */
static const struct ipa_power_data ipa_power_data = {
	.core_clock_rate	= 200 * 1000 * 1000,	/* Hz */
	.interconnect_count	= ARRAY_SIZE(ipa_interconnect_data),
	.interconnect_data	= ipa_interconnect_data,
};

/* IPA-resident memory region data for an SoC having IPA v2.5 */
static const struct ipa_mem ipa_mem_local_data_v2_5[] = {
	{
		.id		= IPA_MEM_UC_SHARED,
		.offset		= 0x0000,
		.size		= 0x0080,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_UC_INFO,
		.offset		= 0x0080,
		.size		= 0x0200,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_V4_FILTER,
		.offset		= 0x0288,
		.size		= 0x0058,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V6_FILTER,
		.offset		= 0x02e8,
		.size		= 0x0058,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V4_ROUTE,
		.offset		= 0x0348,
		.size		= 0x003c,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V6_ROUTE,
		.offset		= 0x0388,
		.size		= 0x003c,
		.canary_count	= 1,
	},
	{
		.id		= IPA_MEM_MODEM_HEADER,
		.offset		= 0x03c8,
		.size		= 0x0140,
		.canary_count	= 1,
	},
	{
		.id		= IPA_MEM_MODEM_PROC_CTX,
		.offset		= 0x0510,
		.size		= 0x0200,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_AP_PROC_CTX,
		.offset		= 0x0710,
		.size		= 0x0200,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_MODEM,
		.offset		= 0x0914,
		.size		= 0x16a8,
		.canary_count	= 1,
	},
};

/* Memory configuration data for an SoC having IPA v2.5 */
static const struct ipa_mem_data ipa_mem_data_v2_5 = {
	.local_count	= ARRAY_SIZE(ipa_mem_local_data_v2_5),
	.local		= ipa_mem_local_data_v2_5,
	.smem_size	= 0x00002000,
};

/* Configuration data for an SoC having IPA v2.5 */
const struct ipa_data ipa_data_v2_5 = {
	.version		= IPA_VERSION_2_5,
	.endpoint_count		= ARRAY_SIZE(ipa_gsi_endpoint_data),
	.endpoint_data		= ipa_gsi_endpoint_data,
	.mem_data		= &ipa_mem_data_v2_5,
	.power_data		= &ipa_power_data,
};

/* IPA-resident memory region data for an SoC having IPA v2.6L */
static const struct ipa_mem ipa_mem_local_data_v2_6L[] = {
	{
		.id		= IPA_MEM_UC_SHARED,
		.offset		= 0x0000,
		.size		= 0x0080,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_UC_INFO,
		.offset		= 0x0080,
		.size		= 0x0200,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_V4_FILTER,
		.offset		= 0x0288,
		.size		= 0x0058,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V6_FILTER,
		.offset		= 0x02e8,
		.size		= 0x0058,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V4_ROUTE,
		.offset		= 0x0348,
		.size		= 0x003c,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V6_ROUTE,
		.offset		= 0x0388,
		.size		= 0x003c,
		.canary_count	= 1,
	},
	{
		.id		= IPA_MEM_MODEM_HEADER,
		.offset		= 0x03c8,
		.size		= 0x0140,
		.canary_count	= 1,
	},
	{
		.id		= IPA_MEM_ZIP,
		.offset		= 0x0510,
		.size		= 0x0200,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_MODEM,
		.offset		= 0x0714,
		.size		= 0x18e8,
		.canary_count	= 1,
	},
	{
		.id		= IPA_MEM_END_MARKER,
		.offset		= 0x2000,
		.size		= 0,
		.canary_count	= 1,
	},
};

/* Memory configuration data for an SoC having IPA v2.6L */
static const struct ipa_mem_data ipa_mem_data_v2_6L = {
	.local_count	= ARRAY_SIZE(ipa_mem_local_data_v2_6L),
	.local		= ipa_mem_local_data_v2_6L,
	.smem_size	= 0x00002000,
};

/* Configuration data for an SoC having IPA v2.6L */
const struct ipa_data ipa_data_v2_6L = {
	.version		= IPA_VERSION_2_6L,
	/* Unfortunately we don't know what this BCR value corresponds to */
	.backward_compat	= 0x1fff7f,
	.endpoint_count		= ARRAY_SIZE(ipa_gsi_endpoint_data),
	.endpoint_data		= ipa_gsi_endpoint_data,
	.mem_data		= &ipa_mem_data_v2_6L,
	.power_data		= &ipa_power_data,
};
