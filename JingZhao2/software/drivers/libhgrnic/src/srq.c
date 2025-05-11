
#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <stdio.h>

#include "hgrnic.h"
#include "doorbell.h"
#include "wqe.h"

struct ibv_srq *hgrnic_create_srq(struct ibv_pd *pd, struct ibv_srq_init_attr *srq_init_attr) {
    struct ibv_srq *srq;

    srq = malloc(sizeof *srq);
    if (!srq)
        return NULL;

    return srq;
}

int hgrnic_modify_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr, enum ibv_srq_attr_mask srq_attr_mask) {
    return 1; // not support modify srq
}

int hgrnic_query_srq(struct ibv_srq *srq, struct ibv_srq_attr *srq_attr) {
    return 1;
}

int hgrnic_destroy_srq(struct ibv_srq *srq) {
    free(srq);
    return 0;
}

int hgrnic_post_srq_recv(struct ibv_srq *srq, struct ibv_recv_wr *recv_wr, struct ibv_recv_wr **bad_recv_wr) {
    return 0;
}