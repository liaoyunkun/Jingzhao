/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2006 Cisco Systems.  All rights reserved.
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

#ifndef HGRNIC_H
#define HGRNIC_H

#include <stddef.h>

#include <infiniband/driver.h>
#include <infiniband/arch.h>

#ifdef HAVE_VALGRIND_MEMCHECK_H

#  include <valgrind/memcheck.h>

#  if !defined(VALGRIND_MAKE_MEM_DEFINED) || !defined(VALGRIND_MAKE_MEM_UNDEFINED)
#    warning "Valgrind support requested, but VALGRIND_MAKE_MEM_(UN)DEFINED not available"
#  endif

#endif /* HAVE_VALGRIND_MEMCHECK_H */

#ifndef VALGRIND_MAKE_MEM_DEFINED
#  define VALGRIND_MAKE_MEM_DEFINED(addr,len)
#endif

#ifndef VALGRIND_MAKE_MEM_UNDEFINED
#  define VALGRIND_MAKE_MEM_UNDEFINED(addr,len)
#endif

#ifndef rmb
#  define rmb() mb()
#endif

#ifndef wmb
#  define wmb() mb()
#endif

#define HIDDEN		__attribute__((visibility ("hidden")))

#define PFX		"hgrnic: "

#define dump_state printf("[%s]: " sta "\n", __FUNCTION__, sta);

enum {
    HGRNIC_CQ_ENTRY_SIZE = 0x20
};

enum {
    HGRNIC_QP_TABLE_BITS = 8,
    HGRNIC_QP_TABLE_SIZE = 1 << HGRNIC_QP_TABLE_BITS,
    HGRNIC_QP_TABLE_MASK = HGRNIC_QP_TABLE_SIZE - 1
};

enum {
    HGRNIC_DB_REC_PAGE_SIZE = 4096,
    HGRNIC_DB_REC_PER_PAGE  = HGRNIC_DB_REC_PAGE_SIZE / 8
};

enum hgrnic_db_type {
	HGRNIC_DB_TYPE_INVALID   = 0x0,
	HGRNIC_DB_TYPE_CQ_SET_CI = 0x1,
	HGRNIC_DB_TYPE_CQ_ARM    = 0x2,
	HGRNIC_DB_TYPE_SQ        = 0x3,
	HGRNIC_DB_TYPE_RQ        = 0x4,
	HGRNIC_DB_TYPE_SRQ       = 0x5,
	HGRNIC_DB_TYPE_GROUP_SEP = 0x7
};

enum {
    HGRNIC_OPCODE_NOP            = 0x00,
    HGRNIC_OPCODE_RDMA_WRITE     = 0x08,
    HGRNIC_OPCODE_RDMA_WRITE_IMM = 0x09,
    HGRNIC_OPCODE_SEND           = 0x0a,
    HGRNIC_OPCODE_SEND_IMM       = 0x0b,
    HGRNIC_OPCODE_RDMA_READ      = 0x10,
    HGRNIC_OPCODE_ATOMIC_CS      = 0x11,
    HGRNIC_OPCODE_ATOMIC_FA      = 0x12,
    HGRNIC_OPCODE_BIND_MW        = 0x18,
    HGRNIC_OPCODE_RECV_ERR       = 0xfe,
    HGRNIC_OPCODE_SEND_ERR       = 0xff
};

struct hgrnic_device {
	struct ibv_device   ibv_dev;
	int                 page_size;
};

struct hgrnic_db_table;

struct hgrnic_context {
    struct ibv_context      ibv_ctx;
    void                    *uar;
    pthread_spinlock_t      uar_lock;
    struct hgrnic_db_table  *db_tab;
    struct ibv_pd           *pd; // CQ, QP queue buffer belongs to this pd
    struct {
        struct hgrnic_qp    **table;
        int                   refcnt;
    }                      qp_table[HGRNIC_QP_TABLE_SIZE];
    pthread_mutex_t        qp_table_mutex;
    int                    num_qps;
    int                    qp_table_shift; // Number of elem in one qp table in log
    int                    qp_table_mask ; // number of elem in one QP table
};

struct hgrnic_buf {
    void           *buf   ; // addr
    size_t          length;
};

struct hgrnic_pd {
    struct ibv_pd         ibv_pd;
    pthread_mutex_t       ah_mutex;
    uint32_t              pdn;
};

struct hgrnic_cq {
    struct ibv_cq       ibv_cq;
    struct hgrnic_buf   buf   ; /* queue buffer */
    pthread_spinlock_t  lock  ;
    struct ibv_mr      *mr    ;
    uint32_t            cqn   ;
    uint32_t            cons_index; /* comsumer index, point to next consuming cqe, never go down */
    uint32_t            cqe_mask  ; /* cqe num - 1 */
};

struct hgrnic_wq {
    pthread_spinlock_t  lock;
    int                 max;
    unsigned            next_ind;
    unsigned            last_comp; /* last completed wqe index, always  < max */
    unsigned            head; /* never go down */
    unsigned            tail; /* never go down */
    void               *last;
    int                 max_gs;
    int                 wqe_shift;

    int                 buf_size;
    struct hgrnic_buf   buf     ;
    struct ibv_mr       *mr     ;

    uint64_t            *wrid   ;
};

struct hgrnic_qp {
    struct ibv_qp     ibv_qp;
    int               max_inline_data;
    struct hgrnic_wq  sq;
    struct hgrnic_wq  rq;
};

struct hgrnic_av {
    uint32_t port_pd;
    uint8_t  reserved1;
    uint8_t  g_slid;
    uint16_t dlid;
    uint8_t  reserved2;
    uint8_t  gid_index;
    uint8_t  msg_sr;
    uint8_t  hop_limit;
    uint32_t sl_tclass_flowlabel;
    uint32_t dgid[4];
};

struct hgrnic_ah {
    struct ibv_ah         ibv_ah;
    struct hgrnic_av      *av;
    uint32_t              key;
};

static inline unsigned long align(unsigned long val, unsigned long align)
{
    return (val + align - 1) & ~(align - 1);
}

#define to_hgxxx(xxx, type)						\
	((struct hgrnic_##type *)					\
	 ((void *) ib##xxx - offsetof(struct hgrnic_##type, ibv_##xxx)))

static inline struct hgrnic_device *to_hgdev(struct ibv_device *ibdev)
{
    return to_hgxxx(dev, device);
}

static inline struct hgrnic_context *to_hgctx(struct ibv_context *ibctx)
{
    return to_hgxxx(ctx, context);
}

static inline struct hgrnic_pd *to_hgpd(struct ibv_pd *ibpd)
{
    return to_hgxxx(pd, pd);
}

static inline struct hgrnic_cq *to_hgcq(struct ibv_cq *ibcq)
{
    return to_hgxxx(cq, cq);
}

static inline struct hgrnic_qp *to_hgqp(struct ibv_qp *ibqp)
{
    return to_hgxxx(qp, qp);
}

static inline struct hgrnic_ah *to_hgah(struct ibv_ah *ibah)
{
    return to_hgxxx(ah, ah);
}

int hgrnic_alloc_buf(struct hgrnic_buf *buf, size_t size, int page_size);
void hgrnic_free_buf(struct hgrnic_buf *buf);

int hgrnic_query_device(struct ibv_context *context,
                        struct ibv_device_attr *attr);
int hgrnic_query_port(struct ibv_context *context, uint8_t port,
                      struct ibv_port_attr *attr);

struct ibv_pd *hgrnic_alloc_pd(struct ibv_context *context);
int hgrnic_free_pd(struct ibv_pd *pd);

struct ibv_mr *hgrnic_reg_mr(struct ibv_pd *pd, void *addr,
                             size_t length, enum ibv_access_flags access);
int hgrnic_dereg_mr(struct ibv_mr *mr);

struct ibv_cq *hgrnic_create_cq(struct ibv_context *context, int cqe,
                                struct ibv_comp_channel *channel,
                                int comp_vector);
int hgrnic_resize_cq(struct ibv_cq *cq, int cqe);
int hgrnic_destroy_cq(struct ibv_cq *cq);
int hgrnic_notify_cq(struct ibv_cq *cq, int solicited_only);
int hgrnic_poll_cq(struct ibv_cq *cq, int ne, struct ibv_wc *wc);
void __hgrnic_cq_clean(struct hgrnic_cq *cq, uint32_t qpn);
void hgrnic_cq_clean(struct hgrnic_cq *cq, uint32_t qpn);
void hgrnic_cq_resize_copy_cqes(struct hgrnic_cq *cq, void *buf, int new_cqe);
int hgrnic_alloc_cq_buf(struct hgrnic_device *dev, struct hgrnic_buf *buf, int nent);

struct ibv_qp *hgrnic_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *attr);
int hgrnic_query_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
                    enum ibv_qp_attr_mask attr_mask,
                    struct ibv_qp_init_attr *init_attr);
int hgrnic_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
                     enum ibv_qp_attr_mask attr_mask);
int hgrnic_destroy_qp(struct ibv_qp *qp);
void hgrnic_init_qp_indices(struct hgrnic_qp *qp);
int hgrnic_post_send(struct ibv_qp *ibqp, struct ibv_send_wr *wr,
			  struct ibv_send_wr **bad_wr);
int hgrnic_post_recv(struct ibv_qp *ibqp, struct ibv_recv_wr *wr,
                     struct ibv_recv_wr **bad_wr);
int hgrnic_alloc_qp_buf(struct ibv_pd *pd, struct ibv_qp_cap *cap,
                        enum ibv_qp_type type, struct hgrnic_qp *qp);
struct hgrnic_qp *hgrnic_find_qp(struct hgrnic_context *ctx, uint32_t qpn);
int hgrnic_store_qp(struct hgrnic_context *ctx, uint32_t qpn, struct hgrnic_qp *qp);
void hgrnic_clear_qp(struct hgrnic_context *ctx, uint32_t qpn);

// SRQ related
struct ibv_srq * hgrnic_create_srq(struct ibv_pd *pd, struct ibv_srq_init_attr *srq_init_attr);
int hgrnic_modify_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr, enum ibv_srq_attr_mask srq_attr_mask);
int hgrnic_query_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr);
int hgrnic_destroy_srq(struct ibv_srq *srq);
int hgrnic_post_srq_recv(struct ibv_srq *srq, struct ibv_recv_wr *recv_wr, struct ibv_recv_wr **bad_recv_wr);

struct ibv_ah *hgrnic_create_ah(struct ibv_pd *pd, struct ibv_ah_attr *attr);
int hgrnic_destroy_ah(struct ibv_ah *ah);
int hgrnic_alloc_av(struct hgrnic_pd *pd, struct ibv_ah_attr *attr,
                    struct hgrnic_ah *ah);
void hgrnic_free_av(struct hgrnic_ah *ah);

#endif /* HGRNIC_H */
