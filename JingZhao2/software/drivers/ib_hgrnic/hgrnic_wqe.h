/**************************************************************
 * @author Kang Ning<kangning18z@ict.ac.cn>, NCIC, ICT, CAS
 * @date 2021.08.30
 * @file hgrnic_wqe.h
 * @note Functions & data structs related to RDMA WQE.
 *************************************************************/

#ifndef __HGRNIC_WQE_H__
#define __HGRNIC_WQE_H__

#include <linux/types.h>

enum {
    HGRNIC_NEXT_DBD          = 1 << 7,
    HGRNIC_NEXT_FENCE        = 1 << 6,
    HGRNIC_NEXT_CQ_UPDATE    = 1 << 3,
    HGRNIC_NEXT_EVENT_GEN    = 1 << 2,
    HGRNIC_NEXT_SOLICIT      = 1 << 1
};

enum {
    HGRNIC_INVAL_LKEY           = 0x100,
    HGRNIC_MAX_WQES_PER_SEND_DB = 255
};

struct hgrnic_next_unit {
    __le32 nda_nop;      /* [31:6] next WQE [4:0] next opcode */
    __le32 ee_nds;      /* [31:8] next EE  [7] DBD [6] F [5:0] next WQE size */
    __le32 flags;       /* [3] CQ [2] Event [1] Solicit */
    __le32 imm;         /* immediate data */
};

struct hgrnic_ud_unit {
    __le32 port;
    __le16 slid;
    __le16 dlid;
    __le32 smac_h;
    __le32 dmac_h;
    u32    rsvd1[4];
    __le32 dqpn;
    __le32 qkey;
    u32    rsvd2[2];
};

struct hgrnic_raddr_unit {
    __le64 raddr;
    __le32 rkey;
    u32    reserved;
};

struct hgrnic_atomic_unit {
    __le64 swap_add;
    __le64 compare;
};

struct hgrnic_inline_unit {
    __le32 length;
};

struct hgrnic_data_unit {
    __le32 byte_count;
    __le32 lkey;
    __le64 addr;
};

static __always_inline void set_raddr_unit(struct hgrnic_raddr_unit *runit,
                                           u64 remote_addr, u32 rkey)
{
    runit->raddr    = cpu_to_le64(remote_addr);
    runit->rkey     = cpu_to_le32(rkey);
    runit->reserved = 0;
}

static __always_inline void set_atomic_seg(struct hgrnic_atomic_unit *aseg,
                                           const struct ib_atomic_wr *wr)
{
    if (wr->wr.opcode == IB_WR_ATOMIC_CMP_AND_SWP) {
        aseg->swap_add = cpu_to_be64(wr->swap);
        aseg->compare  = cpu_to_be64(wr->compare_add);
    } else {
        aseg->swap_add = cpu_to_be64(wr->compare_add);
        aseg->compare  = 0;
    }
}

static __always_inline void set_ud_unit(struct hgrnic_ud_unit *uunit,
                                        const struct ib_ud_wr *wr)
{
    struct hgrnic_ah *hgah = to_hgah(wr->ah);
    uunit->port   = cpu_to_le32(hgah->port);
    uunit->slid   = cpu_to_le16(hgah->slid & 0xffff);
    uunit->smac_h = cpu_to_le32((hgah->slid >> 16) & 0xffffffff);
    uunit->dlid   = cpu_to_le16(hgah->dlid & 0xffff);
    uunit->dmac_h = cpu_to_le32((hgah->dlid >> 16) & 0xffffffff);
}

static __always_inline void hgrnic_send_dbell(struct hgrnic_dev *dev, struct hgrnic_qp *qp, 
                                              int nreq, int size0, u32 f0, u8 op0)
{
    u32 dbhi, dblo;

    dbhi = (qp->qpn << 8) | size0;
    dblo = ((qp->sq.head & 0xffff) << 8) | f0 | op0;

    qp->sq.head += nreq;

    wmb();
    hgrnic_write64(dbhi, dblo, dev->kar + HGRNIC_SEND_DOORBELL,
                HGRNIC_GET_DOORBELL_LOCK(&dev->doorbell_lock));
    hgrnic_dump("0x%x", dbhi);
    hgrnic_dump("0x%x", dblo);
}

static __always_inline void hgrnic_set_inline_unit (struct hgrnic_inline_unit *unit,
                                                    u32 length, u8 *data)
{
    unit->length = (1 << 31) | length;
    unit += sizeof(struct hgrnic_inline_unit);
    memcpy(unit, data, length);
}

static __always_inline void hgrnic_set_data_unit (struct hgrnic_data_unit *dunit,
                                                  struct ib_sge *sg)
{
    dunit->byte_count = cpu_to_le32(sg->length);
    dunit->lkey       = cpu_to_le32(sg->lkey);
    dunit->addr       = cpu_to_le64(sg->addr);
}

static __always_inline void hgrnic_set_data_unit_inval(struct hgrnic_data_unit *dunit)
{
    dunit->byte_count = 0;
    dunit->lkey       = cpu_to_le32(HGRNIC_INVAL_LKEY);
    dunit->addr       = 0;
}

#endif /* __HGRNIC_WQE_H__ */
