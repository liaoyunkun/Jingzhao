/**************************************************************
 * @author Kang Ning<kangning18z@ict.ac.cn>, NCIC, ICT, CAS
 * @date 2021.09.08
 * @file hgrnic_icm.h
 * @note Generate ICM memory map layout for HCA (INIT_HCA would send this layout).
 *************************************************************/

#ifndef __HGRNIC_ICM_H__
#define __HGRNIC_ICM_H__

#include <linux/list.h>
#include <linux/mutex.h>

#include "hgrnic_dev.h"
#include "hgrnic_cmd.h"


#define HGRNIC_ICM_CHUNK_LEN \
    ((256 - sizeof (struct list_head) - 2 * sizeof (int)) /		\
     (sizeof (struct scatterlist)))

enum {
    HGRNIC_ICM_PAGE_SHIFT   = 12,
    HGRNIC_ICM_PAGE_SIZE    = 1 << HGRNIC_ICM_PAGE_SHIFT
};

struct hgrnic_profile {
    int num_qp;
    int num_cq;
    int num_mpt;
    int num_mtt;
};

struct hgrnic_icm_chunk {
    struct list_head   list;
    int                npages; /* number of entries of sg list (before merge) */
    int                nsg;    /* number of entries of sg list (after merge). */
    struct scatterlist mem[HGRNIC_ICM_CHUNK_LEN];
};

struct hgrnic_icm {
    struct list_head chunk_list;
    int              refcount;
};

struct hgrnic_icm_table {
    u64               virt    ; /* start virtual address (ICM space) for this icm resource */
    int               num_icm ; /* Number of chunks allocated for this resources in ICM. */
    int               num_obj ; /* Number of entries for this kind of resource.(in power of 2) */
    int               obj_size; /* Size of the resource (in bytes). */
    int               lowmem  ; /* If this ICM space is in low memory (It seems that
                                 * we don't need to care about it). */
    int               coherent;
    struct mutex      mutex   ;
    struct hgrnic_icm *icm[0]  ; /* Address list for ICM space, 
                                 * one elem in the array stands for the size of one chunk. */
};

struct hgrnic_icm_iter {
    struct hgrnic_icm       *icm;
    struct hgrnic_icm_chunk *chunk;
    int                     page_idx;
};

struct hgrnic_dev;
struct hgrnic_dev_lim;
struct hgrnic_init_hca_param;


s64 hgrnic_make_profile(struct hgrnic_dev *mdev, struct hgrnic_profile *request,
        struct hgrnic_dev_lim *dev_lim, struct hgrnic_init_hca_param *init_hca);

struct hgrnic_icm *hgrnic_alloc_icm(struct hgrnic_dev *dev, int npages,
                                    gfp_t gfp_mask, int coherent);
void hgrnic_free_icm(struct hgrnic_dev *dev, struct hgrnic_icm *icm, int coherent);

struct hgrnic_icm_table *hgrnic_alloc_icm_table (struct hgrnic_dev *dev, 
        u64 virt, int obj_size, int nobj, int reserved, int use_lowmem, 
        int use_coherent, u8 type_sel);
void hgrnic_free_icm_table (struct hgrnic_dev *dev, 
        struct hgrnic_icm_table *table, u8 type_sel);
int hgrnic_reg_icm(struct hgrnic_dev *dev, 
        struct hgrnic_icm_table *table, int obj, u8 type_sel);
void hgrnic_unreg_icm (struct hgrnic_dev *dev, 
        struct hgrnic_icm_table *table, int obj, u8 type_sel);
int hgrnic_reg_icm_range(struct hgrnic_dev *dev, 
        struct hgrnic_icm_table *table, int start, int end, u8 type_sel);
void hgrnic_unreg_icm_range(struct hgrnic_dev *dev, 
        struct hgrnic_icm_table *table, int start, int end, u8 type_sel);

static inline void hgrnic_icm_first (struct hgrnic_icm *icm,
				   struct hgrnic_icm_iter *iter) {
	iter->icm      = icm;
	iter->chunk    = list_empty(&icm->chunk_list) ?
		NULL : list_entry(icm->chunk_list.next,
				  struct hgrnic_icm_chunk, list);
	iter->page_idx = 0;
}

static inline int hgrnic_icm_last (struct hgrnic_icm_iter *iter) {
	return !iter->chunk;
}

static inline void hgrnic_icm_next (struct hgrnic_icm_iter *iter) {
	if (++iter->page_idx >= iter->chunk->nsg) {
		if (iter->chunk->list.next == &iter->icm->chunk_list) {
			iter->chunk = NULL;
			return;
		}

		iter->chunk = list_entry(iter->chunk->list.next,
					 struct hgrnic_icm_chunk, list);
		iter->page_idx = 0;
	}
}

static inline dma_addr_t hgrnic_icm_addr (struct hgrnic_icm_iter *iter) {
	return sg_dma_address(&iter->chunk->mem[iter->page_idx]);
}

static inline unsigned long hgrnic_icm_size (
        struct hgrnic_icm_iter *iter) {
	return sg_dma_len(&iter->chunk->mem[iter->page_idx]); // "byte" is the unit
}


#endif /* __HGRNIC_ICM_H__ */
