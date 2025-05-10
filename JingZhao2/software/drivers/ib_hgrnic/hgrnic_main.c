/**************************************************************
 * @author Kang Ning<kangning18z@ict.ac.cn>, NCIC, ICT, CAS
 * @date 2021.08.30
 * @file hgrnic_main.c
 * @note Configuration and initialization on the RNIC hardware.
 *************************************************************/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/gfp.h>

// Used to delay a given time to wait the finishes of data transmission
#include <linux/delay.h>

#include "hgrnic_dev.h"
#include "hgrnic_iface.h"
#include "hgrnic_cmd.h"
#include "hgrnic_icm.h"
#include "hgrnic_register.h"

MODULE_AUTHOR("Kang Ning");
MODULE_DESCRIPTION("HanGu RDMA NIC low-level driver");
MODULE_LICENSE("Dual BSD/GPL");


static int config_rdma = 1;
module_param(config_rdma, int, 0444);
MODULE_PARM_DESC(config_rdma, "Active RDMA function if non-zero.");

// Protect multi devices from executing 
// the code at the same time.
DEFINE_MUTEX(hgrnic_device_mutex);

#define HGRNIC_DEFAULT_NUM_QP            (1 << 16)
#define HGRNIC_DEFAULT_RDB_PER_QP        (1 << 2)
#define HGRNIC_DEFAULT_NUM_CQ            (1 << 16)
#define HGRNIC_DEFAULT_NUM_MCG           (1 << 13)
#define HGRNIC_DEFAULT_NUM_MPT           (1 << 17)
#define HGRNIC_DEFAULT_NUM_MTT           (1 << 20)
#define HGRNIC_DEFAULT_NUM_UDAV          (1 << 15)
#define HGRNIC_DEFAULT_NUM_RESERVED_MTTS (1 << 18)
#define HGRNIC_DEFAULT_NUM_UARC_SIZE     (1 << 18)

static struct hgrnic_profile hca_profile = {
    .num_qp             = HGRNIC_DEFAULT_NUM_QP,
    .num_cq             = HGRNIC_DEFAULT_NUM_CQ,
    .num_mpt            = HGRNIC_DEFAULT_NUM_MPT,
    .num_mtt            = HGRNIC_DEFAULT_NUM_MTT,
};

module_param_named(num_qp, hca_profile.num_qp, int, 0444);
MODULE_PARM_DESC(num_qp, "maximum number of QPs per HCA");

module_param_named(num_cq, hca_profile.num_cq, int, 0444);
MODULE_PARM_DESC(num_cq, "maximum number of CQs per HCA");

module_param_named(num_mpt, hca_profile.num_mpt, int, 0444);
MODULE_PARM_DESC(num_mpt,
		"maximum number of memory protection table entries per HCA");

module_param_named(num_mtt, hca_profile.num_mtt, int, 0444);
MODULE_PARM_DESC(num_mtt,
		 "maximum number of memory translation table segments per HCA");

// static int log_mtts_per_seg = ilog2(HGRNIC_MTT_SEG_SIZE / 8);
// module_param_named(log_mtts_per_seg, log_mtts_per_seg, int, 0444);
// MODULE_PARM_DESC(log_mtts_per_seg, "Log2 number of MTT entries per segment (1-5)");

static char hgrnic_version[] =
	DRV_NAME ": ICT InfiniBand HCA driver v"
	DRV_VERSION " (" DRV_RELDATE ")\n";

/** 
 * @description:
 *  Set PCI burst from the default to 4096.
 */
static int hgrnic_tune_pci (struct hgrnic_dev *hgdev) {

    if (pci_is_pcie(hgdev->pdev)) {
        if (pcie_set_readrq(hgdev->pdev, 4096)) {
            hgrnic_err(hgdev, "Couldn't write PCI Express read request, "
                "aborting.\n");
            return -ENODEV;
        }
    } else if (hgdev->hgrnic_flags & HGRNIC_FLAG_PCIE) {
        hgrnic_info(hgdev, "No PCI Express capability, "
                "not setting Max Read Request Size.\n");
    }

	return 0;
}

static int hgrnic_dev_lim (struct hgrnic_dev *hgdev, struct hgrnic_dev_lim *dev_lim) {
	int err;

	hgdev->limits.mtt_seg_size = 8;

	err = hgrnic_QUERY_DEV_LIM(hgdev, dev_lim);
	if (err) {
		hgrnic_err(hgdev, "QUERY_DEV_LIM command returned %d"
				", aborting.\n", err);
		return err;
	}
	if (dev_lim->min_page_sz > PAGE_SIZE) {
		hgrnic_err(hgdev, "HCA minimum page size of %d bigger than "
			  "kernel PAGE_SIZE of %ld, aborting.\n",
			  dev_lim->min_page_sz, PAGE_SIZE);
		return -ENODEV;
	}
	if (dev_lim->num_ports > HGRNIC_MAX_PORTS) {
		hgrnic_err(hgdev, "HCA has %d ports, but we only support %d, "
			  "aborting.\n",
			  dev_lim->num_ports, HGRNIC_MAX_PORTS);
		return -ENODEV;
	}

    hgdev->limits.reserved_qps       = dev_lim->reserved_qps;
	hgdev->limits.reserved_cqs       = dev_lim->reserved_cqs;
	hgdev->limits.reserved_eqs       = dev_lim->reserved_eqs;
	hgdev->limits.reserved_mtts      = dev_lim->reserved_mtts;
	hgdev->limits.reserved_mrws      = 0;
	hgdev->limits.reserved_pds       = dev_lim->reserved_pds;

    hgdev->limits.max_wqes           = dev_lim->max_qp_sz;
    
    /**
	 * Subtract 1 from the limit because we need to allocate a
	 * spare CQE so the HCA HW can tell the difference between an
	 * empty CQ and a full CQ. FIXME: What does this mean?
	 */
	hgdev->limits.max_cqes           = dev_lim->max_cq_sz - 1;
    
	hgdev->limits.num_ports      	= dev_lim->num_ports;
	hgdev->limits.vl_cap             = dev_lim->max_vl;
	hgdev->limits.mtu_cap            = dev_lim->max_mtu;
	hgdev->limits.gid_table_len  	= dev_lim->max_gids;
	hgdev->limits.pkey_table_len 	= dev_lim->max_pkeys;
	hgdev->limits.local_ca_ack_delay = dev_lim->local_ca_ack_delay;
	/*
	 * Need to allow for worst case send WQE overhead and check
	 * whether max_desc_sz imposes a lower limit than max_sg; UD
	 * send has the biggest overhead.
	 */
	hgdev->limits.max_sg = 16;
	hgdev->limits.max_desc_sz        = dev_lim->max_desc_sz;
	
    hgdev->limits.port_width_cap     = dev_lim->max_port_width;
	hgdev->limits.page_size_cap      = ~(u32) (dev_lim->min_page_sz - 1);

    hgdev->limits.num_uars   = 0x1000;

	return 0;
}


static int hgrnic_init_icm (struct hgrnic_dev *hgdev, struct hgrnic_dev_lim *dev_lim,
		struct hgrnic_init_hca_param *init_hca) {
	int err;

    printk(KERN_INFO PFX "Start map_eq_icm.\n");
	err = hgrnic_map_eq_icm(hgdev, init_hca->eqc_base);
	if (err) {
		hgrnic_err(hgdev, "Failed to map EQ context memory, aborting.\n");
		return err;
	}

    printk(KERN_INFO PFX "Start map_qp_icm. resved_qps: %d\n", hgdev->limits.reserved_qps);
    hgdev->qp_table.qp_table = hgrnic_alloc_icm_table(hgdev, init_hca->qpc_base,
                dev_lim->qpc_entry_sz, hgdev->limits.num_qps, 
                hgdev->limits.reserved_qps, 0, 0, CXT_REGION);
	if (!hgdev->qp_table.qp_table) {
		hgrnic_err(hgdev, "Failed to map QP context memory, aborting.\n");
		err = -ENOMEM;
		goto err_unmap_eq;
	}

    printk(KERN_INFO PFX "Start map_cq_icm. resved_cqs: %d\n", hgdev->limits.reserved_cqs);
	hgdev->cq_table.table = hgrnic_alloc_icm_table(hgdev, init_hca->cqc_base,
                dev_lim->cqc_entry_sz, hgdev->limits.num_cqs,
                hgdev->limits.reserved_cqs, 0, 0, CXT_REGION);
	if (!hgdev->cq_table.table) {
		hgrnic_err(hgdev, "Failed to map CQ context memory, aborting.\n");
		err = -ENOMEM;
		goto err_unmap_qp;
	}

	printk(KERN_INFO PFX "Start map_mtt_icm. resved_mtts: %d\n", hgdev->limits.reserved_mtts);
    hgdev->limits.reserved_mtts = 0;
	hgdev->mr_table.mtt_table = hgrnic_alloc_icm_table(hgdev, init_hca->mtt_base,
                hgdev->limits.mtt_seg_size, hgdev->limits.num_mtt_segs,
                1, 1, 0, TPT_REGION);
	if (!hgdev->mr_table.mtt_table) {
		hgrnic_err(hgdev, "Failed to map MTT context memory, aborting.\n");
		err = -ENOMEM;
		goto err_unmap_cq;
	}

    printk(KERN_INFO PFX "Start map_mpt_icm. resved_mpt: %d\n", hgdev->limits.reserved_mrws);
	hgdev->mr_table.mpt_table = hgrnic_alloc_icm_table(hgdev, init_hca->mpt_base,
                dev_lim->mpt_entry_sz, hgdev->limits.num_mpts,
                1, 1, 1, TPT_REGION);
	if (!hgdev->mr_table.mpt_table) {
		hgrnic_err(hgdev, "Failed to map MPT context memory, aborting.\n");
		err = -ENOMEM;
		goto err_unmap_mtt;
	}

	return 0;

err_unmap_mtt:
	hgrnic_free_icm_table(hgdev, hgdev->mr_table.mtt_table, TPT_REGION);

err_unmap_cq:
    hgrnic_free_icm_table(hgdev, hgdev->cq_table.table, CXT_REGION);

err_unmap_qp:
	hgrnic_free_icm_table(hgdev, hgdev->qp_table.qp_table, CXT_REGION);

err_unmap_eq:
	hgrnic_unmap_eq_icm(hgdev);
	return err;
}

static void hgrnic_free_icms (struct hgrnic_dev *hgdev) {

	hgrnic_free_icm_table(hgdev, hgdev->mr_table.mpt_table, TPT_REGION);
	hgrnic_free_icm_table(hgdev, hgdev->mr_table.mtt_table, TPT_REGION);
	hgrnic_free_icm_table(hgdev, hgdev->cq_table.table, CXT_REGION);
	hgrnic_free_icm_table(hgdev, hgdev->qp_table.qp_table, CXT_REGION);
    hgrnic_unmap_eq_icm(hgdev);
}

/** 
 * @description:
 * Initialize HCA resource. Write ICM memory layout to 
 * HCA, and Initialize ICM allocation 
 * 
 */
static int hgrnic_init_hca (struct hgrnic_dev *hgdev) {
	int err;

	struct hgrnic_dev_lim        dev_lim;
	struct hgrnic_init_hca_param init_hca;
	s64 icm_size;

    printk(KERN_INFO PFX "Start QUERY ADAPTER.\n");
    

    err = hgrnic_QUERY_ADAPTER(hgdev, &hgdev->board_id);
	if (err) {
		hgrnic_err(hgdev, "QUERY_ADAPTER command returned %d, aborting.\n", err);
		return err;
	}

    printk(KERN_INFO PFX "Start dev lim.\n");
	err = hgrnic_dev_lim(hgdev, &dev_lim);
	if (err) {
		hgrnic_err(hgdev, "QUERY_DEV_LIM returned %d, aborting.\n", err);
		return err;
	}

	icm_size = hgrnic_make_profile(hgdev, &hca_profile, &dev_lim, &init_hca);
	if (icm_size < 0) {
		err = icm_size;
		return err;
	}
    printk(KERN_INFO PFX "Start Init_HCA.\n");
    err = hgrnic_INIT_HCA(hgdev, &init_hca);
	if (err) {
		hgrnic_err(hgdev, "INIT_HCA command returned %d, aborting.\n", err);
		return err;
	}

    printk(KERN_INFO PFX "Start init_icm.\n");
    err = hgrnic_init_icm(hgdev, &dev_lim, &init_hca);
	if (err) {
        goto err_close;
    }

	return 0;

err_close:
	hgrnic_CLOSE_HCA(hgdev);
	return err;
}

static int hgrnic_setup_hca (struct hgrnic_dev *dev) {
	int err;

	HGRNIC_INIT_DOORBELL_LOCK(&dev->doorbell_lock);

    /**
     * Initialize UAR Space (PIO BAR2 space) allocator.
     */
    printk(KERN_INFO PFX "Start hgrnic_init_uar_table.\n");
	err = hgrnic_init_uar_table(dev);
	if (err) {
		hgrnic_err(dev, "Failed to initialize "
			  "user access region table, aborting.\n");
		return err;
	}
    printk(KERN_INFO PFX "Start hgrnic_uar_alloc.\n");
	err = hgrnic_uar_alloc(dev, &dev->driver_uar);
	if (err) {
		hgrnic_err(dev, "Failed to allocate driver access region, "
			  "aborting.\n");
		goto err_uar_table_free;
	}

    // Get Kernel Access Region in BAR2.
    printk(KERN_INFO PFX "Start ioremap.\n");
	dev->kar = ioremap((phys_addr_t) dev->driver_uar.pfn << PAGE_SHIFT, PAGE_SIZE);
	if (!dev->kar) {
		hgrnic_err(dev, "Couldn't map kernel access region, "
			  "aborting.\n");
		err = -ENOMEM;
		goto err_uar_free;
	}

    printk(KERN_INFO PFX "Start hgrnic_init_pd_table.\n");
	err = hgrnic_init_pd_table(dev);
	if (err) {
		hgrnic_err(dev, "Failed to initialize "
			  "protection domain table, aborting.\n");
		goto err_kar_unmap;
	}

    printk(KERN_INFO PFX "Start hgrnic_init_mr_table.\n");
	err = hgrnic_init_mr_table(dev);
	if (err) {
		hgrnic_err(dev, "Failed to initialize "
			  "memory region table, aborting.\n");
		goto err_pd_table_free;
	}

    /** 
     * PD allocation need MR table, so this function must be 
     * called after init_mr_table.
     */
    printk(KERN_INFO PFX "Start hgrnic_pd_alloc.\n");
	err = hgrnic_pd_alloc(dev, 1, &dev->driver_pd);
    (dev->driver_pd).ibpd.device = &(dev->ib_dev);
	if (err) {
		hgrnic_err(dev, "Failed to create driver PD, "
			  "aborting.\n");
		goto err_mr_table_free;
	}

    printk(KERN_INFO PFX "Start hgrnic_init_cq_table.\n");
	err = hgrnic_init_cq_table(dev);
	if (err) {
		hgrnic_err(dev, "Failed to initialize "
			  "completion queue table, aborting.\n");
		goto err_pd_free;
	}

    printk(KERN_INFO PFX "Start hgrnic_init_qp_table.\n");
	err = hgrnic_init_qp_table(dev);
	if (err) {
		hgrnic_err(dev, "Failed to initialize "
			  "queue pair table, aborting.\n");
		goto err_cq_table_free;
	}

	return 0;


err_cq_table_free:
	hgrnic_cleanup_cq_table(dev);

err_pd_free:
	hgrnic_pd_free(dev, &dev->driver_pd);

err_mr_table_free:
	hgrnic_cleanup_mr_table(dev);

err_pd_table_free:
	hgrnic_cleanup_pd_table(dev);

err_kar_unmap:
	iounmap(dev->kar);

err_uar_free:
	hgrnic_uar_free(dev, &dev->driver_uar);

err_uar_table_free:
	hgrnic_cleanup_uar_table(dev);
	return err;
}




#define HANGU_FW_VER(major, minor, subminor) \
    (((u64) (major) << 32) | ((u64) (minor) << 16) | (u64) (subminor))

static struct {
    u64 latest_fw;
    u32 flags;
} hgrnic_hca_table[] = {
    [HANGU_RNIC]  = { .latest_fw = HANGU_FW_VER(1, 0, 0),
                      .flags     = HGRNIC_FLAG_PCIE    }
};

static int __hgrnic_init_one (struct pci_dev *pdev, int hca_type) {
    int err;
    struct hgrnic_dev *hgdev;
    struct hgrnic_cq cq;
    u8 i;

	printk(KERN_INFO PFX "Initializing %s\n",
	       pci_name(pdev));

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Cannot enable PCI device, "
			"aborting.\n");
		return err;
	}

	/**
	 * Check for BARs.  We expect 0: 1MB, 2: 8MB
	 */
	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM) ||
	    pci_resource_len(pdev, 0) != 1 << 20) {
		dev_err(&pdev->dev, "Missing HCRs, aborting.\n");
		err = -ENODEV;
		goto err_disable_pdev;
	}
	if (!(pci_resource_flags(pdev, 2) & IORESOURCE_MEM)) {
		dev_err(&pdev->dev, "Missing UAR, aborting.\n");
		err = -ENODEV;
		goto err_disable_pdev;
	}

    /**
     * Set IORESOURCE_BUSY. From now on, BAR resources is dedicated 
     * to this driver.
     */
	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(&pdev->dev, "Cannot obtain PCI resources, "
			"aborting.\n");
		goto err_disable_pdev;
	}

    /**
     * Set PCIe device as a master device, which could execute
     * DMA ops later.
     */ 
	pci_set_master(pdev);

    /**
     * Set DMA mask which PCIe device uses. 
     */
	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err) {
		dev_warn(&pdev->dev, "Warning: couldn't set 64-bit PCI DMA mask.\n");
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev, "Can't set PCI DMA mask, aborting.\n");
			goto err_free_res;
		}
	}
	err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err) {
		dev_warn(&pdev->dev, "Warning: couldn't set 64-bit "
			 "consistent PCI DMA mask.\n");
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev, "Can't set consistent PCI DMA mask, "
				"aborting.\n");
			goto err_free_res;
		}
	}

	/** 
     * We can handle large RDMA requests, so allow larger segments. 
     * !DEBUG: We don't need it now, maybe fix when debugging.
     */
	// dma_set_max_seg_size(&pdev->dev, 1024 * 1024 * 1024);

    hgdev = ib_alloc_device(hgrnic_dev, ib_dev);
    if (!hgdev) {
        dev_err(&pdev->dev, "Device struct alloc failed, "
            "aborting.\n");
        err = -ENOMEM;
        goto err_free_res;
    }
    hgdev->pdev = pdev;
    hgdev->hgrnic_flags = hgrnic_hca_table[hca_type].flags;

    /** 
     * Allocate virtual address to pcie BAR0 space, and allocate 
     * memory for mailbox.
     */
    err = hgrnic_cmd_init(hgdev);
    if (err) {
        hgrnic_err(hgdev, "Failed to init command interface, aborting.\n");
        goto err_free_dev;
    }

    err = hgrnic_tune_pci(hgdev);
    if (err) {
        goto err_cmd;
    }

    hgrnic_init_en_register(hgdev);

    if (config_rdma) {

        printk(KERN_INFO PFX "Start hgrnic_init_hca.\n");
        err = hgrnic_init_hca(hgdev);
        if (err) {
            goto err_cmd;
        }

        printk(KERN_INFO PFX "Start hgrnic_setup_hca.\n");
        err = hgrnic_setup_hca(hgdev);
        if (err) {
            goto err_close;
        }

        /* allocate cq0 in kernel space */
        hgrnic_init_cq(hgdev, 32, NULL, hgdev->driver_pd.pd_num, &cq);

        printk(KERN_INFO PFX "Start hgrnic_register_device.\n");
        err = hgrnic_register_device(hgdev);
        if (err) {
            goto err_cleanup;
        }
    } else {
        printk(KERN_INFO PFX "RDMA function unconfigured.\n");
    }

    printk(KERN_INFO PFX "Start pci_set_drvdata.\n");
    pci_set_drvdata(pdev, hgdev);

    hgdev->hca_type = hca_type;
    hgdev->active = true;
    for (i = 0; i < HG_GID_SIZE; ++i) {
        hgdev->gid[i] = i;
    }

    return 0;

err_cleanup:
    hgrnic_cleanup_qp_table(hgdev);
    hgrnic_cleanup_cq_table(hgdev);

    hgrnic_pd_free(hgdev, &hgdev->driver_pd);

    hgrnic_cleanup_mr_table(hgdev);
    hgrnic_cleanup_pd_table(hgdev);
    hgrnic_cleanup_uar_table(hgdev);

err_close:
    hgrnic_free_icms(hgdev);
    hgrnic_CLOSE_HCA(hgdev);

err_cmd:
	hgrnic_cmd_cleanup(hgdev);

err_free_dev:
	ib_dealloc_device(&hgdev->ib_dev);

err_free_res:
	pci_release_regions(pdev);

err_disable_pdev:
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	return err;
}




static void __hgrnic_remove_one (struct pci_dev *pdev) {
    struct hgrnic_dev *hgdev = pci_get_drvdata(pdev);

    if (hgdev) {

        if (config_rdma) {
            printk(KERN_INFO PFX "HGRNIC (IB) driver has been removed. Bye Bye!\n");

            ib_unregister_device(&hgdev->ib_dev);

            hgrnic_cleanup_qp_table(hgdev);
            hgrnic_cleanup_cq_table(hgdev);
            hgrnic_pd_free(hgdev, &hgdev->driver_pd);
            hgrnic_cleanup_mr_table(hgdev);
            hgrnic_cleanup_pd_table(hgdev);
            hgrnic_cleanup_uar_table(hgdev);

            hgrnic_free_icms(hgdev);
            hgrnic_CLOSE_HCA(hgdev);
        } else {
            printk(KERN_INFO PFX "HGRNIC (Base) driver has been removed. Bye Bye!\n");
        }

        hgrnic_cmd_cleanup(hgdev);
        
        ib_dealloc_device(&hgdev->ib_dev);

        pci_release_regions(pdev);
        pci_disable_device(pdev);
        
        pci_set_drvdata(pdev, NULL);
    }
}

int __hgrnic_restart_one (struct pci_dev *pdev) {
	struct hgrnic_dev *hgdev;
	int hca_type;

	hgdev = pci_get_drvdata(pdev);
	if (!hgdev)
		return -ENODEV;
	hca_type = hgdev->hca_type;
	__hgrnic_remove_one(pdev);
	return __hgrnic_init_one(pdev, hca_type);
}

/**
 * @description: 
 *  Called when HCA or driver up to initialize the device.
 */
static int hgrnic_init_one (struct pci_dev *pdev, const struct pci_device_id *id) {
	int ret;

	mutex_lock(&hgrnic_device_mutex);

	printk_once(KERN_INFO "%s", hgrnic_version);

	if (id->driver_data >= ARRAY_SIZE(hgrnic_hca_table)) {
		printk(KERN_ERR PFX "%s has invalid driver data %lx\n",
		       pci_name(pdev), id->driver_data);
		mutex_unlock(&hgrnic_device_mutex);
		return -ENODEV;
	}

	ret = __hgrnic_init_one(pdev, id->driver_data);

	mutex_unlock(&hgrnic_device_mutex);

	return ret;
}

/**
 * @description: 
 *  Called when Driver or HCA removed.
 */
static void hgrnic_remove_one (struct pci_dev *pdev) {
    mutex_lock(&hgrnic_device_mutex);
    __hgrnic_remove_one(pdev);
    mutex_unlock(&hgrnic_device_mutex);
}

static const struct pci_device_id hgrnic_pci_table[] = {
    { PCI_DEVICE(PCI_VENDOR_ID_ICT, PCI_DEVICE_ID_ICT_HANGU_RNIC),
      .driver_data = HANGU_RNIC },
    { 0, }
};

MODULE_DEVICE_TABLE(pci, hgrnic_pci_table);

static struct pci_driver hgrnic_driver = {
    .name       = DRV_NAME,
    .id_table   = hgrnic_pci_table,
    .probe      = hgrnic_init_one,
    .remove     = hgrnic_remove_one,
};

static void __init __hgrnic_check_profile_val (const char *name, 
        int *pval, int pval_default) {
	/* value must be positive and power of 2 */
	int old_pval = *pval;

	if (old_pval <= 0) {
        *pval = pval_default;
    } else {
        *pval = roundup_pow_of_two(old_pval);
    }

	if (old_pval != *pval) {
		printk(KERN_WARNING PFX "Invalid value %d for %s in module parameter.\n",
		       old_pval, name);
		printk(KERN_WARNING PFX "Corrected %s to %d.\n", name, *pval);
	}
}

#define hgrnic_check_profile_val(name, default)				\
	__hgrnic_check_profile_val(#name, &hca_profile.name, default)

/** 
 * @description: 
 *  Validate parameters of resource size. 
 */
static void __init hgrnic_validate_profile (void) {
    hgrnic_check_profile_val(num_qp , HGRNIC_DEFAULT_NUM_QP);
    hgrnic_check_profile_val(num_cq , HGRNIC_DEFAULT_NUM_CQ);
    hgrnic_check_profile_val(num_mpt, HGRNIC_DEFAULT_NUM_MPT);
    hgrnic_check_profile_val(num_mtt, HGRNIC_DEFAULT_NUM_MTT);

    // if ((log_mtts_per_seg < 1) || (log_mtts_per_seg > 5)) {
    //     printk(KERN_WARNING PFX "bad log_mtts_per_seg (%d). Using default - %d\n",
    //             log_mtts_per_seg, ilog2(HGRNIC_MTT_SEG_SIZE / 8));
    //     log_mtts_per_seg = ilog2(HGRNIC_MTT_SEG_SIZE / 8);
    // }
}

static int __init hgrnic_init (void) {
    int ret;

    hgrnic_validate_profile();

    ret = pci_register_driver(&hgrnic_driver);
    if (ret < 0)
        return ret;

    return 0;
}

static void __exit hgrnic_cleanup (void) {
    pci_unregister_driver(&hgrnic_driver);
}

module_init(hgrnic_init);
module_exit(hgrnic_cleanup);
