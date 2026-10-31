/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2024 Linaro Ltd.
 */
#ifndef _IPA_VERSION_H_
#define _IPA_VERSION_H_

#include <linux/types.h>

/**
 * enum ipa_version
 * @IPA_VERSION_2_0:	IPA version 2.0
 * @IPA_VERSION_2_5:	IPA version 2.5
 * @IPA_VERSION_2_6L:	IPA version 2.6L
 * @IPA_VERSION_3_0:	IPA version 3.0/GSI version 1.0
 * @IPA_VERSION_3_1:	IPA version 3.1/GSI version 1.0
 * @IPA_VERSION_3_5:	IPA version 3.5/GSI version 1.2
 * @IPA_VERSION_3_5_1:	IPA version 3.5.1/GSI version 1.3
 * @IPA_VERSION_4_0:	IPA version 4.0/GSI version 2.0
 * @IPA_VERSION_4_1:	IPA version 4.1/GSI version 2.0
 * @IPA_VERSION_4_2:	IPA version 4.2/GSI version 2.2
 * @IPA_VERSION_4_5:	IPA version 4.5/GSI version 2.5
 * @IPA_VERSION_4_7:	IPA version 4.7/GSI version 2.7
 * @IPA_VERSION_4_9:	IPA version 4.9/GSI version 2.9
 * @IPA_VERSION_4_11:	IPA version 4.11/GSI version 2.11 (2.1.1)
 * @IPA_VERSION_5_0:	IPA version 5.0/GSI version 3.0
 * @IPA_VERSION_5_1:	IPA version 5.1/GSI version 3.0
 * @IPA_VERSION_5_2:	IPA version 5.2/GSI version 5.2
 * @IPA_VERSION_5_5:	IPA version 5.5/GSI version 5.5
 * @IPA_VERSION_COUNT:	Number of defined IPA versions
 *
 * Defines the version of IPA (and GSI) hardware present on the platform.
 * Please update ipa_version_string() whenever a new version is added.
 *
 * IPA v2.x hardware uses BAM rather than GSI as its DMA transport, and its
 * IPA-resident table entries and command fields are 32 bits wide.
 */
enum ipa_version {
	IPA_VERSION_2_0,
	IPA_VERSION_2_5,
	IPA_VERSION_2_6L,
	IPA_VERSION_3_0,
	IPA_VERSION_3_1,
	IPA_VERSION_3_5,
	IPA_VERSION_3_5_1,
	IPA_VERSION_4_0,
	IPA_VERSION_4_1,
	IPA_VERSION_4_2,
	IPA_VERSION_4_5,
	IPA_VERSION_4_7,
	IPA_VERSION_4_9,
	IPA_VERSION_4_11,
	IPA_VERSION_5_0,
	IPA_VERSION_5_1,
	IPA_VERSION_5_2,
	IPA_VERSION_5_5,
	IPA_VERSION_COUNT,			/* Last; not a version */
};

/* IPA v2.x uses BAM; v3.0+ uses GSI as its DMA transport */
#define IPA_HAS_GSI(version)		((version) > IPA_VERSION_2_6L)

/* IPA v2.x hardware is 32-bit; v3.0+ is 64-bit */
#define IPA_IS_64BIT(version)		((version) > IPA_VERSION_2_6L)

/* True if the IPA version is in the inclusive range [from, to] */
#define IPA_VERSION_RANGE(_version, _from, _to)				\
	((_version) >= (IPA_VERSION_##_from) &&				\
	 (_version) <= (IPA_VERSION_##_to))

/* Execution environment IDs */
enum gsi_ee_id {
	GSI_EE_AP		= 0x0,
	GSI_EE_MODEM		= 0x1,
	GSI_EE_UC		= 0x2,
	GSI_EE_TZ		= 0x3,
};

#endif /* _IPA_VERSION_H_ */
