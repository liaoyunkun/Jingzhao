/**************************************************************
 * @author Kang Ning<kangning18z@ict.ac.cn>, NCIC, ICT, CAS
 * @date 2021.09.08
 * @file hgrnic_cmd.c
 * @note Command for low-level driver to interact with HCA hardware.
 *************************************************************/

#include <linux/completion.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <rdma/ib_mad.h>


#include "hgrnic_cmd.h"

#define CMD_POLL_TOKEN 0xffff

enum {
    HCR_IN_PARAM_OFFSET    = 0x00,
    HCR_IN_MODIFIER_OFFSET = 0x08,
    HCR_OUT_PARAM_OFFSET   = 0x0c,
    HCR_TOKEN_OFFSET       = 0x14,
    HCR_STATUS_OFFSET      = 0x18,

    HCR_OPMOD_SHIFT        = 12,
    HCA_E_BIT              = 22,
    HCR_GO_BIT             = 23
};

enum {
    /* initialization and general commands */
    CMD_QUERY_DEV_LIM   = 0x3,
    CMD_QUERY_ADAPTER   = 0x6,
    CMD_INIT_HCA        = 0x7,
    CMD_CLOSE_HCA       = 0x8,
    // CMD_QUERY_HCA       = 0xb,
    CMD_INIT_IB         = 0x9,
    CMD_CLOSE_IB        = 0xa,
    CMD_SET_IB          = 0xc,
    
    CMD_MAP_ICM         = 0xffa,
    CMD_UNMAP_ICM       = 0xff9,

    /* TPT commands */
    CMD_SW2HW_MPT         = 0xd,
    // CMD_QUERY_MPT         = 0xe,
    CMD_HW2SW_MPT         = 0xf,
    // CMD_READ_MTT        = 0x10,
    CMD_WRITE_MTT       = 0x11,
    // CMD_SYNC_TPT        = 0x2f,

    /* EQ commands */
    CMD_MAP_EQ          = 0x12,
    CMD_SW2HW_EQ         = 0x13,
    CMD_HW2SW_EQ         = 0x14,
    // CMD_QUERY_EQ        = 0x15,

    /* CQ commands */
    CMD_SW2HW_CQ         = 0x16,
    CMD_HW2SW_CQ         = 0x17,
    // CMD_QUERY_CQ         = 0x18,
    CMD_RESIZE_CQ       = 0x2c,

    /* QP/EE commands */
    CMD_RST2INIT_QPEE   = 0x19,
    CMD_INIT2RTR_QPEE   = 0x1a,
    CMD_RTR2RTS_QPEE    = 0x1b,
    CMD_RTS2RTS_QPEE    = 0x1c,
    CMD_SQERR2RTS_QPEE  = 0x1d,
    CMD_2ERR_QPEE       = 0x1e,
    CMD_RTS2SQD_QPEE    = 0x1f,
    CMD_SQD2SQD_QPEE    = 0x38,
    CMD_SQD2RTS_QPEE    = 0x20,
    CMD_ERR2RST_QPEE    = 0x21,
    CMD_QUERY_QPEE      = 0x22,
    CMD_INIT2INIT_QPEE  = 0x2d,
    CMD_SUSPEND_QPEE    = 0x32,
    CMD_UNSUSPEND_QPEE  = 0x33,

    /* special QPs and management commands */
    CMD_CONF_SPECIAL_QP = 0x23,
    CMD_MAD_IFC         = 0x24,

    /* multicast commands */
    CMD_READ_MGM        = 0x25,
    CMD_WRITE_MGM       = 0x26,
    CMD_MGID_HASH       = 0x27,

    /* miscellaneous commands */
    CMD_DIAG_RPRT       = 0x30,
    CMD_NOP             = 0x31,

    /* debug commands */
    CMD_QUERY_DEBUG_MSG = 0x2a,
    CMD_SET_DEBUG_MSG   = 0x2b,
};


enum {
    CMD_TIME_CLASS_A = 60 * HZ,
    CMD_TIME_CLASS_B = 60 * HZ,
    CMD_TIME_CLASS_C = 60 * HZ,
    CMD_TIME_CLASS_D = 60 * HZ
};

enum {
    GO_BIT_TIMEOUT = HZ * 10
};


static inline int go_bit (struct hgrnic_dev *dev) {
    u32 r = readl(dev->hcr + HCR_STATUS_OFFSET);
     
    return r & swab32(1 << HCR_GO_BIT);
}


static int hgrnic_cmd_post_hcr (struct hgrnic_dev *dev, u64 in_param, 
                               u64 out_param, u32 in_modifier, 
                               u8 op_modifier, u16 op, u16 token)
{
    if (go_bit(dev)) {
		printk("go bit unclear\n");
        return -EAGAIN;
	}

    /*
     * We use writel (instead of something like memcpy_toio)
     * because writes of less than 32 bits to the HCR don't work
     * (and some architectures such as ia64 implement memcpy_toio
     * in terms of writeb).
     */
    __raw_writel((__force u32) cpu_to_le32(in_param >> 32),           dev->hcr + 0 * 4);
    __raw_writel((__force u32) cpu_to_le32(in_param & 0xfffffffful),  dev->hcr + 1 * 4);
    __raw_writel((__force u32) cpu_to_le32(in_modifier),              dev->hcr + 2 * 4);
    __raw_writel((__force u32) cpu_to_le32(out_param >> 32),          dev->hcr + 3 * 4);
    __raw_writel((__force u32) cpu_to_le32(out_param & 0xfffffffful), dev->hcr + 4 * 4);
    __raw_writel((__force u32) cpu_to_le32(token << 16),              dev->hcr + 5 * 4);
    /* __raw_writel may not order writes. */
    wmb();

    __raw_writel((__force u32) cpu_to_le32((1 << HCR_GO_BIT) |
                           (0)                               |
                           (op_modifier << HCR_OPMOD_SHIFT)  |
                           op),             dev->hcr + 6 * 4);


    return 0;
}

static int hgrnic_cmd_post (struct hgrnic_dev *dev, u64 in_param, u64 out_param,
                           u32 in_modifier, u8 op_modifier, u16 op, u16 token)
{
    int err = 0;

    mutex_lock(&dev->cmd.hcr_mutex);

    err = hgrnic_cmd_post_hcr(dev, in_param, out_param, in_modifier,
                op_modifier, op, token);

    mutex_unlock(&dev->cmd.hcr_mutex);
    return err;
}


static int hgrnic_status_to_errno (u8 status)
{
    static const int trans_table[] = {
        [HGRNIC_CMD_STAT_INTERNAL_ERR]   = -EIO,
        [HGRNIC_CMD_STAT_BAD_OP]         = -EPERM,
        [HGRNIC_CMD_STAT_BAD_PARAM]      = -EINVAL,
        [HGRNIC_CMD_STAT_BAD_SYS_STATE]  = -ENXIO,
        [HGRNIC_CMD_STAT_BAD_RESOURCE]   = -EBADF,
        [HGRNIC_CMD_STAT_RESOURCE_BUSY]  = -EBUSY,
        [HGRNIC_CMD_STAT_DDR_MEM_ERR]    = -ENOMEM,
        [HGRNIC_CMD_STAT_EXCEED_LIM]     = -ENOMEM,
        [HGRNIC_CMD_STAT_BAD_RES_STATE]  = -EBADF,
        [HGRNIC_CMD_STAT_BAD_INDEX]      = -EBADF,
        [HGRNIC_CMD_STAT_BAD_NVMEM]      = -EFAULT,
        [HGRNIC_CMD_STAT_BAD_QPEE_STATE] = -EINVAL,
        [HGRNIC_CMD_STAT_BAD_SEG_PARAM]  = -EFAULT,
        [HGRNIC_CMD_STAT_REG_BOUND]      = -EBUSY,
        [HGRNIC_CMD_STAT_LAM_NOT_PRE]    = -EAGAIN,
        [HGRNIC_CMD_STAT_BAD_PKT]        = -EBADMSG,
        [HGRNIC_CMD_STAT_BAD_SIZE]       = -ENOMEM,
    };

    if (status >= ARRAY_SIZE(trans_table) ||
            (status != HGRNIC_CMD_STAT_OK
                && trans_table[status] == 0))
        return -EINVAL;

    return trans_table[status];
}


static int hgrnic_cmd_poll (struct hgrnic_dev *dev, u64 in_param, 
        u64 *out_param, int out_is_imm, u32 in_modifier, u8 op_modifier,
        u16 op, unsigned long timeout)
{
    int err = 0;
    unsigned long end;
    u8 status;

    down(&dev->cmd.poll_sem);

    err = hgrnic_cmd_post(dev, in_param, out_param ? *out_param : 0,
                in_modifier, op_modifier, op, CMD_POLL_TOKEN);

    if (err)
        goto out;

    end = timeout + jiffies;
    while (go_bit(dev) && time_before(jiffies, end)) {
        set_current_state(TASK_RUNNING);
        schedule();
    }

    if (go_bit(dev)) {
        err = -EBUSY;
        goto out;
    }

    if (out_is_imm && out_param) {
        *out_param =
            (u64) le32_to_cpu((__force __be32)
                        __raw_readl(dev->hcr + HCR_OUT_PARAM_OFFSET)) << 32 |
            (u64) le32_to_cpu((__force __be32)
                        __raw_readl(dev->hcr + HCR_OUT_PARAM_OFFSET + 4));
    } else if (out_is_imm) {
        err = -EINVAL;
        goto out;
    }

    status = le32_to_cpu((__force __be32) __raw_readl(dev->hcr + HCR_STATUS_OFFSET)) >> 24;
    if (status) {
        hgrnic_dbg(dev, "Command %02x completed with status %02x\n",
                op, status);
        err = hgrnic_status_to_errno(status);
    }

out:
    up(&dev->cmd.poll_sem);
    return err;
}


/* Invoke a command with an output mailbox */
static int hgrnic_cmd_box (struct hgrnic_dev *dev, u64 in_param,
                          u64 out_param, u32 in_modifier, 
                          u8 op_modifier, u16 op, unsigned long timeout)
{
    return hgrnic_cmd_poll(dev, in_param, &out_param, 0,
                          in_modifier, op_modifier, op, timeout);
}

/* Invoke a command with no output parameter */
static int hgrnic_cmd(struct hgrnic_dev *dev, u64 in_param, u32 in_modifier,
                     u8 op_modifier, u16 op, unsigned long timeout)
{
    return hgrnic_cmd_box(dev, in_param, 0, in_modifier,
                         op_modifier, op, timeout);
}


int hgrnic_cmd_init (struct hgrnic_dev *dev) {
    mutex_init(&dev->cmd.hcr_mutex);
    sema_init(&dev->cmd.poll_sem, 1);

    dev->hcr = ioremap(pci_resource_start(dev->pdev, 0) + HGRNIC_HCR_BASE,
               HGRNIC_HCR_SIZE);
    if (!dev->hcr) {
        hgrnic_err(dev, "Couldn't map command register.");
        return -ENOMEM;
    }

    dev->cmd.pool = dma_pool_create("hgrnic_cmd", &dev->pdev->dev,
                    HGRNIC_MAILBOX_SIZE,
                    HGRNIC_MAILBOX_SIZE, 0);
    if (!dev->cmd.pool) {
        iounmap(dev->hcr);
        return -ENOMEM;
    }

    return 0;
}

void hgrnic_cmd_cleanup (struct hgrnic_dev *dev) {
    dma_pool_destroy(dev->cmd.pool);
    iounmap(dev->hcr);
}


struct hgrnic_mailbox *hgrnic_alloc_mailbox (struct hgrnic_dev *dev, 
        gfp_t gfp_mask)
{
    struct hgrnic_mailbox *mailbox;

    mailbox = kmalloc(sizeof *mailbox, gfp_mask);
    if (!mailbox)
        return ERR_PTR(-ENOMEM);

    mailbox->buf = dma_pool_alloc(dev->cmd.pool, gfp_mask, &mailbox->dma);
    if (!mailbox->buf) {
        kfree(mailbox);
        return ERR_PTR(-ENOMEM);
    }

    return mailbox;
}

void hgrnic_free_mailbox (struct hgrnic_dev *dev, struct hgrnic_mailbox *mailbox)
{
    if (!mailbox)
        return;

    dma_pool_free(dev->cmd.pool, mailbox->buf, mailbox->dma);
    kfree(mailbox);
}


/**
 * @description: 
 *  Command function.
 *  Get parameters from HCA hardware.  
 */
int hgrnic_QUERY_DEV_LIM (struct hgrnic_dev *dev, struct hgrnic_dev_lim *dev_lim) {

    struct hgrnic_mailbox *mailbox;
    u32 *outbox;
    u8 field;
    u16 size;
    u64 entry;
    int err;

#define QUERY_DEV_LIM_OUT_SIZE              0x40

// Reserved number of resources, 8 Bytes
#define QUERY_DEV_LIM_RSVD_QP_OFFSET        0x00
#define QUERY_DEV_LIM_RSVD_CQ_OFFSET        0x01
#define QUERY_DEV_LIM_RSVD_EQ_OFFSET        0x02
#define QUERY_DEV_LIM_RSVD_MTT_OFFSET       0x03
#define QUERY_DEV_LIM_RSVD_PD_OFFSET        0x05
#define QUERY_DEV_LIM_RSVD_LKEY_OFFSET      0x07

// Max queue size
#define QUERY_DEV_LIM_MAX_QP_SZ_OFFSET      0x08
#define QUERY_DEV_LIM_MAX_CQ_SZ_OFFSET      0x0a

// max number of resources
#define QUERY_DEV_LIM_MAX_QP_OFFSET         0x0c
#define QUERY_DEV_LIM_MAX_CQ_OFFSET         0x0d
#define QUERY_DEV_LIM_MAX_EQ_OFFSET         0x0e
#define QUERY_DEV_LIM_MAX_MPT_OFFSET        0x0f
#define QUERY_DEV_LIM_MAX_PD_OFFSET         0x10
#define QUERY_DEV_LIM_MAX_GID_OFFSET        0x12
#define QUERY_DEV_LIM_MAX_PKEY_OFFSET       0x13

#define QUERY_DEV_LIM_MAX_MTT_SEG_OFFSET    0x15

// size of context
#define QUERY_DEV_LIM_QPC_ENTRY_SZ_OFFSET   0x18
#define QUERY_DEV_LIM_CQC_ENTRY_SZ_OFFSET   0x1a
#define QUERY_DEV_LIM_EQC_ENTRY_SZ_OFFSET   0x1c
#define QUERY_DEV_LIM_MPT_ENTRY_SZ_OFFSET   0x1e

// aux
#define QUERY_DEV_LIM_ACK_DELAY_OFFSET      0x20
#define QUERY_DEV_LIM_MTU_WIDTH_OFFSET      0x21
#define QUERY_DEV_LIM_VL_PORT_OFFSET        0x23

// description size
#define QUERY_DEV_LIM_PAGE_SZ_OFFSET        0x25
#define QUERY_DEV_LIM_MAX_SG_OFFSET         0x29
#define QUERY_DEV_LIM_MAX_DESC_SZ_OFFSET    0x2a
#define QUERY_DEV_LIM_MAX_SG_RQ_OFFSET      0x2d
#define QUERY_DEV_LIM_MAX_DESC_SZ_RQ_OFFSET 0x2e

// ICM space size
#define QUERY_DEV_LIM_MAX_ICM_SZ_OFFSET     0x30


    mailbox = hgrnic_alloc_mailbox(dev, GFP_KERNEL);
    if (IS_ERR(mailbox))
        return PTR_ERR(mailbox);
    outbox = mailbox->buf;

    printk(KERN_INFO PFX "Enter hgrnic_QUERY_DEV_LIM. %lx\n", (long)mailbox->dma);

    err = hgrnic_cmd_box(dev, 0, mailbox->dma, 0, 0, CMD_QUERY_DEV_LIM,
                CMD_TIME_CLASS_A);
    if (unlikely(err)) {
        goto out;
    }
    
    printk(KERN_INFO PFX "Get hgrnic_QUERY_DEV_LIM.\n");
    printk(KERN_INFO PFX "dev lim outbox: \n0x%x 0x%x 0x%x 0x%x \n"
            "0x%x 0x%x 0x%x 0x%x\n"
            "0x%x 0x%x 0x%x 0x%x\n"
            "0x%x 0x%x 0x%x 0x%x\n", 
            *outbox, *(outbox+1), *(outbox+2), *(outbox+3),
            *(outbox+4), *(outbox+5), *(outbox+6), *(outbox+7),
            *(outbox+8), *(outbox+9), *(outbox+10), *(outbox+11),
            *(outbox+12), *(outbox+13), *(outbox+14), *(outbox+15));


    HGRNIC_GET(field, outbox, QUERY_DEV_LIM_RSVD_QP_OFFSET);
    dev_lim->reserved_qps = 1 << (field & 0xf);
    printk(KERN_INFO PFX "resv_qp: %d", dev_lim->reserved_qps);
    
    HGRNIC_GET(field, outbox, QUERY_DEV_LIM_RSVD_CQ_OFFSET);
    dev_lim->reserved_cqs = 1 << (field & 0xf);
    printk(KERN_INFO PFX "resv_cq: %d", dev_lim->reserved_cqs);
    
    HGRNIC_GET(field, outbox, QUERY_DEV_LIM_RSVD_EQ_OFFSET);
    dev_lim->reserved_eqs = 1 << (field & 0xf);
    printk(KERN_INFO PFX "resv_eq: %d", dev_lim->reserved_eqs);
    
    HGRNIC_GET(field, outbox, QUERY_DEV_LIM_RSVD_MTT_OFFSET);
    dev_lim->reserved_mtts = 1 << (field & 0xf);
    printk(KERN_INFO PFX "resv_mtt: %d", dev_lim->reserved_mtts);

    HGRNIC_GET(field, outbox, QUERY_DEV_LIM_RSVD_PD_OFFSET);
    dev_lim->reserved_pds = 1 << (field & 0xf);
    printk(KERN_INFO PFX "resv_pd: %d", dev_lim->reserved_pds);
    
    HGRNIC_GET(field, outbox, QUERY_DEV_LIM_RSVD_LKEY_OFFSET);
    dev_lim->reserved_lkey = 1 << (field & 0xf);
    printk(KERN_INFO PFX "resv_lkey: %d", dev_lim->reserved_lkey);
    
    HGRNIC_GET(size, outbox, QUERY_DEV_LIM_MAX_QP_SZ_OFFSET);
    dev_lim->max_qp_sz = 1 << size;
    printk(KERN_INFO PFX "max_qp_sz: %d", dev_lim->max_qp_sz);
    
    HGRNIC_GET(size, outbox, QUERY_DEV_LIM_MAX_CQ_SZ_OFFSET);
    dev_lim->max_cq_sz = 1 << size;
    printk(KERN_INFO PFX "max_cq_sz: %d", dev_lim->max_cq_sz);

    HGRNIC_GET(field, outbox, QUERY_DEV_LIM_MAX_QP_OFFSET);
    dev_lim->max_qps = 1 << (field & 0x1f);
    printk(KERN_INFO PFX "max_qps: %d", dev_lim->max_qps);
    
    HGRNIC_GET(field, outbox, QUERY_DEV_LIM_MAX_CQ_OFFSET);
    dev_lim->max_cqs = 1 << (field & 0x1f);
    printk(KERN_INFO PFX "max_cqs: %d", dev_lim->max_cqs);
    
    HGRNIC_GET(field, outbox, QUERY_DEV_LIM_MAX_EQ_OFFSET);
    dev_lim->max_eqs = 1 << (field & 0x7);
    printk(KERN_INFO PFX "max_eqs: %d", dev_lim->max_eqs);
    
    HGRNIC_GET(field, outbox, QUERY_DEV_LIM_MAX_MPT_OFFSET);
    dev_lim->max_mpts = 1 << (field & 0x3f);
    printk(KERN_INFO PFX "max_mpts: %d", dev_lim->max_mpts);
    
    HGRNIC_GET(field, outbox, QUERY_DEV_LIM_MAX_PD_OFFSET);
    dev_lim->max_pds = 1 << (field & 0x3f);
    printk(KERN_INFO PFX "max_pds: %d", dev_lim->max_pds);
    
    HGRNIC_GET(field, outbox, QUERY_DEV_LIM_MAX_GID_OFFSET);
    dev_lim->max_gids = 1 << (field & 0xf);
    printk(KERN_INFO PFX "max_gids: %d", dev_lim->max_gids);
    
    HGRNIC_GET(field, outbox, QUERY_DEV_LIM_MAX_PKEY_OFFSET);
    dev_lim->max_pkeys = 1 << (field & 0xf);
    printk(KERN_INFO PFX "max_pkeys: %d", field & 0xf);

    HGRNIC_GET(field, outbox, QUERY_DEV_LIM_MAX_MTT_SEG_OFFSET);
    dev_lim->max_mtt_seg = field;
    printk(KERN_INFO PFX "max_mtt_seg: %d", dev_lim->max_mtt_seg);

    HGRNIC_GET(size, outbox, QUERY_DEV_LIM_QPC_ENTRY_SZ_OFFSET);
    dev_lim->qpc_entry_sz = size;
    printk(KERN_INFO PFX "qpc_entry_sz: %d", dev_lim->qpc_entry_sz);
    
    HGRNIC_GET(size, outbox, QUERY_DEV_LIM_CQC_ENTRY_SZ_OFFSET);
    dev_lim->cqc_entry_sz = size;
    printk(KERN_INFO PFX "cqc_entry_sz: %d", dev_lim->cqc_entry_sz);
    
    HGRNIC_GET(size, outbox, QUERY_DEV_LIM_EQC_ENTRY_SZ_OFFSET);
    dev_lim->eqc_entry_sz = size;
    printk(KERN_INFO PFX "eqc_entry_sz: %d", dev_lim->eqc_entry_sz);
    
    HGRNIC_GET(size, outbox, QUERY_DEV_LIM_MPT_ENTRY_SZ_OFFSET);
    dev_lim->mpt_entry_sz = size;
    printk(KERN_INFO PFX "mpt_entry_sz: %d", dev_lim->mpt_entry_sz);

    HGRNIC_GET(field, outbox, QUERY_DEV_LIM_ACK_DELAY_OFFSET);
    dev_lim->local_ca_ack_delay = field & 0xf;
    printk(KERN_INFO PFX "local_ca_ack_delay: %d", dev_lim->local_ca_ack_delay);
    
    HGRNIC_GET(field, outbox, QUERY_DEV_LIM_MTU_WIDTH_OFFSET);
    dev_lim->max_mtu        = field >> 4;
    dev_lim->max_port_width = field & 0xf;
    printk(KERN_INFO PFX "max_mtu: %d", dev_lim->max_mtu);
    printk(KERN_INFO PFX "max_port_width: %d", dev_lim->max_port_width);
    
    HGRNIC_GET(field, outbox, QUERY_DEV_LIM_VL_PORT_OFFSET);
    dev_lim->max_vl    = field >> 4;
    dev_lim->num_ports = field & 0xf;
    printk(KERN_INFO PFX "max_vl: %d", dev_lim->max_vl);
    printk(KERN_INFO PFX "num_ports: %d", dev_lim->num_ports);

    HGRNIC_GET(field, outbox, QUERY_DEV_LIM_PAGE_SZ_OFFSET);
    dev_lim->min_page_sz = 1 << field;
    printk(KERN_INFO PFX "min_page_sz: %d", dev_lim->min_page_sz);

    HGRNIC_GET(field, outbox, QUERY_DEV_LIM_MAX_SG_OFFSET);
    dev_lim->max_sg = field;
    printk(KERN_INFO PFX "max_sg: %d", dev_lim->max_sg);
    
    HGRNIC_GET(size, outbox, QUERY_DEV_LIM_MAX_DESC_SZ_OFFSET);
    dev_lim->max_desc_sz = size;
    printk(KERN_INFO PFX "max_desc_sz: %d", dev_lim->max_desc_sz);
    
    HGRNIC_GET(field, outbox, QUERY_DEV_LIM_MAX_SG_RQ_OFFSET);
    dev_lim->max_sg = min_t(int, field, dev_lim->max_sg);
    printk(KERN_INFO PFX "max_sg_rq: %d", field);
    
    HGRNIC_GET(size, outbox, QUERY_DEV_LIM_MAX_DESC_SZ_RQ_OFFSET);
    dev_lim->max_desc_sz = min_t(int, size, dev_lim->max_desc_sz);
    printk(KERN_INFO PFX "max_desc_sz_rq: %d", size);

    HGRNIC_GET(entry, outbox, QUERY_DEV_LIM_MAX_ICM_SZ_OFFSET);
    dev_lim->max_icm_sz = entry;
    printk(KERN_INFO PFX "max_icm_sz: 0x%llx", dev_lim->max_icm_sz);

out:
    hgrnic_free_mailbox(dev, mailbox);
    return err;
}

/** 
 * @description: 
 *  Command function.
 *  Query board information of board id. 
 *  !TODO: We may add more information on this command.  
 */
int hgrnic_QUERY_ADAPTER (struct hgrnic_dev *dev, u64 *board_id) {
    struct hgrnic_mailbox *mailbox;
    u32 *outbox;
    int err;

#define QUERY_ADAPTER_OUT_SIZE             0x20
#define QUERY_ADAPTER_ID_OFFSET            0x18


    mailbox = hgrnic_alloc_mailbox(dev, GFP_KERNEL);
    if (IS_ERR(mailbox)) {
        return PTR_ERR(mailbox);
    }


    outbox = mailbox->buf;
    *(outbox)   = 0xFF;
    *(outbox+1) = 0xFF;
    *(outbox+2) = 0xFF;
    *(outbox+3) = 0xFF; 
    err = hgrnic_cmd_box(dev, 0, mailbox->dma, 0, 0, CMD_QUERY_ADAPTER,
                CMD_TIME_CLASS_A);


    if (unlikely(err)) {
        goto out;
    }


    HGRNIC_GET(*board_id, outbox, QUERY_ADAPTER_ID_OFFSET);
    printk(KERN_INFO PFX "board_id: 0x%llx\n", *board_id);
    // printk(KERN_INFO PFX "board id outbox: 0x%x 0x%x 0x%x 0x%x "
    //         "0x%x 0x%x 0x%x 0x%x.\n", 
    //         *outbox, *(outbox+1), *(outbox+2), *(outbox+3),
    //         *(outbox+4), *(outbox+5), *(outbox+6), *(outbox+7));
out:
    hgrnic_free_mailbox(dev, mailbox);
    return err;
}

/**
 * @description: 
 *  Command function.
 *  Send ICM virtual address layout to HCA board.
 *  This command should execute before map ICM virtual <-> physical address.  
 */
int hgrnic_INIT_HCA (struct hgrnic_dev *mdev, struct hgrnic_init_hca_param *param)
{
    struct hgrnic_mailbox *mailbox;
    __le32 *inbox;
    int err;

#define  INIT_HCA_IN_SIZE                0x40
#define  INIT_HCA_QPC_OFFSET             0x008
#define  INIT_HCA_QPC_BASE_OFFSET        (INIT_HCA_QPC_OFFSET + 0x00)
#define  INIT_HCA_LOG_QP_OFFSET          (INIT_HCA_QPC_OFFSET + 0x07)
#define  INIT_HCA_CQC_BASE_OFFSET        (INIT_HCA_QPC_OFFSET + 0x08)
#define  INIT_HCA_LOG_CQ_OFFSET          (INIT_HCA_QPC_OFFSET + 0x0f)
#define  INIT_HCA_EQC_BASE_OFFSET        (INIT_HCA_QPC_OFFSET + 0x10)
#define  INIT_HCA_LOG_EQ_OFFSET          (INIT_HCA_QPC_OFFSET + 0x17)

#define  INIT_HCA_TPT_OFFSET              0x030
#define  INIT_HCA_MPT_BASE_OFFSET        (INIT_HCA_TPT_OFFSET + 0x00)
#define  INIT_HCA_LOG_MPT_SZ_OFFSET      (INIT_HCA_TPT_OFFSET + 0x07)
#define  INIT_HCA_MTT_BASE_OFFSET        (INIT_HCA_TPT_OFFSET + 0x08)

    mailbox = hgrnic_alloc_mailbox(mdev, GFP_KERNEL);
    if (IS_ERR(mailbox))
        return PTR_ERR(mailbox);
    inbox = mailbox->buf;

    memset(inbox, 0, INIT_HCA_IN_SIZE);

    /* QPC/CQC/EQC attributes */
    HGRNIC_PUT(inbox, param->qpc_base,     INIT_HCA_QPC_BASE_OFFSET);
    HGRNIC_PUT(inbox, param->log_num_qps,  INIT_HCA_LOG_QP_OFFSET);
    HGRNIC_PUT(inbox, param->cqc_base,     INIT_HCA_CQC_BASE_OFFSET);
    HGRNIC_PUT(inbox, param->log_num_cqs,  INIT_HCA_LOG_CQ_OFFSET);
    HGRNIC_PUT(inbox, param->eqc_base,     INIT_HCA_EQC_BASE_OFFSET);
    HGRNIC_PUT(inbox, param->log_num_eqs,  INIT_HCA_LOG_EQ_OFFSET);

    /* TPT attributes */
    HGRNIC_PUT(inbox, param->mpt_base,   INIT_HCA_MPT_BASE_OFFSET);
    HGRNIC_PUT(inbox, param->log_mpt_sz, INIT_HCA_LOG_MPT_SZ_OFFSET);
    HGRNIC_PUT(inbox, param->mtt_base,   INIT_HCA_MTT_BASE_OFFSET);

    printk(KERN_INFO PFX "init hca outbox: 0x%x 0x%x 0x%x 0x%x "
            "0x%x 0x%x 0x%x 0x%x.\n", 
            *inbox, *(inbox+1), *(inbox+2), *(inbox+3),
            *(inbox+4), *(inbox+5), *(inbox+6), *(inbox+7));
    
    printk(KERN_INFO PFX "init hca outbox: 0x%x 0x%x 0x%x 0x%x "
            "0x%x 0x%x 0x%x 0x%x.\n", 
            *(inbox+8), *(inbox+9), *(inbox+10), *(inbox+11),
            *(inbox+12), *(inbox+13), *(inbox+14), *(inbox+15));

    err = hgrnic_cmd(mdev, mailbox->dma, 0, 0,
                     CMD_INIT_HCA, CMD_TIME_CLASS_D);

    hgrnic_free_mailbox(mdev, mailbox);
    return err;
}

/**
 * @description: 
 *  Command function.
 *  Cancel ICM virtual address layout stored on HCA hardware.
 *  Execute this command when the driver is ready to exit 
 *  (error or normally).  
 */
int hgrnic_CLOSE_HCA (struct hgrnic_dev *dev) {
    return hgrnic_cmd(dev, 0, 0, 0, CMD_CLOSE_HCA, CMD_TIME_CLASS_C);
}

/** 
 * @description: 
 *  Command function.
 *  Configure IB network related information (GID, GUID, etc).
 *  !TODO: We haven't implemented this command yet
 */
int hgrnic_INIT_IB (struct hgrnic_dev *dev, struct hgrnic_init_ib_param *param, 
        int port) {
    struct hgrnic_mailbox *mailbox;
    u32 *inbox;
    int err;
    u32 flags;

#define INIT_IB_IN_SIZE          56
#define INIT_IB_FLAGS_OFFSET     0x00
#define INIT_IB_FLAG_SIG         (1 << 18)
#define INIT_IB_FLAG_NG          (1 << 17)
#define INIT_IB_FLAG_G0          (1 << 16)
#define INIT_IB_VL_SHIFT         4
#define INIT_IB_PORT_WIDTH_SHIFT 8
#define INIT_IB_MTU_SHIFT        12
#define INIT_IB_MAX_GID_OFFSET   0x06
#define INIT_IB_MAX_PKEY_OFFSET  0x0a
#define INIT_IB_GUID0_OFFSET     0x10
#define INIT_IB_NODE_GUID_OFFSET 0x18
#define INIT_IB_SI_GUID_OFFSET   0x20

    mailbox = hgrnic_alloc_mailbox(dev, GFP_KERNEL);
    if (IS_ERR(mailbox))
        return PTR_ERR(mailbox);
    inbox = mailbox->buf;

    memset(inbox, 0, INIT_IB_IN_SIZE);

    flags = 0;
    flags |= param->set_guid0     ? INIT_IB_FLAG_G0  : 0;
    flags |= param->set_node_guid ? INIT_IB_FLAG_NG  : 0;
    flags |= param->set_si_guid   ? INIT_IB_FLAG_SIG : 0;
    flags |= param->vl_cap << INIT_IB_VL_SHIFT;
    flags |= param->port_width << INIT_IB_PORT_WIDTH_SHIFT;
    flags |= param->mtu_cap << INIT_IB_MTU_SHIFT;
    HGRNIC_PUT(inbox, flags, INIT_IB_FLAGS_OFFSET);

    HGRNIC_PUT(inbox, param->gid_cap,   INIT_IB_MAX_GID_OFFSET);
    HGRNIC_PUT(inbox, param->pkey_cap,  INIT_IB_MAX_PKEY_OFFSET);
    HGRNIC_PUT(inbox, param->guid0,     INIT_IB_GUID0_OFFSET);
    HGRNIC_PUT(inbox, param->node_guid, INIT_IB_NODE_GUID_OFFSET);
    HGRNIC_PUT(inbox, param->si_guid,   INIT_IB_SI_GUID_OFFSET);

    err = hgrnic_cmd(dev, mailbox->dma, port, 0, CMD_INIT_IB,
            CMD_TIME_CLASS_A);

    hgrnic_free_mailbox(dev, mailbox);
    return err;
}

/** 
 * @description: 
 *  Command function.
 *  Cancel IB network related information (GID, GUID, etc).
 *  !TODO: We haven't implemented this command yet
 */
int hgrnic_CLOSE_IB (struct hgrnic_dev *dev, int port) {
    return hgrnic_cmd(dev, 0, port, 0, CMD_CLOSE_IB, CMD_TIME_CLASS_A);
}

/** 
 * @description: 
 *  Command function.
 *  Cancel IB network related information (GID, GUID, etc).
 *  !TODO: We haven't implemented this command yet.
 */
int hgrnic_SET_IB (struct hgrnic_dev *dev, struct hgrnic_set_ib_param *param,
         int port) {
    struct hgrnic_mailbox *mailbox;
    u32 *inbox;
    int err;
    u32 flags = 0;

#define SET_IB_IN_SIZE         0x40
#define SET_IB_FLAGS_OFFSET    0x00
#define SET_IB_FLAG_SIG        (1 << 18)
#define SET_IB_FLAG_RQK        (1 <<  0)
#define SET_IB_CAP_MASK_OFFSET 0x04
#define SET_IB_SI_GUID_OFFSET  0x08

    mailbox = hgrnic_alloc_mailbox(dev, GFP_KERNEL);
    if (IS_ERR(mailbox))
        return PTR_ERR(mailbox);
    inbox = mailbox->buf;

    memset(inbox, 0, SET_IB_IN_SIZE);

    flags |= param->set_si_guid     ? SET_IB_FLAG_SIG : 0;
    flags |= param->reset_qkey_viol ? SET_IB_FLAG_RQK : 0;
    HGRNIC_PUT(inbox, flags, SET_IB_FLAGS_OFFSET);

    HGRNIC_PUT(inbox, param->cap_mask, SET_IB_CAP_MASK_OFFSET);
    HGRNIC_PUT(inbox, param->si_guid,  SET_IB_SI_GUID_OFFSET);

    err = hgrnic_cmd(dev, mailbox->dma, port, 0, CMD_SET_IB,
            CMD_TIME_CLASS_B);

    hgrnic_free_mailbox(dev, mailbox);
    return err;
}

/**
 * @description: 
 *  Command function.
 *  Write ICM address translation table to HCA hardware. The command
 *  accepts at most HGRNIC_MAILBOX_SIZE / 16 chunks, and 256 pages 
 *  per chunk.
 */
int hgrnic_MAP_ICM (struct hgrnic_dev *dev, struct hgrnic_icm *icm, 
        u8 type_sel, u64 virt) {
    struct hgrnic_mailbox *mailbox;
    struct hgrnic_icm_iter iter;
    __be64 *pages;
    int lg;
    int nent = 1; /* chunk index: 1, 0, 3, 2, 5, 4, ... */
    int i;
    int err = 0;
    int ts = 0; /* total size */
    int tc = 0; /* total chunks (total entries write to hardware) */

#define MAP_ICM_MAX_PAGE_NUM_LOG 8

    mailbox = hgrnic_alloc_mailbox(dev, GFP_KERNEL);
    if (IS_ERR(mailbox))
        return PTR_ERR(mailbox);

    memset(mailbox->buf, 0, HGRNIC_MAILBOX_SIZE);
    pages = mailbox->buf;


    for (hgrnic_icm_first(icm, &iter);
         !hgrnic_icm_last(&iter);
         hgrnic_icm_next(&iter)) {
        /*
         * We have to pass pages that are aligned to their
         * size, so find the least significant 1 in the
         * address or size and use that as our log2 size.
         */
        lg = ffs(hgrnic_icm_addr(&iter) | hgrnic_icm_size(&iter)) - 1;
        if (lg < HGRNIC_ICM_PAGE_SHIFT) {
            hgrnic_warn(dev, "Got FW area not aligned to %d (%llx/%lx).\n",
                   HGRNIC_ICM_PAGE_SIZE,
                   (unsigned long long) hgrnic_icm_addr(&iter),
                   hgrnic_icm_size(&iter));
            err = -EINVAL;
            goto out;
        }

        if (lg > MAP_ICM_MAX_PAGE_NUM_LOG + HGRNIC_ICM_PAGE_SHIFT) {
            lg = MAP_ICM_MAX_PAGE_NUM_LOG + HGRNIC_ICM_PAGE_SHIFT;
        }
        
        for (i = 0; i < hgrnic_icm_size(&iter) >> lg; ++i) {
            
            pages[nent * 2] = cpu_to_be64(virt);
            virt += 1ULL << lg;

            pages[nent * 2 + 1] =
                    cpu_to_be64((hgrnic_icm_addr(&iter) + (i << lg)) |
                    (1ULL << (lg - HGRNIC_ICM_PAGE_SHIFT)));
            ts += 1 << (lg - 10); // Indicate that ts is in KB unit.
            ++tc;

            if (nent % 2 == 0)
                nent += 3;
            else
                --nent;

            if (nent >= HGRNIC_MAILBOX_SIZE / 16) {
                printk(KERN_INFO PFX "MAP_ICM filled max the mailbox!\n");
                err = hgrnic_cmd(dev, mailbox->dma, (nent - 1), type_sel, CMD_MAP_ICM,
                        CMD_TIME_CLASS_B);
                if (err)
                    goto out;
                nent = 1;
            }
        }
    }

    nent = (nent % 2 == 0) ? nent + 1 : nent - 1;
    if (nent) {
        for (i = 0; i < nent; ++i) {
            printk(KERN_INFO PFX "MAP_ICM: 0x%llx 0x%llx 0x%llx 0x%llx\n",
                    *(pages + i), *(pages + i + 1), *(pages + i + 2), *(pages + i + 3));
        }
        err = hgrnic_cmd(dev, mailbox->dma, nent, type_sel, CMD_MAP_ICM,
                CMD_TIME_CLASS_B);
    }
    
    printk(KERN_INFO PFX "Mapped %d chunks/%d KB at %llx for ICM.\n",
            tc, ts, (unsigned long long) virt - (ts << 10));

out:
    hgrnic_free_mailbox(dev, mailbox);
    return err;
}

/**
 * @description: 
 *  Command function.
 *  Write ICM address translation table (one page) to HCA hardware.  
 */
int hgrnic_MAP_ICM_page (struct hgrnic_dev *dev, u64 dma_addr,
        u8 type_sel, u64 virt) {
    struct hgrnic_mailbox *mailbox;
    __be64 *inbox;
    int err;

    mailbox = hgrnic_alloc_mailbox(dev, GFP_KERNEL);
    if (IS_ERR(mailbox))
        return PTR_ERR(mailbox);
    inbox = mailbox->buf;

    /* 
     * When there's odd number of entries, the last entry 
     * put into the higher address.
     */
    inbox[0] = 0;
    inbox[1] = 0;
    inbox[2] = cpu_to_be64(virt);
    inbox[3] = cpu_to_be64(dma_addr | 0x01);

    printk(KERN_INFO PFX "MAP_ICM page inbox: 0x%llx 0x%llx 0x%llx 0x%llx.\n", 
            *inbox, *(inbox+1), *(inbox+2), *(inbox+3));

    err = hgrnic_cmd(dev, mailbox->dma, 1, type_sel, CMD_MAP_ICM,
            CMD_TIME_CLASS_B);

    hgrnic_free_mailbox(dev, mailbox);

    printk(KERN_INFO PFX "Mapped page at %llx to %llx for ICM.\n",
            (unsigned long long) dma_addr, (unsigned long long) virt);

    return err;
}

/**
 * @description: 
 *  Command function.
 *  Cancel ICM address translation table on HCA hardware.  
 */
int hgrnic_UNMAP_ICM (struct hgrnic_dev *dev, u64 virt, 
        u32 page_count, u8 type_sel)
{
    return hgrnic_cmd(dev, virt, page_count, type_sel,
            CMD_UNMAP_ICM, CMD_TIME_CLASS_B);
}

/**
 * @description: 
 *  Command function.
 *  Send one MPT entry to HCA hardware, using mailbox.  
 *  The parameter format is shown in "struct hgrnic_mpt_entry".
 */
int hgrnic_SW2HW_MPT (struct hgrnic_dev *dev, 
        struct hgrnic_mailbox *mailbox, int mpt_index) {
    return hgrnic_cmd(dev, mailbox->dma, mpt_index, 0, CMD_SW2HW_MPT,
                CMD_TIME_CLASS_B);
}

/**
 * @description: 
 *  Command function.
 *  Cancel one MPT entry from HCA hardware.  
 */
int hgrnic_HW2SW_MPT (struct hgrnic_dev *dev, int mpt_index)
{
    return hgrnic_cmd(dev, 0, mpt_index, 0, CMD_HW2SW_MPT, 
                     CMD_TIME_CLASS_B);
}

/**
 * @description: 
 *  Command function.
 *  Send MTT entries to HCA hardware via the mailbox.
 */
int hgrnic_WRITE_MTT (struct hgrnic_dev *dev, 
        struct hgrnic_mailbox *mailbox, int num_mtt)
{
    return hgrnic_cmd(dev, mailbox->dma, num_mtt, 0, CMD_WRITE_MTT,
                     CMD_TIME_CLASS_B);
}

/**
 * @description: 
 *  Command function.
 *  Send EQ event type flags to HCA hardware.
 */
int hgrnic_MAP_EQ (struct hgrnic_dev *dev, u64 event_mask, 
        int unmap, int eq_num) {
    
    printk(KERN_INFO PFX "%s mask %016llx for eqn %d\n",
          unmap ? "Clearing" : "Setting",
          (unsigned long long) event_mask, eq_num);
    
    return hgrnic_cmd(dev, event_mask, (unmap << 31) | eq_num,
             0, CMD_MAP_EQ, CMD_TIME_CLASS_B);
}

/**
 * @description: 
 *  Command function.
 *  Send EQ context to HCA hardware.
 *  The parameter format is shown in "struct hgrnic_eq_context".
 *  Note that this command would not be called in this version of code.
 */
int hgrnic_SW2HW_EQ (struct hgrnic_dev *dev, 
        struct hgrnic_mailbox *mailbox, int eq_num) {
    return hgrnic_cmd(dev, mailbox->dma, eq_num, 0, CMD_SW2HW_EQ,
             CMD_TIME_CLASS_A);
}

/**
 * @description: 
 *  Command function.
 *  Cancel EQ context in HCA hardware.
 */
int hgrnic_HW2SW_EQ (struct hgrnic_dev *dev, int eq_num) {
    
    return hgrnic_cmd(dev, 0, eq_num, 0, CMD_HW2SW_EQ,
                CMD_TIME_CLASS_A);
}

/**
 * @description: 
 *  Command function.
 *  Send CQ context to HCA hardware.
 *  !FIXME: parameter format should also be included in
 *  the command.
 */
int hgrnic_SW2HW_CQ (struct hgrnic_dev *dev, 
        struct hgrnic_mailbox *mailbox, int cq_num)
{
    return hgrnic_cmd(dev, mailbox->dma, cq_num, 0, CMD_SW2HW_CQ,
                     CMD_TIME_CLASS_A);
}

/**
 * @description: 
 *  Command function.
 *  Cancel CQ context in HCA hardware.
 */
int hgrnic_HW2SW_CQ (struct hgrnic_dev *dev, int cq_num)
{
    return hgrnic_cmd(dev, 0, cq_num, 0, CMD_HW2SW_CQ, 
                     CMD_TIME_CLASS_A);
}

/**
 * @description: 
 *  Command function.
 *  Modify CQ queue length (in CQ context) in HCA hardware.
 */
int hgrnic_RESIZE_CQ (struct hgrnic_dev *dev, int cq_num, 
                     u32 lkey, u8 log_size)
{
    struct hgrnic_mailbox *mailbox;
    __be32 *inbox;
    int err;

#define RESIZE_CQ_IN_SIZE           0x20
#define RESIZE_CQ_LOG_SIZE_OFFSET   0x00
#define RESIZE_CQ_LKEY_OFFSET       0x04

    mailbox = hgrnic_alloc_mailbox(dev, GFP_KERNEL);
    if (IS_ERR(mailbox))
        return PTR_ERR(mailbox);
    inbox = mailbox->buf;

    memset(inbox, 0, RESIZE_CQ_IN_SIZE);
    /*
     * Leave start address fields zeroed out -- hgrnic assumes that
     * MRs for CQs always start at virtual address 0.
     */
    HGRNIC_PUT(inbox, log_size, RESIZE_CQ_LOG_SIZE_OFFSET);
    HGRNIC_PUT(inbox, lkey,     RESIZE_CQ_LKEY_OFFSET);

    err = hgrnic_cmd(dev, mailbox->dma, cq_num, 1, CMD_RESIZE_CQ,
                    CMD_TIME_CLASS_B);

    hgrnic_free_mailbox(dev, mailbox);
    return err;
}

/**
 * @description: 
 *  Command function.
 *  Send QP context to HCA hardware.
 *  We don't use op_mod == 3, 
 *  because hardware don't support it.
 */
int hgrnic_MODIFY_QP (struct hgrnic_dev *dev, enum ib_qp_state cur, 
        enum ib_qp_state next, u32 num, struct hgrnic_mailbox *mailbox)
{
    /* op[cur][next] */
    static const u16 op[IB_QPS_ERR + 1][IB_QPS_ERR + 1] = {
        [IB_QPS_RESET] = {
            [IB_QPS_RESET]    = CMD_ERR2RST_QPEE,
            [IB_QPS_ERR]    = CMD_2ERR_QPEE,
            [IB_QPS_INIT]    = CMD_RST2INIT_QPEE,
        },
        [IB_QPS_INIT]  = {
            [IB_QPS_RESET]    = CMD_ERR2RST_QPEE,
            [IB_QPS_ERR]    = CMD_2ERR_QPEE,
            [IB_QPS_INIT]    = CMD_INIT2INIT_QPEE,
            [IB_QPS_RTR]    = CMD_INIT2RTR_QPEE,
        },
        [IB_QPS_RTR]   = {
            [IB_QPS_RESET]    = CMD_ERR2RST_QPEE,
            [IB_QPS_ERR]    = CMD_2ERR_QPEE,
            [IB_QPS_RTS]    = CMD_RTR2RTS_QPEE,
        },
        [IB_QPS_RTS]   = {
            [IB_QPS_RESET]    = CMD_ERR2RST_QPEE,
            [IB_QPS_ERR]    = CMD_2ERR_QPEE,
            [IB_QPS_RTS]    = CMD_RTS2RTS_QPEE,
            [IB_QPS_SQD]    = CMD_RTS2SQD_QPEE,
        },
        [IB_QPS_SQD] = {
            [IB_QPS_RESET]    = CMD_ERR2RST_QPEE,
            [IB_QPS_ERR]    = CMD_2ERR_QPEE,
            [IB_QPS_RTS]    = CMD_SQD2RTS_QPEE,
            [IB_QPS_SQD]    = CMD_SQD2SQD_QPEE,
        },
        [IB_QPS_SQE] = {
            [IB_QPS_RESET]    = CMD_ERR2RST_QPEE,
            [IB_QPS_ERR]    = CMD_2ERR_QPEE,
            [IB_QPS_RTS]    = CMD_SQERR2RTS_QPEE,
        },
        [IB_QPS_ERR] = {
            [IB_QPS_RESET]    = CMD_ERR2RST_QPEE,
            [IB_QPS_ERR]    = CMD_2ERR_QPEE,
        }
    };

    u8 op_mod = 0;
    int err;

    err = hgrnic_cmd(dev, mailbox->dma, num,
                    op_mod, op[cur][next], CMD_TIME_CLASS_C);
    return err;
}

/**
 * @description: 
 *  Command function.
 *  Query QP context from HCA hardware.
 */
int hgrnic_QUERY_QP (struct hgrnic_dev *dev, u32 num, 
        struct hgrnic_mailbox *mailbox)
{
    return hgrnic_cmd_box(dev, 0, mailbox->dma, num, 0,
                         CMD_QUERY_QPEE, CMD_TIME_CLASS_A);
}

/**
 * @description: 
 *  Command function.
 *  Configure special QP (QP0, QP1) context to HCA hardware.
 *  !TODO: We haven't implemented this command yet.
 */
int hgrnic_CONF_SPECIAL_QP (struct hgrnic_dev *dev, int type, u32 qpn) {
    u8 op_mod;

    switch (type) {
    case IB_QPT_SMI:
        op_mod = 0;
        break;
    case IB_QPT_GSI:
        op_mod = 1;
        break;
    case IB_QPT_RAW_IPV6:
        op_mod = 2;
        break;
    case IB_QPT_RAW_ETHERTYPE:
        op_mod = 3;
        break;
    default:
        return -EINVAL;
    }

    return hgrnic_cmd(dev, 0, qpn, op_mod, CMD_CONF_SPECIAL_QP,
             CMD_TIME_CLASS_B);
}

/**
 * @description: 
 *  Command function.
 *  Post MAD packet to HCA hardware, and receive MAD packet accordingly.
 *  !TODO: We haven't implemented this command yet.
 */
int hgrnic_MAD_IFC (struct hgrnic_dev *dev, int ignore_mkey, int ignore_bkey,
          int port, const struct ib_wc *in_wc, const struct ib_grh *in_grh,
          const void *in_mad, void *response_mad) {

    struct hgrnic_mailbox *inmailbox, *outmailbox;
    void *inbox;
    int err;
    u32 in_modifier = port;
    u8 op_modifier = 0;

#define MAD_IFC_BOX_SIZE      0x400
#define MAD_IFC_MY_QPN_OFFSET 0x100
#define MAD_IFC_RQPN_OFFSET   0x108
#define MAD_IFC_SL_OFFSET     0x10c
#define MAD_IFC_G_PATH_OFFSET 0x10d
#define MAD_IFC_RLID_OFFSET   0x10e
#define MAD_IFC_PKEY_OFFSET   0x112
#define MAD_IFC_GRH_OFFSET    0x140

    inmailbox = hgrnic_alloc_mailbox(dev, GFP_KERNEL);
    if (IS_ERR(inmailbox))
        return PTR_ERR(inmailbox);
    inbox = inmailbox->buf;

    outmailbox = hgrnic_alloc_mailbox(dev, GFP_KERNEL);
    if (IS_ERR(outmailbox)) {
        hgrnic_free_mailbox(dev, inmailbox);
        return PTR_ERR(outmailbox);
    }

    memcpy(inbox, in_mad, 256);

    /*
     * Key check traps can't be generated unless we have in_wc to
     * tell us where to send the trap.
     */
    if (ignore_mkey || !in_wc)
        op_modifier |= 0x1;
    if (ignore_bkey || !in_wc)
        op_modifier |= 0x2;

    if (in_wc) {
        u8 val;

        memset(inbox + 256, 0, 256);

        HGRNIC_PUT(inbox, in_wc->qp->qp_num, MAD_IFC_MY_QPN_OFFSET);
        HGRNIC_PUT(inbox, in_wc->src_qp,     MAD_IFC_RQPN_OFFSET);

        val = in_wc->sl << 4;
        HGRNIC_PUT(inbox, val,               MAD_IFC_SL_OFFSET);

        val = in_wc->dlid_path_bits |
            (in_wc->wc_flags & IB_WC_GRH ? 0x80 : 0);
        HGRNIC_PUT(inbox, val,               MAD_IFC_G_PATH_OFFSET);

        HGRNIC_PUT(inbox, ib_lid_cpu16(in_wc->slid), MAD_IFC_RLID_OFFSET);
        HGRNIC_PUT(inbox, in_wc->pkey_index, MAD_IFC_PKEY_OFFSET);

        if (in_grh)
            memcpy(inbox + MAD_IFC_GRH_OFFSET, in_grh, 40);

        op_modifier |= 0x4;

        in_modifier |= ib_lid_cpu16(in_wc->slid) << 16;
    }

    err = hgrnic_cmd_box(dev, inmailbox->dma, outmailbox->dma,
                in_modifier, op_modifier,
                CMD_MAD_IFC, CMD_TIME_CLASS_C);

    if (!err)
        memcpy(response_mad, outmailbox->buf, 256);

    hgrnic_free_mailbox(dev, inmailbox);
    hgrnic_free_mailbox(dev, outmailbox);
    return err;
}

/**
 * @description: 
 *  Command function.
 *  This is a test command which does nothing.
 *  !TODO: We haven't implemented this command yet. 
 *  Next, we may test interrupt of HCA hardware.
 */
int hgrnic_NOP (struct hgrnic_dev *dev) {
    return hgrnic_cmd(dev, 0, 0x1f, 0, CMD_NOP, msecs_to_jiffies(100));
}
