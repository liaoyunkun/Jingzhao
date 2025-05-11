/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies Ltd.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>

#include "hgrnic.h"
#include "doorbell.h"
#include "wqe.h"

enum {
	HGRNIC_SEND_DOORBELL_FENCE = 1 << 5
};

static const uint8_t hgrnic_opcode[] = {
    [IBV_WR_SEND]                 = HGRNIC_OPCODE_SEND,
    [IBV_WR_SEND_WITH_IMM]        = HGRNIC_OPCODE_SEND_IMM,
    [IBV_WR_RDMA_WRITE]           = HGRNIC_OPCODE_RDMA_WRITE,
    [IBV_WR_RDMA_WRITE_WITH_IMM]  = HGRNIC_OPCODE_RDMA_WRITE_IMM,
    [IBV_WR_RDMA_READ]            = HGRNIC_OPCODE_RDMA_READ,
    [IBV_WR_ATOMIC_CMP_AND_SWP]   = HGRNIC_OPCODE_ATOMIC_CS,
    [IBV_WR_ATOMIC_FETCH_AND_ADD] = HGRNIC_OPCODE_ATOMIC_FA,
};

static void *get_wqe(struct hgrnic_wq wq, int n)
{
    return wq.buf.buf + (n << wq.wqe_shift);
}

void hgrnic_init_qp_indices(struct hgrnic_qp *qp)
{
    qp->sq.next_ind  = 0;
    qp->sq.last_comp = qp->sq.max - 1;
    qp->sq.head    	 = 0;
    qp->sq.tail    	 = 0;
    qp->sq.last      = get_wqe(qp->sq, qp->sq.max - 1);

    qp->rq.next_ind	 = 0;
    qp->rq.last_comp = qp->rq.max - 1;
    qp->rq.head    	 = 0;
    qp->rq.tail    	 = 0;
    qp->rq.last      = get_wqe(qp->rq, qp->rq.max - 1);
}

static inline int wq_overflow(struct hgrnic_wq *wq, int nreq, struct hgrnic_cq *cq)
{
    unsigned cur;


    cur = wq->head - wq->tail;
    if (cur + nreq < wq->max)
        return 0;


    pthread_spin_lock(&cq->lock);
    cur = wq->head - wq->tail;
    pthread_spin_unlock(&cq->lock);

    return cur + nreq >= wq->max;
}

static inline void set_data_unit (struct hgrnic_data_unit *dunit,
                                         struct ibv_sge *sg) {
    dunit->byte_count = sg->length;
    dunit->lkey       = sg->lkey  ;
    dunit->addr       = sg->addr  ;
}

static inline void hgrnic_set_data_unit_inval(struct hgrnic_data_unit *dunit)
{
    dunit->byte_count = 0;
    dunit->lkey       = HGRNIC_INVAL_LKEY;
    dunit->addr       = 0;
    //printf("set invalid data unit\n");
}

static inline void set_raddr_unit(struct hgrnic_raddr_unit *runit,
                                  uint64_t remote_addr, uint32_t rkey)
{
    runit->raddr    = remote_addr;
    runit->rkey     = rkey;
    runit->reserved = 0;
}

static inline void set_ud_unit(struct hgrnic_ud_unit *uunit,
                               struct hgrnic_av *av, 
                               uint32_t dst_qpn, uint32_t dst_qkey)
{
    uunit->port = av->port_pd;
    uunit->slid = av->g_slid ;
    uunit->dlid = av->dlid   ;
    uunit->dqpn = dst_qpn    ;
    uunit->qkey = dst_qkey   ;
}

static inline void hgrnic_send_db(struct hgrnic_qp *qp, int nreq, 
                                  uint32_t size0, uint32_t f0, uint32_t op0)
{
    uint32_t db[2];

    db[1] = (qp->ibv_qp.qp_num << 8) | size0;
    db[0] = ((qp->sq.head << (qp->sq.wqe_shift-4)) << 8) | f0 | op0;

    wmb();
    hgrnic_write64(db, to_hgctx(qp->ibv_qp.context), HGRNIC_SEND_DOORBELL);
    
    qp->sq.head += nreq; // never go down

//  	  	printf("Send doorbell[1]: 0x%x\n", db[1]);
//	  	printf("Send doorbell[0]: 0x%x\n", db[0]);
//      	fprintf(stderr, "Send doorbell[1]: 0x%x\n", db[1]);
//      	fprintf(stderr, "Send doorbell[0]: 0x%x\n\n\n", db[0]);
}

int hgrnic_post_send(struct ibv_qp *ibqp, struct ibv_send_wr *wr,
                           struct ibv_send_wr **bad_wr)
{
	//fprintf(stderr, "Post Send Begin\n");
    struct hgrnic_qp *qp = to_hgqp(ibqp);

    void *cur_unit; /* point to current prcessing unit in current wqe */
    void *prev_wqe;
    void *cur_wqe ; /* point to current processing wqe */
    int ret = 0;
    int nreq;
    int size;
    int ind;
    int i;
    /*
     * f0 and op0 cannot be used unless nreq > 0, which means this
     * function makes it through the loop at least once.  So the
     * code inside the if (!size0) will be executed, and f0 and
     * op0 will be initialized.  So any gcc warning about "may be
     * used unitialized" is bogus.
     */
    uint32_t f0;
    uint32_t op0;
    uint32_t size0;

    pthread_spin_lock(&qp->sq.lock);

    ind = qp->sq.head & (qp->sq.max - 1);

    for (nreq = 0; wr; ++nreq, wr = wr->next) {
        if (nreq == HGRNIC_MAX_WQES_PER_SEND_DB) {
            hgrnic_send_db(qp, nreq, size0, f0, op0);
            nreq = 0;
        }

        if (wq_overflow(&qp->sq, nreq, to_hgcq(qp->ibv_qp.send_cq))) {
            struct hgrnic_wq *wq = &qp->sq;
            fprintf(stderr, "wq->head = %x\n", wq->head);
            fprintf(stderr, "wq->tail = %x\n", wq->tail);
            fprintf(stderr, "wq->max = %x\n", wq->max);
            fprintf(stderr, "wq_overflow\n");
            ret = -1;
            *bad_wr = wr;
            goto out;
        }

        cur_wqe  = get_wqe(qp->sq, ind);
        cur_unit = cur_wqe;
        prev_wqe = qp->sq.last;
        qp->sq.last = cur_wqe;

        ((struct hgrnic_next_unit *) cur_unit)->flags =
            ((wr->send_flags & IBV_SEND_SIGNALED)  ? HGRNIC_NEXT_CQ_UPDATE : 0) |
            ((wr->send_flags & IBV_SEND_SOLICITED) ? HGRNIC_NEXT_SOLICIT : 0)   |
            0;
        if (wr->opcode == IBV_WR_SEND_WITH_IMM ||
            wr->opcode == IBV_WR_RDMA_WRITE_WITH_IMM)
            ((struct hgrnic_next_unit *) cur_unit)->imm = wr->imm_data;

        cur_unit += sizeof (struct hgrnic_next_unit);
        size = sizeof (struct hgrnic_next_unit) / 16;

        switch (ibqp->qp_type) {
        case IBV_QPT_RC:
            switch (wr->opcode) {
            case IBV_WR_RDMA_WRITE:
            case IBV_WR_RDMA_WRITE_WITH_IMM:
            case IBV_WR_RDMA_READ:
                set_raddr_unit((struct hgrnic_raddr_unit *)cur_unit, 
                                wr->wr.rdma.remote_addr,
                                wr->wr.rdma.rkey);
                cur_unit += sizeof (struct hgrnic_raddr_unit);
                size     += sizeof (struct hgrnic_raddr_unit) / 16;
                break;

            default:
                break;
            }
            break;

        case IBV_QPT_UC:
            switch (wr->opcode) {
            case IBV_WR_RDMA_WRITE:
            case IBV_WR_RDMA_WRITE_WITH_IMM:
                 set_raddr_unit((struct hgrnic_raddr_unit *)cur_unit, 
                                wr->wr.rdma.remote_addr,
                                wr->wr.rdma.rkey);
                cur_unit += sizeof (struct hgrnic_raddr_unit);
                size     += sizeof (struct hgrnic_raddr_unit) / 16;
                break;

            default:
                /* No extra segments required for sends */
                break;
            }
            break;

        case IBV_QPT_UD:
            set_ud_unit(
                ((struct hgrnic_ud_unit *)cur_unit),
                to_hgah(wr->wr.ud.ah)->av,
                wr->wr.ud.remote_qpn,
                wr->wr.ud.remote_qkey
            );
            cur_unit += sizeof (struct hgrnic_ud_unit);
            size     += sizeof (struct hgrnic_ud_unit) / 16;
            break;

        default:
            break;
        }

        if (wr->num_sge > qp->sq.max_gs) {
			fprintf(stderr, "wr->num_sge > qp->sq.max_gs\n");
            ret = -1;
            *bad_wr = wr;
            goto out;
        }

        if (wr->send_flags & IBV_SEND_INLINE) {
            if (wr->num_sge) {
                struct hgrnic_inline_unit *inline_unit = cur_unit;
                int s = 0;

                cur_unit += sizeof(struct hgrnic_inline_unit);
                for (i = 0; i < wr->num_sge; ++i) {
                    struct ibv_sge *sge = &wr->sg_list[i];

                    s += sge->length;

                    if (s > qp->max_inline_data) {
						fprintf(stderr, "s > qp->max_inline_data\n");
                        ret = -1;
                        *bad_wr = wr;
                        goto out;
                    }

                    memcpy(cur_unit, (void *) (uintptr_t) sge->addr,
                            sge->length);
                    cur_unit += sge->length;
                }

                inline_unit->byte_count = HGRNIC_INLINE_UNIT | s;
                size += align(s + sizeof(struct hgrnic_inline_unit), 16) / 16;
            }
        } else {
            for (i = 0; i < wr->num_sge; ++i) {
                set_data_unit(cur_unit, &wr->sg_list[i]);
                cur_unit += sizeof(struct hgrnic_data_unit);
            }
            size += wr->num_sge * (sizeof(struct hgrnic_data_unit) / 16);
        }

        qp->sq.wrid[ind] = wr->wr_id;

        if (wr->opcode >= sizeof(hgrnic_opcode) / sizeof(hgrnic_opcode[0])) {
			fprintf(stderr, "wr->opcode >= sizeof... ...\n");
            ret = -1;
            *bad_wr = wr;
            goto out;
        }

        if (!nreq) {
            size0 = size;
            op0   = hgrnic_opcode[wr->opcode];
            f0    = wr->send_flags & IBV_SEND_FENCE ?
                    HGRNIC_SEND_DOORBELL_FENCE : 0;
        } else {
            ((struct hgrnic_next_unit *) prev_wqe)->nda_nop =
                    ((ind << (qp->sq.wqe_shift - 4)) << 6) | hgrnic_opcode[wr->opcode];
            ((struct hgrnic_next_unit *) prev_wqe)->ee_nds =
                    HGRNIC_NEXT_DBD | ((wr->send_flags & IBV_SEND_FENCE) ? HGRNIC_NEXT_FENCE : 0) | 
                    size;
        }

        ++ind;
        if (ind >= qp->sq.max)
            ind -= qp->sq.max;
        
        //printf ("qp->sq.mr->lkey 0x%x, head %d, wqe_addr 0x%lx, qp->sq addr 0x%lx, wqe_shift %d ind %d max 0x%x, nreq %d\n", 
        //        qp->sq.mr->lkey, qp->sq.head, (uintptr_t)cur_wqe, (uintptr_t)qp->sq.buf.buf, qp->sq.wqe_shift, ind, qp->sq.max, nreq);
    //    for (int i = 0; i < size * 16 ; ++i)
         //   printf ("cur_wqe[%d]: 0x%x\n", i, ((uint8_t *)cur_wqe)[i]);
    }

out:
    if (nreq)
        hgrnic_send_db(qp, nreq, size0, f0, op0);

	//fprintf(stderr, "Post Send End:\n\tnreq = %d, wq->head = %lx, wq->tail = %lx, wq->max = %lx\n", nreq, (&qp->sq)->head, (&qp->sq)->tail, (&qp->sq)->max);
    pthread_spin_unlock(&qp->sq.lock);
    return ret;
}

int hgrnic_post_recv(struct ibv_qp *ibqp, struct ibv_recv_wr *wr,
                     struct ibv_recv_wr **bad_wr)
{
    struct hgrnic_qp *qp = to_hgqp(ibqp);
    int ret = 0;
    void *wqe;
    struct hgrnic_next_unit *wqe_next_unit = NULL; /* pointingg to previous unit */
    uint32_t *tmp_wqe;
    int nreq;
    int ind;
    int i;

    pthread_spin_lock(&qp->rq.lock);

    ind = qp->rq.head & (qp->rq.max - 1);

    for (nreq = 0; wr; ++nreq, wr = wr->next) {
        if (wq_overflow(&qp->rq, nreq, to_hgcq(qp->ibv_qp.recv_cq))) {
            ret = -1;
            *bad_wr = wr;
            goto out;
        }

        /* fill previous wqe next unit */
        if (wqe_next_unit) {
            wqe_next_unit->nda_nop = (ind << (qp->rq.wqe_shift - 4 + 6)) | HGRNIC_NEXT_VALID; // nda is in 16 byte unit; nop is ignored
            wqe_next_unit->ee_nds  = (wr->num_sge + 1) * sizeof(struct hgrnic_data_unit) / 16; /* Including the invalid data unit; in 16 byte unit */
        }

        wqe = get_wqe(qp->rq, ind);
        wqe_next_unit = wqe;
        tmp_wqe = wqe;

        ((struct hgrnic_next_unit *) wqe)->nda_nop = HGRNIC_NEXT_VALID;
        ((struct hgrnic_next_unit *) wqe)->ee_nds  = 0;
        ((struct hgrnic_next_unit *) wqe)->flags   = 0;

        wqe += sizeof (struct hgrnic_next_unit);

        if (wr->num_sge > qp->rq.max_gs) {
            ret = -1;
            *bad_wr = wr;
            goto out;
        }

        for (i = 0; i < wr->num_sge; ++i) {
            set_data_unit(wqe, &wr->sg_list[i]);
            wqe += sizeof (struct hgrnic_data_unit);
        }

        if (i < qp->rq.max_gs)
            hgrnic_set_data_unit_inval(wqe);

        qp->rq.wrid[ind] = wr->wr_id;

        ++ind;
        if (ind >= qp->rq.max)
            ind -= qp->rq.max;
        
        for (int i = 0; i < 64; ++i) {
            //printf("rcv_wqe[%d] 0x%x\n", i, tmp_wqe[i]);
        }
    }
out:
    if (nreq)
        qp->rq.head += nreq;

    pthread_spin_unlock(&qp->rq.lock);
    return ret;
}

int hgrnic_alloc_qp_buf(struct ibv_pd *pd, struct ibv_qp_cap *cap,
                        enum ibv_qp_type type, struct hgrnic_qp *qp)
{
    // struct hgrnic_data_unit *scatter;
    // struct hgrnic_next_unit *next;
    int max_sq_sge;
    int size;
    // int i;

    qp->rq.max_gs = cap->max_recv_sge;
    qp->sq.max_gs = cap->max_send_sge;
    max_sq_sge    = align(cap->max_inline_data + sizeof (struct hgrnic_inline_unit),
                    sizeof (struct hgrnic_data_unit)) / sizeof (struct hgrnic_data_unit);
    if (max_sq_sge < cap->max_send_sge)
        max_sq_sge = cap->max_send_sge;

    qp->sq.wrid = malloc(qp->sq.max * sizeof (uint64_t));
    if (!qp->sq.wrid)
        return -1;

    qp->rq.wrid = malloc(qp->rq.max * sizeof (uint64_t));
    if (!qp->rq.wrid) {
        free(qp->sq.wrid);
        return -1;
    }

    size = sizeof (struct hgrnic_next_unit) +
        qp->rq.max_gs * sizeof (struct hgrnic_data_unit);
    //printf("rq size is : %d\n", size);

    /* init value is 6, cause we must allow one extra empty 
     * data unit to identify the end of wqe. So, the minimum 
     * size of wqe is 1 * next_unit + 2 * data_unit = 48 bytes. */
    for (qp->rq.wqe_shift = 7; 1 << qp->rq.wqe_shift < size;
         qp->rq.wqe_shift++)
        ; /* nothing */

    qp->rq.buf_size = qp->rq.max << qp->rq.wqe_shift;

    size = max_sq_sge * sizeof (struct hgrnic_data_unit);
    switch (type) {
    case IBV_QPT_UD:
        size += sizeof (struct hgrnic_ud_unit);
        break;

    case IBV_QPT_UC:
        size += sizeof (struct hgrnic_raddr_unit);
        break;

    case IBV_QPT_RC:
        size += sizeof (struct hgrnic_raddr_unit);
        /*
            * An atomic op will require an atomic segment, a
            * remote address segment and one scatter entry.
            */
        if (size < (sizeof (struct hgrnic_atomic_seg) +
                sizeof (struct hgrnic_raddr_unit) +
                sizeof (struct hgrnic_data_unit)))
            size = (sizeof (struct hgrnic_atomic_seg) +
                sizeof (struct hgrnic_raddr_unit) +
                sizeof (struct hgrnic_data_unit));
        break;

    default:
        break;
    }

    size += sizeof (struct hgrnic_next_unit);

    for (qp->sq.wqe_shift = 7; 1 << qp->sq.wqe_shift < size;
            qp->sq.wqe_shift++)
        ; /* nothing */

    qp->sq.buf_size = qp->sq.max << qp->sq.wqe_shift;

    /* Allocate queue space for SQ. */
    if (hgrnic_alloc_buf(&qp->sq.buf,
                align(qp->sq.buf_size, to_hgdev(pd->context->device)->page_size),
                to_hgdev(pd->context->device)->page_size)) {
        free(qp->sq.wrid);
        free(qp->rq.wrid);
        return -1;
    }

    memset(qp->sq.buf.buf, 0, qp->sq.buf_size);

    /* Allocate queue space for RQ. */
    if (qp->rq.buf_size) { // if srq is used, rq buffer size is 0
        if (hgrnic_alloc_buf(&qp->rq.buf,
                    align(qp->rq.buf_size, to_hgdev(pd->context->device)->page_size),
                    to_hgdev(pd->context->device)->page_size)) {
            hgrnic_free_buf(&qp->sq.buf);
            free(qp->rq.wrid);
            free(qp->sq.wrid);
            return -1;
        }

        memset(qp->rq.buf.buf, 0, qp->rq.buf_size);
        
        //printf("rq buf size is : %d\n", qp->rq.buf_size);
    }

    // Initializing RQ queue buffer
    // size = (sizeof (struct hgrnic_next_unit) +
    //         qp->rq.max_gs * sizeof (struct hgrnic_data_unit)) / 16;

    // for (i = 0; i < qp->rq.max; ++i) {
    //     next = get_wqe(qp->rq, i);
        // next->nda_nop = ((i + 1) & (qp->rq.max - 1)) << (qp->rq.wqe_shift - 4 + 6);
        // next->ee_nds = size; /* in 16 byte unit */

        // for (scatter = (void *) (next + 1);
        //         (void *) scatter < (void *) next + (1 << qp->rq.wqe_shift);
        //         ++scatter)
        //     scatter->lkey = HGRNIC_INVAL_LKEY;
    // }

    // Initializing SQ queue buffer
    // for (i = 0; i < qp->sq.max; ++i) {
    //     next = get_wqe(qp->sq, i);
        // next->nda_nop = (((i + 1) & (qp->sq.max - 1)) <<
        //                 qp->sq.wqe_shift) << 6;
    // }

    hgrnic_init_qp_indices(qp);

    return 0;
}

struct hgrnic_qp *hgrnic_find_qp(struct hgrnic_context *ctx, uint32_t qpn)
{
    int tind = (qpn & (ctx->num_qps - 1)) >> ctx->qp_table_shift;

    if (ctx->qp_table[tind].refcnt)
        return ctx->qp_table[tind].table[qpn & ctx->qp_table_mask];
    else
        return NULL;
}

/* store qp in qp table */
int hgrnic_store_qp(struct hgrnic_context *ctx, uint32_t qpn, struct hgrnic_qp *qp)
{
    int tind = (qpn & (ctx->num_qps - 1)) >> ctx->qp_table_shift; // table index

    if (!ctx->qp_table[tind].refcnt) {
        ctx->qp_table[tind].table = calloc(ctx->qp_table_mask + 1,
                            sizeof (struct hgrnic_qp *));
        if (!ctx->qp_table[tind].table)
            return -1;
    }

    ++ctx->qp_table[tind].refcnt;
    ctx->qp_table[tind].table[qpn & ctx->qp_table_mask] = qp;
    return 0;
}

void hgrnic_clear_qp(struct hgrnic_context *ctx, uint32_t qpn)
{
    int tind = (qpn & (ctx->num_qps - 1)) >> ctx->qp_table_shift;

    if (!--ctx->qp_table[tind].refcnt)
        free(ctx->qp_table[tind].table);
    else
        ctx->qp_table[tind].table[qpn & ctx->qp_table_mask] = NULL;
}
