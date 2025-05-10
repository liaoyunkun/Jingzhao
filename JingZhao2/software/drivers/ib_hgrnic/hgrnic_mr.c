/**************************************************************
 * @author Kang Ning<kangning18z@ict.ac.cn>, NCIC, ICT, CAS
 * @date 2021.09.08
 * @file hgrnic_mr.c
 * @note Functions related to Memory region.
 *************************************************************/

#include <linux/slab.h>
#include <linux/errno.h>

#include "hgrnic_dev.h"
#include "hgrnic_cmd.h"
#include "hgrnic_icm.h"

/*
 * Must be packed because mtt_seg is 64 bits but only aligned to 32 bits.
 */
struct hgrnic_mpt_entry {
    __be32 flags;
    __be32 page_size;
    __be32 key;
    __be32 pd;
    __be64 start;
    __be64 length;
    __be32 lkey;
    __be32 window_count;
    __be32 window_count_limit;
    __be64 mtt_seg;
    __be32 mtt_sz;
    u32    reserved[2];
} __packed;

#define HGRNIC_MPT_FLAG_SW_OWNS       (0xfUL << 28)
#define HGRNIC_MPT_FLAG_MIO           (1 << 17)
#define HGRNIC_MPT_FLAG_BIND_ENABLE   (1 << 15)
#define HGRNIC_MPT_FLAG_PHYSICAL      (1 <<  9)
#define HGRNIC_MPT_FLAG_REGION        (1 <<  8)

#define HGRNIC_MTT_FLAG_PRESENT       1

#define HGRNIC_MPT_STATUS_SW 0xF0
#define HGRNIC_MPT_STATUS_HW 0x00

/*
 * Buddy allocator for MTT segments (currently not very efficient
 * since it doesn't keep a free list and just searches linearly
 * through the bitmaps)
 */
static u32 hgrnic_buddy_alloc (struct hgrnic_buddy *buddy, int order)
{
    int o;
    int m; /* Number of elements in bits[o] array */
    u32 seg;

    spin_lock(&buddy->lock);

    for (o = order; o <= buddy->max_order; ++o)
        if (buddy->num_free[o]) {
            m = 1 << (buddy->max_order - o);
            seg = find_first_bit(buddy->bits[o], m);
            if (seg < m) /* if seg == m, not find set bit */
                goto found;
        }

    spin_unlock(&buddy->lock);
    return -1;

 found:
    clear_bit(seg, buddy->bits[o]);
    --buddy->num_free[o];

    while (o > order) {
        --o;
        seg <<= 1;
        set_bit(seg ^ 1, buddy->bits[o]); // ^ 1: same as + 1
        ++buddy->num_free[o];
    }

    spin_unlock(&buddy->lock);

    seg <<= order;

    return seg;
}

static void hgrnic_buddy_free(struct hgrnic_buddy *buddy, u32 seg, int order)
{
    seg >>= order;

    spin_lock(&buddy->lock);

    while (test_bit(seg ^ 1, buddy->bits[order])) {
        clear_bit(seg ^ 1, buddy->bits[order]);
        --buddy->num_free[order];
        seg >>= 1;
        ++order;
    }

    set_bit(seg, buddy->bits[order]);
    ++buddy->num_free[order];

    spin_unlock(&buddy->lock);
}

/**
 * @note Init buddy allocator for mtt resources.
 */ 
static int hgrnic_buddy_init (struct hgrnic_buddy *buddy, int max_order)
{
    int i, s;

    buddy->max_order = max_order;
    spin_lock_init(&buddy->lock);

    buddy->bits = kcalloc(buddy->max_order + 1, sizeof(long *),
                          GFP_KERNEL);
    buddy->num_free = kcalloc((buddy->max_order + 1), sizeof *buddy->num_free,
                              GFP_KERNEL);
    if (!buddy->bits || !buddy->num_free)
        goto err_out;

    for (i = 0; i <= buddy->max_order; ++i) {
        s = BITS_TO_LONGS(1 << (buddy->max_order - i));
        buddy->bits[i] = kmalloc_array(s, sizeof(long), GFP_KERNEL);
        if (!buddy->bits[i])
            goto err_out_free;

        bitmap_zero(buddy->bits[i],
                1 << (buddy->max_order - i));
    }

    set_bit(0, buddy->bits[buddy->max_order]);
    buddy->num_free[buddy->max_order] = 1;

    return 0;

err_out_free:
    for (i = 0; i <= buddy->max_order; ++i)
        kfree(buddy->bits[i]);

err_out:
    kfree(buddy->bits);
    kfree(buddy->num_free);

    return -ENOMEM;
}

/**
 * @note Clean up buddy allocator for mtt resources.
 */
static void hgrnic_buddy_cleanup (struct hgrnic_buddy *buddy)
{
    int i;

    for (i = 0; i <= buddy->max_order; ++i)
        kfree(buddy->bits[i]);

    kfree(buddy->bits);
    kfree(buddy->num_free);
}

/**
 * @note This Func completes the following things: 
 * 1. Allocate a range of ICM virtual addr for mtt,
 * 2. Allocate memories associated with ICM virtual addr, and 
 * 3. Write ICM addr mapping for MTT resources.
 * @param order log_size of allocated mtt entry
 * @return start index of MTT resources. (log_len is order)
 */
static u32 hgrnic_alloc_mtt_range (struct hgrnic_dev *dev, int order, 
                                  struct hgrnic_buddy *buddy)
{
    u32 seg = hgrnic_buddy_alloc(buddy, order);
    if (seg == -1)
        return -1;

    if (hgrnic_reg_icm_range(dev, dev->mr_table.mtt_table, seg,
            seg + (1 << order) - 1, TPT_REGION)) {
        hgrnic_buddy_free(buddy, seg, order);
        seg = -1;
    }

    return seg;
}

/**
 * @note Allocate Space (ICM space) for MTT storage.
 * This function Write ICM to HCA. The actually 
 * allocated size may not be param size.
 *
 * @param size The required size, the actual allocated 
 * size must be the power of 2.
 */
static struct hgrnic_mtt *__hgrnic_alloc_mtt (struct hgrnic_dev *dev, 
                                            int size, 
                                            struct hgrnic_buddy *buddy)
{
    struct hgrnic_mtt *mtt;
    int i;

    if (size <= 0)
        return ERR_PTR(-EINVAL);

    mtt = kmalloc(sizeof *mtt, GFP_KERNEL);
    if (!mtt)
        return ERR_PTR(-ENOMEM);

    // Change size into log num
    mtt->buddy = buddy;
    mtt->order = 0;
    for (i = 1; i < size; i <<= 1) // dev->limits.mtt_seg_size / 8
        ++mtt->order;

    mtt->first_index = hgrnic_alloc_mtt_range(dev, mtt->order, buddy);
    if (mtt->first_index == -1) {
        kfree(mtt);
        return ERR_PTR(-ENOMEM);
    }
    
    return mtt;
}

/**
 * @note Allocate MTT from mtt_buddy table
 */
struct hgrnic_mtt *hgrnic_alloc_mtt (struct hgrnic_dev *dev, int size)
{
    return __hgrnic_alloc_mtt(dev, size, &dev->mr_table.mtt_buddy);
}

/**
 * @note Release MTT entries from MTT table (mtt_buddy).
 */
void hgrnic_free_mtt (struct hgrnic_dev *dev, struct hgrnic_mtt *mtt)
{
    if (!mtt)
        return;

    hgrnic_buddy_free(mtt->buddy, mtt->first_index, mtt->order);

    hgrnic_unreg_icm_range(dev, dev->mr_table.mtt_table,
                          mtt->first_index,
                          mtt->first_index + (1 << mtt->order) - 1, TPT_REGION);

    kfree(mtt);
}

/**
 * @note Write MTT entry into HCA. This function includes
 * data layout in mailbox of MTT.
 * @param start_index start mtt index
 * @param list_len MTT entry number
 */
static int __hgrnic_write_mtt (struct hgrnic_dev *dev, struct hgrnic_mtt *mtt,
                              int start_index, u64 *buffer_list, int list_len)
{
    struct hgrnic_mailbox *mailbox;
    __be64 *mtt_entry;
    int err = 0;
    int i;

    mailbox = hgrnic_alloc_mailbox(dev, GFP_KERNEL);
    if (IS_ERR(mailbox)) {
        return PTR_ERR(mailbox);
    }
    mtt_entry = mailbox->buf;

    while (list_len > 0) {
        mtt_entry[0] = 0;
        mtt_entry[1] = 0;
        mtt_entry[2] = 0;
        mtt_entry[3] = cpu_to_be64(start_index);

        printk(KERN_INFO PFX "start_index: 0x%x\n", start_index);
        for (i = 0; i < list_len && i < HGRNIC_MAILBOX_SIZE / 8 - 4; i+=4) {
            mtt_entry[i + 4] = (i+3) < list_len ? 
                                cpu_to_be64(buffer_list[i+3]) : 0;
            mtt_entry[i + 5] = (i+2) < list_len ? 
                                cpu_to_be64(buffer_list[i+2]) : 0;
            mtt_entry[i + 6] = (i+1) < list_len ? 
                                cpu_to_be64(buffer_list[i+1]) : 0;
            mtt_entry[i + 7] = cpu_to_be64(buffer_list[i]);
            printk(KERN_INFO PFX "mtt_entry: 0x%llx 0x%llx 0x%llx 0x%llx\n", 
            be64_to_cpu(mtt_entry[i + 4]), 
            be64_to_cpu(mtt_entry[i + 5]), 
            be64_to_cpu(mtt_entry[i + 6]), 
            be64_to_cpu(mtt_entry[i + 7]));

        }

        err = hgrnic_WRITE_MTT(dev, mailbox, list_len); // i + 4
        if (err) {
            hgrnic_warn(dev, "WRITE_MTT failed (%d)\n", err);
            goto out;
        }

        list_len    -= i;
        start_index += i;
        buffer_list += i;
    }

out:
    hgrnic_free_mailbox(dev, mailbox);
    return err;
}

int hgrnic_write_mtt_size(struct hgrnic_dev *dev)
{
    /* All MTTs must fit in the same page. */
    return (PAGE_SIZE / sizeof (u64));
}


/**
 * @note Write MTT to HCA.
 */
int hgrnic_write_mtt (struct hgrnic_dev *dev, struct hgrnic_mtt *mtt,
                     int start_index, u64 *buffer_list, int list_len)
{
    return __hgrnic_write_mtt(dev, mtt, start_index, buffer_list, list_len);
}


/**
 * @note Allocate memory region.
 * 1. Allocate MPT and write ICM mapping to HCA;
 * 2. Write MPT entry to HCA.
 *
 * @param buffer_size_shift page size the memory region owns.
 * @param iova start virt addr of MR
 * @param total_size total size of MR
 * @param mr MTT may be included here
 */
int hgrnic_mr_alloc (struct hgrnic_dev *dev, u32 pd, int buffer_size_shift,
                    u64 iova, u64 total_size, u32 access, struct hgrnic_mr *mr)
{
    struct hgrnic_mailbox *mailbox;
    struct hgrnic_mpt_entry *mpt_entry;
    u32 key;
    int err;


    WARN_ON(buffer_size_shift >= 32);

    key = hgrnic_alloc(&dev->mr_table.mpt_alloc);
    if (key == -1)
        return -ENOMEM;

    mr->ibmr.rkey = mr->ibmr.lkey = key;

    err = hgrnic_reg_icm(dev, dev->mr_table.mpt_table, 
                        key, TPT_REGION);
    if (err)
        goto err_out_mpt_free;


    mailbox = hgrnic_alloc_mailbox(dev, GFP_KERNEL);
    if (IS_ERR(mailbox)) {
        err = PTR_ERR(mailbox);
        goto err_out_table;
    }
    mpt_entry = mailbox->buf;

    mpt_entry->flags = cpu_to_be32(HGRNIC_MPT_FLAG_SW_OWNS |
                                   HGRNIC_MPT_FLAG_MIO     |
                                   HGRNIC_MPT_FLAG_REGION  |
                                   access);

    mpt_entry->page_size = cpu_to_be32(1 << buffer_size_shift); /* !TODO: Checkout this variable */
    mpt_entry->key       = cpu_to_be32(key);
    mpt_entry->pd        = cpu_to_be32(pd);
    mpt_entry->start     = cpu_to_be64(iova);
    mpt_entry->length    = cpu_to_be64(total_size);

    memset(&mpt_entry->lkey, 0,
           sizeof *mpt_entry - offsetof(struct hgrnic_mpt_entry, lkey));

    if (mr->mtt)
        mpt_entry->mtt_seg = cpu_to_be64(mr->mtt->first_index);

    err = hgrnic_SW2HW_MPT(dev, mailbox,
                          key & (dev->limits.num_mpts - 1));
    if (err) {
        hgrnic_warn(dev, "SW2HW_MPT failed (%d)\n", err);
        goto err_out_mailbox;
    }
    hgrnic_dump("0x%x",   be32_to_cpu(mpt_entry->flags    ));
    hgrnic_dump("0x%x",   be32_to_cpu(mpt_entry->page_size));
    hgrnic_dump("0x%x",   be32_to_cpu(mpt_entry->key      ));
    hgrnic_dump("0x%x",   be32_to_cpu(mpt_entry->pd       ));
    hgrnic_dump("0x%llx", be64_to_cpu(mpt_entry->start    ));
    hgrnic_dump("0x%llx", be64_to_cpu(mpt_entry->length   ));
    hgrnic_dump("0x%llx", be64_to_cpu(mpt_entry->mtt_seg  ));

    hgrnic_free_mailbox(dev, mailbox);
    return err;

err_out_mailbox:
    hgrnic_free_mailbox(dev, mailbox);

err_out_table:
    hgrnic_unreg_icm(dev, dev->mr_table.mpt_table, key, TPT_REGION);

err_out_mpt_free:
    hgrnic_free(&dev->mr_table.mpt_alloc, key);
    return err;
}

/**
 * @note Allocate Memory (no translation).
 */
int hgrnic_mr_alloc_notrans (struct hgrnic_dev *dev, 
                             u32 pd, u32 access, 
                             struct hgrnic_mr *mr)
{
    mr->mtt = NULL;
    return hgrnic_mr_alloc(dev, pd, 12, 0, ~0ULL, access, mr);
}

/**
 * @note Allocate Memory (With mtt).
 * 
 * @param buffer_size_shift Size of page in log for 
 * this MR (MPT entry)
 */
int hgrnic_mr_alloc_phys (struct hgrnic_dev *dev, u32 pd,
                          u64 *buffer_list, int buffer_size_shift,
                          int list_len, u64 iova, u64 total_size,
                          u32 access, struct hgrnic_mr *mr)
{
    int err;

    mr->mtt = hgrnic_alloc_mtt(dev, list_len);
    if (IS_ERR(mr->mtt))
        return PTR_ERR(mr->mtt);

    /* Write MTT -> HCA */
    err = hgrnic_write_mtt(dev, mr->mtt, mr->mtt->first_index, buffer_list, list_len);
    printk(KERN_INFO PFX "hgrnic_write_mtt: err %d\n", err);
    if (err)
        goto free_mtt;

    /* Allocate MPT && (Write MPT -> HCA) */
    err = hgrnic_mr_alloc(dev, pd, buffer_size_shift, iova,
                         total_size, access, mr);
    printk(KERN_INFO PFX "hgrnic_mr_alloc: mr->lkey 0x%x, 0x%x\n", mr->ibmr.lkey, mr->mtt->first_index);
    if (err)
        goto free_mtt;

    return 0;

free_mtt:
    hgrnic_free_mtt(dev, mr->mtt);
    return err;
}

/* Free mr id and its ICM space */
static void hgrnic_free_region (struct hgrnic_dev *dev, u32 lkey)
{
    hgrnic_unreg_icm(dev, dev->mr_table.mpt_table,
                    lkey, TPT_REGION);

    hgrnic_free(&dev->mr_table.mpt_alloc, lkey);
}

/**
 * @note 
 * 1. Write MPT cancel to HCA; 
 * 2. Free reated MPT resource and unmmap cooresponding ICM space;
 * 3. Free related mtt resource and unmmap cooresponding ICM space.
 */
void hgrnic_free_mr(struct hgrnic_dev *dev, struct hgrnic_mr *mr)
{
    int err;

    err = hgrnic_HW2SW_MPT(dev, 
                          mr->ibmr.lkey & (dev->limits.num_mpts - 1));
    if (err)
        hgrnic_warn(dev, "HW2SW_MPT failed (%d)\n", err);

    hgrnic_free_region(dev, mr->ibmr.lkey);
    hgrnic_free_mtt(dev, mr->mtt);
}

/**
 * @note Initialize Memory region resources (MPT & MTT).
 * 1. Init resource table.
 */
int hgrnic_init_mr_table (struct hgrnic_dev *dev)
{
    int err;

    err = hgrnic_alloc_init(&dev->mr_table.mpt_alloc,
                            dev->limits.num_mpts,
                            ~0);
    if (err)
        return err;

    err = hgrnic_buddy_init(&dev->mr_table.mtt_buddy,
                            fls(dev->limits.num_mtt_segs - 1));
    if (err)
        goto err_mtt_buddy;

    return 0;

err_mtt_buddy:
    hgrnic_alloc_cleanup(&dev->mr_table.mpt_alloc);

    return err;
}

void hgrnic_cleanup_mr_table(struct hgrnic_dev *dev)
{
    hgrnic_buddy_cleanup(&dev->mr_table.mtt_buddy);
    hgrnic_alloc_cleanup(&dev->mr_table.mpt_alloc);
}
