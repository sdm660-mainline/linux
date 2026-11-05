/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2024 Linaro Ltd.
 */
#ifndef _IPA_TABLE_H_
#define _IPA_TABLE_H_

#include <linux/types.h>

#include "ipa_version.h"

struct ipa;
struct ipa_install_fltr_rule_req;

/* The size of a filter or route table entry */
#define IPA_TABLE_ENTRY_SIZE(version)	\
	(IPA_IS_64BIT(version) ? sizeof(__le64) : sizeof(__le32))

/**
 * ipa_filtered_valid() - Validate a filter table endpoint bitmap
 * @ipa:	IPA pointer
 * @filtered:	Filter table endpoint bitmap to check
 *
 * Return:	true if all regions are valid, false otherwise
 */
bool ipa_filtered_valid(struct ipa *ipa, u64 filtered);

/**
 * ipa_table_hash_support() - Return true if hashed tables are supported
 * @ipa:	IPA pointer
 */
bool ipa_table_hash_support(struct ipa *ipa);

/**
 * ipa_table_reset() - Reset filter and route tables entries to "none"
 * @ipa:	IPA pointer
 * @modem:	Whether to reset modem or AP entries
 */
void ipa_table_reset(struct ipa *ipa, bool modem);

/**
 * ipa_table_hash_flush() - Synchronize hashed filter and route updates
 * @ipa:	IPA pointer
 */
int ipa_table_hash_flush(struct ipa *ipa);

/**
 * ipa_table_install_filter_rules() - Install modem filter rules
 * @ipa:	IPA pointer
 * @req:	Decoded INSTALL_FILTER_RULE request
 *
 * Install the filter rules sent by an IPA v2.x modem on the modem's
 * TX pipe, and make sure every routing table entry the rules refer to
 * has a rule that forwards matching packets to the AP.
 *
 * Return:	0 on success, or a negative error code
 */
int ipa_table_install_filter_rules(struct ipa *ipa,
				   const struct ipa_install_fltr_rule_req *req);

/**
 * ipa_table_setup() - Set up filter and route tables
 * @ipa:	IPA pointer
 *
 * There is no need for a matching ipa_table_teardown() function.
 */
int ipa_table_setup(struct ipa *ipa);

/**
 * ipa_table_config() - Configure filter and route tables
 * @ipa:	IPA pointer
 *
 * There is no need for a matching ipa_table_deconfig() function.
 */
void ipa_table_config(struct ipa *ipa);

/**
 * ipa_table_init() - Do early initialization of filter and route tables
 * @ipa:	IPA pointer
 */
int ipa_table_init(struct ipa *ipa);

/**
 * ipa_table_exit() - Inverse of ipa_table_init()
 * @ipa:	IPA pointer
 */
void ipa_table_exit(struct ipa *ipa);

/**
 * ipa_table_mem_valid() - Validate sizes of table memory regions
 * @ipa:	IPA pointer
 * @filter:	Whether to check filter or routing tables
 */
bool ipa_table_mem_valid(struct ipa *ipa, bool filter);

#endif /* _IPA_TABLE_H_ */
