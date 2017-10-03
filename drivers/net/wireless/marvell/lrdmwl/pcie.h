#ifndef _PCIE_H_
#define _PCIE_H_

#include <linux/pci.h>
#include <linux/pcieport_if.h>
#include <linux/interrupt.h>

#include "main.h"

#define MAC_REG_ADDR(offset)      (offset)
#define MAC_REG_ADDR_PCI(offset)  ((card->iobase1 + 0xA000) + offset)

#define MCU_CCA_CNT               MAC_REG_ADDR(0x06A0)
#define MCU_TXPE_CNT              MAC_REG_ADDR(0x06A4)
#define MCU_LAST_READ             MAC_REG_ADDR(0x06A8)

#define MAC_REG_TCQ0_WRPTR		MAC_REG_ADDR(0xa040)
#define MAC_REG_TCQ0_RDPTR		MAC_REG_ADDR(0xa044)

#define MAC_REG_CW0_MIN		MAC_REG_ADDR(0xa0a0)
#define MAC_REG_CW0_MAX		MAC_REG_ADDR(0xa0a4)
#define MAC_REG_TXOP0		MAC_REG_ADDR(0xa260)
#define MAC_REG_AIFSN0		MAC_REG_ADDR(0xa680)

struct mwl_pcie_card {
	struct mwl_priv *priv;
	struct pci_dev *pdev;
	int chip_type;
	void __iomem *iobase0; /* MEM Base Address Register 0  */
	void __iomem *iobase1; /* MEM Base Address Register 1  */
	u32 next_bar_num;
	struct mwl_desc_data desc_data[SYSADPT_NUM_OF_DESC_DATA];
	/* number of descriptors owned by fw at any one time */
	int fw_desc_cnt[SYSADPT_NUM_OF_DESC_DATA];
};

void mwl_pcie_tx_done(unsigned long data);
void mwl_pcie_rx_recv(unsigned long data);

#endif
