/**************************************************************
 * @author Kang Ning<kangning18z@ict.ac.cn>, NCIC, ICT, CAS
 * @date 2021.09.08
 * @file hgrnic_icm.c
 * @note 1. Generate ICM memory map layout for HCA.
 *       2. ICM utilize functions.
 *************************************************************/


#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <asm/page.h>

#include "hgrnic_dev.h"
#include "hgrnic_cmd.h"
#include "hgrnic_icm.h"

enum {
    HGRNIC_RES_QP,
    HGRNIC_RES_CQ,
    HGRNIC_RES_EQ,
    HGRNIC_RES_MPT,
    HGRNIC_RES_MTT,
    HGRNIC_RES_NUM
};

enum {
    HGRNIC_NUM_EQS = 32,
    HGRNIC_NUM_PDS = 1 << 15
};

/*
 * We allocate in as big chunks as we can, up to a maximum of 256 KB
 * per chunk.
 */
enum {
    HGRNIC_ICM_ALLOC_SIZE   = 1 << 18,
    HGRNIC_TABLE_CHUNK_SIZE = 1 << 18
};

static void hgrnic_free_icm_pages (struct hgrnic_dev *dev, 
                                   struct hgrnic_icm_chunk *chunk)
{
    int i;

    if (chunk->nsg > 0) {
        pci_unmap_sg(dev->pdev, chunk->mem, chunk->npages,
                    PCI_DMA_BIDIRECTIONAL);
    }

    for (i = 0; i < chunk->npages; ++i) {
        __free_pages(sg_page(&chunk->mem[i]),
                get_order(chunk->mem[i].length));
    }
}

static void hgrnic_free_icm_coherent (struct hgrnic_dev *dev, 
        struct hgrnic_icm_chunk *chunk) {
    int i;

    for (i = 0; i < chunk->npages; ++i) {
        dma_free_coherent(&dev->pdev->dev, chunk->mem[i].length,
                    lowmem_page_address(sg_page(&chunk->mem[i])),
                    sg_dma_address(&chunk->mem[i]));
    }
}

void hgrnic_free_icm (struct hgrnic_dev *dev, 
        struct hgrnic_icm *icm, int coherent)
{
    struct hgrnic_icm_chunk *chunk, *tmp;

    if (!icm)
        return;

    list_for_each_entry_safe(chunk, tmp, &icm->chunk_list, list) {
        if (coherent)
            hgrnic_free_icm_coherent(dev, chunk);
        else
            hgrnic_free_icm_pages(dev, chunk);

        kfree(chunk);
    }

    kfree(icm);
}

static int hgrnic_alloc_icm_pages (struct scatterlist *mem, 
        int order, gfp_t gfp_mask) {
    struct page *page;

    /*
     * Use __GFP_ZERO because buggy firmware assumes ICM pages are
     * cleared, and subtle failures are seen if they aren't.
     */
    page = alloc_pages(gfp_mask | __GFP_ZERO, order);
    if (!page) {
        return -ENOMEM;
    }

    sg_set_page(mem, page, PAGE_SIZE << order, 0);
    return 0;
}

/**
 * @note Allocate addr for ICM space uses.
 * @param mem is the actual physical address (bus addr) 
 * through which HCA can access the memory.
 */
static int hgrnic_alloc_icm_coherent (struct device *dev, 
        struct scatterlist *mem, int order, gfp_t gfp_mask) {
    void *buf = dma_alloc_coherent(dev, PAGE_SIZE << order, &sg_dma_address(mem),
                       gfp_mask);
    if (!buf) {
        return -ENOMEM;
    }

    /* set ICM space as zero, in case of HW reads 
     * invalid data. */
    memset(buf, 0, PAGE_SIZE << order);

    sg_set_buf(mem, buf, PAGE_SIZE << order);
    BUG_ON(mem->offset);
    sg_dma_len(mem) = PAGE_SIZE << order; /* may be ueless ? */
    return 0;
}

/**
 * @note Allocate npages of pages for ICM space
 * @param npages number of pages to be allocated.
 */
struct hgrnic_icm *hgrnic_alloc_icm (struct hgrnic_dev *dev, int npages,
        gfp_t gfp_mask, int coherent) {
    struct hgrnic_icm *icm;
    struct hgrnic_icm_chunk *chunk = NULL;
    int cur_order;
    int ret;

    /* We use sg_set_buf for coherent allocs, which assumes low memory */
    BUG_ON(coherent && (gfp_mask & __GFP_HIGHMEM));

    icm = kmalloc(sizeof *icm, gfp_mask & ~(__GFP_HIGHMEM | __GFP_NOWARN));
    if (!icm)
        return icm;

    icm->refcount = 0;
    INIT_LIST_HEAD(&icm->chunk_list);

    cur_order = get_order(HGRNIC_ICM_ALLOC_SIZE);

    while (npages > 0) {
        if (!chunk) {
            chunk = kmalloc(sizeof *chunk,
                    gfp_mask & ~(__GFP_HIGHMEM | __GFP_NOWARN));
            if (!chunk)
                goto fail;

            sg_init_table(chunk->mem, HGRNIC_ICM_CHUNK_LEN);
            chunk->npages = 0;
            chunk->nsg    = 0;
            list_add_tail(&chunk->list, &icm->chunk_list);
        }

        while (1 << cur_order > npages)
            --cur_order;

        if (coherent) {
            ret = hgrnic_alloc_icm_coherent(&dev->pdev->dev,
                    &chunk->mem[chunk->npages],
                    cur_order, gfp_mask);
        } else {
            ret = hgrnic_alloc_icm_pages(&chunk->mem[chunk->npages],
                    cur_order, gfp_mask);
        }

        if (!ret) {
            ++chunk->npages;

            if (coherent) {
                ++chunk->nsg;
            } else if (chunk->npages == HGRNIC_ICM_CHUNK_LEN) {
                chunk->nsg = pci_map_sg(dev->pdev, chunk->mem,
                            chunk->npages,
                            PCI_DMA_BIDIRECTIONAL);

                if (chunk->nsg <= 0)
                    goto fail;
            }

            if (chunk->npages == HGRNIC_ICM_CHUNK_LEN)
                chunk = NULL;

            npages -= 1 << cur_order;
        } else {
            --cur_order;
            if (cur_order < 0)
                goto fail;
        }
    }
    
    if (!coherent && chunk) {
        chunk->nsg = pci_map_sg(dev->pdev, chunk->mem,
                    chunk->npages,
                    PCI_DMA_BIDIRECTIONAL);

        if (chunk->nsg <= 0)
            goto fail;
    }

    return icm;

fail:
    hgrnic_free_icm(dev, icm, coherent);
    return NULL;
}

/**
 * @note Allocate physical address of ICM space, and 
 * write ICM memory map (ICM virt addr : phy addr) 
 * into HCA.
 * @param table Resource table that records ICM usage.
 * @param obj the start addr of one chunk of resource
 * @param type_sel type of the resource
 */
int hgrnic_reg_icm (struct hgrnic_dev *dev, 
                   struct hgrnic_icm_table *table, 
                   int obj, u8 type_sel)
{
    int i = (obj & (table->num_obj - 1)) * table->obj_size / HGRNIC_TABLE_CHUNK_SIZE;
    int ret = 0;

    mutex_lock(&table->mutex);

    /** 
     * Since ICM space is allocated at the grantuarlity of Chunk, 
     * the obj may have been allocated before.
     */
    if (table->icm[i]) { 
        ++table->icm[i]->refcount;
        goto out;
    }

    /* Get ICM memory from kernel */
    table->icm[i] = hgrnic_alloc_icm(dev, HGRNIC_TABLE_CHUNK_SIZE >> PAGE_SHIFT,
                    (table->lowmem ? GFP_KERNEL : GFP_HIGHUSER) |
                    __GFP_NOWARN, table->coherent);
    if (!table->icm[i]) {
        ret = -ENOMEM;
        goto out;
    }

    /* Write ICM memory map to HCA. */
    if (hgrnic_MAP_ICM(dev, table->icm[i], type_sel, 
            table->virt + i * HGRNIC_TABLE_CHUNK_SIZE)) {
        hgrnic_free_icm(dev, table->icm[i], table->coherent);
        table->icm[i] = NULL;
        ret = -ENOMEM;
        goto out;
    }

    ++table->icm[i]->refcount;

out:
    mutex_unlock(&table->mutex);
    return ret;
}

/**
 * @note unmap ICM addr space in HCA hardware and free
 * corresponding ICM table entries.
 */
void hgrnic_unreg_icm (struct hgrnic_dev *dev, 
                      struct hgrnic_icm_table *table, int obj, u8 type_sel)
{
    int i;

    i = (obj & (table->num_obj - 1)) * table->obj_size / HGRNIC_TABLE_CHUNK_SIZE;

    mutex_lock(&table->mutex);

    if (--table->icm[i]->refcount == 0) {
        hgrnic_UNMAP_ICM(dev, table->virt + i * HGRNIC_TABLE_CHUNK_SIZE,
                HGRNIC_TABLE_CHUNK_SIZE / HGRNIC_ICM_PAGE_SIZE, type_sel);
        hgrnic_free_icm(dev, table->icm[i], table->coherent);
        table->icm[i] = NULL;
    }

    mutex_unlock(&table->mutex);
}


/**
 * @note This function does the following things: 
 * 1. Allocate a range of ICM space, and
 * 2. write its map to HCA hardware.
 * @param start start of ICM resource index
 * @param end   end of ICM resource index
 */
int hgrnic_reg_icm_range(struct hgrnic_dev *dev, 
        struct hgrnic_icm_table *table, int start, int end, u8 type_sel)
{
    int inc = HGRNIC_TABLE_CHUNK_SIZE / table->obj_size;
    int i, err;

    for (i = start; i <= end; i += inc) {
        err = hgrnic_reg_icm(dev, table, i, type_sel);
        if (err)
            goto fail;
    }

    return 0;

fail:
    while (i > start) {
        i -= inc;
        hgrnic_unreg_icm(dev, table, i, type_sel);
    }

    return err;
}

void hgrnic_unreg_icm_range (struct hgrnic_dev *dev, 
        struct hgrnic_icm_table *table, int start, int end, u8 type_sel)
{
    int i;

    for (i = start; i <= end; 
            i += HGRNIC_TABLE_CHUNK_SIZE / table->obj_size) {
        hgrnic_unreg_icm(dev, table, i, type_sel);
    }
}

/**
 * @description:
 * Initialize ICM table in software, who stores meta data for HCA resources
 * stored in ICM space (like QP, CQ, MPT, MTT).
 */
struct hgrnic_icm_table *hgrnic_alloc_icm_table (struct hgrnic_dev *dev, 
        u64 virt, int obj_size, int nobj, int reserved, int use_lowmem, 
        int use_coherent, u8 type_sel) {
	struct hgrnic_icm_table *table;
	int obj_per_chunk;
	int num_icm;
	unsigned chunk_size;
	int i;

    if (obj_size == 0 || nobj == 0) {
        hgrnic_err(dev, "Error! obj_size or nobj is zero.\n");
        return NULL;
    }
	obj_per_chunk = HGRNIC_TABLE_CHUNK_SIZE / obj_size;
	num_icm = DIV_ROUND_UP(nobj, obj_per_chunk);

	table = kmalloc(struct_size(table, icm, num_icm), GFP_KERNEL);
	if (!table) {
        return NULL;
    }
	table->virt     = virt;
	table->num_icm  = num_icm;
	table->num_obj  = nobj;
	table->obj_size = obj_size;
	table->lowmem   = use_lowmem;
	table->coherent = use_coherent;
	mutex_init(&table->mutex);

	for (i = 0; i < num_icm; ++i) {
        table->icm[i] = NULL;
    }

    /**
     * We currently set reserved size as 0, 
     * so this code block is useless. 
     * !TODO: We may delete it in later version.
     */
	for (i = 0; i * HGRNIC_TABLE_CHUNK_SIZE < reserved * obj_size; ++i) {
		chunk_size = HGRNIC_TABLE_CHUNK_SIZE;
		if ((i + 1) * HGRNIC_TABLE_CHUNK_SIZE > nobj * obj_size) {
            chunk_size = nobj * obj_size - i * HGRNIC_TABLE_CHUNK_SIZE;
        }

		table->icm[i] = hgrnic_alloc_icm(dev, chunk_size >> PAGE_SHIFT,
						(use_lowmem ? GFP_KERNEL : GFP_HIGHUSER) |
						__GFP_NOWARN, use_coherent);
		if (!table->icm[i]) {
            goto err;
        }
        printk(KERN_INFO PFX "Start hgrnic_MAP_ICM.\n");
		if (hgrnic_MAP_ICM(dev, table->icm[i], type_sel, 
				    virt + i * HGRNIC_TABLE_CHUNK_SIZE)) {
			hgrnic_free_icm(dev, table->icm[i], table->coherent);
			table->icm[i] = NULL;
			goto err;
		}


		/*
		 * Add a reference to this ICM chunk so that it never
		 * gets freed (since it contains reserved firmware objects).
		 */
		++table->icm[i]->refcount;
	}

	return table;

err:
	for (i = 0; i < num_icm; ++i) {
        if (table->icm[i]) {
			hgrnic_UNMAP_ICM(dev, virt + i * HGRNIC_TABLE_CHUNK_SIZE,
					HGRNIC_TABLE_CHUNK_SIZE / HGRNIC_ICM_PAGE_SIZE, type_sel);
			hgrnic_free_icm(dev, table->icm[i], table->coherent);
		}
    }

	kfree(table);

	return NULL;
}

void hgrnic_free_icm_table (struct hgrnic_dev *dev, 
        struct hgrnic_icm_table *table, u8 type_sel) {
	int i;

	for (i = 0; i < table->num_icm; ++i) {
        if (table->icm[i]) {
			hgrnic_UNMAP_ICM(dev,
					table->virt + i * HGRNIC_TABLE_CHUNK_SIZE,
					HGRNIC_TABLE_CHUNK_SIZE / HGRNIC_ICM_PAGE_SIZE, type_sel);
			hgrnic_free_icm(dev, table->icm[i], table->coherent);
		}
    }

	kfree(table);
}



s64 hgrnic_make_profile (struct hgrnic_dev *dev, 
        struct hgrnic_profile *request, struct hgrnic_dev_lim *dev_lim, 
        struct hgrnic_init_hca_param *init_hca) {
	struct hgrnic_resource {
		u64 size ;  // size of every resource region (in bytes)
		u64 start;
		int type ;
		int num  ;  // number of entries
		int log_num;
	};

	u64 mem_base, mem_avail;
	s64 total_size = 0;
	struct hgrnic_resource *profile;
	int i, j;

	profile = kcalloc(HGRNIC_RES_NUM, sizeof(*profile), GFP_KERNEL);
	if (!profile) {
        return -ENOMEM;   
    }

	profile[HGRNIC_RES_QP].size   = dev_lim->qpc_entry_sz;
	profile[HGRNIC_RES_CQ].size   = dev_lim->cqc_entry_sz;
	profile[HGRNIC_RES_EQ].size   = dev_lim->eqc_entry_sz;
	profile[HGRNIC_RES_MPT].size  = dev_lim->mpt_entry_sz;
	profile[HGRNIC_RES_MTT].size  = dev_lim->max_mtt_seg ;
	
	profile[HGRNIC_RES_QP].num    = request->num_qp;
	profile[HGRNIC_RES_CQ].num    = request->num_cq;
	profile[HGRNIC_RES_EQ].num    = HGRNIC_NUM_EQS;
	profile[HGRNIC_RES_MPT].num   = request->num_mpt;
	profile[HGRNIC_RES_MTT].num   = request->num_mtt;
	
	for (i = 0; i < HGRNIC_RES_NUM; ++i) {
		profile[i].type     = i;
		profile[i].log_num  = max(ffs(profile[i].num) - 1, 0);
		profile[i].size    *= profile[i].num;
		profile[i].size = max(profile[i].size, (u64) PAGE_SIZE);
	}
    
    mem_base  = 0;
    mem_avail = dev_lim->max_icm_sz;

	/*
	 * Sort the resources in decreasing order of size.  Since they
	 * all have sizes that are powers of 2, we'll be able to keep
	 * resources aligned to their size and pack them without gaps
	 * using the sorted order.
	 */
	for (i = HGRNIC_RES_NUM; i > 0; --i)
		for (j = 1; j < i; ++j) {
			if (profile[j].size > profile[j - 1].size)
				swap(profile[j], profile[j - 1]);
		}

	for (i = 0; i < HGRNIC_RES_NUM; ++i) {
		if (profile[i].size) {
			profile[i].start = mem_base + total_size;
			total_size      += profile[i].size;
		}
		if (total_size > mem_avail) {
			hgrnic_err(dev, "Profile requires 0x%llx bytes; "
				  "won't fit in 0x%llx bytes of context memory.\n",
				  (unsigned long long) total_size,
				  (unsigned long long) mem_avail);
			kfree(profile);
			return -ENOMEM;
		}

		if (profile[i].size)
			printk(KERN_INFO PFX "profile[%2d]--%2d/%2d @ 0x%16llx "
				  "(size 0x%8llx)\n",
				  i, profile[i].type, profile[i].log_num,
				  (unsigned long long) profile[i].start,
				  (unsigned long long) profile[i].size);
	}

	printk(KERN_INFO PFX "HCA context memory: reserving %d KB\n",
			  (int) (total_size >> 10));

	for (i = 0; i < HGRNIC_RES_NUM; ++i) {
		switch (profile[i].type) {
		case HGRNIC_RES_QP:
			dev->limits.num_qps   = profile[i].num;
			init_hca->qpc_base    = profile[i].start;
			init_hca->log_num_qps = profile[i].log_num;
			break;
		case HGRNIC_RES_CQ:
			dev->limits.num_cqs   = profile[i].num;
			init_hca->cqc_base    = profile[i].start;
			init_hca->log_num_cqs = profile[i].log_num;
			break;
		case HGRNIC_RES_EQ:
			dev->limits.num_eqs   = profile[i].num;
			init_hca->eqc_base    = profile[i].start;
			init_hca->log_num_eqs = profile[i].log_num;
			break;
		case HGRNIC_RES_MPT:
			dev->limits.num_mpts   = profile[i].num;
			dev->mr_table.mpt_base = profile[i].start;
			init_hca->mpt_base     = profile[i].start;
			init_hca->log_mpt_sz   = profile[i].log_num;
			break;
		case HGRNIC_RES_MTT:
			dev->limits.num_mtt_segs = profile[i].num;
			dev->mr_table.mtt_base   = profile[i].start;
			init_hca->mtt_base       = profile[i].start;
			break;
		default:
			break;
		}
	}

	/*
	 * PDs don't take any HCA memory, but we assign them as part
	 * of the HCA profile anyway.
	 */
	dev->limits.num_pds = HGRNIC_NUM_PDS;

	kfree(profile);
	return total_size;
}
