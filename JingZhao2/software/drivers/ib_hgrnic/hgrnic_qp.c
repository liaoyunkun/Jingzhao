/**************************************************************
 * @author Kang Ning<kangning18z@ict.ac.cn>, NCIC, ICT, CAS
 * @date 2021.08.30
 * @file hgrnic_qp.c
 * @note Functions related to QPs.
 *************************************************************/

#include <linux/string.h>
#include <linux/slab.h>
#include <linux/sched.h>

#include <asm/io.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_cache.h>
#include <rdma/ib_pack.h>
#include <rdma/uverbs_ioctl.h>

#include "hgrnic_dev.h"
#include "hgrnic_cmd.h"
#include "hgrnic_wqe.h"


enum {
    HGRNIC_QP_STATE_RST  = 0,
    HGRNIC_QP_STATE_INIT = 1,
    HGRNIC_QP_STATE_RTR  = 2,
    HGRNIC_QP_STATE_RTS  = 3,
    HGRNIC_QP_STATE_SQE  = 4,
    HGRNIC_QP_STATE_SQD  = 5,
    HGRNIC_QP_STATE_ERR  = 6,
    HGRNIC_QP_STATE_DRAINING = 7
};

enum {
    HGRNIC_QP_ST_RC 	= 0x0,
    HGRNIC_QP_ST_UC 	= 0x1,
    HGRNIC_QP_ST_RD 	= 0x2,
    HGRNIC_QP_ST_UD 	= 0x3,
    HGRNIC_QP_ST_RSV = 0x7
};

enum {
    HGRNIC_SEND_DOORBELL_FENCE = 1 << 5
};

enum {
      HGRNIC_RATE_FULL    = 0,
      HGRNIC_RATE_QUARTER = 1,
      HGRNIC_RATE_EIGHTH  = 2,
      HGRNIC_RATE_HALF    = 3
};

struct hgrnic_qp_path {
    __be32 port_pkey  ; /* port number is larger than 0 */
    u8     rnr_retry  ;
    u8     g_mylmc    ; /* not used
                         * [7:7],  [6:0] 
                         * has grh mylmc */
    __be16 rlid       ; /* not used */
    u8     ackto      ; /* ack timeout, not used */
    u8     mgid_index ; /* not used */
    u8     static_rate; /* maximum bandwidth, not used */
    u8     hop_limit  ; /* not used */
    __be32 sl_tclass_flowlabel; /* not used */
    u8     rgid[16]   ; /* not used */
} __packed;

struct hgrnic_qp_context {
    __be32 flags;
    u8     mtu_msgmax;
    u8     rq_entry_sz_log; /* size of one entry in RQ in log format */
    u8     sq_entry_sz_log; /* size of one entry in SQ in log format */
    u8     rlkey_sched_queue; /* not used */
    __be32 usr_page; /* not used */
    __be32 local_qpn;
    __be32 remote_qpn;
    struct hgrnic_qp_path pri_path;
    __be32 reserved1[8];
    __be32 pd;
    __be32 wqe_base; /* not used */
    __be32 wqe_lkey; /* not used */
    __be32 reserved2;
    __be32 next_send_psn;
    __be32 cqn_snd;
    __be32 snd_wqe_base_l;  /* Send Queue lkey */
    __be32 snd_wqe_len;     /* length of Send Queue */
    __be32 last_acked_psn;
    __be32 ssn;             /* not used */
    __be32 rnr_nextrecvpsn;
    __be32 ra_buff_indx;    /* not used */
    __be32 cqn_rcv;
    __be32 rcv_wqe_base_l;  /* Recv Queue lkey */
    __be32 rcv_wqe_len;     /* length of Send Queue */
    __be32 qkey;            /* not used */
    __be32 rmsn;            /* not used */
    __be16 rq_wqe_counter; /* not used */
    __be16 sq_wqe_counter; /* not used */
} __packed;

struct hgrnic_qp_param {
    __be32 opt_param_mask;
    u32    reserved1;
    struct hgrnic_qp_context context;
} __packed;

enum {
    HGRNIC_QP_OPTPAR_ALT_ADDR_PATH     = 1 << 0,
    HGRNIC_QP_OPTPAR_RRE               = 1 << 1,
    HGRNIC_QP_OPTPAR_RAE               = 1 << 2,
    HGRNIC_QP_OPTPAR_RWE               = 1 << 3,
    HGRNIC_QP_OPTPAR_PKEY_INDEX        = 1 << 4,
    HGRNIC_QP_OPTPAR_Q_KEY             = 1 << 5,
    HGRNIC_QP_OPTPAR_RNR_TIMEOUT       = 1 << 6,
    HGRNIC_QP_OPTPAR_PRIMARY_ADDR_PATH = 1 << 7,
    HGRNIC_QP_OPTPAR_SRA_MAX           = 1 << 8,
    HGRNIC_QP_OPTPAR_RRA_MAX           = 1 << 9,
    HGRNIC_QP_OPTPAR_PM_STATE          = 1 << 10,
    HGRNIC_QP_OPTPAR_PORT_NUM          = 1 << 11,
    HGRNIC_QP_OPTPAR_RETRY_COUNT       = 1 << 12,
    HGRNIC_QP_OPTPAR_ALT_RNR_RETRY     = 1 << 13,
    HGRNIC_QP_OPTPAR_ACK_TIMEOUT       = 1 << 14,
    HGRNIC_QP_OPTPAR_RNR_RETRY         = 1 << 15,
    HGRNIC_QP_OPTPAR_SCHED_QUEUE       = 1 << 16
};

static const u8 hgrnic_opcode[] = {
    [IB_WR_SEND]                 = HGRNIC_OPCODE_SEND,
    [IB_WR_SEND_WITH_IMM]        = HGRNIC_OPCODE_SEND_IMM,
    [IB_WR_RDMA_WRITE]           = HGRNIC_OPCODE_RDMA_WRITE,
    [IB_WR_RDMA_WRITE_WITH_IMM]  = HGRNIC_OPCODE_RDMA_WRITE_IMM,
    [IB_WR_RDMA_READ]            = HGRNIC_OPCODE_RDMA_READ,
    [IB_WR_ATOMIC_CMP_AND_SWP]   = HGRNIC_OPCODE_ATOMIC_CS,
    [IB_WR_ATOMIC_FETCH_AND_ADD] = HGRNIC_OPCODE_ATOMIC_FA,
};

static void *get_wqe (struct hgrnic_wq wq, int n) {
    return wq.queue.page_list[(n << wq.entry_sz_log) >> PAGE_SHIFT].buf +
        ((n << wq.entry_sz_log) & (PAGE_SIZE - 1));
}

static void hgrnic_wq_reset(struct hgrnic_wq *wq)
{
    wq->next_ind  = 0;
    wq->last_cpl = wq->max - 1;
    wq->head      = 0;
    wq->tail      = 0;
}

static int to_hgrnic_state(enum ib_qp_state ib_state)
{
    switch (ib_state) {
    case IB_QPS_RESET: return HGRNIC_QP_STATE_RST;
    case IB_QPS_INIT:  return HGRNIC_QP_STATE_INIT;
    case IB_QPS_RTR:   return HGRNIC_QP_STATE_RTR;
    case IB_QPS_RTS:   return HGRNIC_QP_STATE_RTS;
    case IB_QPS_SQD:   return HGRNIC_QP_STATE_SQD;
    case IB_QPS_SQE:   return HGRNIC_QP_STATE_SQE;
    case IB_QPS_ERR:   return HGRNIC_QP_STATE_ERR;
    default:           return -1;
    }
}

enum { RC, UC, UD, RD, RSV, NUM_TRANS };

static int to_hgrnic_st(int transport)
{
    switch (transport) {
    case RC:  return HGRNIC_QP_ST_RC;
    case UC:  return HGRNIC_QP_ST_UC;
    case UD:  return HGRNIC_QP_ST_UD;
    case RD:  return HGRNIC_QP_ST_RD;
    case RSV: return HGRNIC_QP_ST_RSV;
    default:  return -1;
    }
}



static inline enum ib_qp_state to_ib_qp_state(int hgrnic_state)
{
    switch (hgrnic_state) {
    case HGRNIC_QP_STATE_RST:      return IB_QPS_RESET;
    case HGRNIC_QP_STATE_INIT:     return IB_QPS_INIT;
    case HGRNIC_QP_STATE_RTR:      return IB_QPS_RTR;
    case HGRNIC_QP_STATE_RTS:      return IB_QPS_RTS;
    case HGRNIC_QP_STATE_DRAINING:
    case HGRNIC_QP_STATE_SQD:      return IB_QPS_SQD;
    case HGRNIC_QP_STATE_SQE:      return IB_QPS_SQE;
    case HGRNIC_QP_STATE_ERR:      return IB_QPS_ERR;
    default:                      return -1;
    }
}

static inline enum ib_mig_state to_ib_mig_state(int hgrnic_mig_state)
{
	switch (hgrnic_mig_state) {
	case 0:  return IB_MIG_ARMED;
	case 1:  return IB_MIG_REARM;
	case 3:  return IB_MIG_MIGRATED;
	default: return -1;
	}
}

static enum ib_rate memfree_rate_to_ib(u8 hgrnic_rate, u8 port_rate)
{
	switch (hgrnic_rate) {
	case HGRNIC_RATE_EIGHTH:
		return mult_to_ib_rate(port_rate >> 3);
	case HGRNIC_RATE_QUARTER:
		return mult_to_ib_rate(port_rate >> 2);
	case HGRNIC_RATE_HALF:
		return mult_to_ib_rate(port_rate >> 1);
	case HGRNIC_RATE_FULL:
	default:
		return mult_to_ib_rate(port_rate);
	}
}

enum ib_rate hgrnic_rate_to_ib(struct hgrnic_dev *dev, u8 hgrnic_rate, u8 port)
{
    /* Handle old Arbel FW */
    if (dev->limits.stat_rate_support == 0x3 && hgrnic_rate)
        return IB_RATE_2_5_GBPS;

    return memfree_rate_to_ib(hgrnic_rate, dev->rate[port - 1]);
}

static void to_rdma_ah_attr(struct hgrnic_dev *dev,
                            struct rdma_ah_attr *ah_attr,
                            struct hgrnic_qp_path *path)
{
    u8 port_num = (be32_to_cpu(path->port_pkey) >> 24) & 0x3;

    memset(ah_attr, 0, sizeof(*ah_attr));

    if (port_num == 0 || port_num > dev->limits.num_ports)
        return;
    ah_attr->type = rdma_ah_find_type(&dev->ib_dev, port_num);
    rdma_ah_set_port_num(ah_attr, port_num);

    rdma_ah_set_dlid(ah_attr, be16_to_cpu(path->rlid));
    rdma_ah_set_sl(ah_attr, be32_to_cpu(path->sl_tclass_flowlabel) >> 28);
    rdma_ah_set_path_bits(ah_attr, path->g_mylmc & 0x7f);
    rdma_ah_set_static_rate(ah_attr,
                hgrnic_rate_to_ib(dev,
                            path->static_rate & 0xf,
                            port_num));
    if (path->g_mylmc & (1 << 7)) {
        u32 tc_fl = be32_to_cpu(path->sl_tclass_flowlabel);

        rdma_ah_set_grh(ah_attr, NULL,
                tc_fl & 0xfffff,
                path->mgid_index &
                (dev->limits.gid_table_len - 1),
                path->hop_limit,
                (tc_fl >> 20) & 0xff);
        rdma_ah_set_dgid_raw(ah_attr, path->rgid);
    }
}

int hgrnic_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr, int qp_attr_mask,
                   struct ib_qp_init_attr *qp_init_attr)
{
    struct hgrnic_dev *dev = to_hgdev(ibqp->device);
    struct hgrnic_qp *qp = to_hgqp(ibqp);
    int err = 0;
    struct hgrnic_mailbox *mailbox = NULL;
    struct hgrnic_qp_param *qp_param;
    struct hgrnic_qp_context *context;
    int hgrnic_state;

    mutex_lock(&qp->mutex);

    if (qp->state == IB_QPS_RESET) {
        qp_attr->qp_state = IB_QPS_RESET;
        goto done;
    }

    mailbox = hgrnic_alloc_mailbox(dev, GFP_KERNEL);
    if (IS_ERR(mailbox)) {
        err = PTR_ERR(mailbox);
        goto out;
    }

    err = hgrnic_QUERY_QP(dev, qp->qpn, mailbox);
    if (err) {
        hgrnic_warn(dev, "QUERY_QP failed (%d)\n", err);
        goto out_mailbox;
    }

    qp_param    = mailbox->buf;
    context     = &qp_param->context;
    hgrnic_state = be32_to_cpu(context->flags) >> 28;

    qp->state            = to_ib_qp_state(hgrnic_state);
    qp_attr->qp_state    = qp->state;
    qp_attr->path_mtu    = context->mtu_msgmax >> 5;
    qp_attr->qkey        = be32_to_cpu(context->qkey);
    qp_attr->rq_psn      = be32_to_cpu(context->rnr_nextrecvpsn) & 0xffffff;
    qp_attr->sq_psn      = be32_to_cpu(context->next_send_psn) & 0xffffff;
    qp_attr->dest_qp_num = be32_to_cpu(context->remote_qpn) & 0xffffff;

    qp_attr->path_mig_state  = 0; /* reserved */


    if (qp->transport == RC || qp->transport == UC) {
        to_rdma_ah_attr(dev, &qp_attr->ah_attr, &context->pri_path);
    }

    qp_attr->pkey_index = be32_to_cpu(context->pri_path.port_pkey) & 0x7f;
    qp_attr->port_num   =
        (be32_to_cpu(context->pri_path.port_pkey) >> 24) & 0x3;

    /* qp_attr->en_sqd_async_notify is only applicable in modify qp */
    qp_attr->sq_draining = hgrnic_state == HGRNIC_QP_STATE_DRAINING;

    qp_attr->min_rnr_timer      =
        (be32_to_cpu(context->rnr_nextrecvpsn) >> 24) & 0x1f;
    qp_attr->timeout        = context->pri_path.ackto >> 3;
    qp_attr->rnr_retry      = context->pri_path.rnr_retry >> 5;

done:
    qp_attr->cur_qp_state        = qp_attr->qp_state;
    qp_attr->cap.max_send_wr     = qp->sq.max;
    qp_attr->cap.max_recv_wr     = qp->rq.max;
    qp_attr->cap.max_send_sge    = qp->sq.max_gs;
    qp_attr->cap.max_recv_sge    = qp->rq.max_gs;
    qp_attr->cap.max_inline_data = qp->max_inline_data;

    qp_init_attr->cap        = qp_attr->cap;

out_mailbox:
    hgrnic_free_mailbox(dev, mailbox);

out:
    mutex_unlock(&qp->mutex);
    return err;
}

static int __hgrnic_modify_qp (struct ib_qp *ibqp,
        const struct ib_qp_attr *attr, int attr_mask,
        enum ib_qp_state cur_state, enum ib_qp_state new_state,
        struct ib_udata *udata) {
            
    struct hgrnic_dev *dev = to_hgdev(ibqp->device);
    struct hgrnic_qp *qp = to_hgqp(ibqp);
    struct hgrnic_ucontext *context = rdma_udata_to_drv_context(
                    udata, struct hgrnic_ucontext, ibucontext);
    struct hgrnic_mailbox *mailbox;
    struct hgrnic_qp_param *qp_param;
    struct hgrnic_qp_context *qp_context;
    int err = -EINVAL;

    printk(KERN_INFO PFX "\n\n__hgrnic_modify_qp (QP 0x%x): cur_state %d -> new_state %d.\n", 
            qp->qpn, cur_state, new_state);
    
    mailbox = hgrnic_alloc_mailbox(dev, GFP_KERNEL);
    if (IS_ERR(mailbox))
        return PTR_ERR(mailbox);

    qp_param = mailbox->buf;
    qp_context = &qp_param->context;
    memset(qp_param, 0, sizeof *qp_param);

    qp_context->flags = cpu_to_be32((to_hgrnic_state(new_state) << 28) |
                                    (to_hgrnic_st(qp->transport) << 16));

    if (qp->transport == UD) {
        qp->mtu_msgmax = (IB_MTU_4096 << 5) | 11;
    } else if (attr_mask & IB_QP_PATH_MTU) {
        if (attr->path_mtu < IB_MTU_256 || attr->path_mtu > IB_MTU_4096) {
            hgrnic_dbg(dev, "path MTU (%u) is invalid\n",
                    attr->path_mtu);
            goto out_mailbox;
        }
        qp->mtu_msgmax = (attr->path_mtu << 5) | 31;
    }
    qp_context->mtu_msgmax = qp->mtu_msgmax;

    qp_context->rq_entry_sz_log = qp->rq.entry_sz_log;
    qp_context->sq_entry_sz_log = qp->sq.entry_sz_log;

    if (qp->ibqp.uobject)
        qp_context->usr_page = cpu_to_be32(context->uar.index);
    else
        qp_context->usr_page = cpu_to_be32(dev->driver_uar.index);
    

    /* local_qp and remote_qp should be masked with num_qps, 
     * cause RNIC hardware only supports `num_qps` QP.
     */
    qp_context->local_qpn  = cpu_to_be32(qp->qpn & (dev->limits.num_qps - 1));

    if (attr_mask & IB_QP_DEST_QPN)
        qp->remote_qpn = attr->dest_qp_num;
    qp_context->remote_qpn = cpu_to_be32(qp->remote_qpn & (dev->limits.num_qps - 1));

    if (attr_mask & IB_QP_PORT) {
        /* Port num starts from 1 */
        qp->port_num = attr->port_num << 24;
        qp_param->opt_param_mask |= cpu_to_be32(HGRNIC_QP_OPTPAR_PORT_NUM);
    }
    qp_context->pri_path.port_pkey = cpu_to_be32(qp->port_num);
    
    // qp_context->pri_path.port_pkey |= cpu_to_be32(attr->pkey_index);
    // qp_param->opt_param_mask |= cpu_to_be32(HGRNIC_QP_OPTPAR_PKEY_INDEX);

    // qp_context->pri_path.rnr_retry = cpu_to_be32(attr->rnr_retry << 5);
    // qp_param->opt_param_mask |= cpu_to_be32(HGRNIC_QP_OPTPAR_RNR_RETRY);

    qp_context->pd         = cpu_to_be32(to_hgpd(ibqp->pd)->pd_num);

    qp_context->wqe_base   = 0;
    qp_context->wqe_lkey   = cpu_to_be32(qp->sq.mr.ibmr.lkey); /* !FIXME: This is a bug on hardware, 
                                                                  and we may fix it laer */

    if (attr_mask & IB_QP_SQ_PSN)
        qp->psn = attr->sq_psn;
    qp_context->next_send_psn = cpu_to_be32(qp->psn);
//Modified by YangFan
    qp_context->last_acked_psn = cpu_to_be32(qp->psn);

    qp_context->cqn_snd = cpu_to_be32(to_hgcq(ibqp->send_cq)->cqn);
    qp_context->snd_wqe_base_l = cpu_to_be32(qp->sq.mr.ibmr.lkey);
    qp_context->snd_wqe_len   = cpu_to_be32(qp->sq.mr.ibmr.length);

    if (attr_mask & IB_QP_MIN_RNR_TIMER) {
        qp->min_rnr_timer = attr->min_rnr_timer << 24;
        qp_param->opt_param_mask |= cpu_to_be32(HGRNIC_QP_OPTPAR_RNR_TIMEOUT);
    }
    qp_context->rnr_nextrecvpsn |= cpu_to_be32(qp->min_rnr_timer);

    if (attr_mask & IB_QP_RQ_PSN)
        qp->epsn = attr->rq_psn;
    qp_context->rnr_nextrecvpsn |= cpu_to_be32(qp->epsn);
    
    qp_context->cqn_rcv = cpu_to_be32(to_hgcq(ibqp->recv_cq)->cqn);
    qp_context->rcv_wqe_base_l = cpu_to_be32(qp->rq.mr.ibmr.lkey);
    qp_context->rcv_wqe_len   = cpu_to_be32(qp->rq.mr.ibmr.length);

    err = hgrnic_MODIFY_QP(dev, cur_state, new_state, qp->qpn, mailbox);
    if (err) {
        hgrnic_warn(dev, "modify QP %d->%d returned %d.\n",
                   cur_state, new_state, err);
        goto out_mailbox;
    }
    printk(KERN_INFO PFX "----------------------------------");
    hgrnic_dump("0x%x", be32_to_cpu(qp_param->opt_param_mask));
    hgrnic_dump("0x%x", be32_to_cpu(qp_context->flags       ));
    hgrnic_dump("0x%x", qp_context->mtu_msgmax);
    hgrnic_dump("0x%x", qp_context->rq_entry_sz_log);
    hgrnic_dump("0x%x", qp_context->sq_entry_sz_log);
    hgrnic_dump("0x%x", qp_context->rlkey_sched_queue);
    hgrnic_dump("0x%x", be32_to_cpu(qp_context->usr_page          ));
    hgrnic_dump("0x%x", be32_to_cpu(qp_context->local_qpn         ));
    hgrnic_dump("0x%x", be32_to_cpu(qp_context->remote_qpn        ));
    hgrnic_dump("0x%x", be32_to_cpu(qp_context->pri_path.port_pkey));
    hgrnic_dump("0x%x", qp_context->pri_path.rnr_retry);
    hgrnic_dump("0x%x", be32_to_cpu(qp_context->pd             ));
    hgrnic_dump("0x%x", be32_to_cpu(qp_context->wqe_base       ));
    hgrnic_dump("0x%x", be32_to_cpu(qp_context->wqe_lkey       ));
    hgrnic_dump("0x%x", be32_to_cpu(qp_context->next_send_psn  ));
    hgrnic_dump("0x%x", be32_to_cpu(qp_context->cqn_snd        ));
    hgrnic_dump("0x%x", be32_to_cpu(qp_context->snd_wqe_base_l ));
    hgrnic_dump("0x%x", be32_to_cpu(qp_context->snd_wqe_len    ));
    hgrnic_dump("0x%x", be32_to_cpu(qp_context->last_acked_psn ));
    hgrnic_dump("0x%x", be32_to_cpu(qp_context->ssn            ));
    hgrnic_dump("0x%x", be32_to_cpu(qp_context->rnr_nextrecvpsn));
    hgrnic_dump("0x%x", be32_to_cpu(qp_context->ra_buff_indx   ));
    hgrnic_dump("0x%x", be32_to_cpu(qp_context->cqn_rcv        ));
    hgrnic_dump("0x%x", be32_to_cpu(qp_context->rcv_wqe_base_l ));
    hgrnic_dump("0x%x", be32_to_cpu(qp_context->rcv_wqe_len    ));
    hgrnic_dump("0x%x", be32_to_cpu(qp_context->qkey           ));
    hgrnic_dump("0x%x", be32_to_cpu(qp_context->rmsn           ));
    hgrnic_dump("0x%x", be32_to_cpu(qp_context->rq_wqe_counter ));
    hgrnic_dump("0x%x", be32_to_cpu(qp_context->sq_wqe_counter ));
    printk(KERN_INFO PFX "----------------------------------\n\n");

    qp->state = new_state;
    if (attr_mask & IB_QP_PORT)
        qp->port = attr->port_num;

    /*
     * If we moved a kernel QP to RESET, clean up all old CQ
     * entries and reinitialize the QP.
     */
    if (new_state == IB_QPS_RESET && !qp->ibqp.uobject) {
        hgrnic_cq_clean(dev, to_hgcq(qp->ibqp.recv_cq), qp->qpn);
        if (qp->ibqp.send_cq != qp->ibqp.recv_cq)
            hgrnic_cq_clean(dev, to_hgcq(qp->ibqp.send_cq), qp->qpn);

        hgrnic_wq_reset(&qp->sq);
        qp->sq.last = get_wqe(qp->sq, qp->sq.max - 1);

        hgrnic_wq_reset(&qp->rq);
        qp->rq.last = get_wqe(qp->rq, qp->rq.max - 1);
    }

out_mailbox:
    hgrnic_free_mailbox(dev, mailbox);
    return err;
}

int hgrnic_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr, int attr_mask,
                    struct ib_udata *udata)
{
    struct hgrnic_dev *dev = to_hgdev(ibqp->device);
    struct hgrnic_qp *qp = to_hgqp(ibqp);
    enum ib_qp_state cur_state, new_state;
    int err = -EINVAL;

    mutex_lock(&qp->mutex);
    if (attr_mask & IB_QP_CUR_STATE)
        cur_state = attr->cur_qp_state;
    else {
        spin_lock_irq(&qp->sq.lock);
        spin_lock(&qp->rq.lock);
        cur_state = qp->state;
        spin_unlock(&qp->rq.lock);
        spin_unlock_irq(&qp->sq.lock);
    }

    new_state = attr_mask & IB_QP_STATE ? attr->qp_state : cur_state;
    
    /* Check if QP state transformation is OK 
        !TODO: May be useful later */
    printk(KERN_INFO PFX "Start ib_modify_qp_is_ok.\n");
    if (!ib_modify_qp_is_ok(cur_state, new_state, ibqp->qp_type,
    			attr_mask)) {
    	hgrnic_dbg(dev, "Bad QP transition (transport %d) "
    		  "%d->%d with attr 0x%08x\n",
    		  qp->transport, cur_state, new_state,
    		  attr_mask);
    	goto out;
    }

    if ((attr_mask & IB_QP_PORT) &&
        (attr->port_num == 0 || attr->port_num > dev->limits.num_ports)) {
        hgrnic_dbg(dev, "Port number (%u) is invalid\n", attr->port_num);
        goto out;
    }

    if (cur_state == new_state && cur_state == IB_QPS_RESET) {
        err = 0;
        goto out;
    }

    err = __hgrnic_modify_qp(ibqp, attr, attr_mask, cur_state, new_state,
                            udata);

out:
    mutex_unlock(&qp->mutex);
    return err;
}

/**
 * @note calculate maximum data size according to the 
 * maximum descriptor size.
 */
static int hgrnic_max_data_size (struct hgrnic_dev *dev, 
        struct hgrnic_qp *qp, int desc_sz)
{
    /*
     * Calculate the maximum size of WQE s/g segments, excluding
     * the next segment and other non-data segments.
     */
    int max_data_size = desc_sz - sizeof (struct hgrnic_next_unit);

    switch (qp->transport) {
    case UD:
        max_data_size -= sizeof (struct hgrnic_ud_unit);
        break;

    default:
        max_data_size -= sizeof (struct hgrnic_raddr_unit);
        break;
    }

    return max_data_size;
}

/*
 * Allocate and register buffer for WQEs.  qp->rq.max, sq.max,
 * rq.max_gs and sq.max_gs must all be assigned.
 * hgrnic_alloc_wqe_buf will calculate is_direct, and
 * queue
 */
static int hgrnic_alloc_wqe_buf(struct hgrnic_dev *dev,
                               struct hgrnic_pd *pd,
                               int *is_direct,
                               struct hgrnic_wq *wq,
                               struct ib_udata *udata)
{
    int err;

    wq->wr_id = kmalloc_array(wq->max, sizeof(u64), GFP_KERNEL);
    if (!wq->wr_id)
        return -ENOMEM;

    printk(KERN_INFO PFX "Start hgrnic_alloc_wqe_buf: hgrnic_buf_alloc");
    err = hgrnic_buf_alloc(dev, PAGE_ALIGN(wq->que_size),
                          &wq->queue, is_direct, pd, 0, &wq->mr);
    if (err) {
        kfree(wq->wr_id);
        return err;
    }

    return 0;
}

static void hgrnic_free_wqe_buf(struct hgrnic_dev *dev,
                               int is_direct,
                               struct hgrnic_wq *wq)
{
    hgrnic_buf_free(dev, PAGE_ALIGN(wq->que_size),
                   &wq->queue, is_direct, &wq->mr);
    kfree(wq->wr_id);
}


static int hgrnic_alloc_qp_buf (struct hgrnic_dev *dev,
                                  struct hgrnic_pd *pd,
                                  struct hgrnic_cq *send_cq,
                                  struct hgrnic_cq *recv_cq,
                                  struct hgrnic_qp *qp,
                                  struct ib_udata *udata)
{
    int ret;
    int i;
    struct hgrnic_next_unit *next;
    struct hgrnic_data_unit *scatter;
    int size;

    qp->refcount = 1;
    init_waitqueue_head(&qp->wait);
    mutex_init(&qp->mutex);
    qp->state    	 = IB_QPS_RESET;
    qp->atomic_rd_en = 0;
    qp->resp_depth   = 0;
    hgrnic_wq_reset(&qp->sq);
    hgrnic_wq_reset(&qp->rq);

    spin_lock_init(&qp->sq.lock);
    spin_lock_init(&qp->rq.lock);

    ret = hgrnic_reg_icm(dev, dev->qp_table.qp_table, 
                        qp->qpn, CXT_REGION);
    if (ret)
        return ret;

    /**
     * If this is a userspace QP, we're done now.  The doorbells
     * will be allocated and buffers will be initialized in
     * userspace.
     */
    if (udata)
        return 0;
    
    printk(KERN_INFO PFX "Start kernel qp processing!\n");

    /* Allocate queue space for SQ. */
    ret = hgrnic_alloc_wqe_buf(dev, pd, &qp->is_direct, &qp->sq, udata);
    if (ret) {
        hgrnic_unreg_icm(dev, dev->qp_table.qp_table, qp->qpn, CXT_REGION);
        return ret;
    }

    /* Allocate queue space for RQ. */
    ret = hgrnic_alloc_wqe_buf(dev, pd, &qp->is_direct, &qp->rq, udata);
    if (ret) {
        hgrnic_free_wqe_buf(dev, qp->is_direct, &qp->sq);
        hgrnic_unreg_icm(dev, dev->qp_table.qp_table, qp->qpn, CXT_REGION);
        return ret;
    }

    size = (sizeof (struct hgrnic_next_unit) +
            qp->rq.max_gs * sizeof (struct hgrnic_data_unit)) / 16;

    for (i = 0; i < qp->rq.max; ++i) {
        next = get_wqe(qp->rq, i);
        next->nda_nop = cpu_to_le32(((i + 1) & (qp->rq.max - 1)) <<
                        qp->rq.entry_sz_log);
        next->ee_nds = cpu_to_le32(size);

        for (scatter = (void *) (next + 1);
             (void *) scatter < (void *) next + (1 << qp->rq.entry_sz_log);
             ++scatter)
            scatter->lkey = cpu_to_le32(HGRNIC_INVAL_LKEY);
    }

    for (i = 0; i < qp->sq.max; ++i) {
        next = get_wqe(qp->sq, i);
        next->nda_nop = cpu_to_be32((((i + 1) & (qp->sq.max - 1)) <<
                        qp->sq.entry_sz_log));
    }

    qp->sq.last = get_wqe(qp->sq, qp->sq.max - 1);
    qp->rq.last = get_wqe(qp->rq, qp->rq.max - 1);

    return 0;
}

static int hgrnic_set_qp_entry_sz (struct hgrnic_dev *dev, struct hgrnic_qp *qp)
{
    int size;

    size = sizeof (struct hgrnic_next_unit) +
        qp->rq.max_gs * sizeof (struct hgrnic_data_unit);

    if (size > dev->limits.max_desc_sz)
        return -EINVAL;

    for (qp->rq.entry_sz_log = 7; 1 << qp->rq.entry_sz_log < size;
         qp->rq.entry_sz_log++);
    qp->rq.que_size = qp->rq.max << qp->rq.entry_sz_log;
    // qp->rq.mr.ibmr.length = qp->rq.que_size;
    // hgrnic_dump("0x%x", qp->rq.mr.ibmr.length);
    // hgrnic_dump("0x%x", qp->rq.que_size);

    size = qp->sq.max_gs * sizeof (struct hgrnic_data_unit);
    switch (qp->transport) {
    case UD:
        size += sizeof (struct hgrnic_ud_unit);
        break;
    case UC:
        size += sizeof (struct hgrnic_raddr_unit);
        break;
    case RC:
        size += sizeof (struct hgrnic_raddr_unit);
        /*
         * An atomic op will require an atomic segment, a
         * remote address segment and one scatter entry.
         */
        size = max_t(int, size,
                     sizeof (struct hgrnic_atomic_unit) +
                     sizeof (struct hgrnic_raddr_unit) +
                     sizeof (struct hgrnic_data_unit));
        break;
    default:
        break;
    }

    size += sizeof (struct hgrnic_next_unit);

    if (size > dev->limits.max_desc_sz)
        return -EINVAL;

    for (qp->sq.entry_sz_log = 7; 1 << qp->sq.entry_sz_log < size;
         qp->sq.entry_sz_log++);
    qp->sq.que_size = qp->sq.max << qp->sq.entry_sz_log;
    // qp->sq.mr.ibmr.length = qp->sq.que_size;
    // hgrnic_dump("0x%x", qp->sq.mr.ibmr.length);
    // hgrnic_dump("0x%x", qp->sq.que_size);

    return 0;
}

static int hgrnic_set_qp_size (struct hgrnic_dev *dev, 
                              struct ib_qp_cap *cap, 
                              struct hgrnic_pd *pd, 
                              struct hgrnic_qp *qp)
{
    int max_data_size = hgrnic_max_data_size(dev, qp, dev->limits.max_desc_sz);

    hgrnic_dump("%d", cap->max_send_wr);
    hgrnic_dump("%d", cap->max_recv_wr);
    hgrnic_dump("%d", cap->max_send_sge);
    hgrnic_dump("%d", cap->max_recv_sge);
    hgrnic_dump("%d", cap->max_inline_data);
    hgrnic_dump("%d", dev->limits.max_wqes);
    hgrnic_dump("%d", dev->limits.max_sg);
    hgrnic_dump("%d", dev->limits.max_desc_sz);
    hgrnic_dump("%d", max_data_size);

    /* Sanity check QP size before proceeding */
    if (cap->max_send_wr     > dev->limits.max_wqes ||
        cap->max_recv_wr     > dev->limits.max_wqes ||
        cap->max_send_sge    > dev->limits.max_sg   ||
        cap->max_recv_sge    > dev->limits.max_sg   ||
        cap->max_inline_data > max_data_size - sizeof(struct hgrnic_inline_unit)) {

        return -EINVAL;
    }

    qp->rq.max = cap->max_recv_wr;
    qp->sq.max = cap->max_send_wr;
    qp->rq.max_gs = cap->max_recv_sge;
    qp->sq.max_gs = cap->max_send_sge;
    qp->max_inline_data = cap->max_inline_data;

    return hgrnic_set_qp_entry_sz(dev, qp);
}

int hgrnic_alloc_qp (struct hgrnic_dev *dev,
                    struct hgrnic_pd *pd, struct hgrnic_cq *send_cq,
                    struct hgrnic_cq *recv_cq, enum ib_qp_type type,
                    struct ib_qp_cap *cap, struct hgrnic_qp *qp, 
                    struct ib_udata *udata)
{
    int err;

    switch (type) {
    case IB_QPT_RC: qp->transport = RC; break;
    case IB_QPT_UC: qp->transport = UC; break;
    case IB_QPT_UD: qp->transport = UD; break;
    default: return -EINVAL;
    }

    err = hgrnic_set_qp_size(dev, cap, pd, qp);
    if (err)
        return err;

    qp->qpn = hgrnic_alloc(&dev->qp_table.alloc);
    if (qp->qpn == -1)
        return -ENOMEM;

    qp->port = 0; /* not specifying any port */

    err = hgrnic_alloc_qp_buf(dev, pd, send_cq, recv_cq, qp, udata);
    if (err) {
        hgrnic_free(&dev->qp_table.alloc, qp->qpn);
        return err;
    }

    spin_lock_irq(&dev->qp_table.lock);
    hgrnic_array_set(&dev->qp_table.qp,
                    qp->qpn & (dev->limits.num_qps - 1), qp);
    spin_unlock_irq(&dev->qp_table.lock);

    return 0;
}

static void hgrnic_lock_cqs(struct hgrnic_cq *send_cq, struct hgrnic_cq *recv_cq)
	__acquires(&send_cq->lock) __acquires(&recv_cq->lock)
{
	if (send_cq == recv_cq) {
		spin_lock_irq(&send_cq->lock);
		__acquire(&recv_cq->lock);
	} else if (send_cq->cqn < recv_cq->cqn) {
		spin_lock_irq(&send_cq->lock);
		spin_lock_nested(&recv_cq->lock, SINGLE_DEPTH_NESTING);
	} else {
		spin_lock_irq(&recv_cq->lock);
		spin_lock_nested(&send_cq->lock, SINGLE_DEPTH_NESTING);
	}
}

static void hgrnic_unlock_cqs(struct hgrnic_cq *send_cq, struct hgrnic_cq *recv_cq)
	__releases(&send_cq->lock) __releases(&recv_cq->lock)
{
	if (send_cq == recv_cq) {
		__release(&recv_cq->lock);
		spin_unlock_irq(&send_cq->lock);
	} else if (send_cq->cqn < recv_cq->cqn) {
		spin_unlock(&recv_cq->lock);
		spin_unlock_irq(&send_cq->lock);
	} else {
		spin_unlock(&send_cq->lock);
		spin_unlock_irq(&recv_cq->lock);
	}
}



static inline int get_qp_refcount(struct hgrnic_dev *dev, struct hgrnic_qp *qp)
{
	int c;

	spin_lock_irq(&dev->qp_table.lock);
	c = qp->refcount;
	spin_unlock_irq(&dev->qp_table.lock);

	return c;
}

void hgrnic_free_qp(struct hgrnic_dev *dev,
                   struct hgrnic_qp *qp)
{
    struct hgrnic_cq *send_cq;
    struct hgrnic_cq *recv_cq;
    struct hgrnic_mailbox *mailbox;
    struct hgrnic_qp_param *qp_param;

    send_cq = to_hgcq(qp->ibqp.send_cq);
    recv_cq = to_hgcq(qp->ibqp.recv_cq);

    /*
     * Lock CQs here, so that CQ polling code can do QP lookup
     * without taking a lock.
     */
    hgrnic_lock_cqs(send_cq, recv_cq);

    spin_lock(&dev->qp_table.lock);
    hgrnic_array_clear(&dev->qp_table.qp,
                      qp->qpn & (dev->limits.num_qps - 1));
    --qp->refcount;
    spin_unlock(&dev->qp_table.lock);

    hgrnic_unlock_cqs(send_cq, recv_cq);

    wait_event(qp->wait, !get_qp_refcount(dev, qp));

    if (qp->state != IB_QPS_RESET) {
        mailbox = hgrnic_alloc_mailbox(dev, GFP_KERNEL);
        qp_param = mailbox->buf;
        memset(qp_param, 0, sizeof *qp_param);
        hgrnic_MODIFY_QP(dev, qp->state, IB_QPS_RESET, qp->qpn, mailbox);

        hgrnic_free_mailbox(dev, mailbox);
    }
        

    /*
     * If this is a userspace QP, the buffers, MR, CQs and so on
     * will be cleaned up in userspace, so all we have to do is
     * unref the mem-free tables and free the QPN in our table.
     */
    if (!qp->ibqp.uobject) {
        hgrnic_cq_clean(dev, recv_cq, qp->qpn);
        if (send_cq != recv_cq)
            hgrnic_cq_clean(dev, send_cq, qp->qpn);

        hgrnic_free_wqe_buf(dev, qp->is_direct, &qp->sq);
        hgrnic_free_wqe_buf(dev, qp->is_direct, &qp->rq);
    }

    hgrnic_unreg_icm(dev, dev->qp_table.qp_table, qp->qpn, CXT_REGION);

    hgrnic_free(&dev->qp_table.alloc, qp->qpn);
}

static inline int hgrnic_wq_overflow (struct hgrnic_wq *wq, int nreq,
                                     struct ib_cq *ib_cq)
{
    unsigned cur;
    struct hgrnic_cq *cq;

    cur = wq->head - wq->tail;
    if (likely(cur + nreq < wq->max))
        return 0;

    cq = to_hgcq(ib_cq);
    spin_lock(&cq->lock);
    cur = wq->head - wq->tail;
    spin_unlock(&cq->lock);

    return cur + nreq >= wq->max;
}

int hgrnic_post_send (struct ib_qp *ibqp, const struct ib_send_wr *wr, 
                     const struct ib_send_wr **bad_wr) {
    
    struct hgrnic_dev *dev = to_hgdev(ibqp->device);
    struct hgrnic_qp *qp = to_hgqp(ibqp);
    
    void *cur_wqe ; /* point to current processing wqe */
    void *cur_unit; /* point to current prcessing unit in current wqe */
    void *prev_wqe;
    unsigned long flags;
    int err = 0;
    int nreq;
    int i;
    int size;
    /*
     * f0 and size0 are only used if nreq != 0, and they will
     * always be initialized the first time through the main loop
     * before nreq is incremented.  So nreq cannot become non-zero
     * without initializing f0 and size0, and they are in fact
     * never used uninitialized.
     */
    int uninitialized_var(size0);
    u32 uninitialized_var(f0);
    int ind;
    u8 op0 = 0;

    spin_lock_irqsave(&qp->sq.lock, flags);

    ind = qp->sq.head & (qp->sq.max - 1);

    for (nreq = 0; wr; ++nreq, wr = wr->next) {
        if (unlikely(nreq == HGRNIC_MAX_WQES_PER_SEND_DB)) {
            hgrnic_send_dbell(dev, qp, nreq, size0, f0, op0);
            
            nreq = 0;
            qp->sq.head += HGRNIC_MAX_WQES_PER_SEND_DB;
        }

        if (hgrnic_wq_overflow(&qp->sq, nreq, qp->ibqp.send_cq)) {
            hgrnic_err(dev, "SQ %06x full (%u head, %u tail,"
                      " %d max, %d nreq)\n", qp->qpn,
                      qp->sq.head, qp->sq.tail,
                      qp->sq.max, nreq);
            err = -ENOMEM;
            *bad_wr = wr;
            goto out;
        }

        cur_unit = get_wqe(qp->sq, ind);
        cur_wqe = cur_unit;
        prev_wqe = qp->sq.last;
        qp->sq.last = cur_unit;
        printk(KERN_INFO PFX "hgrnic_post_send: wqe addr is 0x%llx\n", (u64)cur_wqe);

        memset(cur_unit, 0, sizeof(struct hgrnic_next_unit));
        if (wr->opcode == IB_WR_SEND_WITH_IMM ||
                wr->opcode == IB_WR_RDMA_WRITE_WITH_IMM)
            ((struct hgrnic_next_unit *) cur_unit)->imm = wr->ex.imm_data;
        else
            ((struct hgrnic_next_unit *) cur_unit)->imm = 0;

        cur_unit += sizeof (struct hgrnic_next_unit);
        size      = sizeof (struct hgrnic_next_unit) / 16;

        switch (qp->transport) {
          case RC:
            switch (wr->opcode) {
              case IB_WR_RDMA_READ:
              case IB_WR_RDMA_WRITE:
              case IB_WR_RDMA_WRITE_WITH_IMM:
                set_raddr_unit(cur_unit, rdma_wr(wr)->remote_addr,
                              rdma_wr(wr)->rkey);
                cur_unit += sizeof (struct hgrnic_raddr_unit);
                size     += sizeof (struct hgrnic_raddr_unit) / 16;
                break;

              default:
                /* No extra segments required for RC sends */
                break;
            }
            break;

          case UC:
            switch (wr->opcode) {
              case IB_WR_RDMA_WRITE:
              case IB_WR_RDMA_WRITE_WITH_IMM:
                set_raddr_unit(cur_unit, rdma_wr(wr)->remote_addr,
                              rdma_wr(wr)->rkey);
                cur_unit += sizeof (struct hgrnic_raddr_unit);
                size     += sizeof (struct hgrnic_raddr_unit) / 16;
                break;

              default:
                /* No extra segments required for UC sends */
                break;
            }
            break;

          case UD:
            set_ud_unit(cur_unit, ud_wr(wr));
            cur_unit += sizeof (struct hgrnic_ud_unit);
            size     += sizeof (struct hgrnic_ud_unit) / 16;
            break;
        }

        if (wr->num_sge > qp->sq.max_gs) {
            hgrnic_err(dev, "too many gathers\n");
            err = -EINVAL;
            *bad_wr = wr;
            goto out;
        }

        for (i = 0; i < wr->num_sge; ++i) {
            hgrnic_set_data_unit(cur_unit, wr->sg_list + i);
            cur_unit += sizeof (struct hgrnic_data_unit);
            size     += sizeof (struct hgrnic_data_unit) / 16;
        }

        qp->sq.wr_id[ind] = wr->wr_id;

        if (wr->opcode >= ARRAY_SIZE(hgrnic_opcode)) {
            hgrnic_err(dev, "opcode invalid\n");
            err = -EINVAL;
            *bad_wr = wr;
            goto out;
        }

        ((struct hgrnic_next_unit *) prev_wqe)->nda_nop =
            cpu_to_le32(((ind << qp->sq.entry_sz_log)) |
                    hgrnic_opcode[wr->opcode]);
        ((struct hgrnic_next_unit *) prev_wqe)->ee_nds =
            cpu_to_le32(HGRNIC_NEXT_DBD | size |
                    ((wr->send_flags & IB_SEND_FENCE) ?
                        HGRNIC_NEXT_FENCE : 0));

        if (!nreq) {
            size0 = size;
            op0   = hgrnic_opcode[wr->opcode];
            f0    = wr->send_flags & IB_SEND_FENCE ?
                HGRNIC_SEND_DOORBELL_FENCE : 0;
        }

        ++ind;
        if (unlikely(ind >= qp->sq.max))
            ind -= qp->sq.max;

        /* print one wqe entry, which may be removed later
         * !TODO */
        for (i = 0; i < (size * 16); ++i)
            printk(KERN_INFO PFX "hgrnic_post_send: wqe[%d]: 0x%x", i, ((u8*)cur_wqe)[i]);
    }

out:
    if (likely(nreq))
        hgrnic_send_dbell(dev, qp, nreq, size0, f0, op0);

    spin_unlock_irqrestore(&qp->sq.lock, flags);
    return err;
}

int hgrnic_post_receive(struct ib_qp *ibqp, const struct ib_recv_wr *wr,
                             const struct ib_recv_wr **bad_wr)
{
    struct hgrnic_dev *dev = to_hgdev(ibqp->device);
    struct hgrnic_qp *qp = to_hgqp(ibqp);
    unsigned long flags;
    int err = 0;
    int nreq;
    int ind;
    int i;
    void *wqe;

    spin_lock_irqsave(&qp->rq.lock, flags);

    ind = qp->rq.head & (qp->rq.max - 1);

    for (nreq = 0; wr; ++nreq, wr = wr->next) {
        if (hgrnic_wq_overflow(&qp->rq, nreq, qp->ibqp.recv_cq)) {
            hgrnic_err(dev, "RQ %06x full (%u head, %u tail,"
                    " %d max, %d nreq)\n", qp->qpn,
                    qp->rq.head, qp->rq.tail,
                    qp->rq.max, nreq);
            err = -ENOMEM;
            *bad_wr = wr;
            goto out;
        }

        wqe = get_wqe(qp->rq, ind);

        ((struct hgrnic_next_unit *) wqe)->flags = 0;

        wqe += sizeof (struct hgrnic_next_unit);

        if (unlikely(wr->num_sge > qp->rq.max_gs)) {
            err = -EINVAL;
            *bad_wr = wr;
            goto out;
        }

        for (i = 0; i < wr->num_sge; ++i) {
            hgrnic_set_data_unit(wqe, wr->sg_list + i);
            wqe += sizeof (struct hgrnic_data_unit);
        }

        /* end of data unit */
        if (i < qp->rq.max_gs)
            hgrnic_set_data_unit_inval(wqe);

        qp->rq.wr_id[ind] = wr->wr_id;

        ++ind;
        if (unlikely(ind >= qp->rq.max))
            ind -= qp->rq.max;
    }

out:
    if (likely(nreq))
        qp->rq.head += nreq;

    spin_unlock_irqrestore(&qp->rq.lock, flags);
    return err;
}

int hgrnic_init_qp_table (struct hgrnic_dev *dev) {
	int err;
	// int i;

	spin_lock_init(&dev->qp_table.lock);

	/*
	 * We reserve 2 extra QPs per port for the special QPs.  The
	 * special QP for port 1 has to be even, so round up.
	 */
	dev->qp_table.sqp_start = (dev->limits.reserved_qps + 1) & ~1UL;
	err = hgrnic_alloc_init(&dev->qp_table.alloc,
			       dev->limits.num_qps,
			       (1 << 24) - 1);
	if (err) {
        return err;
    }

	err = hgrnic_array_init(&dev->qp_table.qp,
			       dev->limits.num_qps);
	if (err) {
		hgrnic_alloc_cleanup(&dev->qp_table.alloc);
		return err;
	}

	return 0;

}

void hgrnic_cleanup_qp_table(struct hgrnic_dev *dev)
{
	// int i;

	// for (i = 0; i < 2; ++i)
	// 	hgrnic_CONF_SPECIAL_QP(dev, i, 0);

	hgrnic_array_cleanup(&dev->qp_table.qp, dev->limits.num_qps);
	hgrnic_alloc_cleanup(&dev->qp_table.alloc);
}
