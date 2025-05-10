/**************************************************************
 * @author Kang Ning<kangning18z@ict.ac.cn>, NCIC, ICT, CAS
 * @date 2021.09.08
 * @file hgrnic_register.c
 * @note Provide interface for ethernet driver registering
 *************************************************************/
#include "hgrnic_register.h"

static struct hgrnic_dev* hgdev;

int hgrnic_init_en_register (struct hgrnic_dev* dev)
{
    hgdev = dev;
    return 0;
}

int hgrnic_register_en_driver (struct hgrnic_endev *endev)
{
    
    printk(KERN_INFO PFX "Start pci_set_drvdata.\n");
    return 0;
    
    if (!hgdev->active)
        return -1;

    hgdev->endev = endev;

    endev->reg      = ioremap(pci_resource_start(hgdev->pdev, 0) + HGRNIC_EN_REG_BASE,
                              HGRNIC_EN_REG_SIZE);
    endev->msix_tab = ioremap(pci_resource_start(hgdev->pdev, 0) + HGRNIC_MSIX_TAB_BASE,
                              HGRNIC_MSIX_TAB_SIZE);
    endev->msix_pnd = ioremap(pci_resource_start(hgdev->pdev, 0) + HGRNIC_MSIX_PND_BASE,
                              HGRNIC_MSIX_PND_SIZE);
    
    return 0;
}
EXPORT_SYMBOL(hgrnic_register_en_driver);
