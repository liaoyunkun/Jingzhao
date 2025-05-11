/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
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
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>

#include "hgrnic.h"

int hgrnic_alloc_av(struct hgrnic_pd *pd, struct ibv_ah_attr *attr,
                    struct hgrnic_ah *ah)
{
    ah->av = malloc(sizeof *ah->av);
    if (!ah->av)
        return -1;

    memset(ah->av, 0, sizeof *ah->av);

    ah->av->port_pd = pd->pdn | (attr->port_num << 24);
    ah->av->g_slid  = attr->src_path_bits;
    ah->av->dlid    = attr->dlid;
    ah->av->msg_sr  = (3 << 4) | /* 2K message */
                      attr->static_rate;
    ah->av->sl_tclass_flowlabel = attr->sl << 28;
    if (attr->is_global) {
        ah->av->g_slid |= 0x80;
        /* XXX get gid_table length */
        ah->av->gid_index = (attr->port_num - 1) * 32 +
            attr->grh.sgid_index;
        ah->av->hop_limit = attr->grh.hop_limit;
        ah->av->sl_tclass_flowlabel |=
            (attr->grh.traffic_class << 20) |
                    attr->grh.flow_label;
        memcpy(ah->av->dgid, attr->grh.dgid.raw, 16);
    } else {
        ah->av->dgid[3] = 2;
    }

    return 0;
}

void hgrnic_free_av(struct hgrnic_ah *ah)
{
    free(ah->av);
}
