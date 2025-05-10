/**************************************************************
 * @author Kang Ning<kangning18z@ict.ac.cn>, NCIC, ICT, CAS
 * @date 2021.09.08
 * @file hgrnic_abi.h
 * @note Device specific data and useful function for ethernet
 *************************************************************/

#ifndef __HGRNIC_EN_ABI_H__
#define __HGRNIC_EN_ABI_H__

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>

struct hgrnic_endev {
    void __iomem    *reg;
    void __iomem    *msix_tab;
    void __iomem    *msix_pnd;
};

int hgrnic_register_en_driver (struct hgrnic_endev *endev);

#endif // __HGRNIC_ABI_H__