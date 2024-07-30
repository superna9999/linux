// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "iris_hfi_common.h"
#include "iris_hfi_gen2.h"
#include "iris_hfi_gen2_packet.h"

u32 iris_hfi_gen2_get_color_primaries(u32 primaries)
{
	u32 hfi_primaries = HFI_PRIMARIES_RESERVED;

	switch (primaries) {
	case V4L2_COLORSPACE_DEFAULT:
		hfi_primaries = HFI_PRIMARIES_RESERVED;
		break;
	case V4L2_COLORSPACE_REC709:
		hfi_primaries = HFI_PRIMARIES_BT709;
		break;
	case V4L2_COLORSPACE_470_SYSTEM_M:
		hfi_primaries = HFI_PRIMARIES_BT470_SYSTEM_M;
		break;
	case V4L2_COLORSPACE_470_SYSTEM_BG:
		hfi_primaries = HFI_PRIMARIES_BT470_SYSTEM_BG;
		break;
	case V4L2_COLORSPACE_SMPTE170M:
		hfi_primaries = HFI_PRIMARIES_BT601_525;
		break;
	case V4L2_COLORSPACE_SMPTE240M:
		hfi_primaries = HFI_PRIMARIES_SMPTE_ST240M;
		break;
	case V4L2_COLORSPACE_BT2020:
		hfi_primaries = HFI_PRIMARIES_BT2020;
		break;
	case V4L2_COLORSPACE_DCI_P3:
		hfi_primaries = HFI_PRIMARIES_SMPTE_RP431_2;
		break;
	default:
		break;
	}

	return hfi_primaries;
}

u32 iris_hfi_gen2_get_transfer_char(u32 characterstics)
{
	u32 hfi_characterstics = HFI_TRANSFER_RESERVED;

	switch (characterstics) {
	case V4L2_XFER_FUNC_DEFAULT:
		hfi_characterstics = HFI_TRANSFER_RESERVED;
		break;
	case V4L2_XFER_FUNC_709:
		hfi_characterstics = HFI_TRANSFER_BT709;
		break;
	case V4L2_XFER_FUNC_SMPTE240M:
		hfi_characterstics = HFI_TRANSFER_SMPTE_ST240M;
		break;
	case V4L2_XFER_FUNC_SRGB:
		hfi_characterstics = HFI_TRANSFER_SRGB_SYCC;
		break;
	case V4L2_XFER_FUNC_SMPTE2084:
		hfi_characterstics = HFI_TRANSFER_SMPTE_ST2084_PQ;
		break;
	default:
		break;
	}

	return hfi_characterstics;
}

u32 iris_hfi_gen2_get_matrix_coefficients(u32 coefficients)
{
	u32 hfi_coefficients = HFI_MATRIX_COEFF_RESERVED;

	switch (coefficients) {
	case V4L2_YCBCR_ENC_DEFAULT:
		hfi_coefficients = HFI_MATRIX_COEFF_RESERVED;
		break;
	case V4L2_YCBCR_ENC_709:
		hfi_coefficients = HFI_MATRIX_COEFF_BT709;
		break;
	case V4L2_YCBCR_ENC_XV709:
		hfi_coefficients = HFI_MATRIX_COEFF_BT709;
		break;
	case V4L2_YCBCR_ENC_XV601:
		hfi_coefficients = HFI_MATRIX_COEFF_BT470_SYS_BG_OR_BT601_625;
		break;
	case V4L2_YCBCR_ENC_601:
		hfi_coefficients = HFI_MATRIX_COEFF_BT601_525_BT1358_525_OR_625;
		break;
	case V4L2_YCBCR_ENC_SMPTE240M:
		hfi_coefficients = HFI_MATRIX_COEFF_SMPTE_ST240;
		break;
	case V4L2_YCBCR_ENC_BT2020:
		hfi_coefficients = HFI_MATRIX_COEFF_BT2020_NON_CONSTANT;
		break;
	case V4L2_YCBCR_ENC_BT2020_CONST_LUM:
		hfi_coefficients = HFI_MATRIX_COEFF_BT2020_CONSTANT;
		break;
	default:
		break;
	}

	return hfi_coefficients;
}

u32 iris_hfi_gen2_get_color_info(u32 matrix_coeff, u32 transfer_char, u32 primaries,
				 u32 colour_description_present_flag, u32 full_range,
				 u32 video_format, u32 video_signal_type_present_flag)
{
	return (matrix_coeff & 0xFF) |
		((transfer_char << 8) & 0xFF00) |
		((primaries << 16) & 0xFF0000) |
		((colour_description_present_flag << 24) & 0x1000000) |
		((full_range << 25) & 0x2000000) |
		((video_format << 26) & 0x1C000000) |
		((video_signal_type_present_flag << 29) & 0x20000000);
}

static void iris_hfi_gen2_create_header(struct iris_hfi_header *hdr,
					u32 session_id, u32 header_id)
{
	memset(hdr, 0, sizeof(*hdr));

	hdr->size = sizeof(*hdr);
	hdr->session_id = session_id;
	hdr->header_id = header_id;
	hdr->num_packets = 0;
}

static void iris_hfi_gen2_create_packet(struct iris_hfi_header *hdr, u32 pkt_type,
					u32 pkt_flags, u32 payload_type, u32 port,
					u32 packet_id, void *payload, u32 payload_size)
{
	struct iris_hfi_packet *pkt;
	u32 pkt_size;

	pkt = (struct iris_hfi_packet *)((u8 *)hdr + hdr->size);
	pkt_size = sizeof(*pkt) + payload_size;

	memset(pkt, 0, pkt_size);
	pkt->size = pkt_size;
	pkt->type = pkt_type;
	pkt->flags = pkt_flags;
	pkt->payload_info = payload_type;
	pkt->port = port;
	pkt->packet_id = packet_id;
	if (payload_size)
		memcpy(&pkt->payload[0], payload, payload_size);

	hdr->num_packets++;
	hdr->size += pkt->size;
}

void iris_hfi_gen2_packet_sys_init(struct iris_core *core, struct iris_hfi_header *hdr)
{
	u32 payload = 0;

	iris_hfi_gen2_create_header(hdr, 0, core->header_id++);

	payload = HFI_VIDEO_ARCH_LX;
	iris_hfi_gen2_create_packet(hdr,
				    HFI_CMD_INIT,
				    (HFI_HOST_FLAGS_RESPONSE_REQUIRED |
				    HFI_HOST_FLAGS_INTR_REQUIRED |
				    HFI_HOST_FLAGS_NON_DISCARDABLE),
				    HFI_PAYLOAD_U32,
				    HFI_PORT_NONE,
				    core->packet_id++,
				    &payload,
				    sizeof(u32));

	payload = core->iris_platform_data->ubwc_config->max_channels;
	iris_hfi_gen2_create_packet(hdr,
				    HFI_PROP_UBWC_MAX_CHANNELS,
				    HFI_HOST_FLAGS_NONE,
				    HFI_PAYLOAD_U32,
				    HFI_PORT_NONE,
				    core->packet_id++,
				    &payload,
				    sizeof(u32));

	payload = core->iris_platform_data->ubwc_config->mal_length;
	iris_hfi_gen2_create_packet(hdr,
				    HFI_PROP_UBWC_MAL_LENGTH,
				    HFI_HOST_FLAGS_NONE,
				    HFI_PAYLOAD_U32,
				    HFI_PORT_NONE,
				    core->packet_id++,
				    &payload,
				    sizeof(u32));

	payload = core->iris_platform_data->ubwc_config->highest_bank_bit;
	iris_hfi_gen2_create_packet(hdr,
				    HFI_PROP_UBWC_HBB,
				    HFI_HOST_FLAGS_NONE,
				    HFI_PAYLOAD_U32,
				    HFI_PORT_NONE,
				    core->packet_id++,
				    &payload,
				    sizeof(u32));

	payload = core->iris_platform_data->ubwc_config->bank_swzl_level;
	iris_hfi_gen2_create_packet(hdr,
				    HFI_PROP_UBWC_BANK_SWZL_LEVEL1,
				    HFI_HOST_FLAGS_NONE,
				    HFI_PAYLOAD_U32,
				    HFI_PORT_NONE,
				    core->packet_id++,
				    &payload,
				    sizeof(u32));

	payload = core->iris_platform_data->ubwc_config->bank_swz2_level;
	iris_hfi_gen2_create_packet(hdr,
				    HFI_PROP_UBWC_BANK_SWZL_LEVEL2,
				    HFI_HOST_FLAGS_NONE,
				    HFI_PAYLOAD_U32,
				    HFI_PORT_NONE,
				    core->packet_id++,
				    &payload,
				    sizeof(u32));

	payload = core->iris_platform_data->ubwc_config->bank_swz3_level;
	iris_hfi_gen2_create_packet(hdr,
				    HFI_PROP_UBWC_BANK_SWZL_LEVEL3,
				    HFI_HOST_FLAGS_NONE,
				    HFI_PAYLOAD_U32,
				    HFI_PORT_NONE,
				    core->packet_id++,
				    &payload,
				    sizeof(u32));

	payload = core->iris_platform_data->ubwc_config->bank_spreading;
	iris_hfi_gen2_create_packet(hdr,
				    HFI_PROP_UBWC_BANK_SPREADING,
				    HFI_HOST_FLAGS_NONE,
				    HFI_PAYLOAD_U32,
				    HFI_PORT_NONE,
				    core->packet_id++,
				    &payload,
				    sizeof(u32));
}

void iris_hfi_gen2_packet_image_version(struct iris_core *core, struct iris_hfi_header *hdr)
{
	iris_hfi_gen2_create_header(hdr, 0, core->header_id++);

	iris_hfi_gen2_create_packet(hdr,
				    HFI_PROP_IMAGE_VERSION,
				    (HFI_HOST_FLAGS_RESPONSE_REQUIRED |
				    HFI_HOST_FLAGS_INTR_REQUIRED |
				    HFI_HOST_FLAGS_GET_PROPERTY),
				    HFI_PAYLOAD_NONE,
				    HFI_PORT_NONE,
				    core->packet_id++,
				    NULL, 0);
}

void iris_hfi_gen2_packet_session_command(struct iris_inst *inst, u32 pkt_type,
					  u32 flags, u32 port, u32 session_id,
					  u32 payload_type, void *payload,
					  u32 payload_size)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	struct iris_core *core = inst->core;

	iris_hfi_gen2_create_header(inst_hfi_gen2->packet, session_id, core->header_id++);

	iris_hfi_gen2_create_packet(inst_hfi_gen2->packet,
				    pkt_type,
				    flags,
				    payload_type,
				    port,
				    core->packet_id++,
				    payload,
				    payload_size);
}

void iris_hfi_gen2_packet_session_property(struct iris_inst *inst,
					   u32 pkt_type, u32 flags, u32 port,
					   u32 payload_type, void *payload, u32 payload_size)
{
	struct iris_inst_hfi_gen2 *inst_hfi_gen2 = to_iris_inst_hfi_gen2(inst);
	struct iris_core *core = inst->core;

	iris_hfi_gen2_create_header(inst_hfi_gen2->packet, inst->session_id, core->header_id++);

	iris_hfi_gen2_create_packet(inst_hfi_gen2->packet,
				    pkt_type,
				    flags,
				    payload_type,
				    port,
				    core->packet_id++,
				    payload,
				    payload_size);
}

void iris_hfi_gen2_packet_sys_interframe_powercollapse(struct iris_core *core,
						       struct iris_hfi_header *hdr)
{
	u32 payload = 1; /* HFI_TRUE */

	iris_hfi_gen2_create_header(hdr, 0 /*session_id*/, core->header_id++);

	iris_hfi_gen2_create_packet(hdr,
				    HFI_PROP_INTRA_FRAME_POWER_COLLAPSE,
				    HFI_HOST_FLAGS_NONE,
				    HFI_PAYLOAD_U32,
				    HFI_PORT_NONE,
				    core->packet_id++,
				    &payload,
				    sizeof(u32));
}

void iris_hfi_gen2_packet_sys_pc_prep(struct iris_core *core, struct iris_hfi_header *hdr)
{
	iris_hfi_gen2_create_header(hdr, 0 /*session_id*/, core->header_id++);

	iris_hfi_gen2_create_packet(hdr,
				    HFI_CMD_POWER_COLLAPSE,
				    HFI_HOST_FLAGS_NONE,
				    HFI_PAYLOAD_NONE,
				    HFI_PORT_NONE,
				    core->packet_id++,
				    NULL, 0);
}
