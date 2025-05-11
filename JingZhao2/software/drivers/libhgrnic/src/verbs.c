/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
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

#define _GNU_SOURCE /* See feature_test_macros(7) */
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <execinfo.h>

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

#define my_ompi_code_path_name "/home/huangtianhao/code/mpi_code_huangtianhao/"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#if HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <pthread.h>
#include <errno.h>
#include <netinet/in.h>

#include "hgrnic.h"
#include "hgrnic-abi.h"

int hgrnic_query_device(struct ibv_context *context, struct ibv_device_attr *attr)
{
    struct ibv_query_device cmd;
    uint64_t raw_fw_ver;
    unsigned major, minor, sub_minor;
    int ret;

    ret = ibv_cmd_query_device(context, attr, &raw_fw_ver, &cmd, sizeof cmd);
    if (ret)
        return ret;

    major = (raw_fw_ver >> 32) & 0xffff;
    minor = (raw_fw_ver >> 16) & 0xffff;
    sub_minor = raw_fw_ver & 0xffff;

    snprintf(attr->fw_ver, sizeof attr->fw_ver,
             "%d.%d.%d", major, minor, sub_minor);

    return 0;
}

int hgrnic_query_port(struct ibv_context *context, uint8_t port,
                      struct ibv_port_attr *attr)
{
    struct ibv_query_port cmd;

    return ibv_cmd_query_port(context, port, attr, &cmd, sizeof cmd);
}

struct ibv_pd *hgrnic_alloc_pd(struct ibv_context *context)
{
    struct ibv_alloc_pd cmd;
    struct hgrnic_alloc_pd_resp resp;
    struct hgrnic_pd *pd;

    pd = malloc(sizeof *pd);
    if (!pd)
        return NULL;

    if (ibv_cmd_alloc_pd(context, &pd->ibv_pd, &cmd, sizeof cmd,
                         &resp.ibv_resp, sizeof resp))
    {
        free(pd);
        return NULL;
    }

    pd->pdn = resp.pdn;

    return &pd->ibv_pd;
}

int hgrnic_free_pd(struct ibv_pd *pd)
{
    int ret;

    ret = ibv_cmd_dealloc_pd(pd);
    if (ret)
        return ret;

    free(to_hgpd(pd));
    return 0;
}

/**
 * @param dma_sync This is set to 1 when the operation to this MR
 * is required that all dma operations before this
 * region should be completed.
 */
static struct ibv_mr *__hgrnic_reg_mr(struct ibv_pd *pd, void *addr,
                                      size_t length, uint64_t hca_va,
                                      int access,
                                      int dma_sync)
{
    struct ibv_reg_mr_resp resp;
    struct hgrnic_reg_mr cmd;
    struct ibv_mr *mr;
    int ret;

    /*
     * Old kernels just ignore the extra data we pass in with the
     * reg_mr command structure, so there's no need to add an ABI
     * version check here (and indeed the kernel ABI was not
     * incremented due to this change).
     */
    cmd.mr_attrs = dma_sync ? HGRNIC_MR_DMASYNC : 0;
    cmd.reserved = 0;

    mr = malloc(sizeof *mr);
    if (!mr)
        return NULL;

    ret = ibv_cmd_reg_mr(pd, addr, length, hca_va, access, mr,
                         &cmd.ibv_cmd, sizeof cmd, &resp, sizeof resp);
    if (ret)
    {
        free(mr);
        return NULL;
    }

    return mr;
}

struct ibv_mr *hgrnic_reg_mr(struct ibv_pd *pd, void *addr,
                             size_t length, enum ibv_access_flags access)
{
    return __hgrnic_reg_mr(pd, addr, length, (uintptr_t)addr, (int)access, 0);
}

int hgrnic_dereg_mr(struct ibv_mr *mr)
{
    int ret;

    ret = ibv_cmd_dereg_mr(mr);
    if (ret)
        return ret;

    free(mr);
    return 0;
}

static int align_cq_size(int cqe)
{
    int nent;

    for (nent = 1; nent < cqe; nent <<= 1)
        ; /* nothing */

    return nent;
}

struct ibv_cq *hgrnic_create_cq(struct ibv_context *context, int cqe,
                                struct ibv_comp_channel *channel,
                                int comp_vector)
{
    struct hgrnic_create_cq        cmd;
    struct hgrnic_create_cq_resp   resp;
    struct hgrnic_cq               *cq;
    int                            ret;

    /* Sanity check CQ size before proceeding */
    if (cqe > 0x20000)
        return NULL;

    cq = malloc(sizeof *cq);
    if (!cq)
        return NULL;

    cq->cons_index = 0;

    if (pthread_spin_init(&cq->lock, PTHREAD_PROCESS_PRIVATE))
        goto err;

    cqe = align_cq_size(cqe);
    cq->cqe_mask = cqe - 1; // cqe is in log size, so (cqe - 1) could be the mask
    if (hgrnic_alloc_cq_buf(to_hgdev(context->device), &cq->buf, cqe))
        goto err;

    cq->mr = __hgrnic_reg_mr(to_hgctx(context)->pd, cq->buf.buf,
                             cqe * HGRNIC_CQ_ENTRY_SIZE,
                             0, IBV_ACCESS_LOCAL_WRITE, 1);
    if (!cq->mr)
        goto err_buf;

    cq->mr->context = context;

    cmd.lkey   = cq->mr->lkey;
    cmd.pdn    = to_hgpd(to_hgctx(context)->pd)->pdn;
    ret = ibv_cmd_create_cq(context, cqe - 1, channel, comp_vector,
                &cq->ibv_cq, &cmd.ibv_cmd, sizeof cmd,
                &resp.ibv_resp, sizeof resp);
    if (ret)
        goto err_unreg;

    cq->cqn = resp.cqn;

    //printf("create_cq: cqn 0x%x, lkey 0x%x, cq_handle %d\n", cq->cqn, cq->mr->lkey, cq->ibv_cq.handle);

    {
        //printf("\033[1;32;40m-------\n");
        //printf("function name:%s, %s\n", __func__, __FILE__);
        void *buffer[100];
        memset(buffer, 0, 100 * (sizeof(void *)));
        int nptrs = backtrace(buffer, 100);
        //printf("backtrace() returned %d addresses\n", nptrs);
        // char * file_name[200] = my_ompi_code_path_name;
        // sprintf(file_name + strlen(file_name), "hgrnic_create_cq.log", );
        int fd = open(my_ompi_code_path_name "hgrnic_create_cq.log", O_CREAT | O_RDWR | O_APPEND, S_IRWXO);
        //printf("fd = %d\n", fd);

        char full_str[1000] = {};
        sprintf(full_str, "pid:%d,tid:%ld,in function:%s\n", getpid(), syscall(__NR_gettid), __func__);
        write(fd, full_str, strlen(full_str));
        backtrace_symbols_fd(buffer, nptrs, fd);
        close(fd);
        //printf("\n-------\033[0;m\n");
    }
    {
        //printf("pid:%d,tid:%ld,in function:%s\n", getpid(), syscall(__NR_gettid), __func__);
    }

    //printf("hgrnic_create_cq create cq cqn 0x%x, handle %u\n", cq->cqn, cq->ibv_cq.handle);
    if(0)
    {
        static int k = 0;
        k++;
        int q = 2;
        if (k == q)
        {
            printf("k = %d\n",k);
            while (1)
            {
            }
        }
    }
    return &cq->ibv_cq;

err_unreg:
    hgrnic_dereg_mr(cq->mr);

err_buf:
    hgrnic_free_buf(&cq->buf);

err:
    free(cq);

    return NULL;
}

int hgrnic_resize_cq(struct ibv_cq *ibcq, int cqe)
{
    struct hgrnic_cq *cq = to_hgcq(ibcq);
    struct hgrnic_resize_cq cmd;
    struct ibv_mr *mr;
    struct hgrnic_buf buf;
    int old_cqe;
    int ret;

    if (cqe > 0x20000)
        return EINVAL;

    /* The code block below executes atomically.
       hgrnic_poll_one cannot execute at this time. */
    pthread_spin_lock(&cq->lock);

    cqe = align_cq_size(cqe);
    cq->cqe_mask = cqe - 1;
    if (cqe == ibcq->cqe + 1)
    {
        ret = 0;
        goto out;
    }

    ret = hgrnic_alloc_cq_buf(to_hgdev(ibcq->context->device), &buf, cqe);
    if (ret)
        goto out;

    mr = __hgrnic_reg_mr(to_hgctx(ibcq->context)->pd, buf.buf,
                         cqe * HGRNIC_CQ_ENTRY_SIZE,
                         0, IBV_ACCESS_LOCAL_WRITE, 1);
    if (!mr)
    {
        hgrnic_free_buf(&buf);
        ret = ENOMEM;
        goto out;
    }

    mr->context = ibcq->context;

    old_cqe = ibcq->cqe; /* cqe in ibv_cq can be the mask */

    cmd.lkey = mr->lkey;

    struct ibv_resize_cq_resp resp;
    ret = ibv_cmd_resize_cq(ibcq, cqe - 1, &cmd.ibv_cmd, sizeof cmd,
                            &resp, sizeof resp);
    if (ret)
    {
        hgrnic_dereg_mr(mr);
        hgrnic_free_buf(&buf);
        goto out;
    }

    hgrnic_cq_resize_copy_cqes(cq, buf.buf, old_cqe);

    hgrnic_dereg_mr(cq->mr);
    hgrnic_free_buf(&cq->buf);

    cq->buf = buf;
    cq->mr = mr;

out:
    pthread_spin_unlock(&cq->lock);
    return ret;
}

int hgrnic_destroy_cq(struct ibv_cq *cq)
{
    int ret;

    ret = ibv_cmd_destroy_cq(cq);
    if (ret)
        return ret;

    hgrnic_dereg_mr(to_hgcq(cq)->mr);
    hgrnic_free_buf(&to_hgcq(cq)->buf);
    free(to_hgcq(cq));

    return 0;
}

static int align_queue_size(struct ibv_context *context, int size, int spare)
{
    int ret;

    /*
     * If someone asks for a 0-sized queue, presumably they're not
     * going to use it.  So don't mess with their size.
     */
    if (!size)
        return 0;

    for (ret = 1; ret < size + spare; ret <<= 1)
        ; /* nothing */

    return ret;
}

struct ibv_qp *hgrnic_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *attr)
{
	//fprintf(stderr, "\033[32m libhgrnic : Enter create qp! \033[0m\n");
    struct hgrnic_create_qp cmd;
    struct ibv_create_qp_resp resp;
    struct hgrnic_qp *qp;
    int ret;

    /* Sanity check QP size before proceeding */
    if (attr->cap.max_send_wr > 65536 ||
        attr->cap.max_recv_wr > 65536 ||
        attr->cap.max_send_sge > 64 ||
        attr->cap.max_recv_sge > 64 ||
        attr->cap.max_inline_data > 1024)
	{
		//fprintf(stderr, "\033[31m QP size check failed! \033[0m\n");
        return NULL;
	}

    qp = malloc(sizeof *qp);
    if (!qp)
        return NULL;

//    printf("max_send_wr %d, max_recv_wr %d, max_send_sge %d, max_recv_sge %d\n",
//           attr->cap.max_send_wr, attr->cap.max_recv_wr, attr->cap.max_send_sge, attr->cap.max_recv_sge);
//
    qp->sq.max = align_queue_size(pd->context, attr->cap.max_send_wr, 0);
    qp->rq.max = align_queue_size(pd->context, attr->cap.max_recv_wr, 0);

    if (hgrnic_alloc_qp_buf(pd, &attr->cap, attr->qp_type, qp))
        goto err;

    if (pthread_spin_init(&qp->sq.lock, PTHREAD_PROCESS_PRIVATE) ||
        pthread_spin_init(&qp->rq.lock, PTHREAD_PROCESS_PRIVATE))
        goto err_free;

    qp->sq.mr = __hgrnic_reg_mr(pd, qp->sq.buf.buf, qp->sq.buf_size, 0, 0, 0);
    if (!qp->sq.mr)
        goto err_free;

    qp->sq.mr->context = pd->context;
    cmd.sq_lkey = qp->sq.mr->lkey;

    if (qp->rq.buf_size)
    {
        qp->rq.mr = __hgrnic_reg_mr(pd, qp->rq.buf.buf, qp->rq.buf_size, 0, 0, 0);
        if (!qp->sq.mr)
            goto err_sq_mr_unreg;

        qp->rq.mr->context = pd->context;
        cmd.rq_lkey = qp->rq.mr->lkey;
    }

    pthread_mutex_lock(&to_hgctx(pd->context)->qp_table_mutex);
    //printf("pthread_mutex_lock\n");
    ret = ibv_cmd_create_qp(pd, &qp->ibv_qp, attr, &cmd.ibv_cmd, sizeof cmd,
                            &resp, sizeof resp);
    if (ret)
        goto err_rq_mr_unreg;
    //printf("ibv_cmd_create_qp qptype = %d, qpn = %u\n", qp->ibv_qp.qp_type, qp->ibv_qp.qp_num);

    ret = hgrnic_store_qp(to_hgctx(pd->context), qp->ibv_qp.qp_num, qp);
    if (ret)
        goto err_destroy;
    pthread_mutex_unlock(&to_hgctx(pd->context)->qp_table_mutex);
    //printf("hgrnic_store_qp\n");

    // qp->sq.max          = attr->cap.max_send_wr;
    // qp->rq.max          = attr->cap.max_recv_wr;
    // qp->sq.max_gs       = attr->cap.max_send_sge;
    // qp->rq.max_gs       = attr->cap.max_recv_sge;
    qp->max_inline_data = attr->cap.max_inline_data;

	//fprintf(stderr, "\033[32m libhgrnic : Exit create qp! \033[0m\n");
    return &qp->ibv_qp;

err_destroy:
	//fprintf(stderr, "\033[31m libhgrnic : err_destory! \033[0m\n");
    ibv_cmd_destroy_qp(&qp->ibv_qp);

err_rq_mr_unreg:
	//fprintf(stderr, "\033[31m libhgrnic : err_rq_mr_unreg! \033[0m\n");
    pthread_mutex_unlock(&to_hgctx(pd->context)->qp_table_mutex);
    if (qp->rq.buf_size)
        hgrnic_dereg_mr(qp->rq.mr);

err_sq_mr_unreg:
	//fprintf(stderr, "\033[31m libhgrnic : err_sq_mr_unreg! \033[0m\n");
    hgrnic_dereg_mr(qp->sq.mr);

err_free:
	//fprintf(stderr, "\033[31m libhgrnic : err_free! \033[0m\n");
    free(qp->sq.wrid);
    free(qp->rq.wrid);
    hgrnic_free_buf(&qp->sq.buf);
    hgrnic_free_buf(&qp->rq.buf);

err:
	//fprintf(stderr, "\033[31m libhgrnic : err! \033[0m\n");
    free(qp);

	//fprintf(stderr, "\033[31m libhgrnic : Exit create qp! \033[0m\n");
    return NULL;
}

int hgrnic_query_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
                    enum ibv_qp_attr_mask attr_mask,
                    struct ibv_qp_init_attr *init_attr)
{
    struct ibv_query_qp cmd;

    return ibv_cmd_query_qp(qp, attr, attr_mask, init_attr, &cmd, sizeof cmd);
}

int hgrnic_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
                     enum ibv_qp_attr_mask attr_mask)
{
    struct ibv_modify_qp cmd;
    int ret;

    ret = ibv_cmd_modify_qp(qp, attr, attr_mask, &cmd, sizeof cmd);

    if (!ret &&
        (attr_mask & IBV_QP_STATE) &&
        attr->qp_state == IBV_QPS_RESET)
    {
        hgrnic_cq_clean(to_hgcq(qp->recv_cq), qp->qp_num);
        if (qp->send_cq != qp->recv_cq)
            hgrnic_cq_clean(to_hgcq(qp->send_cq), qp->qp_num);

        hgrnic_init_qp_indices(to_hgqp(qp));
    }

    return ret;
}

static void hgrnic_lock_cqs(struct ibv_qp *qp)
{
    struct hgrnic_cq *send_cq = to_hgcq(qp->send_cq);
    struct hgrnic_cq *recv_cq = to_hgcq(qp->recv_cq);

    if (send_cq == recv_cq)
        pthread_spin_lock(&send_cq->lock);
    else if (send_cq->cqn < recv_cq->cqn)
    {
        pthread_spin_lock(&send_cq->lock);
        pthread_spin_lock(&recv_cq->lock);
    }
    else
    {
        pthread_spin_lock(&recv_cq->lock);
        pthread_spin_lock(&send_cq->lock);
    }
}

static void hgrnic_unlock_cqs(struct ibv_qp *qp)
{
    struct hgrnic_cq *send_cq = to_hgcq(qp->send_cq);
    struct hgrnic_cq *recv_cq = to_hgcq(qp->recv_cq);

    if (send_cq == recv_cq)
        pthread_spin_unlock(&send_cq->lock);
    else if (send_cq->cqn < recv_cq->cqn)
    {
        pthread_spin_unlock(&recv_cq->lock);
        pthread_spin_unlock(&send_cq->lock);
    }
    else
    {
        pthread_spin_unlock(&send_cq->lock);
        pthread_spin_unlock(&recv_cq->lock);
    }
}

int hgrnic_destroy_qp(struct ibv_qp *qp)
{
    int ret;

    pthread_mutex_lock(&to_hgctx(qp->context)->qp_table_mutex);
    ret = ibv_cmd_destroy_qp(qp);
    if (ret)
    {
        pthread_mutex_unlock(&to_hgctx(qp->context)->qp_table_mutex);
        return ret;
    }

    hgrnic_lock_cqs(qp);

    __hgrnic_cq_clean(to_hgcq(qp->recv_cq), qp->qp_num);
    if (qp->send_cq != qp->recv_cq)
        __hgrnic_cq_clean(to_hgcq(qp->send_cq), qp->qp_num);

    hgrnic_clear_qp(to_hgctx(qp->context), qp->qp_num);

    hgrnic_unlock_cqs(qp);
    pthread_mutex_unlock(&to_hgctx(qp->context)->qp_table_mutex);

    hgrnic_dereg_mr(to_hgqp(qp)->sq.mr);
    hgrnic_dereg_mr(to_hgqp(qp)->rq.mr);
    hgrnic_free_buf(&to_hgqp(qp)->sq.buf);
    hgrnic_free_buf(&to_hgqp(qp)->rq.buf);
    free(to_hgqp(qp)->sq.wrid);
    free(to_hgqp(qp)->rq.wrid);
    free(to_hgqp(qp));

    return 0;
}

struct ibv_ah *hgrnic_create_ah(struct ibv_pd *pd, struct ibv_ah_attr *attr)
{
    struct hgrnic_ah *ah;

    ah = malloc(sizeof *ah);
    if (!ah)
        return NULL;

    if (hgrnic_alloc_av(to_hgpd(pd), attr, ah))
    {
        free(ah);
        return NULL;
    }

    return &ah->ibv_ah;
}

int hgrnic_destroy_ah(struct ibv_ah *ah)
{
    hgrnic_free_av(to_hgah(ah));
    free(to_hgah(ah));

    return 0;
}
