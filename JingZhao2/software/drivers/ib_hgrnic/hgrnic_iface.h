/**************************************************************
 * @author Kang Ning<kangning18z@ict.ac.cn>, NCIC, ICT, CAS
 * @date 2021.09.08
 * @file hgrnic_iface.h
 * @note Data structs & functions related to doorbell
 *************************************************************/

#ifndef __HGRNIC_IFACE_H__
#define __HGRNIC_IFACE_H__

/* ---------------RDMA BAR Space Interface{begin}-------------- */

// --------------- BAR 0-1 ---------------//
#define HGRNIC_HCR_BASE         0x00000
#define HGRNIC_HCR_SIZE         0x01000
#define HGRNIC_EN_REG_BASE      0x01000
#define HGRNIC_EN_REG_SIZE      0x00400
#define HGRNIC_MSIX_TAB_BASE    0x20000
#define HGRNIC_MSIX_TAB_SIZE    0x01000
#define HGRNIC_MSIX_PND_BASE    0x30000
#define HGRNIC_MSIX_PND_SIZE    0x01000


// --------------- BAR 2-3 ---------------//
#define HGRNIC_SEND_DOORBELL    0x00 // Temporary set as 0.
/* --------RDMA BAR Space Interface{end}------- */

#endif /* __HGRNIC_IFACE_H__ */
