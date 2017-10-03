
#include "port.h"

#define IS_PFU_ENABLED(ct) (ct == MWL8997)

/* PCIE read data pointer for queue 0 and 1 */
#define PCIE_RD_DATA_PTR_Q0_Q1          0xC1A4 /* 0x8000C1A4 */
/* PCIE read data pointer for queue 2 and 3 */
#define PCIE_RD_DATA_PTR_Q2_Q3          0xC1A8 /* 0x8000C1A8 */
/* PCIE write data pointer for queue 0 and 1 */
#define PCIE_WR_DATA_PTR_Q0_Q1          0xC174 /* 0x8000C174 */
/* PCIE write data pointer for queue 2 and 3 */
#define PCIE_WR_DATA_PTR_Q2_Q3          0xC178 /* 0x8000C178 */


/* TX buffer description read pointer */
#define REG_TXBD_RDPTR                  PCIE_RD_DATA_PTR_Q0_Q1
/* TX buffer description write pointer */
#define REG_TXBD_WRPTR                  PCIE_WR_DATA_PTR_Q0_Q1


#define PCIE_TX_START_PTR               16

#define MLAN_TXBD_MASK                  0x0FFF0000
#define MLAN_TXBD_WRAP_MASK             0x1FFF0000


#define MLAN_BD_FLAG_RX_ROLLOVER_IND    MBIT(12)
#define MLAN_BD_FLAG_TX_START_PTR       MBIT(16)
#define MLAN_BD_FLAG_TX_ROLLOVER_IND    MBIT(28)
#define MLAN_BD_FLAG_TX2_START_PTR      MBIT(0)
#define MLAN_BD_FLAG_TX2_ROLLOVER_IND   MBIT(12)

#define MLAN_BD_FLAG_FIRST_DESC         MBIT(0)
#define MLAN_BD_FLAG_LAST_DESC          MBIT(1)


#define MLAN_MAX_TXRX_BD         0x20



#define PCIE_TXBD_NOT_FULL(wrptr, rdptr)                    \
	(((wrptr & MLAN_TXBD_MASK) != (rdptr & MLAN_TXBD_MASK)) \
	 || ((wrptr & MLAN_BD_FLAG_TX_ROLLOVER_IND) ==          \
	     (rdptr & MLAN_BD_FLAG_TX_ROLLOVER_IND)))


MLAN_PACK_START struct _mlan_pcie_data_buf {
	 /** Buffer descriptor flags */
	 unsigned short   flags;
	 /** Offset of fragment/pkt to start of ip header */
	 unsigned short   offset;
	 /** Fragment length of the buffer */
	 unsigned short   frag_len;
	 /** Length of the buffer */
	 unsigned short   len;
	 /** Physical address of the buffer */
	 unsigned long long   paddr;
	 /** Reserved */
	 unsigned int   reserved;
} MLAN_PACK_END;

int wlan_pcie_create_txbd_ring(struct ieee80211_hw *hw);
int wlan_pcie_delete_txbd_ring(struct ieee80211_hw *hw);
