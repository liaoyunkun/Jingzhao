#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <netinet/in.h>
#include <string.h>

#include <infiniband/opcode.h>

#include "hgrnic.h"
#include "wqe.h"


enum {
	CQ_OK		=  0,
	CQ_EMPTY	= -1,
	CQ_POLL_ERR	= -2
};

enum {
	HGRNIC_CQ_ENTRY_OWNER_SW     = 0x00,
	HGRNIC_CQ_ENTRY_OWNER_HW     = 0x80,
	HGRNIC_ERROR_CQE_OPCODE_MASK = 0xfe
};

enum {
    SYNDROME_LOCAL_LENGTH_ERR        = 0x01,
    SYNDROME_LOCAL_QP_OP_ERR         = 0x02,
    SYNDROME_LOCAL_EEC_OP_ERR        = 0x03,
    SYNDROME_LOCAL_PROT_ERR          = 0x04,
    SYNDROME_WR_FLUSH_ERR            = 0x05,
    SYNDROME_MW_BIND_ERR             = 0x06,
    SYNDROME_BAD_RESP_ERR            = 0x10,
    SYNDROME_LOCAL_ACCESS_ERR        = 0x11,
    SYNDROME_REMOTE_INVAL_REQ_ERR    = 0x12,
    SYNDROME_REMOTE_ACCESS_ERR       = 0x13,
    SYNDROME_REMOTE_OP_ERR           = 0x14,
    SYNDROME_RETRY_EXC_ERR           = 0x15,
    SYNDROME_RNR_RETRY_EXC_ERR       = 0x16,
    SYNDROME_LOCAL_RDD_VIOL_ERR      = 0x20,
    SYNDROME_REMOTE_INVAL_RD_REQ_ERR = 0x21,
    SYNDROME_REMOTE_ABORTED_ERR      = 0x22,
    SYNDROME_INVAL_EECN_ERR          = 0x23,
    SYNDROME_INVAL_EEC_STATE_ERR     = 0x24
};

struct hgrnic_cqe {
    uint32_t    my_qpn;
    uint32_t    my_ee;
    uint32_t    rqpn;
    uint8_t     sl_ipok;
    uint8_t     g_mlpath;
    uint16_t    rlid;
    uint32_t    imm;
    uint32_t    byte_cnt;
    uint32_t    wqe; // addr offset of completed wqe
    uint8_t     opcode;
    uint8_t     is_send;
    uint8_t     reserved;
    uint8_t     owner;
};

struct hgrnic_err_cqe {
    uint32_t    my_qpn;
    uint32_t    reserved1[3];
    uint8_t     syndrome;
    uint8_t     vendor_err;
    uint16_t    db_cnt;
    uint32_t    reserved2;
    uint32_t    wqe;
    uint8_t     opcode;
    uint8_t     reserved3[2];
    uint8_t     owner;
};

static inline struct hgrnic_cqe *get_cqe(struct hgrnic_cq *cq, int entry)
{
    return cq->buf.buf + entry * HGRNIC_CQ_ENTRY_SIZE;
}

static inline struct hgrnic_cqe *cqe_sw(struct hgrnic_cq *cq, int i)
{
    struct hgrnic_cqe *cqe = get_cqe(cq, i);
    return HGRNIC_CQ_ENTRY_OWNER_HW & cqe->owner ? NULL : cqe;
}

static inline struct hgrnic_cqe *next_cqe_sw(struct hgrnic_cq *cq)
{
    return cqe_sw(cq, cq->cons_index & cq->cqe_mask);
}

static inline void set_cqe_hw(struct hgrnic_cqe *cqe)
{
    VALGRIND_MAKE_MEM_UNDEFINED(cqe, sizeof *cqe);
    cqe->owner = HGRNIC_CQ_ENTRY_OWNER_HW;
}

static void dump_cqe(void *cqe_ptr)
{
    uint32_t *cqe = cqe_ptr;
    int i;

    //for (i = 0; i < 8; ++i)
    //    printf("  [%2x] %08x\n", i * 4, ((uint32_t *) cqe)[i]);
}

static int handle_error_cqe(struct hgrnic_cq *cq,
                            struct hgrnic_qp *qp, int wqe_index, int is_send,
                            struct hgrnic_err_cqe *cqe,
                            struct ibv_wc *wc)
{
    if (cqe->syndrome == SYNDROME_LOCAL_QP_OP_ERR) {
        //printf("local QP operation err "
        //        "(QPN %06x, WQE @ %08x, CQN %06x, index %d)\n",
        //        ntohl(cqe->my_qpn), ntohl(cqe->wqe),
        //        cq->cqn, cq->cons_index);
        //dump_cqe(cqe);
    }

    /*
     * For completions in error, only work request ID, status, vendor error
     * (and freed resource count for RD) have to be set.
     */
    switch (cqe->syndrome) {
    case SYNDROME_LOCAL_LENGTH_ERR:
        wc->status = IBV_WC_LOC_LEN_ERR;
        break;
    case SYNDROME_LOCAL_QP_OP_ERR:
        wc->status = IBV_WC_LOC_QP_OP_ERR;
        break;
    case SYNDROME_LOCAL_EEC_OP_ERR:
        wc->status = IBV_WC_LOC_EEC_OP_ERR;
        break;
    case SYNDROME_LOCAL_PROT_ERR:
        wc->status = IBV_WC_LOC_PROT_ERR;
        break;
    case SYNDROME_WR_FLUSH_ERR:
        wc->status = IBV_WC_WR_FLUSH_ERR;
        break;
    case SYNDROME_MW_BIND_ERR:
        wc->status = IBV_WC_MW_BIND_ERR;
        break;
    case SYNDROME_BAD_RESP_ERR:
        wc->status = IBV_WC_BAD_RESP_ERR;
        break;
    case SYNDROME_LOCAL_ACCESS_ERR:
        wc->status = IBV_WC_LOC_ACCESS_ERR;
        break;
    case SYNDROME_REMOTE_INVAL_REQ_ERR:
        wc->status = IBV_WC_REM_INV_REQ_ERR;
        break;
    case SYNDROME_REMOTE_ACCESS_ERR:
        wc->status = IBV_WC_REM_ACCESS_ERR;
        break;
    case SYNDROME_REMOTE_OP_ERR:
        wc->status = IBV_WC_REM_OP_ERR;
        break;
    case SYNDROME_RETRY_EXC_ERR:
        wc->status = IBV_WC_RETRY_EXC_ERR;
        break;
    case SYNDROME_RNR_RETRY_EXC_ERR:
        wc->status = IBV_WC_RNR_RETRY_EXC_ERR;
        break;
    case SYNDROME_LOCAL_RDD_VIOL_ERR:
        wc->status = IBV_WC_LOC_RDD_VIOL_ERR;
        break;
    case SYNDROME_REMOTE_INVAL_RD_REQ_ERR:
        wc->status = IBV_WC_REM_INV_RD_REQ_ERR;
        break;
    case SYNDROME_REMOTE_ABORTED_ERR:
        wc->status = IBV_WC_REM_ABORT_ERR;
        break;
    case SYNDROME_INVAL_EECN_ERR:
        wc->status = IBV_WC_INV_EECN_ERR;
        break;
    case SYNDROME_INVAL_EEC_STATE_ERR:
        wc->status = IBV_WC_INV_EEC_STATE_ERR;
        break;
    default:
        wc->status = IBV_WC_GENERAL_ERR;
        break;
    }

    wc->vendor_err = cqe->vendor_err;

    return 0;
}

static inline int hgrnic_poll_one(struct hgrnic_cq *cq,
                                  struct hgrnic_qp **cur_qp,
                                  struct ibv_wc *wc)
{
    struct hgrnic_cqe *cqe;
    struct hgrnic_wq *wq;
    int wqe_index;
    int is_error;
    int is_send;
    int err = 0;

    cqe = next_cqe_sw(cq);
    if (!cqe)
        return CQ_EMPTY;

    VALGRIND_MAKE_MEM_DEFINED(cqe, sizeof *cqe);

    /*
     * Make sure we read CQ entry contents after we've checked the
     * ownership bit.
     */
    rmb();

    is_error = (cqe->opcode == HGRNIC_OPCODE_SEND_ERR) ||
               (cqe->opcode == HGRNIC_OPCODE_RECV_ERR);
    is_send  = is_error ? (cqe->opcode == HGRNIC_OPCODE_SEND_ERR) 
                        : cqe->is_send;

   	//printf("hgrnic_poll_one: get cqe! is_error %d, is_send %d\n", is_error, is_send);
    //if ((cq->buf.length / 4096) == 4) {
    //    printf("lkey 0x%x, cqn 0x%x, mask 0x%x cmp_index %d\n", cq->mr->lkey, cq->cqn, cq->cqe_mask, cq->cons_index);
    //    printf("CQE vaddr 0x%lx\n", (uint64_t)cq->buf.buf);
    //    for (int j = 0; j < (cq->buf.length / 4096); ++j) {
    //        for (int i = 0; i < 2; ++i) {
    //            	printf("\tCurrent CQE address %lx\n : ", (uint64_t)(cq->buf.buf + j * 4096 + i * 32)); 
    //            	printf("\tCQ Page Index: %d,\n \t\tCQE Index in Page: %d,\n \t\t\tcqe[0]: 0x%x; cqe[1]: 0x%x; cqe[2]: 0x%x; cqe[3]: 0x%x; cqe[4]: 0x%x; cqe[5]: 0x%x; cqe[6]: 0x%x; cqe[7]: 0x%x\n", 
	//					j,
	//					i,
    //            	    ((uint32_t *)(cq->buf.buf + j * 4096))[i * 8 + 0],
    //            	    ((uint32_t *)(cq->buf.buf + j * 4096))[i * 8 + 1],
    //            	    ((uint32_t *)(cq->buf.buf + j * 4096))[i * 8 + 2],
    //            	    ((uint32_t *)(cq->buf.buf + j * 4096))[i * 8 + 3],
    //            	    ((uint32_t *)(cq->buf.buf + j * 4096))[i * 8 + 4],
    //            	    ((uint32_t *)(cq->buf.buf + j * 4096))[i * 8 + 5],
    //            	    ((uint32_t *)(cq->buf.buf + j * 4096))[i * 8 + 6],
    //            	    ((uint32_t *)(cq->buf.buf + j * 4096))[i * 8 + 7]);
    //        }
    //    }
    //}

	//if(!is_send) {
	//	printf("Get a recv cqe!\n");
    //	printf("is_send %d\n", is_send);
    //	printf("is_error %d\n", is_error);
	//}

    if (!*cur_qp || cqe->my_qpn != (*cur_qp)->ibv_qp.qp_num) {
        /*
         * We do not have to take the QP table lock here,
         * because CQs will be locked while QPs are removed
         * from the table.
         */
        *cur_qp = hgrnic_find_qp(to_hgctx(cq->ibv_cq.context), cqe->my_qpn);
        if (!*cur_qp) {
            err = CQ_POLL_ERR;
            goto out;
        }
    }

    wc->qp_num = (*cur_qp)->ibv_qp.qp_num;

    if (is_send) {
        wq = &(*cur_qp)->sq;
        wqe_index = cqe->wqe >> wq->wqe_shift;
        wc->wr_id = (*cur_qp)->sq.wrid[wqe_index];
    } else {
        wq = &(*cur_qp)->rq;
        wqe_index = cqe->wqe >> wq->wqe_shift;
        wc->wr_id = (*cur_qp)->rq.wrid[wqe_index];

        /* clear valid bit in next_unit */
        memset(wq->buf.buf + cqe->wqe, 0, sizeof(struct hgrnic_next_unit));
    }

    if (wq) {
        if (wq->last_comp < wqe_index)
            wq->tail += wqe_index - wq->last_comp;
        else
            wq->tail += wqe_index + wq->max - wq->last_comp;
		
		if(is_send) {
			//fprintf(stderr, "Get a Send CQE:\n\twq->tail = %lx, wq->last_comp = %lx, wqe_index = %lx\n", wq->tail, wq->last_comp, wqe_index);
		}
		else {
			//fprintf(stderr, "Get a Recv CQE:\n\twq->tail = %lx, wq->last_comp = %lx, wqe_index = %lx\n", wq->tail, wq->last_comp, wqe_index);
		}

        wq->last_comp = wqe_index;
    }

    if (is_error) {
        err = handle_error_cqe(cq, *cur_qp, wqe_index, is_send,
                               (struct hgrnic_err_cqe *) cqe,
                               wc);
        goto out;
    }

    if (is_send) {
        wc->wc_flags = 0;
        switch (cqe->opcode) {
        case HGRNIC_OPCODE_RDMA_WRITE:
            wc->opcode    = IBV_WC_RDMA_WRITE;
            break;
        case HGRNIC_OPCODE_RDMA_WRITE_IMM:
            wc->opcode    = IBV_WC_RDMA_WRITE;
            wc->wc_flags |= IBV_WC_WITH_IMM;
            break;
        case HGRNIC_OPCODE_SEND:
            wc->opcode    = IBV_WC_SEND;
            break;
        case HGRNIC_OPCODE_SEND_IMM:
            wc->opcode    = IBV_WC_SEND;
            wc->wc_flags |= IBV_WC_WITH_IMM;
            break;
        case HGRNIC_OPCODE_RDMA_READ:
            wc->opcode    = IBV_WC_RDMA_READ;
            wc->byte_len  = cqe->byte_cnt;
            break;
        case HGRNIC_OPCODE_ATOMIC_CS:
            wc->opcode    = IBV_WC_COMP_SWAP;
            wc->byte_len  = cqe->byte_cnt;
            break;
        case HGRNIC_OPCODE_ATOMIC_FA:
            wc->opcode    = IBV_WC_FETCH_ADD;
            wc->byte_len  = cqe->byte_cnt;
            break;
        case HGRNIC_OPCODE_BIND_MW:
            wc->opcode    = IBV_WC_BIND_MW;
            break;
        default:
            /* assume it's a send completion */
            wc->opcode    = IBV_WC_SEND;
            break;
        }
    } else {
        wc->byte_len = cqe->byte_cnt;
        switch (cqe->opcode & 0x1f) {
        case IBV_OPCODE_SEND_LAST_WITH_IMMEDIATE:
        case IBV_OPCODE_SEND_ONLY_WITH_IMMEDIATE:
            wc->wc_flags = IBV_WC_WITH_IMM;
            wc->imm_data = cqe->imm;
            wc->opcode = IBV_WC_RECV;
            break;
        case IBV_OPCODE_RDMA_WRITE_LAST_WITH_IMMEDIATE:
        case IBV_OPCODE_RDMA_WRITE_ONLY_WITH_IMMEDIATE:
            wc->wc_flags = IBV_WC_WITH_IMM;
            wc->imm_data = cqe->imm;
            wc->opcode = IBV_WC_RECV_RDMA_WITH_IMM;
            break;
        default:
            wc->wc_flags = 0;
            wc->opcode = IBV_WC_RECV;
            break;
        }
        wc->slid           = cqe->rlid;
        wc->src_qp         = cqe->rqpn & 0xffffff;
        
        wc->sl             = 0; /* This is not used */
        wc->dlid_path_bits = 0; /* This is not used */
        wc->pkey_index     = 0; /* This is not used */
    }

    wc->status = IBV_WC_SUCCESS;

out:
    set_cqe_hw(cqe);
    ++cq->cons_index;

    return err;
}

int hgrnic_notify_cq(struct ibv_cq *cq, int solicited_only) {
    return 0;
}

int hgrnic_poll_cq(struct ibv_cq *ibcq, int ne, struct ibv_wc *wc)
{
    struct hgrnic_cq *cq = to_hgcq(ibcq);
    struct hgrnic_qp *qp = NULL;
    int err = CQ_OK;
    int npolled;

    pthread_spin_lock(&cq->lock);

    for (npolled = 0; npolled < ne; ++npolled) {
        err = hgrnic_poll_one(cq, &qp, wc + npolled);
        if (err != CQ_OK)
            break;
    }

    pthread_spin_unlock(&cq->lock);

    return err == CQ_POLL_ERR ? err : npolled;
}

static inline int is_recv_cqe(struct hgrnic_cqe *cqe)
{
	if ((cqe->opcode & HGRNIC_ERROR_CQE_OPCODE_MASK) ==
	    HGRNIC_ERROR_CQE_OPCODE_MASK)
		return !(cqe->opcode & 0x01);
	else
		return !(cqe->is_send & 0x80);
}

void __hgrnic_cq_clean(struct hgrnic_cq *cq, uint32_t qpn)
{
    struct hgrnic_cqe *cqe;
    uint32_t prod_index;
    int i, nfreed = 0;

    /*
     * First we need to find the current producer index, so we
     * know where to start cleaning from.  It doesn't matter if HW
     * adds new entries after this loop -- the QP we're worried
     * about is already in RESET, so the new entries won't come
     * from our QP and therefore don't need to be checked.
     */
    for (prod_index = cq->cons_index;
         cqe_sw(cq, prod_index & cq->ibv_cq.cqe);
         ++prod_index)
        if (prod_index == cq->cons_index + cq->ibv_cq.cqe)
            break;

    /*
     * Now sweep backwards through the CQ, removing CQ entries
     * that match our QP by copying older entries on top of them.
     */
    while ((int) --prod_index - (int) cq->cons_index >= 0) {
        cqe = get_cqe(cq, prod_index & cq->ibv_cq.cqe);
        //printf("[__hgrnic_cq_clean] my_qpn is 0x%x, qpn is 0x%x\n", cqe->my_qpn, qpn);
        if (cqe->my_qpn == qpn) // !TODO: The order need to notice
            ++nfreed;
        else if (nfreed)
            memcpy(get_cqe(cq, (prod_index + nfreed) & cq->ibv_cq.cqe),
                   cqe, HGRNIC_CQ_ENTRY_SIZE);
    }

    if (nfreed) {
        for (i = 0; i < nfreed; ++i)
            set_cqe_hw(get_cqe(cq, (cq->cons_index + i) & cq->ibv_cq.cqe));
        wmb();
        cq->cons_index += nfreed;
    }
}

void hgrnic_cq_clean(struct hgrnic_cq *cq, uint32_t qpn)
{
    pthread_spin_lock(&cq->lock);
    __hgrnic_cq_clean(cq, qpn);
    pthread_spin_unlock(&cq->lock);
}

void hgrnic_cq_resize_copy_cqes(struct hgrnic_cq *cq, void *buf, int old_cqe)
{
    int i;

    for (i = cq->cons_index; cqe_sw(cq, i & old_cqe); ++i)
        memcpy(buf + (i & cq->ibv_cq.cqe) * HGRNIC_CQ_ENTRY_SIZE,
                get_cqe(cq, i & old_cqe), HGRNIC_CQ_ENTRY_SIZE);
}

int hgrnic_alloc_cq_buf(struct hgrnic_device *dev, struct hgrnic_buf *buf, int nent)
{
    int i;

    if (hgrnic_alloc_buf(buf, align(nent * HGRNIC_CQ_ENTRY_SIZE, dev->page_size),
            dev->page_size))
        return -1;

    for (i = 0; i < nent; ++i)
        ((struct hgrnic_cqe *) buf->buf)[i].owner = HGRNIC_CQ_ENTRY_OWNER_HW;

    return 0;
}
