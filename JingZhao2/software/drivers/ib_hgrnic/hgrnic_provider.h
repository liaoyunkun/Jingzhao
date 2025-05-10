/**************************************************************
 * @author Kang Ning<kangning18z@ict.ac.cn>, NCIC, ICT, CAS
 * @date 2021.09.08
 * @file hgrnic_provider.h
 * @note Functions & Data struct related to interface with ib_uverbs.
 *************************************************************/

#ifndef HGRNIC_PROVIDER_H
#define HGRNIC_PROVIDER_H

#include <rdma/ib_verbs.h>
#include <rdma/ib_pack.h>

#define HGRNIC_MPT_FLAG_ATOMIC        (1 << 3)
#define HGRNIC_MPT_FLAG_REMOTE_WRITE  (1 << 1)
#define HGRNIC_MPT_FLAG_REMOTE_READ   (1 << 2)
#define HGRNIC_MPT_FLAG_LOCAL_WRITE   (1 << 0)
#define HGRNIC_MPT_FLAG_LOCAL_READ    (1 << 7)

struct hgrnic_buf_list {
    void *buf;                      /* Vaddr  */
    DEFINE_DMA_UNMAP_ADDR(mapping); /* Bus addr  */
};

union hgrnic_buf {
    struct hgrnic_buf_list direct;
    struct hgrnic_buf_list *page_list;
};

struct hgrnic_uar {
    unsigned long pfn;
    int           index;
};

struct hgrnic_ucontext {
    struct ib_ucontext          ibucontext;
    struct hgrnic_uar            uar;
};

struct hgrnic_mtt {
    struct hgrnic_buddy *buddy;
    int                 order; /* Actual size in log allocated mtt. */
    u32                 first_index; /* MTT first index **/
};

struct hgrnic_mr {
    struct ib_mr      ibmr;
    struct ib_umem   *umem;
    struct hgrnic_mtt *mtt;
};

struct hgrnic_pd {
	struct ib_pd    ibpd;
	u32             pd_num;
	int             privileged;
};

struct hgrnic_eq {
	struct hgrnic_dev      *dev;
	int                    eqn;
	u32                    eqn_mask;
	u32                    cons_index;
	u16                    msi_x_vector;
	u16                    msi_x_entry;
	int                    have_irq;
	int                    nent; // Number of EQs in this device (the n-th power of 2)
	struct hgrnic_buf_list *page_list; // Allocated memory for Event Queue
	struct hgrnic_mr        mr;
	char		       irq_name[IB_DEVICE_NAME_MAX];
};

struct hgrnic_ah {
    struct ib_ah ibah;
    u32          port; /* used port */
    u64          slid;
    u64          dlid;
    u32          pd  ; /* PD for the UD */
};

/*
 * Quick description of our CQ/QP locking scheme:
 *
 * We have one global lock that protects dev->cq/qp_table.  Each
 * struct hgrnic_cq/qp also has its own lock.  An individual qp lock
 * may be taken inside of an individual cq lock.  Both cqs attached to
 * a qp may be locked, with the cq with the lower cqn locked first.
 * No other nesting should be done.
 *
 * Each struct hgrnic_cq/qp also has an ref count, protected by the
 * corresponding table lock.  The pointer from the cq/qp_table to the
 * struct counts as one reference.  This reference also is good for
 * access through the consumer API, so modifying the CQ/QP etc doesn't
 * need to take another reference.  Access to a QP because of a
 * completion being polled does not need a reference either.
 *
 * Finally, each struct hgrnic_cq/qp has a wait_queue_head_t for the
 * destroy function to sleep on.
 *
 * This means that access from the consumer API requires nothing but
 * taking the struct's lock.
 *
 * Access because of a completion event should go as follows:
 * - lock cq/qp_table and look up struct
 * - increment ref count in struct
 * - drop cq/qp_table lock
 * - lock struct, do your thing, and unlock struct
 * - decrement ref count; if zero, wake up waiters
 *
 * To destroy a CQ/QP, we can do the following:
 * - lock cq/qp_table
 * - remove pointer and decrement ref count
 * - unlock cq/qp_table lock
 * - wait_event until ref count is zero
 *
 * It is the consumer's responsibilty to make sure that no QP
 * operations (WQE posting or state modification) are pending when a
 * QP is destroyed.  Also, the consumer must make sure that calls to
 * qp_modify are serialized.  Similarly, the consumer is responsible
 * for ensuring that no CQ resize operations are pending when a CQ
 * is destroyed.
 *
 * Possible optimizations (wait for profile data to see if/where we
 * have locks bouncing between CPUs):
 * - split cq/qp table lock into n separate (cache-aligned) locks,
 *   indexed (say) by the page in the table
 * - split QP struct lock into three (one for common info, one for the
 *   send queue and one for the receive queue)
 */

struct hgrnic_cq_buf {
    union hgrnic_buf     queue;
    struct hgrnic_mr     mr;
    int                 is_direct;
};

struct hgrnic_cq_resize {
    struct hgrnic_cq_buf	buf;
    int                 cqe; /* we actual allocate one 
                              * element larger than cqe */
    enum {
        CQ_RESIZE_ALLOC,
        CQ_RESIZE_READY,
        CQ_RESIZE_SWAPPED
    } state;
};

struct hgrnic_cq {
    struct ib_cq ibcq; /* we actual allocate one 
                        * element larger than ibcq->cqe */
    spinlock_t   lock;
    int          refcount;
    int          cqn;
    u32          cons_index; /* consumer index, point 
                              * to first unpolled cqe.
                              * Never rotate back. */
    struct hgrnic_cq_buf     buf;
    struct hgrnic_cq_resize *resize_buf;
    int                     is_kernel;

    wait_queue_head_t wait;
    struct mutex      mutex;
};

struct hgrnic_wq {
    spinlock_t lock     ;
    int        max      ; // Supported maximum number of WQEs in WQ
    unsigned   next_ind ;
    unsigned   last_cpl ;
    unsigned   head     ; /* never roll back, so it need to & (max - 1) when in use */
    unsigned   tail     ; /* never roll back, so it need to & (max - 1) when in use */
    void      *last     ;
    int        max_gs   ;
    int        entry_sz_log ;
    int        que_size ; /* que_size = max << entry_sz_log */

    struct hgrnic_mr       mr   ; /* memory region allocated for work queue */
    u64                   *wr_id; /* work request id, used when pooling cqc */
    union hgrnic_buf       queue; /* work queue */
};

struct hgrnic_qp {
    struct ib_qp           ibqp;
    int                    refcount;
    u32                    qpn;
    int                    is_direct;
    u8                     port; /* for SQP and memfree use only */
    u8                     transport; /* transport type: RC, UD, UC */
    u8                     state;
    u8                     atomic_rd_en;
    u8                     resp_depth;

    struct hgrnic_wq        rq;
    struct hgrnic_wq        sq;
    int                    max_inline_data;

    u8                      mtu_msgmax;
    u32                     remote_qpn;
    u8                      min_rnr_timer;
    u32                     port_num;
    u32                     psn;
    u32                     epsn;

    wait_queue_head_t      wait;
    struct mutex	       mutex;
};

struct hgrnic_sqp {
	struct hgrnic_qp qp;
	int             pkey_index;
	u32             qkey;
	u32             send_psn;
	struct ib_ud_header ud_header;
	int             header_buf_size;
	void           *header_buf;
	dma_addr_t      header_dma;
};

static inline u32 convert_access(int acc)
{
    return (acc & IB_ACCESS_REMOTE_ATOMIC ? HGRNIC_MPT_FLAG_ATOMIC       : 0) |
           (acc & IB_ACCESS_REMOTE_WRITE  ? HGRNIC_MPT_FLAG_REMOTE_WRITE : 0) |
           (acc & IB_ACCESS_REMOTE_READ   ? HGRNIC_MPT_FLAG_REMOTE_READ  : 0) |
           (acc & IB_ACCESS_LOCAL_WRITE   ? HGRNIC_MPT_FLAG_LOCAL_WRITE  : 0) |
           HGRNIC_MPT_FLAG_LOCAL_READ;
}

static inline struct hgrnic_ucontext *to_hgucontext(struct ib_ucontext *ibucontext) {
    return container_of(ibucontext, struct hgrnic_ucontext, ibucontext);
}

static inline struct hgrnic_mr *to_hgmr(struct ib_mr *ibmr) {
    return container_of(ibmr, struct hgrnic_mr, ibmr);
}

static inline struct hgrnic_pd *to_hgpd(struct ib_pd *ibpd) {
    return container_of(ibpd, struct hgrnic_pd, ibpd);
}

static inline struct hgrnic_ah *to_hgah(struct ib_ah *ibah)
{
    return container_of(ibah, struct hgrnic_ah, ibah);
}

static inline struct hgrnic_cq *to_hgcq(struct ib_cq *ibcq) {
    return container_of(ibcq, struct hgrnic_cq, ibcq);
}

static inline struct hgrnic_qp *to_hgqp(struct ib_qp *ibqp) {
    return container_of(ibqp, struct hgrnic_qp, ibqp);
}


#endif /* HGRNIC_PROVIDER_H */
