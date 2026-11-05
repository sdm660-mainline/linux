// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2024 Linaro Ltd.
 */
#include <linux/stddef.h>

#include <linux/soc/qcom/qmi.h>

#include "ipa_qmi_msg.h"

/* QMI message structure definition for struct ipa_indication_register_req */
const struct qmi_elem_info ipa_indication_register_req_ei[] = {
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_indication_register_req,
				     master_driver_init_complete_valid),
		.tlv_type	= 0x10,
		.offset		= offsetof(struct ipa_indication_register_req,
					   master_driver_init_complete_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_indication_register_req,
				     master_driver_init_complete),
		.tlv_type	= 0x10,
		.offset		= offsetof(struct ipa_indication_register_req,
					   master_driver_init_complete),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_indication_register_req,
				     data_usage_quota_reached_valid),
		.tlv_type	= 0x11,
		.offset		= offsetof(struct ipa_indication_register_req,
					   data_usage_quota_reached_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_indication_register_req,
				     data_usage_quota_reached),
		.tlv_type	= 0x11,
		.offset		= offsetof(struct ipa_indication_register_req,
					   data_usage_quota_reached),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_indication_register_req,
				     ipa_mhi_ready_ind_valid),
		.tlv_type	= 0x12,
		.offset		= offsetof(struct ipa_indication_register_req,
					   ipa_mhi_ready_ind_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_indication_register_req,
				     ipa_mhi_ready_ind),
		.tlv_type	= 0x12,
		.offset		= offsetof(struct ipa_indication_register_req,
					   ipa_mhi_ready_ind),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_indication_register_req,
				     endpoint_desc_ind_valid),
		.tlv_type	= 0x13,
		.offset		= offsetof(struct ipa_indication_register_req,
					   endpoint_desc_ind_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_indication_register_req,
				     endpoint_desc_ind),
		.tlv_type	= 0x13,
		.offset		= offsetof(struct ipa_indication_register_req,
					   endpoint_desc_ind),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_indication_register_req,
				     bw_change_ind_valid),
		.tlv_type	= 0x14,
		.offset		= offsetof(struct ipa_indication_register_req,
					   bw_change_ind_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_indication_register_req,
				     bw_change_ind),
		.tlv_type	= 0x14,
		.offset		= offsetof(struct ipa_indication_register_req,
					   bw_change_ind),
	},
	{
		.data_type	= QMI_EOTI,
	},
};

/* QMI message structure definition for struct ipa_indication_register_rsp */
const struct qmi_elem_info ipa_indication_register_rsp_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_indication_register_rsp,
				     rsp),
		.tlv_type	= 0x02,
		.offset		= offsetof(struct ipa_indication_register_rsp,
					   rsp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type	= QMI_EOTI,
	},
};

/* QMI message structure definition for struct ipa_driver_init_complete_req */
const struct qmi_elem_info ipa_driver_init_complete_req_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_driver_init_complete_req,
				     status),
		.tlv_type	= 0x01,
		.offset		= offsetof(struct ipa_driver_init_complete_req,
					   status),
	},
	{
		.data_type	= QMI_EOTI,
	},
};

/* QMI message structure definition for struct ipa_driver_init_complete_rsp */
const struct qmi_elem_info ipa_driver_init_complete_rsp_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_driver_init_complete_rsp,
				     rsp),
		.tlv_type	= 0x02,
		.offset		= offsetof(struct ipa_driver_init_complete_rsp,
					   rsp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type	= QMI_EOTI,
	},
};

/* QMI message structure definition for struct ipa_init_complete_ind */
const struct qmi_elem_info ipa_init_complete_ind_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_complete_ind,
				     status),
		.tlv_type	= 0x02,
		.offset		= offsetof(struct ipa_init_complete_ind,
					   status),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type	= QMI_EOTI,
	},
};

/* QMI message structure definition for struct ipa_mem_bounds */
const struct qmi_elem_info ipa_mem_bounds_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_mem_bounds, start),
		.offset		= offsetof(struct ipa_mem_bounds, start),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_mem_bounds, end),
		.offset		= offsetof(struct ipa_mem_bounds, end),
	},
	{
		.data_type	= QMI_EOTI,
	},
};

/* QMI message structure definition for struct ipa_mem_array */
const struct qmi_elem_info ipa_mem_array_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_mem_array, start),
		.offset		= offsetof(struct ipa_mem_array, start),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_mem_array, count),
		.offset		= offsetof(struct ipa_mem_array, count),
	},
	{
		.data_type	= QMI_EOTI,
	},
};

/* QMI message structure definition for struct ipa_mem_range */
const struct qmi_elem_info ipa_mem_range_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_mem_range, start),
		.offset		= offsetof(struct ipa_mem_range, start),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_mem_range, size),
		.offset		= offsetof(struct ipa_mem_range, size),
	},
	{
		.data_type	= QMI_EOTI,
	},
};

/* QMI message structure definition for struct ipa_init_modem_driver_req */
const struct qmi_elem_info ipa_init_modem_driver_req_ei[] = {
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     platform_type_valid),
		.tlv_type	= 0x10,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   platform_type_valid),
	},
	{
		.data_type	= QMI_SIGNED_4_BYTE_ENUM,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     platform_type),
		.tlv_type	= 0x10,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   platform_type),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hdr_tbl_info_valid),
		.tlv_type	= 0x11,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hdr_tbl_info_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hdr_tbl_info),
		.tlv_type	= 0x11,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hdr_tbl_info),
		.ei_array	= ipa_mem_bounds_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v4_route_tbl_info_valid),
		.tlv_type	= 0x12,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v4_route_tbl_info_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v4_route_tbl_info),
		.tlv_type	= 0x12,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v4_route_tbl_info),
		.ei_array	= ipa_mem_bounds_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v6_route_tbl_info_valid),
		.tlv_type	= 0x13,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v6_route_tbl_info_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v6_route_tbl_info),
		.tlv_type	= 0x13,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v6_route_tbl_info),
		.ei_array	= ipa_mem_bounds_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v4_filter_tbl_start_valid),
		.tlv_type	= 0x14,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v4_filter_tbl_start_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v4_filter_tbl_start),
		.tlv_type	= 0x14,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v4_filter_tbl_start),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v6_filter_tbl_start_valid),
		.tlv_type	= 0x15,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v6_filter_tbl_start_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v6_filter_tbl_start),
		.tlv_type	= 0x15,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v6_filter_tbl_start),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     modem_mem_info_valid),
		.tlv_type	= 0x16,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   modem_mem_info_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     modem_mem_info),
		.tlv_type	= 0x16,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   modem_mem_info),
		.ei_array	= ipa_mem_range_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     ctrl_comm_dest_end_pt_valid),
		.tlv_type	= 0x17,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   ctrl_comm_dest_end_pt_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     ctrl_comm_dest_end_pt),
		.tlv_type	= 0x17,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   ctrl_comm_dest_end_pt),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     skip_uc_load_valid),
		.tlv_type	= 0x18,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   skip_uc_load_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     skip_uc_load),
		.tlv_type	= 0x18,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   skip_uc_load),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hdr_proc_ctx_tbl_info_valid),
		.tlv_type	= 0x19,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hdr_proc_ctx_tbl_info_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hdr_proc_ctx_tbl_info),
		.tlv_type	= 0x19,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hdr_proc_ctx_tbl_info),
		.ei_array	= ipa_mem_bounds_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     zip_tbl_info_valid),
		.tlv_type	= 0x1a,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   zip_tbl_info_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     zip_tbl_info),
		.tlv_type	= 0x1a,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   zip_tbl_info),
		.ei_array	= ipa_mem_bounds_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v4_hash_route_tbl_info_valid),
		.tlv_type	= 0x1b,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v4_hash_route_tbl_info_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v4_hash_route_tbl_info),
		.tlv_type	= 0x1b,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v4_hash_route_tbl_info),
		.ei_array	= ipa_mem_bounds_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v6_hash_route_tbl_info_valid),
		.tlv_type	= 0x1c,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v6_hash_route_tbl_info_valid),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v6_hash_route_tbl_info),
		.tlv_type	= 0x1c,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v6_hash_route_tbl_info),
		.ei_array	= ipa_mem_bounds_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v4_hash_filter_tbl_start_valid),
		.tlv_type	= 0x1d,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v4_hash_filter_tbl_start_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v4_hash_filter_tbl_start),
		.tlv_type	= 0x1d,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v4_hash_filter_tbl_start),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v6_hash_filter_tbl_start_valid),
		.tlv_type	= 0x1e,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v6_hash_filter_tbl_start_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     v6_hash_filter_tbl_start),
		.tlv_type	= 0x1e,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   v6_hash_filter_tbl_start),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hw_stats_quota_base_addr_valid),
		.tlv_type	= 0x1f,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hw_stats_quota_base_addr_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hw_stats_quota_base_addr),
		.tlv_type	= 0x1f,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hw_stats_quota_base_addr),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hw_stats_quota_size_valid),
		.tlv_type	= 0x20,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hw_stats_quota_size_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hw_stats_quota_size),
		.tlv_type	= 0x20,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hw_stats_quota_size),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hw_stats_drop_base_addr_valid),
		.tlv_type	= 0x21,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hw_stats_drop_base_addr_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hw_stats_drop_base_addr),
		.tlv_type	= 0x21,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hw_stats_drop_base_addr),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hw_stats_drop_size_valid),
		.tlv_type	= 0x22,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hw_stats_drop_size_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_req,
				     hw_stats_drop_size),
		.tlv_type	= 0x22,
		.offset		= offsetof(struct ipa_init_modem_driver_req,
					   hw_stats_drop_size),
	},
	{
		.data_type	= QMI_EOTI,
	},
};

/* QMI message structure definition for struct ipa_init_modem_driver_rsp */
const struct qmi_elem_info ipa_init_modem_driver_rsp_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_rsp,
				     rsp),
		.tlv_type	= 0x02,
		.offset		= offsetof(struct ipa_init_modem_driver_rsp,
					   rsp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_rsp,
				     ctrl_comm_dest_end_pt_valid),
		.tlv_type	= 0x10,
		.offset		= offsetof(struct ipa_init_modem_driver_rsp,
					   ctrl_comm_dest_end_pt_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_rsp,
				     ctrl_comm_dest_end_pt),
		.tlv_type	= 0x10,
		.offset		= offsetof(struct ipa_init_modem_driver_rsp,
					   ctrl_comm_dest_end_pt),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_rsp,
				     default_end_pt_valid),
		.tlv_type	= 0x11,
		.offset		= offsetof(struct ipa_init_modem_driver_rsp,
					   default_end_pt_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_rsp,
				     default_end_pt),
		.tlv_type	= 0x11,
		.offset		= offsetof(struct ipa_init_modem_driver_rsp,
					   default_end_pt),
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_rsp,
				     modem_driver_init_pending_valid),
		.tlv_type	= 0x12,
		.offset		= offsetof(struct ipa_init_modem_driver_rsp,
					   modem_driver_init_pending_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	=
			sizeof_field(struct ipa_init_modem_driver_rsp,
				     modem_driver_init_pending),
		.tlv_type	= 0x12,
		.offset		= offsetof(struct ipa_init_modem_driver_rsp,
					   modem_driver_init_pending),
	},
	{
		.data_type	= QMI_EOTI,
	},
};

/* Element info for struct ipa_ipfltr_range_eq_16 */
static const struct qmi_elem_info ipa_ipfltr_range_eq_16_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_ipfltr_range_eq_16,
					       offset),
		.offset		= offsetof(struct ipa_ipfltr_range_eq_16,
					   offset),
	},
	{
		.data_type	= QMI_UNSIGNED_2_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_ipfltr_range_eq_16,
					       range_low),
		.offset		= offsetof(struct ipa_ipfltr_range_eq_16,
					   range_low),
	},
	{
		.data_type	= QMI_UNSIGNED_2_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_ipfltr_range_eq_16,
					       range_high),
		.offset		= offsetof(struct ipa_ipfltr_range_eq_16,
					   range_high),
	},
	{ .data_type = QMI_EOTI },
};

/* Element info for struct ipa_ipfltr_mask_eq_32 */
static const struct qmi_elem_info ipa_ipfltr_mask_eq_32_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_ipfltr_mask_eq_32,
					       offset),
		.offset		= offsetof(struct ipa_ipfltr_mask_eq_32,
					   offset),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_ipfltr_mask_eq_32,
					       mask),
		.offset		= offsetof(struct ipa_ipfltr_mask_eq_32,
					   mask),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_ipfltr_mask_eq_32,
					       value),
		.offset		= offsetof(struct ipa_ipfltr_mask_eq_32,
					   value),
	},
	{ .data_type = QMI_EOTI },
};

/* Element info for struct ipa_ipfltr_eq_16 */
static const struct qmi_elem_info ipa_ipfltr_eq_16_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_ipfltr_eq_16, offset),
		.offset		= offsetof(struct ipa_ipfltr_eq_16, offset),
	},
	{
		.data_type	= QMI_UNSIGNED_2_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_ipfltr_eq_16, value),
		.offset		= offsetof(struct ipa_ipfltr_eq_16, value),
	},
	{ .data_type = QMI_EOTI },
};

/* Element info for struct ipa_ipfltr_eq_32 */
static const struct qmi_elem_info ipa_ipfltr_eq_32_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_ipfltr_eq_32, offset),
		.offset		= offsetof(struct ipa_ipfltr_eq_32, offset),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_ipfltr_eq_32, value),
		.offset		= offsetof(struct ipa_ipfltr_eq_32, value),
	},
	{ .data_type = QMI_EOTI },
};

/* Element info for struct ipa_ipfltr_mask_eq_128 */
static const struct qmi_elem_info ipa_ipfltr_mask_eq_128_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_ipfltr_mask_eq_128,
					       offset),
		.offset		= offsetof(struct ipa_ipfltr_mask_eq_128,
					   offset),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 16,
		.elem_size	= sizeof_field(struct ipa_ipfltr_mask_eq_128,
					       mask[0]),
		.array_type	= STATIC_ARRAY,
		.offset		= offsetof(struct ipa_ipfltr_mask_eq_128, mask),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 16,
		.elem_size	= sizeof_field(struct ipa_ipfltr_mask_eq_128,
					       value[0]),
		.array_type	= STATIC_ARRAY,
		.offset		= offsetof(struct ipa_ipfltr_mask_eq_128, value),
	},
	{ .data_type = QMI_EOTI },
};

/* Element info for struct ipa_filter_rule */
const struct qmi_elem_info ipa_filter_rule_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_2_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_rule,
					       rule_eq_bitmap),
		.offset		= offsetof(struct ipa_filter_rule,
					   rule_eq_bitmap),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_rule,
					       tos_eq_present),
		.offset		= offsetof(struct ipa_filter_rule,
					   tos_eq_present),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_rule, tos_eq),
		.offset		= offsetof(struct ipa_filter_rule, tos_eq),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_rule,
					       protocol_eq_present),
		.offset		= offsetof(struct ipa_filter_rule,
					   protocol_eq_present),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_rule,
					       protocol_eq),
		.offset		= offsetof(struct ipa_filter_rule,
					   protocol_eq),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_rule,
					       num_ihl_offset_range_16),
		.offset		= offsetof(struct ipa_filter_rule,
					   num_ihl_offset_range_16),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 2,
		.elem_size	= sizeof(struct ipa_ipfltr_range_eq_16),
		.array_type	= STATIC_ARRAY,
		.offset		= offsetof(struct ipa_filter_rule,
					   ihl_offset_range_16),
		.ei_array	= ipa_ipfltr_range_eq_16_ei,
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_rule,
					       num_offset_meq_32),
		.offset		= offsetof(struct ipa_filter_rule,
					   num_offset_meq_32),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 2,
		.elem_size	= sizeof(struct ipa_ipfltr_mask_eq_32),
		.array_type	= STATIC_ARRAY,
		.offset		= offsetof(struct ipa_filter_rule,
					   offset_meq_32),
		.ei_array	= ipa_ipfltr_mask_eq_32_ei,
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_rule,
					       tc_eq_present),
		.offset		= offsetof(struct ipa_filter_rule,
					   tc_eq_present),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_rule, tc_eq),
		.offset		= offsetof(struct ipa_filter_rule, tc_eq),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_rule,
					       flow_eq_present),
		.offset		= offsetof(struct ipa_filter_rule,
					   flow_eq_present),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_rule,
					       flow_eq),
		.offset		= offsetof(struct ipa_filter_rule, flow_eq),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_rule,
					       ihl_offset_eq_16_present),
		.offset		= offsetof(struct ipa_filter_rule,
					   ihl_offset_eq_16_present),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct ipa_ipfltr_eq_16),
		.offset		= offsetof(struct ipa_filter_rule,
					   ihl_offset_eq_16),
		.ei_array	= ipa_ipfltr_eq_16_ei,
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_rule,
					       ihl_offset_eq_32_present),
		.offset		= offsetof(struct ipa_filter_rule,
					   ihl_offset_eq_32_present),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct ipa_ipfltr_eq_32),
		.offset		= offsetof(struct ipa_filter_rule,
					   ihl_offset_eq_32),
		.ei_array	= ipa_ipfltr_eq_32_ei,
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_rule,
					       num_ihl_offset_meq_32),
		.offset		= offsetof(struct ipa_filter_rule,
					   num_ihl_offset_meq_32),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 2,
		.elem_size	= sizeof(struct ipa_ipfltr_mask_eq_32),
		.array_type	= STATIC_ARRAY,
		.offset		= offsetof(struct ipa_filter_rule,
					   ihl_offset_meq_32),
		.ei_array	= ipa_ipfltr_mask_eq_32_ei,
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_rule,
					       num_offset_meq_128),
		.offset		= offsetof(struct ipa_filter_rule,
					   num_offset_meq_128),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 2,
		.elem_size	= sizeof(struct ipa_ipfltr_mask_eq_128),
		.array_type	= STATIC_ARRAY,
		.offset		= offsetof(struct ipa_filter_rule,
					   offset_meq_128),
		.ei_array	= ipa_ipfltr_mask_eq_128_ei,
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_rule,
					       metadata_meq32_present),
		.offset		= offsetof(struct ipa_filter_rule,
					   metadata_meq32_present),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct ipa_ipfltr_mask_eq_32),
		.offset		= offsetof(struct ipa_filter_rule,
					   metadata_meq32),
		.ei_array	= ipa_ipfltr_mask_eq_32_ei,
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_rule,
					       ipv4_frag_eq_present),
		.offset		= offsetof(struct ipa_filter_rule,
					   ipv4_frag_eq_present),
	},
	{ .data_type = QMI_EOTI },
};

/* Element info for struct ipa_filter_spec */
const struct qmi_elem_info ipa_filter_spec_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_spec,
					       filter_spec_identifier),
		.offset		= offsetof(struct ipa_filter_spec,
					   filter_spec_identifier),
	},
	{
		.data_type	= QMI_SIGNED_4_BYTE_ENUM,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_spec, ip_type),
		.offset		= offsetof(struct ipa_filter_spec, ip_type),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct ipa_filter_rule),
		.offset		= offsetof(struct ipa_filter_spec,
					   filter_rule),
		.ei_array	= ipa_filter_rule_ei,
	},
	{
		.data_type	= QMI_SIGNED_4_BYTE_ENUM,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_spec,
					       filter_action),
		.offset		= offsetof(struct ipa_filter_spec,
					   filter_action),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_spec,
					is_routing_table_index_valid),
		.offset		= offsetof(struct ipa_filter_spec,
					   is_routing_table_index_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_spec,
					       route_table_index),
		.offset		= offsetof(struct ipa_filter_spec,
					   route_table_index),
	},
	{
		.data_type	= QMI_UNSIGNED_1_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_spec,
					       is_mux_id_valid),
		.offset		= offsetof(struct ipa_filter_spec,
					   is_mux_id_valid),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_spec, mux_id),
		.offset		= offsetof(struct ipa_filter_spec, mux_id),
	},
	{ .data_type = QMI_EOTI },
};

/* Element info for struct ipa_install_fltr_rule_req */
const struct qmi_elem_info ipa_install_fltr_rule_req_ei[] = {
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_install_fltr_rule_req,
					       filter_spec_list_valid),
		.tlv_type	= 0x10,
		.offset		= offsetof(struct ipa_install_fltr_rule_req,
					   filter_spec_list_valid),
	},
	{
		.data_type	= QMI_DATA_LEN,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_install_fltr_rule_req,
					       filter_spec_list_len),
		.tlv_type	= 0x10,
		.offset		= offsetof(struct ipa_install_fltr_rule_req,
					   filter_spec_list_len),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= IPA_QMI_MAX_FILTERS,
		.elem_size	= sizeof(struct ipa_filter_spec),
		.array_type	= VAR_LEN_ARRAY,
		.tlv_type	= 0x10,
		.offset		= offsetof(struct ipa_install_fltr_rule_req,
					   filter_spec_list),
		.ei_array	= ipa_filter_spec_ei,
	},
	{ .data_type = QMI_EOTI },
};

/* Element info for struct ipa_filter_handle */
static const struct qmi_elem_info ipa_filter_handle_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_handle,
					       filter_spec_identifier),
		.offset		= offsetof(struct ipa_filter_handle,
					   filter_spec_identifier),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_handle,
					       filter_handle),
		.offset		= offsetof(struct ipa_filter_handle,
					   filter_handle),
	},
	{ .data_type = QMI_EOTI },
};

/* Element info for struct ipa_install_fltr_rule_rsp */
const struct qmi_elem_info ipa_install_fltr_rule_rsp_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_install_fltr_rule_rsp,
					       rsp),
		.tlv_type	= 0x02,
		.offset		= offsetof(struct ipa_install_fltr_rule_rsp,
					   rsp),
		.ei_array	= qmi_response_type_v01_ei,
	},
	{
		.data_type	= QMI_OPT_FLAG,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_install_fltr_rule_rsp,
					       filter_handle_list_valid),
		.tlv_type	= 0x10,
		.offset		= offsetof(struct ipa_install_fltr_rule_rsp,
					   filter_handle_list_valid),
	},
	{
		.data_type	= QMI_DATA_LEN,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_install_fltr_rule_rsp,
					       filter_handle_list_len),
		.tlv_type	= 0x10,
		.offset		= offsetof(struct ipa_install_fltr_rule_rsp,
					   filter_handle_list_len),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= IPA_QMI_MAX_FILTERS,
		.elem_size	= sizeof(struct ipa_filter_handle),
		.array_type	= VAR_LEN_ARRAY,
		.tlv_type	= 0x10,
		.offset		= offsetof(struct ipa_install_fltr_rule_rsp,
					   filter_handle_list),
		.ei_array	= ipa_filter_handle_ei,
	},
	{ .data_type = QMI_EOTI },
};

/* Element info for struct ipa_filter_handle_index */
static const struct qmi_elem_info ipa_filter_handle_index_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_handle_index,
					       filter_handle),
		.offset		= offsetof(struct ipa_filter_handle_index,
					   filter_handle),
	},
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_filter_handle_index,
					       filter_index),
		.offset		= offsetof(struct ipa_filter_handle_index,
					   filter_index),
	},
	{ .data_type = QMI_EOTI },
};

/* Element info for struct ipa_fltr_installed_notif_req */
const struct qmi_elem_info ipa_fltr_installed_notif_req_ei[] = {
	{
		.data_type	= QMI_UNSIGNED_4_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_fltr_installed_notif_req,
					       source_pipe_index),
		.tlv_type	= 0x01,
		.offset		= offsetof(struct ipa_fltr_installed_notif_req,
					   source_pipe_index),
	},
	{
		.data_type	= QMI_UNSIGNED_2_BYTE,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_fltr_installed_notif_req,
					       install_status),
		.tlv_type	= 0x02,
		.offset		= offsetof(struct ipa_fltr_installed_notif_req,
					   install_status),
	},
	{
		.data_type	= QMI_DATA_LEN,
		.elem_len	= 1,
		.elem_size	= sizeof_field(struct ipa_fltr_installed_notif_req,
					       filter_index_list_len),
		.tlv_type	= 0x03,
		.offset		= offsetof(struct ipa_fltr_installed_notif_req,
					   filter_index_list_len),
	},
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= IPA_QMI_MAX_FILTERS,
		.elem_size	= sizeof(struct ipa_filter_handle_index),
		.array_type	= VAR_LEN_ARRAY,
		.tlv_type	= 0x03,
		.offset		= offsetof(struct ipa_fltr_installed_notif_req,
					   filter_index_list),
		.ei_array	= ipa_filter_handle_index_ei,
	},
	{ .data_type = QMI_EOTI },
};

/* Element info for the response to the notification */
const struct qmi_elem_info ipa_fltr_installed_notif_rsp_ei[] = {
	{
		.data_type	= QMI_STRUCT,
		.elem_len	= 1,
		.elem_size	= sizeof(struct qmi_response_type_v01),
		.tlv_type	= 0x02,
		.offset		= 0,
		.ei_array	= qmi_response_type_v01_ei,
	},
	{ .data_type = QMI_EOTI },
};
