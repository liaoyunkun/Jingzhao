/**************************************************************
 * @author Kang Ning<kangning18z@ict.ac.cn>, NCIC, ICT, CAS
 * @date 2021.09.08
 * @file hgrnic_allocator.c
 * @note Queue allocator & resource ID allocator for hgrnic.
 *************************************************************/

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/bitmap.h>

#include "hgrnic_dev.h"

/**
 * @description: Allocate one resource element (UAR, PD, MPT, CQ, QP).
 * Trivial bitmap-based allocator
 */
u32 hgrnic_alloc (struct hgrnic_alloc *alloc)
{
    unsigned long flags;
    u32 obj;

    spin_lock_irqsave(&alloc->lock, flags);

    obj = find_next_zero_bit(alloc->table, alloc->max, alloc->last);
    if (obj >= alloc->max) {
        alloc->top = (alloc->top + alloc->max) & alloc->mask;
        obj = find_first_zero_bit(alloc->table, alloc->max);
    }

    if (obj < alloc->max) {
        alloc->last = obj + 1;
        if (alloc->last == alloc->max)
            alloc->last = 0;

        set_bit(obj, alloc->table);
        obj |= alloc->top;
    } else
        obj = -1;

    spin_unlock_irqrestore(&alloc->lock, flags);

    return obj;
}

void hgrnic_free (struct hgrnic_alloc *alloc, u32 obj)
{
    unsigned long flags;

    obj &= alloc->max - 1;

    spin_lock_irqsave(&alloc->lock, flags);

    clear_bit(obj, alloc->table);
    // alloc->last = min(alloc->last, obj); FIXME: This line is removed because of the BUG on hardware.
    alloc->top = (alloc->top + alloc->max) & alloc->mask;

    spin_unlock_irqrestore(&alloc->lock, flags);
}

/**
 * @note Init hgrnic resources number (including PD, UAR, MPT, EQ, CQ, QP).
 */
int hgrnic_alloc_init (struct hgrnic_alloc *alloc, u32 num, u32 mask) {
	// int i;

	/* num must be a power of 2 */
	if (num != 1 << (ffs(num) - 1)) {
        return -EINVAL;
    }

	alloc->last = 0;
	alloc->top  = 0;
	alloc->max  = num;
	alloc->mask = mask;
	spin_lock_init(&alloc->lock);
	alloc->table = kmalloc_array(BITS_TO_LONGS(num), sizeof(long),
				     GFP_KERNEL);
	if (!alloc->table) {
        return -ENOMEM;
    }

	bitmap_zero(alloc->table, num);

	return 0;
}

/**
 * @note Init hgrnic resources (including PD, UAR, MPT).
 */
void hgrnic_alloc_cleanup (struct hgrnic_alloc *alloc) {
	kfree(alloc->table);
}

/*
 * Array of pointers with lazy allocation of leaf pages.  Callers of
 * _get, _set and _clear methods must use a lock or otherwise
 * serialize access to the array.
 */

#define HGRNIC_ARRAY_MASK (PAGE_SIZE / sizeof (void *) - 1)

/**
 * @note Get Resource (QP, CQ) data struct according to resource id.
 * @param array Resource table
 * @param index Resource id (QPN, CQN)
 * @return Data struct pointer of the resources
 */
void *hgrnic_array_get(struct hgrnic_array *array, int index)
{
    int p = (index * sizeof (void *)) >> PAGE_SHIFT;

    if (array->page_list[p].page)
        return array->page_list[p].page[index & HGRNIC_ARRAY_MASK];
    else
        return NULL;
}

/**
 * @note Set Resource (QP, CQ) data struct according to resource ID.
 * @param array Resource table
 * @param index Resource ID (QPN, CQN)
 * @param value Data struct pointer of the resources
 */
int hgrnic_array_set(struct hgrnic_array *array, int index, void *value)
{
    int p = (index * sizeof (void *)) >> PAGE_SHIFT;

    /* Allocate with GFP_ATOMIC because we'll be called with locks held. */
    if (!array->page_list[p].page)
        array->page_list[p].page = (void **) get_zeroed_page(GFP_ATOMIC);

    if (!array->page_list[p].page)
        return -ENOMEM;

    array->page_list[p].page[index & HGRNIC_ARRAY_MASK] = value;
    ++array->page_list[p].used;

    return 0;
}

void hgrnic_array_clear(struct hgrnic_array *array, int index)
{
    int p = (index * sizeof (void *)) >> PAGE_SHIFT;

    if (--array->page_list[p].used == 0) {
        free_page((unsigned long) array->page_list[p].page);
        array->page_list[p].page = NULL;
    } else
        array->page_list[p].page[index & HGRNIC_ARRAY_MASK] = NULL;

    if (array->page_list[p].used < 0)
        pr_debug("Array %p index %d page %d with ref count %d < 0\n",
                 array, index, p, array->page_list[p].used);
}

/**
 * @note The array is used to find resource data struct according 
 * to resource number (QPN, CQN)
 * @param array resource lookup array.
 * @param nent maximum number of resources.
 */
int hgrnic_array_init (struct hgrnic_array *array, int nent)
{
    int npage = (nent * sizeof (void *) + PAGE_SIZE - 1) / PAGE_SIZE;
    int i;

    array->page_list = kmalloc_array(npage, sizeof(*array->page_list),
                                     GFP_KERNEL);
    if (!array->page_list)
        return -ENOMEM;

    for (i = 0; i < npage; ++i) {
        array->page_list[i].page = NULL;
        array->page_list[i].used = 0;
    }

    return 0;
}

void hgrnic_array_cleanup(struct hgrnic_array *array, int nent)
{
	int i;

	for (i = 0; i < (nent * sizeof (void *) + PAGE_SIZE - 1) / PAGE_SIZE; ++i)
		free_page((unsigned long) array->page_list[i].page);

	kfree(array->page_list);
}

/**
 * Handling for queue buffers -- we allocate a bunch of memory and
 * register it in a memory region at HCA virtual address 0.  If the
 * requested size is > max_direct, we split the allocation into
 * multiple pages, so we don't require too much contiguous memory.
 */
int hgrnic_buf_alloc(struct hgrnic_dev *dev, int size, 
        union hgrnic_buf *buf, int *is_direct, struct hgrnic_pd *pd,
        int hca_write, struct hgrnic_mr *mr)
{
    int err = -ENOMEM;
    int npages, shift;
    u64 *dma_list = NULL;
    dma_addr_t t;
    int i;

    *is_direct = 0;
    npages     = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    shift      = PAGE_SHIFT;

    dma_list = kmalloc_array(npages, sizeof(*dma_list),
                    GFP_KERNEL);
    if (!dma_list)
        return -ENOMEM;

    buf->page_list = kmalloc_array(npages,
                                   sizeof(*buf->page_list),
                                   GFP_KERNEL);
    if (!buf->page_list)
        goto err_out;

    for (i = 0; i < npages; ++i)
        buf->page_list[i].buf = NULL;

    for (i = 0; i < npages; ++i) {
        buf->page_list[i].buf =
            dma_alloc_coherent(&dev->pdev->dev, PAGE_SIZE,
                        &t, GFP_KERNEL);
        if (!buf->page_list[i].buf)
            goto err_free;

        dma_list[i] = t;
        dma_unmap_addr_set(&buf->page_list[i], mapping, t);

        clear_page(buf->page_list[i].buf);
    }

    hgrnic_dump("%d", npages);
    err = hgrnic_mr_alloc_phys(dev, pd->pd_num,
                              dma_list, shift, npages,
                              0, size,
                              HGRNIC_MPT_FLAG_LOCAL_READ |
                              (hca_write ? HGRNIC_MPT_FLAG_LOCAL_WRITE : 0),
                              mr);
    if (err)
        goto err_free;

    kfree(dma_list);

    return 0;

err_free:
    hgrnic_buf_free(dev, size, buf, *is_direct, NULL);

err_out:
    kfree(dma_list);

    return err;
}

void hgrnic_buf_free(struct hgrnic_dev *dev, int size, union hgrnic_buf *buf,
            int is_direct, struct hgrnic_mr *mr)
{
    int i;

    if (mr)
        hgrnic_free_mr(dev, mr);

    if (is_direct)
        dma_free_coherent(&dev->pdev->dev, size, buf->direct.buf,
                          dma_unmap_addr(&buf->direct, mapping));
    else {
        for (i = 0; i < (size + PAGE_SIZE - 1) / PAGE_SIZE; ++i)
            dma_free_coherent(&dev->pdev->dev, PAGE_SIZE,
                              buf->page_list[i].buf,
                              dma_unmap_addr(&buf->page_list[i],
                              mapping));
        kfree(buf->page_list);
    }
}
