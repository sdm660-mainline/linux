/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2024-2025 Linaro Ltd. */

#ifndef _IPA_DATA_FORMAT_H_
#define _IPA_DATA_FORMAT_H_

#include <linux/types.h>

struct ipa;

/**
 * ipa_data_format_setup() - Prepare for modem data-port mapping
 * @ipa:	IPA pointer
 *
 * Allocate the data-port mapping state and start listening for the
 * modem's DPM service.  This is a no-op (and returns 0) for hardware
 * that does not use this mechanism, or when the mapping is disabled.
 *
 * Return: 0 on success, or a negative errno on failure
 */
int ipa_data_format_setup(struct ipa *ipa);

/**
 * ipa_data_format_teardown() - Inverse of ipa_data_format_setup()
 * @ipa:	IPA pointer
 */
void ipa_data_format_teardown(struct ipa *ipa);

/**
 * ipa_data_format_start() - Map the modem's data port
 * @ipa:	IPA pointer
 *
 * Called when the modem's IPA QMI handshake completes.  Sends the DPM
 * OPEN_PORT request that maps the modem's data port to the AP's IPA
 * endpoints.
 */
void ipa_data_format_start(struct ipa *ipa);

/**
 * ipa_data_format_stop() - Stop data-port mapping
 * @ipa:	IPA pointer
 *
 * Called when the modem goes away.  The mapping no longer applies, so
 * stop retrying and forget the modem's DPM service.
 */
void ipa_data_format_stop(struct ipa *ipa);

#endif /* _IPA_DATA_FORMAT_H_ */
