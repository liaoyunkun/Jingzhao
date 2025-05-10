/**************************************************************
 * @author Kang Ning<kangning18z@ict.ac.cn>, NCIC, ICT, CAS
 * @date 2021.09.08
 * @file hgrnic_cmd.h
 * @note Command for low-level driver to interact with HCA hardware.
 *************************************************************/

#ifndef __HGRNIC_CMD_H__
#define __HGRNIC_CMD_H__

#include <rdma/ib_verbs.h>
#include "hgrnic_dev.h"
#include "hgrnic_iface.h"

#define HGRNIC_MAILBOX_SIZE 4096

enum {
    /* command completed successfully: */
    HGRNIC_CMD_STAT_OK 	      = 0x00,
    /* Internal error (such as a bus error) occurred while processing command: */
    HGRNIC_CMD_STAT_INTERNAL_ERR   = 0x01,
    /* Operation/command not supported or opcode modifier not supported: */
    HGRNIC_CMD_STAT_BAD_OP 	      = 0x02,
    /* Parameter not supported or parameter out of range: */
    HGRNIC_CMD_STAT_BAD_PARAM      = 0x03,
    /* System not enabled or bad system state: */
    HGRNIC_CMD_STAT_BAD_SYS_STATE  = 0x04,
    /* Attempt to access reserved or unallocaterd resource: */
    HGRNIC_CMD_STAT_BAD_RESOURCE   = 0x05,
    /* Requested resource is currently executing a command, or is otherwise busy: */
    HGRNIC_CMD_STAT_RESOURCE_BUSY  = 0x06,
    /* memory error: */
    HGRNIC_CMD_STAT_DDR_MEM_ERR    = 0x07,
    /* Required capability exceeds device limits: */
    HGRNIC_CMD_STAT_EXCEED_LIM     = 0x08,
    /* Resource is not in the appropriate state or ownership: */
    HGRNIC_CMD_STAT_BAD_RES_STATE  = 0x09,
    /* Index out of range: */
    HGRNIC_CMD_STAT_BAD_INDEX      = 0x0a,
    /* FW image corrupted: */
    HGRNIC_CMD_STAT_BAD_NVMEM      = 0x0b,
    /* Attempt to modify a QP/EE which is not in the presumed state: */
    HGRNIC_CMD_STAT_BAD_QPEE_STATE = 0x10,
    /* Bad segment parameters (Address/Size): */
    HGRNIC_CMD_STAT_BAD_SEG_PARAM  = 0x20,
    /* Memory Region has Memory Windows bound to: */
    HGRNIC_CMD_STAT_REG_BOUND      = 0x21,
    /* HCA local attached memory not present: */
    HGRNIC_CMD_STAT_LAM_NOT_PRE    = 0x22,
    /* Bad management packet (silently discarded): */
    HGRNIC_CMD_STAT_BAD_PKT 	      = 0x30,
    /* More outstanding CQEs in CQ than new CQ size: */
    HGRNIC_CMD_STAT_BAD_SIZE       = 0x40
};

enum {
    CXT_REGION = 1,
    TPT_REGION,
};

enum {
    DEV_LIM_FLAG_RC                 = 1 << 0,
    DEV_LIM_FLAG_UC                 = 1 << 1,
    DEV_LIM_FLAG_UD                 = 1 << 2,
    DEV_LIM_FLAG_RD                 = 1 << 3,
    DEV_LIM_FLAG_RAW_IPV6           = 1 << 4,
    DEV_LIM_FLAG_RAW_ETHER          = 1 << 5,
    DEV_LIM_FLAG_SRQ                = 1 << 6,
    DEV_LIM_FLAG_IPOIB_CSUM         = 1 << 7,
    DEV_LIM_FLAG_BAD_PKEY_CNTR      = 1 << 8,
    DEV_LIM_FLAG_BAD_QKEY_CNTR      = 1 << 9,
    DEV_LIM_FLAG_MW                 = 1 << 16,
    DEV_LIM_FLAG_AUTO_PATH_MIG      = 1 << 17,
    DEV_LIM_FLAG_ATOMIC             = 1 << 18,
    DEV_LIM_FLAG_RAW_MULTI          = 1 << 19,
    DEV_LIM_FLAG_UD_AV_PORT_ENFORCE = 1 << 20,
    DEV_LIM_FLAG_UD_MULTI           = 1 << 21,
};

struct hgrnic_mailbox {
    dma_addr_t dma;
    void      *buf;
};

struct hgrnic_dev_lim {
    int reserved_qps;
    int reserved_cqs;
    int reserved_eqs;
    int reserved_mtts;
    int reserved_pds;
    u32 reserved_lkey;
    
    int max_qp_sz; // Supported max number of WQEs in SQ (or RQ)
    int max_cq_sz; // Supported max number of CQEs in a CQ

    int max_qps;
    int max_cqs;
    int max_eqs;
    int max_mpts;
    int max_pds;
    int max_gids;
    int max_pkeys;

    int max_mtt_seg;

    // size for every entry
    int qpc_entry_sz;
    int cqc_entry_sz;
    int eqc_entry_sz;
    int mpt_entry_sz;

    int local_ca_ack_delay;
    int max_mtu;
    int max_port_width;
    int max_vl;
    int num_ports;

    int min_page_sz;

    int max_sg;
    int max_desc_sz;

    u64 max_icm_sz; // in byte
};


struct hgrnic_init_hca_param {
    u8  log_num_qps;
    u8  log_num_cqs;
    u8  log_num_eqs;
    u8  log_mpt_sz;
    
    u64 qpc_base;
    u64 cqc_base;
    u64 eqc_base;
    u64 mpt_base;
    u64 mtt_base;
};

struct hgrnic_init_ib_param {
    int port_width;
    int vl_cap;
    int mtu_cap;
    u16 gid_cap;
    u16 pkey_cap;
    int set_guid0;
    u64 guid0;
    int set_node_guid;
    u64 node_guid;
    int set_si_guid;
    u64 si_guid;
};

struct hgrnic_set_ib_param {
    int set_si_guid;
    int reset_qkey_viol;
    u64 si_guid ;
    u32 cap_mask;
};

struct hgrnic_icm;

int hgrnic_cmd_init(struct hgrnic_dev *dev);
void hgrnic_cmd_cleanup(struct hgrnic_dev *dev);

struct hgrnic_mailbox *hgrnic_alloc_mailbox(struct hgrnic_dev *dev,
                                            gfp_t gfp_mask);
void hgrnic_free_mailbox(struct hgrnic_dev *dev, struct hgrnic_mailbox *mailbox);


int hgrnic_QUERY_DEV_LIM (struct hgrnic_dev *dev, struct hgrnic_dev_lim *dev_lim);
int hgrnic_QUERY_ADAPTER (struct hgrnic_dev *dev, u64 *board_id);
            
int hgrnic_INIT_HCA (struct hgrnic_dev *dev, struct hgrnic_init_hca_param *param);
int hgrnic_CLOSE_HCA (struct hgrnic_dev *dev);

int hgrnic_INIT_IB(struct hgrnic_dev *dev, struct hgrnic_init_ib_param *param,
                  int port);
int hgrnic_CLOSE_IB(struct hgrnic_dev *dev, int port);
int hgrnic_SET_IB(struct hgrnic_dev *dev, struct hgrnic_set_ib_param *param,
                 int port);

int hgrnic_MAP_ICM (struct hgrnic_dev *dev, struct hgrnic_icm *icm, 
        u8 type_sel, u64 virt);
int hgrnic_MAP_ICM_page (struct hgrnic_dev *dev, u64 dma_addr, 
        u8 type_sel, u64 virt);
int hgrnic_UNMAP_ICM (struct hgrnic_dev *dev,
        u64 virt, u32 page_count, u8 type_sel);

int hgrnic_SW2HW_MPT (struct hgrnic_dev *dev, 
        struct hgrnic_mailbox *mailbox, int mpt_index);
int hgrnic_HW2SW_MPT (struct hgrnic_dev *dev, int mpt_index);
int hgrnic_WRITE_MTT (struct hgrnic_dev *dev, struct hgrnic_mailbox *mailbox,
		    int num_mtt);

int hgrnic_MAP_EQ (struct hgrnic_dev *dev, u64 event_mask, int unmap,
		 int eq_num);
int hgrnic_SW2HW_EQ (struct hgrnic_dev *dev, struct hgrnic_mailbox *mailbox,
		   int eq_num);
int hgrnic_HW2SW_EQ (struct hgrnic_dev *dev, int eq_num);

int hgrnic_SW2HW_CQ (struct hgrnic_dev *dev, struct hgrnic_mailbox *mailbox,
		   int cq_num);
int hgrnic_HW2SW_CQ (struct hgrnic_dev *dev, int cq_num);
int hgrnic_RESIZE_CQ (struct hgrnic_dev *dev, int cq_num, u32 lkey, u8 log_size);
int hgrnic_MODIFY_QP (struct hgrnic_dev *dev, enum ib_qp_state cur,
                      enum ib_qp_state next, u32 num, struct hgrnic_mailbox *mailbox);
int hgrnic_QUERY_QP (struct hgrnic_dev *dev, u32 num, 
                     struct hgrnic_mailbox *mailbox);
int hgrnic_CONF_SPECIAL_QP (struct hgrnic_dev *dev, int type, u32 qpn);
int hgrnic_MAD_IFC (struct hgrnic_dev *dev, int ignore_mkey, int ignore_bkey,
                    int port, const struct ib_wc *in_wc, const struct ib_grh *in_grh,
                    const void *in_mad, void *response_mad);
int hgrnic_NOP (struct hgrnic_dev *dev);

#endif /* __HGRNIC_CMD_H__ */
