/*
 *======================= START OF LICENSE NOTICE =======================
 *  Copyright (C) 2021 Kang Ning, NCIC, ICT, CAS.
 *  All Rights Reserved.
 *
 *  NO WARRANTY. THE PRODUCT IS PROVIDED BY DEVELOPER "AS IS" AND ANY
 *  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 *  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL DEVELOPER BE LIABLE FOR
 *  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *  GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 *  IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THE PRODUCT, EVEN
 *  IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *======================== END OF LICENSE NOTICE ========================
 *  Primary Author: Kang Ning
 *  <kangning18z@ict.ac.cn>
 */

#ifndef HGRNIC_ABI_USER_H
#define HGRNIC_ABI_USER_H

#include <infiniband/kern-abi.h>

#define HGRNIC_UVERBS_ABI_VERSION	1

struct hgrnic_alloc_ucontext_resp {
    struct ibv_get_context_resp ibv_resp;
    __u32                       qp_tab_size;
    __u32                       reserved;
};

struct hgrnic_alloc_pd_resp {
    struct ibv_alloc_pd_resp    ibv_resp;
    __u32                       pdn;
    __u32                       reserved;
};


/*
 * Mark the memory region with a DMA attribute that causes
 * in-flight DMA to be flushed when the region is written to:
 */
#define HGRNIC_MR_DMASYNC	0x1

struct hgrnic_reg_mr {
    struct ibv_reg_mr       ibv_cmd;
    __u32                   mr_attrs;
    __u32                   reserved;
};

struct hgrnic_create_cq {
    struct ibv_create_cq    ibv_cmd;
    __u32                   lkey   ;
    __u32                   pdn    ;
};

struct hgrnic_create_cq_resp {
    struct ibv_create_cq_resp   ibv_resp;
    __u32                       cqn     ;
    __u32                       reserved;
};

struct hgrnic_resize_cq {
    struct ibv_resize_cq    ibv_cmd ;
    __u32                   lkey    ;
    __u32                   reserved;
};

struct hgrnic_create_qp {
    struct ibv_create_qp    ibv_cmd;
    __u32                   rq_lkey;
    __u32                   sq_lkey;
};

#endif /* HGRNIC_ABI_USER_H */