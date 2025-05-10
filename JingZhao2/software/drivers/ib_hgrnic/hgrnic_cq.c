/**************************************************************
 * @author Kang Ning<kangning18z@ict.ac.cn>, NCIC, ICT, CAS
 * @date 2021.09.08
 * @file hgrnic_cq.c
 * @note Data structs & functions related to CQ
 *************************************************************/

#include <linux/gfp.h>
#include <linux/hardirq.h>
#include <linux/sched.h>

#include <asm/io.h>

#include <rdma/ib_pack.h>

#include "hgrnic_dev.h"
#include "hgrnic_cmd.h"
#include "hgrnic_icm.h"

enum {
    HGRNIC_ATOMIC_BYTE_LEN = 8
};

/*
 * Must be packed because start is 64 bits but only aligned to 32 bits.
 */
struct hgrnic_cq_context {
    __be32 flags;
    __be64 start;
    __be32 logsize_usrpage; /* | 8-bit | 24-bit | */
    __be32 comp_eqn;
    __be32 pd;
    __be32 lkey;
    __be32 reserved0[4];
    __be32 cqn;
    __be32 reserved1[4];
} __packed;

#define HGRNIC_CQ_STATUS_OK          ( 0 << 28)
#define HGRNIC_CQ_STATUS_OVERFLOW    ( 9 << 28)
#define HGRNIC_CQ_STATUS_WRITE_FAIL  (10 << 28)
#define HGRNIC_CQ_FLAG_TR            ( 1 << 18)
#define HGRNIC_CQ_FLAG_OI            ( 1 << 17)
#define HGRNIC_CQ_STATE_DISARMED     ( 0 <<  8)
#define HGRNIC_CQ_STATE_ARMED        ( 1 <<  8)
#define HGRNIC_CQ_STATE_ARMED_SOL    ( 4 <<  8)
#define HGRNIC_EQ_STATE_FIRED        (10 <<  8)


enum {
    SYNDROME_LOCAL_LENGTH_ERR           = 0x01,
    SYNDROME_LOCAL_QP_OP_ERR            = 0x02,
    SYNDROME_LOCAL_EEC_OP_ERR           = 0x03,
    SYNDROME_LOCAL_PROT_ERR             = 0x04,
    SYNDROME_WR_FLUSH_ERR               = 0x05,
    SYNDROME_MW_BIND_ERR                = 0x06,
    SYNDROME_BAD_RESP_ERR               = 0x10,
    SYNDROME_LOCAL_ACCESS_ERR           = 0x11,
    SYNDROME_REMOTE_INVAL_REQ_ERR       = 0x12,
    SYNDROME_REMOTE_ACCESS_ERR          = 0x13,
    SYNDROME_REMOTE_OP_ERR              = 0x14,
    SYNDROME_RETRY_EXC_ERR              = 0x15,
    SYNDROME_RNR_RETRY_EXC_ERR          = 0x16,
    SYNDROME_LOCAL_RDD_VIOL_ERR         = 0x20,
    SYNDROME_REMOTE_INVAL_RD_REQ_ERR    = 0x21,
    SYNDROME_REMOTE_ABORTED_ERR         = 0x22,
    SYNDROME_INVAL_EECN_ERR             = 0x23,
    SYNDROME_INVAL_EEC_STATE_ERR        = 0x24
};

struct hgrnic_cqe {
    __le32 my_qpn;
    __le32 my_ee;
    __le32 rqpn;
    u8     sl_ipok;
    u8     g_mlpath;
    __le16 rlid;
    __le32 imm;
    __le32 byte_cnt;
    __le32 wqe; /* wqe offset in WQ */
    u8     opcode;
    u8     is_send;
    u8     reserved;
    u8     owner;
};

struct hgrnic_err_cqe {
    __le32 my_qpn;
    u32    reserved1[3];
    u8     syndrome;
    u8     vendor_err;
    __le16 db_cnt;
    u32    reserved2;
    __le32 wqe;
    u8     opcode;
    u8     reserved3[2];
    u8     owner;
};

#define HGRNIC_CQ_ENTRY_OWNER_SW      (0 << 7)
#define HGRNIC_CQ_ENTRY_OWNER_HW      (1 << 7)

#define HGRNIC_TAVOR_CQ_DB_INC_CI       (1 << 24)
#define HGRNIC_TAVOR_CQ_DB_REQ_NOT      (2 << 24)
#define HGRNIC_TAVOR_CQ_DB_REQ_NOT_SOL  (3 << 24)
#define HGRNIC_TAVOR_CQ_DB_SET_CI       (4 << 24)
#define HGRNIC_TAVOR_CQ_DB_REQ_NOT_MULT (5 << 24)

#define HGRNIC_ARBEL_CQ_DB_REQ_NOT_SOL  (1 << 24)
#define HGRNIC_ARBEL_CQ_DB_REQ_NOT      (2 << 24)
#define HGRNIC_ARBEL_CQ_DB_REQ_NOT_MULT (3 << 24)

static inline struct hgrnic_cqe *get_cqe_from_buf(struct hgrnic_cq_buf *buf,
                                                 int entry)
{
    if (buf->is_direct)
        return buf->queue.direct.buf + (entry * sizeof(struct hgrnic_cqe));
    else
        return buf->queue.page_list[entry * sizeof(struct hgrnic_cqe) / PAGE_SIZE].buf
            + (entry * sizeof(struct hgrnic_cqe)) % PAGE_SIZE;
}

static inline struct hgrnic_cqe *get_cqe(struct hgrnic_cq *cq, int entry)
{
    return get_cqe_from_buf(&cq->buf, entry);
}

static inline struct hgrnic_cqe *cqe_sw(struct hgrnic_cqe *cqe)
{
    return HGRNIC_CQ_ENTRY_OWNER_HW & cqe->owner ? NULL : cqe;
}

static inline struct hgrnic_cqe *next_cqe_sw(struct hgrnic_cq *cq)
{
    return cqe_sw(get_cqe(cq, cq->cons_index & cq->ibcq.cqe));
}

static inline void set_cqe_hw(struct hgrnic_cqe *cqe)
{
    cqe->owner = HGRNIC_CQ_ENTRY_OWNER_HW;
}

static inline void dump_cqe(struct hgrnic_dev *dev, void *cqe_ptr)
{
    __le32 *cqe = cqe_ptr;

    (void) cqe;	/* avoid warning if hgrnic_dbg compiled away... */
    hgrnic_dbg(dev, "CQE contents %08x %08x %08x %08x %08x %08x %08x %08x\n",
            le32_to_cpu(cqe[0]), le32_to_cpu(cqe[1]), le32_to_cpu(cqe[2]),
            le32_to_cpu(cqe[3]), le32_to_cpu(cqe[4]), le32_to_cpu(cqe[5]),
            le32_to_cpu(cqe[6]), le32_to_cpu(cqe[7]));
}

/**
 * @param qpn QP need to be cleaned
 * @param cq  CQ the qp attached to
 * Clean qpn from cq.
 */
void hgrnic_cq_clean(struct hgrnic_dev *dev, struct hgrnic_cq *cq, u32 qpn)
{
    struct hgrnic_cqe *cqe;
    u32 prod_index;
    int i, nfreed = 0;

    spin_lock_irq(&cq->lock);

    /*
     * First we need to find the current producer index, so we
     * know where to start cleaning from.  It doesn't matter if HW
     * adds new entries after this loop -- the QP we're worried
     * about is already in RESET, so the new entries won't come
     * from our QP and therefore don't need to be checked.
     */
    for (prod_index = cq->cons_index;
         cqe_sw(get_cqe(cq, prod_index & cq->ibcq.cqe));
         ++prod_index)
        if (prod_index == cq->cons_index + cq->ibcq.cqe)
            break;

    hgrnic_dbg(dev, "Cleaning QPN %06x from CQN %06x; ci %d, pi %d\n",
            qpn, cq->cqn, cq->cons_index, prod_index);

    /*
     * Now sweep backwards through the CQ, removing CQ entries
     * that match our QP by copying older entries on top of them.
     */
    while ((int) --prod_index - (int) cq->cons_index >= 0) {
        cqe = get_cqe(cq, prod_index & cq->ibcq.cqe);
        if (cqe->my_qpn == cpu_to_le32(qpn))
            ++nfreed;
        else if (nfreed)
            memcpy(get_cqe(cq, (prod_index + nfreed) & cq->ibcq.cqe),
                    cqe, sizeof(struct hgrnic_cqe));
    }

    if (nfreed) {
        for (i = 0; i < nfreed; ++i)
            set_cqe_hw(get_cqe(cq, (cq->cons_index + i) & cq->ibcq.cqe));
        wmb();
        cq->cons_index += nfreed;
    }

    spin_unlock_irq(&cq->lock);
}

/**
 * 
 * Migrate CQE to resize buf of same consumer index 
 */
void hgrnic_cq_resize_copy_cqes(struct hgrnic_cq *cq)
{
    int i;

    for (i = cq->cons_index; cqe_sw(get_cqe(cq, i & cq->ibcq.cqe)); ++i)
        memcpy(get_cqe_from_buf(&cq->resize_buf->buf,
                    i & cq->resize_buf->cqe),
                get_cqe(cq, i & cq->ibcq.cqe), sizeof(struct hgrnic_cqe));
}

/**
 * @note Allocate buffer for completion Que. 
 * 
 * @param buf cq buffer struct
 * @param nent type of the resource
 */
int hgrnic_alloc_cq_buf(struct hgrnic_dev *dev, struct hgrnic_cq_buf *buf, 
        int nent)
{
    int ret;
    int i;

    ret = hgrnic_buf_alloc(dev, nent * sizeof(struct hgrnic_cqe),
                          &buf->queue, &buf->is_direct,
                          &dev->driver_pd, 1, &buf->mr);
    if (ret)
        return ret;

    for (i = 0; i < nent; ++i)
        set_cqe_hw(get_cqe_from_buf(buf, i));

    return 0;
}

void hgrnic_free_cq_buf(struct hgrnic_dev *dev, struct hgrnic_cq_buf *buf, int cqe)
{
    hgrnic_buf_free(dev, (cqe + 1) * sizeof(struct hgrnic_cqe), &buf->queue,
                   buf->is_direct, &buf->mr);
}

static void handle_error_cqe(struct hgrnic_dev *dev, struct hgrnic_cq *cq,
                             struct hgrnic_qp *qp, int wqe_index, int is_send,
                             struct hgrnic_err_cqe *cqe,
                             struct ib_wc *entry)
{
    if (cqe->syndrome == SYNDROME_LOCAL_QP_OP_ERR) {
        hgrnic_dbg(dev, "local QP operation err "
                "(QPN %06x, WQE @ %08x, CQN %06x, consumer index %d)\n",
                le32_to_cpu(cqe->my_qpn), le32_to_cpu(cqe->wqe),
                cq->cqn, cq->cons_index);
        dump_cqe(dev, cqe);
    }

    /*
     * For completions in error, only work request ID, status
     * have to be set.
     */
    switch (cqe->syndrome) {
    case SYNDROME_LOCAL_LENGTH_ERR:
        entry->status = IB_WC_LOC_LEN_ERR;
        break;
    case SYNDROME_LOCAL_QP_OP_ERR:
        entry->status = IB_WC_LOC_QP_OP_ERR;
        break;
    case SYNDROME_LOCAL_EEC_OP_ERR:
        entry->status = IB_WC_LOC_EEC_OP_ERR;
        break;
    case SYNDROME_LOCAL_PROT_ERR:
        entry->status = IB_WC_LOC_PROT_ERR;
        break;
    case SYNDROME_WR_FLUSH_ERR:
        entry->status = IB_WC_WR_FLUSH_ERR;
        break;
    case SYNDROME_MW_BIND_ERR:
        entry->status = IB_WC_MW_BIND_ERR;
        break;
    case SYNDROME_BAD_RESP_ERR:
        entry->status = IB_WC_BAD_RESP_ERR;
        break;
    case SYNDROME_LOCAL_ACCESS_ERR:
        entry->status = IB_WC_LOC_ACCESS_ERR;
        break;
    case SYNDROME_REMOTE_INVAL_REQ_ERR:
        entry->status = IB_WC_REM_INV_REQ_ERR;
        break;
    case SYNDROME_REMOTE_ACCESS_ERR:
        entry->status = IB_WC_REM_ACCESS_ERR;
        break;
    case SYNDROME_REMOTE_OP_ERR:
        entry->status = IB_WC_REM_OP_ERR;
        break;
    case SYNDROME_RETRY_EXC_ERR:
        entry->status = IB_WC_RETRY_EXC_ERR;
        break;
    case SYNDROME_RNR_RETRY_EXC_ERR:
        entry->status = IB_WC_RNR_RETRY_EXC_ERR;
        break;
    case SYNDROME_LOCAL_RDD_VIOL_ERR:
        entry->status = IB_WC_LOC_RDD_VIOL_ERR;
        break;
    case SYNDROME_REMOTE_INVAL_RD_REQ_ERR:
        entry->status = IB_WC_REM_INV_RD_REQ_ERR;
        break;
    case SYNDROME_REMOTE_ABORTED_ERR:
        entry->status = IB_WC_REM_ABORT_ERR;
        break;
    case SYNDROME_INVAL_EECN_ERR:
        entry->status = IB_WC_INV_EECN_ERR;
        break;
    case SYNDROME_INVAL_EEC_STATE_ERR:
        entry->status = IB_WC_INV_EEC_STATE_ERR;
        break;
    default:
        entry->status = IB_WC_GENERAL_ERR;
        break;
    }

    /* !TODO: unused field */
    entry->vendor_err = cqe->vendor_err;
}

static inline int hgrnic_poll_one(struct hgrnic_dev *dev,
                                 struct hgrnic_cq *cq,
                                 struct hgrnic_qp **cur_qp,
                                 struct ib_wc *entry)
{
    struct hgrnic_wq *wq;
    struct hgrnic_cqe *cqe;
    int wqe_index;
    int is_error;
    int is_send;
    int err = 0;

    cqe = next_cqe_sw(cq);
    if (!cqe)
        return -EAGAIN;

    hgrnic_dbg(dev, "%x/%d: CQE -> QPN %06x, WQE @ %08x\n",
              cq->cqn, cq->cons_index, le32_to_cpu(cqe->my_qpn),
              le32_to_cpu(cqe->wqe));
    dump_cqe(dev, cqe);

    is_error = (cqe->opcode == HGRNIC_OPCODE_SEND_ERR) ||
               (cqe->opcode == HGRNIC_OPCODE_RECV_ERR);
    is_send  = is_error ? (cqe->opcode == HGRNIC_OPCODE_SEND_ERR) 
                        : cqe->is_send;

    if (!*cur_qp || le32_to_cpu(cqe->my_qpn) != (*cur_qp)->qpn) {
        /*
         * We do not have to take the QP table lock here,
         * because CQs will be locked while QPs are removed
         * from the table.
         */
        *cur_qp = hgrnic_array_get(&dev->qp_table.qp,
                                  le32_to_cpu(cqe->my_qpn) &
                                  (dev->limits.num_qps - 1));
        if (!*cur_qp) {
            hgrnic_warn(dev, "CQ entry for unknown QP %06x\n",
                    le32_to_cpu(cqe->my_qpn) & 0xffffff);
            err = -EINVAL;
            goto out;
        }
    }

    entry->qp = &(*cur_qp)->ibqp;

    if (is_send) {
        wq = &(*cur_qp)->sq;
        wqe_index = le32_to_cpu(cqe->wqe) >> wq->entry_sz_log;
        entry->wr_id = (*cur_qp)->sq.wr_id[wqe_index];
    } else {
        wq = &(*cur_qp)->rq;
        wqe_index = le32_to_cpu(cqe->wqe) >> wq->entry_sz_log;
        entry->wr_id = (*cur_qp)->sq.wr_id[wqe_index];
    }

    if (wq) {
        /* last_cpl is inited with max-1, 
         * so we don't have to consider the 
         * start case. */
        if (wq->last_cpl < wqe_index)
            wq->tail += wqe_index - wq->last_cpl;
        else
            wq->tail += wqe_index + wq->max - wq->last_cpl;

        wq->last_cpl = wqe_index;
    }

    if (is_error) {
        handle_error_cqe(dev, cq, *cur_qp, wqe_index, is_send,
                    (struct hgrnic_err_cqe *) cqe,
                    entry);
        goto out;
    }

    if (is_send) {
        entry->wc_flags = 0;
        switch (cqe->opcode) {
        case HGRNIC_OPCODE_RDMA_WRITE:
            entry->opcode    = IB_WC_RDMA_WRITE;
            break;
        case HGRNIC_OPCODE_RDMA_WRITE_IMM:
            entry->opcode    = IB_WC_RDMA_WRITE;
            entry->wc_flags |= IB_WC_WITH_IMM;
            break;
        case HGRNIC_OPCODE_SEND:
            entry->opcode    = IB_WC_SEND;
            break;
        case HGRNIC_OPCODE_SEND_IMM:
            entry->opcode    = IB_WC_SEND;
            entry->wc_flags |= IB_WC_WITH_IMM;
            break;
        case HGRNIC_OPCODE_RDMA_READ:
            entry->opcode    = IB_WC_RDMA_READ;
            entry->byte_len  = le32_to_cpu(cqe->byte_cnt);
            break;
        case HGRNIC_OPCODE_ATOMIC_CS:
            entry->opcode    = IB_WC_COMP_SWAP;
            entry->byte_len  = HGRNIC_ATOMIC_BYTE_LEN;
            break;
        case HGRNIC_OPCODE_ATOMIC_FA:
            entry->opcode    = IB_WC_FETCH_ADD;
            entry->byte_len  = HGRNIC_ATOMIC_BYTE_LEN;
            break;
        default:
            entry->opcode    = HGRNIC_OPCODE_NOP;
            break;
        }
    } else {
        entry->byte_len = le32_to_cpu(cqe->byte_cnt);
        switch (cqe->opcode & 0x1f) {
        case IB_OPCODE_SEND_LAST_WITH_IMMEDIATE:
        case IB_OPCODE_SEND_ONLY_WITH_IMMEDIATE:
            entry->wc_flags = IB_WC_WITH_IMM;
            entry->ex.imm_data = cqe->imm;
            entry->opcode = IB_WC_RECV;
            break;
        case IB_OPCODE_RDMA_WRITE_LAST_WITH_IMMEDIATE:
        case IB_OPCODE_RDMA_WRITE_ONLY_WITH_IMMEDIATE:
            entry->wc_flags = IB_WC_WITH_IMM;
            entry->ex.imm_data = cqe->imm;
            entry->opcode = IB_WC_RECV_RDMA_WITH_IMM;
            break;
        default:
            entry->wc_flags = 0;
            entry->opcode = IB_WC_RECV;
            break;
        }
        entry->slid        = le16_to_cpu(cqe->rlid);
        entry->src_qp      = le32_to_cpu(cqe->rqpn) & 0xffffff;

        entry->sl             = 0; /* This is not used */
        entry->dlid_path_bits = 0; /* This is not used */
        entry->pkey_index     = 0; /* This is not used */
    }

    entry->status = IB_WC_SUCCESS;

out:
    set_cqe_hw(cqe);
    ++cq->cons_index;

    return err;
}


/**
 * * ib_poll_cq - poll a CQ for completion(s)
 * @cq:the CQ being polled
 * @num_entries:maximum number of completions to return
 * @wc:array of at least @num_entries &struct ib_wc where completions
 *   will be returned
 *
 * If the return value is < 0, an error occurred. 
 * If the return value is >= 0, it is the number of 
 * completions returned.  If the return value is
 * non-negative and < num_entries, then the CQ was 
 * emptied.
 */
int hgrnic_poll_cq(struct ib_cq *ibcq, int num_entries,
                  struct ib_wc *entry)
{
    struct hgrnic_dev *dev = to_hgdev(ibcq->device);
    struct hgrnic_cq *cq = to_hgcq(ibcq);
    struct hgrnic_qp *qp = NULL;
    unsigned long flags;
    int err = 0;
    int npolled;

    spin_lock_irqsave(&cq->lock, flags);

    npolled = 0;
repoll:
    while (npolled < num_entries) {
        err = hgrnic_poll_one(dev, cq, &qp,
                             entry + npolled);
        if (err)
            break;
        ++npolled;
    }

    /*
     * If a CQ resize is in progress and we discovered that the
     * old buffer is empty, then peek in the new buffer, and if
     * it's not empty, switch to the new buffer and continue
     * polling there.
     */
    if (unlikely(err == -EAGAIN && cq->resize_buf &&
             cq->resize_buf->state == CQ_RESIZE_READY)) {

        if (cqe_sw(get_cqe_from_buf(&cq->resize_buf->buf,
                                    cq->cons_index & cq->resize_buf->cqe))) {
            struct hgrnic_cq_buf tbuf;
            int tcqe;

            tbuf         = cq->buf;
            tcqe         = cq->ibcq.cqe;
            cq->buf      = cq->resize_buf->buf;
            cq->ibcq.cqe = cq->resize_buf->cqe;

            cq->resize_buf->buf   = tbuf;
            cq->resize_buf->cqe   = tcqe;
            cq->resize_buf->state = CQ_RESIZE_SWAPPED;

            goto repoll;
        }
    }

    spin_unlock_irqrestore(&cq->lock, flags);

    return (err == 0 || err == -EAGAIN) ? npolled : err;
}


int hgrnic_arm_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags) {

	printk(KERN_INFO PFX "Enter hgrnic_arm_cq! This callback is left empty!\n");
	return 0;
}

/** 
 * @description: Allocate cqn and queue(if it is from kernel space)
 * @param nent : unmber of cqe supported in the CQ. (may get from userspace driver)
 * @param ctx  : ctx cq belonged to, is NULL if the caller is from kernel space.
 * @param pdn  : pd number this cq belonged to. (may get from user space driver)
 * @param cq   : struct hgrnic_cq allocated for this cq.
 */
int hgrnic_init_cq (struct hgrnic_dev *dev, int nent,
        struct hgrnic_ucontext *ctx, u32 pdn, struct hgrnic_cq *cq)
{
    struct hgrnic_mailbox *mailbox;
    struct hgrnic_cq_context *cq_context;
    int err = -ENOMEM;

    cq->ibcq.cqe  = nent - 1; /* cqe is also a mask (pow(2) - 1) */
    cq->is_kernel = !ctx;

    cq->cqn = hgrnic_alloc(&dev->cq_table.alloc);
    if (cq->cqn == -1)
        return -ENOMEM;

    err = hgrnic_reg_icm(dev, dev->cq_table.table, 
            cq->cqn, CXT_REGION);
    if (err)
        goto err_out;

    /* If this is kernel owned CQ, allocated CQ buffer */
    if (cq->is_kernel) {
        err = hgrnic_alloc_cq_buf(dev, &cq->buf, nent);
        if (err)
            goto err_out_icm;
    }

    spin_lock_init(&cq->lock);
    cq->refcount = 1;
    init_waitqueue_head(&cq->wait);
    mutex_init(&cq->mutex);

    mailbox = hgrnic_alloc_mailbox(dev, GFP_KERNEL);
    if (IS_ERR(mailbox))
        goto err_out_free_mr;

    cq_context = mailbox->buf;
    memset(cq_context, 0, sizeof *cq_context);
    cq_context->flags = cpu_to_be32(HGRNIC_CQ_STATUS_OK      |
                                    HGRNIC_CQ_STATE_DISARMED |
                                    HGRNIC_CQ_FLAG_TR);
    cq_context->logsize_usrpage = cpu_to_be32((ffs(nent) - 1) << 24); /* TODO: This logsize needs to be checked */
    cq_context->pd              = cpu_to_be32(pdn);
    cq_context->lkey            = cpu_to_be32(cq->buf.mr.ibmr.lkey);
    cq_context->cqn             = cpu_to_be32(cq->cqn);

    err = hgrnic_SW2HW_CQ(dev, mailbox, cq->cqn);
    if (err) {
        hgrnic_warn(dev, "SW2HW_CQ failed (%d)\n", err);
        goto err_out_mailbox;
    }
    hgrnic_dump("0x%x" , be32_to_cpu(cq_context->flags));
    hgrnic_dump("0x%llx", be64_to_cpu(cq_context->start));
    hgrnic_dump("0x%x", be32_to_cpu(cq_context->logsize_usrpage));
    hgrnic_dump("0x%x", be32_to_cpu(cq_context->comp_eqn       ));
    hgrnic_dump("0x%x", be32_to_cpu(cq_context->pd             ));
    hgrnic_dump("0x%x", be32_to_cpu(cq_context->lkey           ));
    hgrnic_dump("0x%x", be32_to_cpu(cq_context->cqn            ));

    cq->cons_index = 0;

    hgrnic_free_mailbox(dev, mailbox);

    spin_lock_irq(&dev->cq_table.lock);
    if (hgrnic_array_set(&dev->cq_table.cq,
                cq->cqn & (dev->limits.num_cqs - 1),
                cq)) {
        spin_unlock_irq(&dev->cq_table.lock);
        goto err_out_free_mr;
    }
    spin_unlock_irq(&dev->cq_table.lock);

    return 0;

err_out_mailbox:
    hgrnic_free_mailbox(dev, mailbox);

err_out_free_mr:
    if (cq->is_kernel)
        hgrnic_free_cq_buf(dev, &cq->buf, cq->ibcq.cqe);

err_out_icm:
    hgrnic_unreg_icm(dev, dev->cq_table.table, cq->cqn, CXT_REGION);

err_out:
    hgrnic_free(&dev->cq_table.alloc, cq->cqn);

    return err;
}

static inline int get_cq_refcount(struct hgrnic_dev *dev, struct hgrnic_cq *cq)
{
    int c;

    spin_lock_irq(&dev->cq_table.lock);
    c = cq->refcount;
    spin_unlock_irq(&dev->cq_table.lock);

    return c;
}

void hgrnic_free_cq(struct hgrnic_dev *dev,
                   struct hgrnic_cq *cq)
{
    int err;

    err = hgrnic_HW2SW_CQ(dev, cq->cqn);
    if (err)
        hgrnic_warn(dev, "HW2SW_CQ failed (%d)\n", err);


    spin_lock_irq(&dev->cq_table.lock);
    hgrnic_array_clear(&dev->cq_table.cq,
                      cq->cqn & (dev->limits.num_cqs - 1));
    --cq->refcount;
    spin_unlock_irq(&dev->cq_table.lock);

    wait_event(cq->wait, !get_cq_refcount(dev, cq));

    if (cq->is_kernel)
        hgrnic_free_cq_buf(dev, &cq->buf, cq->ibcq.cqe);

    hgrnic_unreg_icm(dev, dev->cq_table.table, cq->cqn, CXT_REGION);
    hgrnic_free(&dev->cq_table.alloc, cq->cqn);
}

int hgrnic_init_cq_table (struct hgrnic_dev *dev) {
	int err;

	spin_lock_init(&dev->cq_table.lock);

	err = hgrnic_alloc_init(&dev->cq_table.alloc,
			       dev->limits.num_cqs,
			       (1 << 24) - 1);
	if (err) {
        return err;
    }

	err = hgrnic_array_init(&dev->cq_table.cq,
			       dev->limits.num_cqs);
	if (err) {
        hgrnic_alloc_cleanup(&dev->cq_table.alloc);
    }

	return err;
}

void hgrnic_cleanup_cq_table(struct hgrnic_dev *dev) {
	hgrnic_array_cleanup(&dev->cq_table.cq, dev->limits.num_cqs);
	hgrnic_alloc_cleanup(&dev->cq_table.alloc);
}
