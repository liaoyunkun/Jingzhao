/**************************************************************
 * @author Kang Ning<kangning18z@ict.ac.cn>, NCIC, ICT, CAS
 * @date 2021.09.08
 * @file hgrnic_eq.c
 * @note Functions & Data structs related to EQ.
 *************************************************************/

#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include "hgrnic_dev.h"
#include "hgrnic_cmd.h"
#include "hgrnic_iface.h"

enum {
	HGRNIC_NUM_ASYNC_EQE = 0x80,
	HGRNIC_NUM_CMD_EQE   = 0x80,
	HGRNIC_NUM_SPARE_EQE = 0x80,
	HGRNIC_EQ_ENTRY_SIZE = 0x20
};

/*
 * Must be packed because start is 64 bits but only aligned to 32 bits.
 */
struct hgrnic_eq_context {
	__be32 flags;
	__be64 start;
	__be32 logsize;
	u8     reserved1[7];
	u8     intr;
	__be32 pd;
	__be32 lkey;
	u32    reserved2[2];
	__be32 consumer_index;
	__be32 producer_index;
	u32    reserved3[4];
} __packed;

#define HGRNIC_EQ_STATUS_OK          ( 0 << 28)
#define HGRNIC_EQ_OWNER_SW           ( 0 << 24)
#define HGRNIC_EQ_OWNER_HW           ( 1 << 24)
#define HGRNIC_EQ_FLAG_TR            ( 1 << 18)
#define HGRNIC_EQ_FLAG_OI            ( 1 << 17)
#define HGRNIC_EQ_STATE_ARMED        ( 1 <<  8)

enum {
	HGRNIC_EVENT_TYPE_COMP       	    = 0x00,
	HGRNIC_EVENT_TYPE_PATH_MIG   	    = 0x01,
	HGRNIC_EVENT_TYPE_COMM_EST   	    = 0x02,
	HGRNIC_EVENT_TYPE_SQ_DRAINED 	    = 0x03,
	HGRNIC_EVENT_TYPE_SRQ_QP_LAST_WQE    = 0x13,
	HGRNIC_EVENT_TYPE_SRQ_LIMIT	    = 0x14,
	HGRNIC_EVENT_TYPE_CQ_ERROR   	    = 0x04,
	HGRNIC_EVENT_TYPE_WQ_CATAS_ERROR     = 0x05,
	HGRNIC_EVENT_TYPE_EEC_CATAS_ERROR    = 0x06,
	HGRNIC_EVENT_TYPE_PATH_MIG_FAILED    = 0x07,
	HGRNIC_EVENT_TYPE_WQ_INVAL_REQ_ERROR = 0x10,
	HGRNIC_EVENT_TYPE_WQ_ACCESS_ERROR    = 0x11,
	HGRNIC_EVENT_TYPE_SRQ_CATAS_ERROR    = 0x12,
	HGRNIC_EVENT_TYPE_LOCAL_CATAS_ERROR  = 0x08,
	HGRNIC_EVENT_TYPE_PORT_CHANGE        = 0x09,
	HGRNIC_EVENT_TYPE_EQ_OVERFLOW        = 0x0f,
	HGRNIC_EVENT_TYPE_ECC_DETECT         = 0x0e,
	HGRNIC_EVENT_TYPE_CMD                = 0x0a
};

#define HGRNIC_ASYNC_EVENT_MASK ((1ULL << HGRNIC_EVENT_TYPE_PATH_MIG)           | \
				(1ULL << HGRNIC_EVENT_TYPE_COMM_EST)           | \
				(1ULL << HGRNIC_EVENT_TYPE_SQ_DRAINED)         | \
				(1ULL << HGRNIC_EVENT_TYPE_CQ_ERROR)           | \
				(1ULL << HGRNIC_EVENT_TYPE_WQ_CATAS_ERROR)     | \
				(1ULL << HGRNIC_EVENT_TYPE_EEC_CATAS_ERROR)    | \
				(1ULL << HGRNIC_EVENT_TYPE_PATH_MIG_FAILED)    | \
				(1ULL << HGRNIC_EVENT_TYPE_WQ_INVAL_REQ_ERROR) | \
				(1ULL << HGRNIC_EVENT_TYPE_WQ_ACCESS_ERROR)    | \
				(1ULL << HGRNIC_EVENT_TYPE_LOCAL_CATAS_ERROR)  | \
				(1ULL << HGRNIC_EVENT_TYPE_PORT_CHANGE)        | \
				(1ULL << HGRNIC_EVENT_TYPE_ECC_DETECT))
#define HGRNIC_SRQ_EVENT_MASK   ((1ULL << HGRNIC_EVENT_TYPE_SRQ_CATAS_ERROR)    | \
				(1ULL << HGRNIC_EVENT_TYPE_SRQ_QP_LAST_WQE)    | \
				(1ULL << HGRNIC_EVENT_TYPE_SRQ_LIMIT))
#define HGRNIC_CMD_EVENT_MASK    (1ULL << HGRNIC_EVENT_TYPE_CMD)


#define  HGRNIC_EQ_ENTRY_OWNER_SW      (0 << 7)
#define  HGRNIC_EQ_ENTRY_OWNER_HW      (1 << 7)





/**
 * @param icm_virt virtual address of ICM space
 * @note Init EQ table stored in struct hgrnic_dev.
 */
int hgrnic_map_eq_icm (struct hgrnic_dev *dev, u64 icm_virt) {
	int ret;

	/*
	 * We assume that mapping one page is enough for the whole EQ
	 * context table. Currently we haven't implemented the mechanism
     * of EQ.
	 */
	dev->eq_table.icm_virt = icm_virt;
	dev->eq_table.icm_page = alloc_page(GFP_HIGHUSER); // allocate only one page
	if (!dev->eq_table.icm_page) {
        return -ENOMEM;
    }

    // Set the page being accessed by DMA.
	dev->eq_table.icm_dma  = pci_map_page(dev->pdev, dev->eq_table.icm_page, 0,
					      PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
	if (pci_dma_mapping_error(dev->pdev, dev->eq_table.icm_dma)) {
		__free_page(dev->eq_table.icm_page);
		return -ENOMEM;
	}

    // Write allocated ICM addr (bus addr, and its attached 
    // ICM virtual Addr) for EQ to Hardware.
    printk(KERN_INFO PFX "Start hgrnic_MAP_ICM_page for EQ.\n");
	ret = hgrnic_MAP_ICM_page(dev, dev->eq_table.icm_dma, 
            CXT_REGION, icm_virt);
	if (ret) {
		pci_unmap_page(dev->pdev, dev->eq_table.icm_dma, PAGE_SIZE,
			       PCI_DMA_BIDIRECTIONAL);
		__free_page(dev->eq_table.icm_page);
	}

	return ret;
}

void hgrnic_unmap_eq_icm (struct hgrnic_dev *dev) {
	
    hgrnic_UNMAP_ICM(dev, dev->eq_table.icm_virt, 1, CXT_REGION);
	pci_unmap_page(dev->pdev, dev->eq_table.icm_dma, PAGE_SIZE,
		       PCI_DMA_BIDIRECTIONAL);
	__free_page(dev->eq_table.icm_page);
}
