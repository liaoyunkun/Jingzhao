/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
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

#ifndef WQE_H
#define WQE_H

enum {
    HGRNIC_SEND_DOORBELL    = 0x00,
    HGRNIC_RECV_DOORBELL    = 0x18
};

enum {
	HGRNIC_NEXT_DBD       = 1 << 7,
	HGRNIC_NEXT_FENCE     = 1 << 6,
    HGRNIC_NEXT_VALID     = 1 << 5, // indicate that this next_unit is valid
	HGRNIC_NEXT_CQ_UPDATE = 1 << 3,
	HGRNIC_NEXT_EVENT_GEN = 1 << 2,
	HGRNIC_NEXT_SOLICIT   = 1 << 1,
};

enum {
	HGRNIC_INLINE_UNIT = 1 << 31
};

enum {
    HGRNIC_INVAL_LKEY             = 0x0,
    HGRNIC_MAX_WQES_PER_SEND_DB   = 255
};

struct hgrnic_next_unit {
    uint32_t    nda_nop; /* [31:6] next WQE [4:0] next opcode */
    uint32_t    ee_nds; /* [31:8] next EE  [7] DBD [6] F [5:0] next WQE size */
    uint32_t    flags;  /* [3] CQ [2] Event [1] Solicit */
    uint32_t    imm;    /* immediate data */
};

struct hgrnic_ud_unit {
    uint32_t    port;
    uint16_t    slid;
    uint16_t    dlid;
    uint32_t    smac_h;
    uint32_t    dmac_h;
    uint32_t    rsvd1[4];
    uint32_t    dqpn;
    uint32_t    qkey;
    uint32_t    rsvd2[2];
};

struct hgrnic_raddr_unit {
    uint64_t    raddr;
    uint32_t    rkey;
    uint32_t    reserved;
};

struct hgrnic_atomic_seg {
	uint64_t	swap_add;
	uint64_t	compare;
};

struct hgrnic_data_unit {
    uint32_t    byte_count;
    uint32_t    lkey;
    uint64_t    addr;
};

struct hgrnic_inline_unit {
    uint32_t    byte_count;
};

#endif /* WQE_H */
