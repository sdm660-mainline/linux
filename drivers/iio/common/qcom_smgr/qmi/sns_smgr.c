// SPDX-License-Identifier: GPL-2.0-only
/*
 * QMI element info arrays and helper functions for Qualcomm Sensor Manager
 *
 * Copyright (c) 2021, Yassine Oudjana <y.oudjana@protonmail.com>
 */

#include <linux/iio/common/qcom_smgr.h>
#include <linux/module.h>
#include <linux/soc/qcom/qmi.h>
#include <linux/types.h>

#include "sns_smgr.h"

static const struct qmi_elem_info sns_smgr_all_sensor_info_ei[] = {
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(struct sns_smgr_all_sensor_info, id),
		.array_type = NO_ARRAY,
		.offset = offsetof(struct sns_smgr_all_sensor_info, id),
	},
	{
		.data_type = QMI_DATA_LEN,
		.elem_len = 1,
		.elem_size =
			sizeof_field(struct sns_smgr_all_sensor_info, type_len),
		.array_type = NO_ARRAY,
		.offset = offsetof(struct sns_smgr_all_sensor_info, type_len),
	},
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = SNS_SMGR_SENSOR_TYPE_MAX_LEN,
		.elem_size = sizeof(char),
		.array_type = VAR_LEN_ARRAY,
		.offset = offsetof(struct sns_smgr_all_sensor_info, type),
	},
	{
		.data_type = QMI_EOTI,
	},
};

const struct qmi_elem_info sns_smgr_all_sensor_info_resp_ei[] = {
	{
		.data_type = QMI_UNSIGNED_2_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(struct sns_smgr_all_sensor_info_resp,
					  result),
		.array_type = NO_ARRAY,
		.tlv_type = 0x02,
		.offset =
			offsetof(struct sns_smgr_all_sensor_info_resp, result),
	},
	{
		.data_type = QMI_DATA_LEN,
		.elem_len = 1,
		.elem_size = sizeof_field(struct sns_smgr_all_sensor_info_resp,
					  item_len),
		.array_type = NO_ARRAY,
		.tlv_type = 0x03,
		.offset = offsetof(struct sns_smgr_all_sensor_info_resp,
				   item_len),
	},
	{
		.data_type = QMI_STRUCT,
		.elem_len = SNS_SMGR_ALL_SENSOR_INFO_MAX_LEN,
		.elem_size = sizeof(struct sns_smgr_all_sensor_info),
		.array_type = VAR_LEN_ARRAY,
		.tlv_type = 0x03,
		.offset = offsetof(struct sns_smgr_all_sensor_info_resp, items),
		.ei_array = sns_smgr_all_sensor_info_ei,
	},
	{
		.data_type = QMI_EOTI,
	},
};
EXPORT_SYMBOL_GPL(sns_smgr_all_sensor_info_resp_ei);

const struct qmi_elem_info sns_smgr_single_sensor_info_req_ei[] = {
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_single_sensor_info_req, sensor_id),
		.array_type = NO_ARRAY,
		.tlv_type = 0x01,
		.offset = offsetof(struct sns_smgr_single_sensor_info_req,
				   sensor_id),
	},
	{
		.data_type = QMI_EOTI,
	},
};
EXPORT_SYMBOL_GPL(sns_smgr_single_sensor_info_req_ei);

static const struct qmi_elem_info sns_smgr_single_sensor_info_data_type_ei[] = {
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_single_sensor_info_data_type,
			sensor_id),
		.array_type = NO_ARRAY,
		.offset = offsetof(struct sns_smgr_single_sensor_info_data_type,
				   sensor_id),
	},
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_single_sensor_info_data_type,
			data_type),
		.array_type = NO_ARRAY,
		.offset = offsetof(struct sns_smgr_single_sensor_info_data_type,
				   data_type),
	},
	{
		.data_type = QMI_DATA_LEN,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_single_sensor_info_data_type, name_len),
		.array_type = NO_ARRAY,
		.offset = offsetof(struct sns_smgr_single_sensor_info_data_type,
				   name_len),
	},
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 0xff,
		.elem_size = sizeof(char),
		.array_type = VAR_LEN_ARRAY,
		.offset = offsetof(struct sns_smgr_single_sensor_info_data_type,
				   name),
	},
	{
		.data_type = QMI_DATA_LEN,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_single_sensor_info_data_type,
			vendor_len),
		.array_type = NO_ARRAY,
		.offset = offsetof(struct sns_smgr_single_sensor_info_data_type,
				   vendor_len),
	},
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 0xff,
		.elem_size = sizeof(char),
		.array_type = VAR_LEN_ARRAY,
		.offset = offsetof(struct sns_smgr_single_sensor_info_data_type,
				   vendor),
	},
	{
		.data_type = QMI_UNSIGNED_4_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_single_sensor_info_data_type, val1),
		.array_type = NO_ARRAY,
		.offset = offsetof(struct sns_smgr_single_sensor_info_data_type,
				   val1),
	},
	{
		.data_type = QMI_UNSIGNED_2_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_single_sensor_info_data_type,
			max_sample_rate_hz),
		.array_type = NO_ARRAY,
		.offset = offsetof(struct sns_smgr_single_sensor_info_data_type,
				   max_sample_rate_hz),
	},
	{
		.data_type = QMI_UNSIGNED_2_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_single_sensor_info_data_type, val2),
		.array_type = NO_ARRAY,
		.offset = offsetof(struct sns_smgr_single_sensor_info_data_type,
				   val2),
	},
	{
		.data_type = QMI_UNSIGNED_2_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_single_sensor_info_data_type,
			current_ua),
		.array_type = NO_ARRAY,
		.offset = offsetof(struct sns_smgr_single_sensor_info_data_type,
				   current_ua),
	},
	{
		.data_type = QMI_UNSIGNED_4_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_single_sensor_info_data_type, range),
		.array_type = NO_ARRAY,
		.offset = offsetof(struct sns_smgr_single_sensor_info_data_type,
				   range),
	},
	{
		.data_type = QMI_UNSIGNED_4_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_single_sensor_info_data_type,
			resolution),
		.array_type = NO_ARRAY,
		.offset = offsetof(struct sns_smgr_single_sensor_info_data_type,
				   resolution),
	},
	{
		.data_type = QMI_EOTI,
	},
};

static const struct qmi_elem_info sns_smgr_single_sensor_info_native_sample_rates_ei[] = {
	{
		.data_type = QMI_DATA_LEN,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_single_sensor_info_native_sample_rates, rate_count),
		.array_type = NO_ARRAY,
		.offset = offsetof(struct sns_smgr_single_sensor_info_native_sample_rates,
				   rate_count),
	},
	{
		.data_type = QMI_UNSIGNED_2_BYTE,
		.elem_len = SNS_SMGR_NATIVE_SAMPLE_RATES_MAX_LEN,
		.elem_size = sizeof(u16),
		.array_type = VAR_LEN_ARRAY,
		.offset = offsetof(struct sns_smgr_single_sensor_info_native_sample_rates,
				   rates),
	},
	{
		.data_type = QMI_EOTI,
	},
};

const struct qmi_elem_info sns_smgr_single_sensor_info_resp_ei[] = {
	{
		.data_type = QMI_UNSIGNED_2_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_single_sensor_info_resp, result),
		.array_type = NO_ARRAY,
		.tlv_type = 0x02,
		.offset = offsetof(struct sns_smgr_single_sensor_info_resp,
				   result),
	},
	{
		.data_type = QMI_DATA_LEN,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_single_sensor_info_resp, data_type_len),
		.array_type = NO_ARRAY,
		.tlv_type = 0x03,
		.offset = offsetof(struct sns_smgr_single_sensor_info_resp,
				   data_type_len),
	},
	{
		.data_type = QMI_STRUCT,
		.elem_len = SNS_SMGR_DATA_TYPE_COUNT,
		.elem_size =
			sizeof(struct sns_smgr_single_sensor_info_data_type),
		.array_type = VAR_LEN_ARRAY,
		.tlv_type = 0x03,
		.offset = offsetof(struct sns_smgr_single_sensor_info_resp,
				   data_types),
		.ei_array = sns_smgr_single_sensor_info_data_type_ei,
	},
	{
		.data_type = QMI_DATA_LEN,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_single_sensor_info_resp, data1_len),
		.array_type = NO_ARRAY,
		.tlv_type = 0x10,
		.offset = offsetof(struct sns_smgr_single_sensor_info_resp,
				   data1_len),
	},
	{
		.data_type = QMI_UNSIGNED_4_BYTE,
		.elem_len = SNS_SMGR_DATA_TYPE_COUNT,
		.elem_size = sizeof(u32),
		.array_type = VAR_LEN_ARRAY,
		.tlv_type = 0x10,
		.offset = offsetof(struct sns_smgr_single_sensor_info_resp,
				   data1),
	},
	{
		.data_type = QMI_UNSIGNED_4_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_single_sensor_info_resp, data2),
		.array_type = NO_ARRAY,
		.tlv_type = 0x11,
		.offset = offsetof(struct sns_smgr_single_sensor_info_resp,
				   data2),
	},
	{
		.data_type = QMI_DATA_LEN,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_single_sensor_info_resp, data3_len),
		.array_type = NO_ARRAY,
		.tlv_type = 0x12,
		.offset = offsetof(struct sns_smgr_single_sensor_info_resp,
				   data3_len),
	},
	{
		.data_type = QMI_UNSIGNED_8_BYTE,
		.elem_len = SNS_SMGR_DATA_TYPE_COUNT,
		.elem_size = sizeof(u64),
		.array_type = VAR_LEN_ARRAY,
		.tlv_type = 0x12,
		.offset = offsetof(struct sns_smgr_single_sensor_info_resp,
				   data3),
	},
	{
		.data_type = QMI_DATA_LEN,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_single_sensor_info_resp,
			native_sample_rates_len),
		.array_type = NO_ARRAY,
		.tlv_type = 0x13,
		.offset = offsetof(struct sns_smgr_single_sensor_info_resp,
				   native_sample_rates_len),
	},
	{
		.data_type = QMI_STRUCT,
		.elem_len = SNS_SMGR_DATA_TYPE_COUNT,
		.elem_size = sizeof(
			struct sns_smgr_single_sensor_info_native_sample_rates),
		.array_type = VAR_LEN_ARRAY,
		.tlv_type = 0x13,
		.offset = offsetof(struct sns_smgr_single_sensor_info_resp,
				   native_sample_rates),
		.ei_array = sns_smgr_single_sensor_info_native_sample_rates_ei,
	},
	{
		.data_type = QMI_DATA_LEN,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_single_sensor_info_resp, data5_len),
		.array_type = NO_ARRAY,
		.tlv_type = 0x14,
		.offset = offsetof(struct sns_smgr_single_sensor_info_resp,
				   data5_len),
	},
	{
		.data_type = QMI_UNSIGNED_4_BYTE,
		.elem_len = SNS_SMGR_DATA_TYPE_COUNT,
		.elem_size = sizeof(u32),
		.array_type = VAR_LEN_ARRAY,
		.tlv_type = 0x14,
		.offset = offsetof(struct sns_smgr_single_sensor_info_resp,
				   data5),
	},
	{
		.data_type = QMI_EOTI,
	},
};
EXPORT_SYMBOL_GPL(sns_smgr_single_sensor_info_resp_ei);

static const struct qmi_elem_info sns_smgr_buffering_req_item_ei[] = {
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(struct sns_smgr_buffering_req_item,
					  sensor_id),
		.array_type = NO_ARRAY,
		.offset =
			offsetof(struct sns_smgr_buffering_req_item, sensor_id),
	},
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(struct sns_smgr_buffering_req_item,
					  data_type),
		.array_type = NO_ARRAY,
		.offset =
			offsetof(struct sns_smgr_buffering_req_item, data_type),
	},
	{
		.data_type = QMI_UNSIGNED_2_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(struct sns_smgr_buffering_req_item,
					  val1),
		.array_type = NO_ARRAY,
		.offset = offsetof(struct sns_smgr_buffering_req_item,
				   val1),
	},
	{
		.data_type = QMI_UNSIGNED_2_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(struct sns_smgr_buffering_req_item,
					  sampling_rate),
		.array_type = NO_ARRAY,
		.offset = offsetof(struct sns_smgr_buffering_req_item,
				   sampling_rate),
	},
	{
		.data_type = QMI_UNSIGNED_2_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(struct sns_smgr_buffering_req_item,
					  val2),
		.array_type = NO_ARRAY,
		.offset = offsetof(struct sns_smgr_buffering_req_item,
				   val2),
	},
	{
		.data_type = QMI_EOTI,
	},
};

static const struct qmi_elem_info sns_smgr_buffering_req_notify_suspend_ei[] = {
	{
		.data_type = QMI_UNSIGNED_2_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_buffering_req_notify_suspend,
			proc_type),
		.array_type = NO_ARRAY,
		.offset = offsetof(struct sns_smgr_buffering_req_notify_suspend,
				   proc_type),
	},
	{
		.data_type = QMI_UNSIGNED_2_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_buffering_req_notify_suspend,
			send_indications_during_suspend),
		.array_type = NO_ARRAY,
		.offset = offsetof(struct sns_smgr_buffering_req_notify_suspend,
				   send_indications_during_suspend),
	},
	{
		.data_type = QMI_EOTI,
	},
};

const struct qmi_elem_info sns_smgr_buffering_req_ei[] = {
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size =
			sizeof_field(struct sns_smgr_buffering_req, report_id),
		.array_type = NO_ARRAY,
		.tlv_type = 0x01,
		.offset = offsetof(struct sns_smgr_buffering_req, report_id),
	},
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size =
			sizeof_field(struct sns_smgr_buffering_req, action),
		.array_type = NO_ARRAY,
		.tlv_type = 0x02,
		.offset = offsetof(struct sns_smgr_buffering_req, action),
	},
	{
		.data_type = QMI_UNSIGNED_4_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(struct sns_smgr_buffering_req,
					  report_rate),
		.array_type = NO_ARRAY,
		.tlv_type = 0x03,
		.offset = offsetof(struct sns_smgr_buffering_req, report_rate),
	},
	{
		.data_type = QMI_DATA_LEN,
		.elem_len = 1,
		.elem_size =
			sizeof_field(struct sns_smgr_buffering_req, item_len),
		.array_type = NO_ARRAY,
		.tlv_type = 0x04,
		.offset = offsetof(struct sns_smgr_buffering_req, item_len),
	},
	{
		.data_type = QMI_STRUCT,
		.elem_len = SNS_SMGR_DATA_TYPE_COUNT,
		.elem_size = sizeof(struct sns_smgr_buffering_req_item),
		.array_type = VAR_LEN_ARRAY,
		.tlv_type = 0x04,
		.offset = offsetof(struct sns_smgr_buffering_req, items),
		.ei_array = sns_smgr_buffering_req_item_ei,
	},
	{
		.data_type = QMI_OPT_FLAG,
		.elem_len = 1,
		.elem_size = sizeof_field(struct sns_smgr_buffering_req,
					  notify_suspend_valid),
		.array_type = NO_ARRAY,
		.tlv_type = 0x10,
		.offset = offsetof(struct sns_smgr_buffering_req,
				   notify_suspend_valid),
	},
	{
		.data_type = QMI_STRUCT,
		.elem_len = 1,
		.elem_size = sizeof_field(struct sns_smgr_buffering_req,
					  notify_suspend),
		.array_type = NO_ARRAY,
		.tlv_type = 0x10,
		.offset =
			offsetof(struct sns_smgr_buffering_req, notify_suspend),
		.ei_array = sns_smgr_buffering_req_notify_suspend_ei,
	},
	{
		.data_type = QMI_EOTI,
	},
};
EXPORT_SYMBOL_GPL(sns_smgr_buffering_req_ei);

const struct qmi_elem_info sns_smgr_buffering_resp_ei[] = {
	{
		.data_type = QMI_UNSIGNED_2_BYTE,
		.elem_len = 1,
		.elem_size =
			sizeof_field(struct sns_smgr_buffering_resp, result),
		.array_type = NO_ARRAY,
		.tlv_type = 0x02,
		.offset = offsetof(struct sns_smgr_buffering_resp, result),
	},
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size =
			sizeof_field(struct sns_smgr_buffering_resp, report_id),
		.array_type = NO_ARRAY,
		.tlv_type = 0x10,
		.offset = offsetof(struct sns_smgr_buffering_resp, report_id),
	},
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size =
			sizeof_field(struct sns_smgr_buffering_resp, ack_nak),
		.array_type = NO_ARRAY,
		.tlv_type = 0x11,
		.offset = offsetof(struct sns_smgr_buffering_resp, ack_nak),
	},
	{
		.data_type = QMI_EOTI,
	},
};
EXPORT_SYMBOL_GPL(sns_smgr_buffering_resp_ei);

static const struct qmi_elem_info sns_smgr_buffering_report_metadata_ei[] = {
	{
		.data_type = QMI_UNSIGNED_4_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_buffering_report_metadata, val1),
		.array_type = NO_ARRAY,
		.offset = offsetof(struct sns_smgr_buffering_report_metadata,
				   val1),
	},
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size =
			sizeof_field(struct sns_smgr_buffering_report_metadata,
				     sample_count),
		.array_type = NO_ARRAY,
		.offset = offsetof(struct sns_smgr_buffering_report_metadata,
				   sample_count),
	},
	{
		.data_type = QMI_UNSIGNED_4_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_buffering_report_metadata, timestamp),
		.array_type = NO_ARRAY,
		.offset = offsetof(struct sns_smgr_buffering_report_metadata,
				   timestamp),
	},
	{
		.data_type = QMI_UNSIGNED_4_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_buffering_report_metadata, val2),
		.array_type = NO_ARRAY,
		.offset = offsetof(struct sns_smgr_buffering_report_metadata,
				   val2),
	},
	{
		.data_type = QMI_EOTI,
	},
};

static const struct qmi_elem_info sns_smgr_buffering_report_sample_ei[] = {
	{
		.data_type = QMI_UNSIGNED_4_BYTE,
		.elem_len = 3,
		.elem_size = sizeof(u32),
		.array_type = STATIC_ARRAY,
		.offset = offsetof(struct sns_smgr_buffering_report_sample,
				   values),
	},
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_buffering_report_sample, val1),
		.array_type = NO_ARRAY,
		.offset =
			offsetof(struct sns_smgr_buffering_report_sample, val1),
	},
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_buffering_report_sample, val2),
		.array_type = NO_ARRAY,
		.offset =
			offsetof(struct sns_smgr_buffering_report_sample, val2),
	},
	{
		.data_type = QMI_UNSIGNED_2_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(
			struct sns_smgr_buffering_report_sample, val3),
		.array_type = NO_ARRAY,
		.offset =
			offsetof(struct sns_smgr_buffering_report_sample, val3),
	},
	{
		.data_type = QMI_EOTI,
	},
};

const struct qmi_elem_info sns_smgr_buffering_report_ind_ei[] = {
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(struct sns_smgr_buffering_report_ind,
					  report_id),
		.array_type = NO_ARRAY,
		.tlv_type = 0x01,
		.offset = offsetof(struct sns_smgr_buffering_report_ind,
				   report_id),
	},
	{
		.data_type = QMI_STRUCT,
		.elem_len = 1,
		.elem_size = sizeof_field(struct sns_smgr_buffering_report_ind,
					  metadata),
		.array_type = NO_ARRAY,
		.tlv_type = 0x02,
		.offset = offsetof(struct sns_smgr_buffering_report_ind,
				   metadata),
		.ei_array = sns_smgr_buffering_report_metadata_ei,
	},
	{
		.data_type = QMI_DATA_LEN,
		.elem_len = 1,
		.elem_size = sizeof_field(struct sns_smgr_buffering_report_ind,
					  samples_len),
		.array_type = NO_ARRAY,
		.tlv_type = 0x03,
		.offset = offsetof(struct sns_smgr_buffering_report_ind,
				   samples_len),
	},
	{
		.data_type = QMI_STRUCT,
		.elem_len = SNS_SMGR_SAMPLES_MAX_LEN,
		.elem_size = sizeof(struct sns_smgr_buffering_report_sample),
		.array_type = VAR_LEN_ARRAY,
		.tlv_type = 0x03,
		.offset =
			offsetof(struct sns_smgr_buffering_report_ind, samples),
		.ei_array = sns_smgr_buffering_report_sample_ei,
	},
	{
		.data_type = QMI_UNSIGNED_1_BYTE,
		.elem_len = 1,
		.elem_size = sizeof_field(struct sns_smgr_buffering_report_ind,
					  val2),
		.array_type = NO_ARRAY,
		.tlv_type = 0x10,
		.offset = offsetof(struct sns_smgr_buffering_report_ind, val2),
	},
	{
		.data_type = QMI_EOTI,
	},
};
EXPORT_SYMBOL_GPL(sns_smgr_buffering_report_ind_ei);

static const char *smgr_sensor_type_names[SNS_SMGR_SENSOR_TYPE_COUNT] = {
	[SNS_SMGR_SENSOR_TYPE_ACCEL] = "ACCEL",
	[SNS_SMGR_SENSOR_TYPE_GYRO] = "GYRO",
	[SNS_SMGR_SENSOR_TYPE_MAG] = "MAG",
	[SNS_SMGR_SENSOR_TYPE_PROX_LIGHT] = "PROX_LIGHT",
	[SNS_SMGR_SENSOR_TYPE_PRESSURE] = "PRESSURE",
	[SNS_SMGR_SENSOR_TYPE_HALL_EFFECT] = "HALL_EFFECT"
};

enum qcom_smgr_sensor_type sns_smgr_sensor_type_from_str(const char *str)
{
	enum qcom_smgr_sensor_type i;

	for (i = SNS_SMGR_SENSOR_TYPE_UNKNOWN + 1;
	     i < SNS_SMGR_SENSOR_TYPE_COUNT; i++)
		if (!strcmp(str, smgr_sensor_type_names[i]))
			return i;

	return SNS_SMGR_SENSOR_TYPE_UNKNOWN;
}
EXPORT_SYMBOL_GPL(sns_smgr_sensor_type_from_str);

MODULE_LICENSE("GPL");
