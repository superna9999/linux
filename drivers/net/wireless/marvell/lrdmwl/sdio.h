/*
 * Marvell Wireless LAN device driver: SDIO specific definitions
 *
 * Copyright (C) 2011-2014, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#ifndef	_MWLWIFI_SDIO_H
#define	_MWLWIFI_SDIO_H
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>


#define MWL_MAX_FUNC2_REG_NUM	13
#define MWL_TX_DATA_BUF_SIZE_4K   4096

/*
	type of the sdio data path
*/
enum sdio_pkt_type {
	MWL_TYPE_DATA = 0,
	MWL_TYPE_CMD,
	MWL_TYPE_TX_DONE,    /* not used, it's been embedded in SDIO_MAC_EVT */
	MWL_TYPE_EVENT,
	MWL_TYPE_DUMMY_PKT,
	MWL_TYPE_HIGH_PRI_DATA_PKT,
	MWL_TYPE_LOOPBACK_DATA_PKT,
	MWL_TYPE_AGGR_DATA = 0x0a,
	MWL_TYPE_MGMT = 0x0b,
	MWL_TYPE_BEACON = 0x0c,
};

#define MWL_DMA_ALIGN_SZ         4
#define MWL_RX_HEADROOM         64
#define MAX_TXPD_SZ		          32
#define INTF_HDR_ALIGN		       4

#define MWL_MIN_DATA_HEADER_LEN (MWL_DMA_ALIGN_SZ + INTF_HDR_ALIGN + \
				     MAX_TXPD_SZ)

/* Address alignment */
#define MWL_ALIGN_ADDR(p, a) (((long)(p) + (a) - 1) & ~((a) - 1))


#define REG_PORT			0
#define CTRL_PORT			0
#define MEM_PORT			0x10000

#define CMD53_NEW_MODE			(0x1U << 0)
#define CMD_PORT_RD_LEN_EN		(0x1U << 2)
#define CMD_PORT_AUTO_EN		(0x1U << 0)

/* Misc. Config Register : Auto Re-enable interrupts */
#define AUTO_RE_ENABLE_INT        (0x1U << 4)

#define CMD_PORT_UPLD_INT_MASK		(0x1U<<6)
#define CMD_PORT_DNLD_INT_MASK		(0x1U<<7)


/* Host Control Registers : Upload host interrupt mask */
#define UP_LD_HOST_INT_MASK		(0x1U)
/* Host Control Registers : Download host interrupt mask */
#define DN_LD_HOST_INT_MASK		(0x2U)


/* Host Control Registers : Upload host interrupt status */
#define UP_LD_HOST_INT_STATUS		(0x1U)
/* Host Control Registers : Download host interrupt status */
#define DN_LD_HOST_INT_STATUS		(0x2U)
/* Host Control Registers : Command Port Upload host interrupt status */
#define UP_LD_CMD_PORT_HOST_INT_STATUS	(0x40U)
/* Host Control Registers : Command Port Download host interrupt status */
#define DN_LD_CMD_PORT_HOST_INT_STATUS	(0x80U)

/* Card Control Registers : Card I/O ready */
#define CARD_IO_READY                   (0x1U << 3)
/* Card Control Registers : Download card ready */
#define DN_LD_CARD_RDY                  (0x1U << 0)

#define CMD_PORT_SLCT			0x8000

/* Host Control Registers : Configuration */
#define CONFIGURATION_REG		0x00


#define MWL_MP_AGGR_BUF_SIZE_MAX    (65280)
#define MWL_MP_AGGR_BUF_SIZE_32K	  (32768)
#define SDIO_MAX_AGGR_BUF_SIZE      (256 * 256)
#define MWL_SDIO_BLOCK_SIZE         256

#define BLOCK_MODE	1
#define BYTE_MODE	0

#define MWL_UPLD_SIZE               (2312)
#define MAX_POLL_TRIES               100
#define MAX_WRITE_IOMEM_RETRY        2
#define MAX_FIRMWARE_POLL_TRIES      100
#define FIRMWARE_READY_SDIO         0xfedc
#define MWL_SDIO_BYTE_MODE_MASK     0x80000000
#define MWL_SDIO_IO_PORT_MASK       0xfffff
#define SDIO_MPA_ADDR_BASE		      0x1000
#define BLOCK_NUMBER_OFFSET         15
#define SDIO_HEADER_OFFSET          28


/* SDIO Tx aggregation in progress ? */
#define MP_TX_AGGR_IN_PROGRESS(a) (a->mpa_tx.pkt_cnt > 0)


/* SDIO Tx aggregation buffer room for next packet ? */
#define MP_TX_AGGR_BUF_HAS_ROOM(a, len) ((a->mpa_tx.buf_len+len)	\
						<= a->mpa_tx.buf_size)

/* Copy current packet (SDIO Tx aggregation buffer) to SDIO buffer */
#define MP_TX_AGGR_BUF_PUT(a, payload, pkt_len, port) do {		\
	memmove(&a->mpa_tx.buf[a->mpa_tx.buf_len],			\
			payload, pkt_len);				\
	a->mpa_tx.buf_len += pkt_len;					\
	if (!a->mpa_tx.pkt_cnt)						\
		a->mpa_tx.start_port = port;				\
	if (a->mpa_tx.start_port <= port)				\
		a->mpa_tx.ports |= (1<<(a->mpa_tx.pkt_cnt));		\
	else								\
		a->mpa_tx.ports |= (1<<(a->mpa_tx.pkt_cnt+1+		\
						(a->max_ports -	\
						a->mp_end_port)));	\
	a->mpa_tx.pkt_cnt++;						\
} while (0)

/* SDIO Tx aggregation limit ? */
#define MP_TX_AGGR_PKT_LIMIT_REACHED(a)					\
			(a->mpa_tx.pkt_cnt == a->mpa_tx.pkt_aggr_limit)

/* Reset SDIO Tx aggregation buffer parameters */
#define MP_TX_AGGR_BUF_RESET(a) do {					\
	a->mpa_tx.pkt_cnt = 0;						\
	a->mpa_tx.buf_len = 0;						\
	a->mpa_tx.ports = 0;						\
	a->mpa_tx.start_port = 0;					\
} while (0)


/* SDIO Rx aggregation limit ? */
#define MP_RX_AGGR_PKT_LIMIT_REACHED(a)					\
			(a->mpa_rx.pkt_cnt == a->mpa_rx.pkt_aggr_limit)

/* SDIO Rx aggregation in progress ? */
#define MP_RX_AGGR_IN_PROGRESS(a) (a->mpa_rx.pkt_cnt > 0)

/* SDIO Rx aggregation buffer room for next packet ? */
#define MP_RX_AGGR_BUF_HAS_ROOM(a, rx_len)				\
			((a->mpa_rx.buf_len+rx_len) <= a->mpa_rx.buf_size)

/* Reset SDIO Rx aggregation buffer parameters */
#define MP_RX_AGGR_BUF_RESET(a) do {					\
	a->mpa_rx.pkt_cnt = 0;						\
	a->mpa_rx.buf_len = 0;						\
	a->mpa_rx.ports = 0;						\
	a->mpa_rx.start_port = 0;					\
} while (0)

struct mwl_rxinfo {
	struct sk_buff *parent;
	u8 bss_num;
	u8 bss_type;
	u8 use_count;
	u8 buf_type;
};

struct mwl_txinfo {
	u32 status_code;
	u8 flags;
	u8 bss_num;
	u8 bss_type;
	u8 aggr_num;
	u32 pkt_len;
	u8 ack_frame_id;
	u64 cookie;
};

struct mwl_cb {
	union {
		struct mwl_rxinfo rx_info;
		struct mwl_txinfo tx_info;
	};
};

struct mwl_sdio_card_reg {
	u8 start_rd_port;
	u8 start_wr_port;
	u8 base_0_reg;
	u8 base_1_reg;
	u8 poll_reg;
	u8 host_int_enable;
	u8 host_int_rsr_reg;
	u8 host_int_status_reg;
	u8 host_int_mask_reg;
	u8 status_reg_0;
	u8 status_reg_1;
	u8 sdio_int_mask;
	u32 data_port_mask;
	u8 io_port_0_reg;
	u8 io_port_1_reg;
	u8 io_port_2_reg;
	u8 max_mp_regs;
	u8 rd_bitmap_l;
	u8 rd_bitmap_u;
	u8 rd_bitmap_1l;
	u8 rd_bitmap_1u;
	u8 wr_bitmap_l;
	u8 wr_bitmap_u;
	u8 wr_bitmap_1l;
	u8 wr_bitmap_1u;
	u8 rd_len_p0_l;
	u8 rd_len_p0_u;
	u8 card_misc_cfg_reg;
	u8 card_cfg_2_1_reg;
	u8 cmd_rd_len_0;
	u8 cmd_rd_len_1;
	u8 cmd_rd_len_2;
	u8 cmd_rd_len_3;
	u8 cmd_cfg_0;
	u8 cmd_cfg_1;
	u8 cmd_cfg_2;
	u8 cmd_cfg_3;
	u8 fw_dump_host_ready;
	u8 fw_dump_ctrl;
	u8 fw_dump_start;
	u8 fw_dump_end;
	u8 func1_dump_reg_start;
	u8 func1_dump_reg_end;
	u8 func1_scratch_reg;
	u8 func1_spec_reg_num;
	u8 func1_spec_reg_table[MWL_MAX_FUNC2_REG_NUM];
};

static const struct mwl_sdio_card_reg mwl_reg_sd8897 = {
	.start_rd_port = 0,
	.start_wr_port = 0,
	.base_0_reg = 0x60,
	.base_1_reg = 0x61,
	.poll_reg = 0x50,
	.host_int_enable = UP_LD_HOST_INT_MASK | DN_LD_HOST_INT_MASK |
			CMD_PORT_UPLD_INT_MASK | CMD_PORT_DNLD_INT_MASK,
	.host_int_rsr_reg = 0x1,
	.host_int_status_reg = 0x03,
	.host_int_mask_reg = 0x02,
	.status_reg_0 = 0xc0,
	.status_reg_1 = 0xc1,
	.sdio_int_mask = 0xff,
	.data_port_mask = 0xffffffff,
	.io_port_0_reg = 0xD8,
	.io_port_1_reg = 0xD9,
	.io_port_2_reg = 0xDA,
	.max_mp_regs = 184,
	.rd_bitmap_l = 0x04,
	.rd_bitmap_u = 0x05,
	.rd_bitmap_1l = 0x06,
	.rd_bitmap_1u = 0x07,
	.wr_bitmap_l = 0x08,
	.wr_bitmap_u = 0x09,
	.wr_bitmap_1l = 0x0a,
	.wr_bitmap_1u = 0x0b,
	.rd_len_p0_l = 0x0c,
	.rd_len_p0_u = 0x0d,
	.card_misc_cfg_reg = 0xcc,
	.card_cfg_2_1_reg = 0xcd,
	.cmd_rd_len_0 = 0xb4,
	.cmd_rd_len_1 = 0xb5,
	.cmd_rd_len_2 = 0xb6,
	.cmd_rd_len_3 = 0xb7,
	.cmd_cfg_0 = 0xb8,
	.cmd_cfg_1 = 0xb9,
	.cmd_cfg_2 = 0xba,
	.cmd_cfg_3 = 0xbb,
	.fw_dump_host_ready = 0xee,
	.fw_dump_ctrl = 0xe2,
	.fw_dump_start = 0xe3,
	.fw_dump_end = 0xea,
	.func1_dump_reg_start = 0x0,
	.func1_dump_reg_end = 0xb,
	.func1_scratch_reg = 0xc0,
	.func1_spec_reg_num = 8,
	.func1_spec_reg_table = {0x4C, 0x50, 0x54, 0x55, 0x58,
				 0x59, 0x5c, 0x5d},
};

static const struct mwl_sdio_card_reg mwl_reg_sd8997 = {
	.start_rd_port = 0,
	.start_wr_port = 0,
	.base_0_reg = 0xf8,
	.base_1_reg = 0xf9,
	.poll_reg = 0x5c,
	.host_int_enable = UP_LD_HOST_INT_MASK | DN_LD_HOST_INT_MASK |
			CMD_PORT_UPLD_INT_MASK | CMD_PORT_DNLD_INT_MASK,
	.host_int_rsr_reg = 0x4,
	.host_int_status_reg = 0x0c,
	.host_int_mask_reg = 0x08,
	.status_reg_0 = 0xe8,
	.status_reg_1 = 0xe9,
	.sdio_int_mask = 0xff,
	.data_port_mask = 0xffffffff,
	.io_port_0_reg = 0xE4,
	.io_port_1_reg = 0xE5,
	.io_port_2_reg = 0xE6,
	.max_mp_regs = 196,
	.rd_bitmap_l = 0x10,
	.rd_bitmap_u = 0x11,
	.rd_bitmap_1l = 0x12,
	.rd_bitmap_1u = 0x13,
	.wr_bitmap_l = 0x14,
	.wr_bitmap_u = 0x15,
	.wr_bitmap_1l = 0x16,
	.wr_bitmap_1u = 0x17,
	.rd_len_p0_l = 0x18,
	.rd_len_p0_u = 0x19,
	.card_misc_cfg_reg = 0xd8,
	.card_cfg_2_1_reg = 0xd9,
	.cmd_rd_len_0 = 0xc0,
	.cmd_rd_len_1 = 0xc1,
	.cmd_rd_len_2 = 0xc2,
	.cmd_rd_len_3 = 0xc3,
	.cmd_cfg_0 = 0xc4,
	.cmd_cfg_1 = 0xc5,
	.cmd_cfg_2 = 0xc6,
	.cmd_cfg_3 = 0xc7,
	.fw_dump_host_ready = 0xee,
	.fw_dump_ctrl = 0xe2,
	.fw_dump_start = 0xe3,
	.fw_dump_end = 0xea,
	.func1_dump_reg_start = 0x0,
	.func1_dump_reg_end = 0xb,
	.func1_scratch_reg = 0xc0,
	.func1_spec_reg_num = 8,
	.func1_spec_reg_table = {0x4C, 0x50, 0x54, 0x55, 0x58,
				 0x59, 0x5c, 0x5d},
};


/* data structure for SDIO MPA TX */
struct mwifiex_sdio_mpa_tx {
	/* multiport tx aggregation buffer pointer */
	u8 *buf;
	u32 buf_len;
	u32 pkt_cnt;
	u32 ports;
	u16 start_port;
	u8 enabled;
	u32 buf_size;
	u32 pkt_aggr_limit;
};

struct mwifiex_sdio_mpa_rx {
	u8 *buf;
	u32 buf_len;
	u32 pkt_cnt;
	u32 ports;
	u16 start_port;

	struct sk_buff **skb_arr;
	u32 *len_arr;

	u8 enabled;
	u32 buf_size;
	u32 pkt_aggr_limit;
};

/* 16 bit SDIO event code */
#define SDEVENT_RADAR_DETECT				0x0001
#define SDEVENT_CHNL_SWITCH					0x0002
#define SDEVENT_BA_WATCHDOG					0x0003

struct mwl_host_event_mac_t {
	u16	event_id;
};

struct mwl_hostevent {
	u32 next;		/* Used in firmware only */
	u32 callback;		/* Used in firmware only */
	u16 type;
	u16 length;
	union {
		struct mwl_host_event_mac_t mac_event;
	};
};

struct mwl_wait_queue {
	wait_queue_head_t wait;
	int status;
};

struct mwl_sdio_card {
	struct mwl_priv *priv;
	struct sdio_func *func;
	struct device *dev;
	const char *firmware;
	const struct mwl_sdio_card_reg *reg;
	struct sk_buff_head rx_data_q;
	spinlock_t int_lock;
	spinlock_t rx_proc_lock;
	u8 int_status;
	struct workqueue_struct *tx_workq;
	struct work_struct tx_work;
	/*
	 *	Variables for data path
	 */
	u8 max_ports;			/* max port on card. = 32 for KF */
	u8 mp_agg_pkt_limit;		/* max aggregated pkts, = 16 for KF*/
	u16 tx_buf_size;		/* tx buffer size = 4k for KF */
	u32 mp_tx_agg_buf_size;		/* = 65280 for KF? */
	u32 mp_rx_agg_buf_size;		/* = 65280 for KF? */

	u32 mp_rd_bitmap;	/* =[rd_bitmap_1u ~ rd_bitmap_l] from reg*/
	u32 mp_wr_bitmap;	/* =[wr_bitmap_1u ~ reg->wr_bitmap_l] */

	u16 mp_end_port;		/* = 0x0020 for KF */
	u32 mp_data_port_mask;

	u8 curr_rd_port;
	u8 curr_wr_port;

	u8 *mp_regs;
	struct mwifiex_sdio_mpa_tx mpa_tx;
	struct mwifiex_sdio_mpa_rx mpa_rx;
	u16 sdio_rx_block_size;
	u8 data_sent;
	u8 data_received;

	/*
	 */
	bool is_suspended;
	bool hs_enabling;
	bool host_disable_sdio_rx_aggr;
	bool sdio_rx_aggr_enable;
	bool rx_work_enabled;
	atomic_t rx_pending;

	int chip_type;
	u32 ioport;
	u8 cmd_cond;

	struct mwl_wait_queue cmd_wait_q;
	u16	cmd_id;

	u8 cmd_sent;
	u32 rate_info;
	/* needed for card reset */
	const struct sdio_device_id *dev_id;
};

static inline bool
mp_tx_aggr_port_limit_reached(struct mwl_sdio_card *card)
{
	u16 tmp;

	if (card->curr_wr_port < card->mpa_tx.start_port) {
			tmp = card->mp_end_port >> 1;

		if (((card->max_ports - card->mpa_tx.start_port) +
		    card->curr_wr_port) >= tmp)
			return true;
	}

	if ((card->curr_wr_port - card->mpa_tx.start_port) >=
	    (card->mp_end_port >> 1))
		return true;

	return false;
}

static inline bool
mp_rx_aggr_port_limit_reached(struct mwl_sdio_card *card)
{
	u8 tmp;

	if (card->curr_rd_port < card->mpa_rx.start_port) {
		tmp = card->mp_end_port >> 1;

		if (((card->max_ports - card->mpa_rx.start_port) +
		    card->curr_rd_port) >= tmp)
			return true;
	}


	if ((card->curr_rd_port - card->mpa_rx.start_port) >=
	    (card->mp_end_port >> 1))
		return true;

	return false;
}


/* Prepare to copy current packet from card to SDIO Rx aggregation buffer */
static inline void mp_rx_aggr_setup(struct mwl_sdio_card *card,
				    u16 rx_len, u8 port)
{
	card->mpa_rx.buf_len += rx_len;

	if (!card->mpa_rx.pkt_cnt)
		card->mpa_rx.start_port = port;

	card->mpa_rx.ports |= (1 << port);
	card->mpa_rx.skb_arr[card->mpa_rx.pkt_cnt] = NULL;
	card->mpa_rx.len_arr[card->mpa_rx.pkt_cnt] = rx_len;
	card->mpa_rx.pkt_cnt++;
}

static inline struct mwl_rxinfo *MWL_SKB_RXCB(struct sk_buff *skb)
{
	struct mwl_cb *cb = (struct mwl_cb *)skb->cb;

	BUILD_BUG_ON(sizeof(struct mwl_cb) > sizeof(skb->cb));
	return &cb->rx_info;
}

static inline struct mwl_txinfo *MWL_SKB_TXCB(struct sk_buff *skb)
{
	struct mwl_cb *cb = (struct mwl_cb *)skb->cb;

	return &cb->tx_info;
}

void mwl_handle_rx_packet(struct mwl_priv *priv, struct sk_buff *skb);

#endif /* _MWLWIFI_SDIO_H */
