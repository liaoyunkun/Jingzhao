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
/** @file
 * Configuration and initialization on the RNIC hardware.
 *
 */

#ifndef HGRNIC_ABI_USER_H
#define HGRNIC_ABI_USER_H

#include <linux/types.h>

/*
 * Increment this value if any changes that break userspace ABI
 * compatibility are made.
 */
#define HGRNIC_UVERBS_ABI_VERSION	1

/*
 * Make sure that all structs defined in this file remain laid out so
 * that they pack the same way on 32-bit and 64-bit architectures (to
 * avoid incompatibility between 32-bit userspace and 64-bit kernels).
 * In particular do not use pointer types -- pass pointers in __u64
 * instead.
 */
struct hgrnic_alloc_ucontext_resp {
    __u32 qp_tab_size;
    __u32 reserved;
};

struct hgrnic_alloc_pd_resp {
    __u32 pdn;
    __u32 reserved;
};

/*
 * Mark the memory region with a DMA attribute that causes
 * in-flight DMA to be flushed when the region is written to:
 */
#define HGRNIC_MR_DMASYNC	0x1

struct hgrnic_reg_mr {
    __u32 mr_attrs;
    __u32 reserved;
};

struct hgrnic_create_cq {
    __u32 lkey;
    __u32 pdn;
};

struct hgrnic_create_cq_resp {
    __u32 cqn;
    __u32 reserved;
};

struct hgrnic_resize_cq {
    __u32 lkey;
    __u32 reserved;
};

struct hgrnic_create_qp {
    __u32 rq_lkey;
    __u32 sq_lkey;
};
#endif /* HGRNIC_ABI_USER_H */
