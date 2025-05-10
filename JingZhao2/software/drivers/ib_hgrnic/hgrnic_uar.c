/**************************************************************
 * @author Kang Ning<kangning18z@ict.ac.cn>, NCIC, ICT, CAS
 * @date 2021.09.08
 * @file hgrnic_uar.c
 * @note Functions related to uar space.
 *************************************************************/

#include <asm/page.h>       /* PAGE_SHIFT */

#include "hgrnic_dev.h"

/**
 * @note Allocate one page from BAR2 space(uar space).
 */
int hgrnic_uar_alloc (struct hgrnic_dev *dev, struct hgrnic_uar *uar)
{
    uar->index = hgrnic_alloc(&dev->uar_table.alloc);
    if (uar->index == -1)
        return -ENOMEM;

    uar->pfn = (pci_resource_start(dev->pdev, 2) >> PAGE_SHIFT) + uar->index;

    return 0;
}

void hgrnic_uar_free (struct hgrnic_dev *dev, struct hgrnic_uar *uar)
{
    hgrnic_free(&dev->uar_table.alloc, uar->index);
}

int hgrnic_init_uar_table (struct hgrnic_dev *dev)
{
    printk(KERN_INFO PFX "dev->limits.num_uars is %d\n", dev->limits.num_uars);
    return hgrnic_alloc_init(&dev->uar_table.alloc,
                             dev->limits.num_uars, dev->limits.num_uars - 1);
}

void hgrnic_cleanup_uar_table (struct hgrnic_dev *dev)
{
    hgrnic_alloc_cleanup(&dev->uar_table.alloc);
}
