/**************************************************************
 * @author Kang Ning<kangning18z@ict.ac.cn>, NCIC, ICT, CAS
 * @date 2021.09.08
 * @file hgrnic_dev.h
 * @note Device specific data and useful function
 *************************************************************/

#ifndef HGRNIC_DEV_H
#define HGRNIC_DEV_H

#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/timer.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/semaphore.h>

#include "hgrnic_provider.h"
#include "hgrnic_doorbell.h"
#include "hgrnic_iface.h"
#include "hgrnic_icm.h"
#include "hgrnic_en_abi.h"

#define PCI_VENDOR_ID_ICT                   0x10EE
#define PCI_DEVICE_ID_ICT_HANGU_RNIC        0x7028

#define HANGU_HCA      0 /* Han GU HCA */
#define HANGU_RNIC     1 /* HAN GU RNIC */


#define DRV_NAME	"ib_hgrnic"
#define PFX		DRV_NAME ": "
#define DRV_VERSION	"1.0"
#define DRV_RELDATE	"April 4, 2021"

#define HG_GID_SIZE 16

#define CONFIG_INFINIBAND_HGRNIC_DEBUG

#ifdef CONFIG_INFINIBAND_HGRNIC_DEBUG

#define hgrnic_dbg(mdev, format, arg...)                             \
    do {                                                            \
        dev_printk(KERN_DEBUG, &mdev->pdev->dev, format, ## arg);   \
    } while (0)

#define hgrnic_dump(type, var) printk(KERN_INFO PFX "[%s] " #var ": " type "\n", __FUNCTION__, var)
#define status_dump(var) printk(KERN_INFO PFX "[%s] " var " successful!\n", __FUNCTION__)

#else /* CONFIG_INFINIBAND_HGRNIC_DEBUG */

#define hgrnic_dbg(mdev, format, arg...) do { (void) mdev; } while (0)

#define hgrnic_dump(type, var) do { ; } while (0)
#define status_dump(type, var) do { ; } while (0)

#endif /* CONFIG_INFINIBAND_HGRNIC_DEBUG */

#define hgrnic_err(mdev, format, arg...) \
    dev_err(&mdev->pdev->dev, format, ## arg)
#define hgrnic_info(mdev, format, arg...) \
    dev_info(&mdev->pdev->dev, format, ## arg)
#define hgrnic_warn(mdev, format, arg...) \
    dev_warn(&mdev->pdev->dev, format, ## arg)


enum {
	HGRNIC_FLAG_PCIE       = 1 << 1
};

enum {
	HGRNIC_MAX_PORTS = 1
};


enum {
	HGRNIC_EQ_CONTEXT_SIZE =  0x40,
	HGRNIC_CQ_CONTEXT_SIZE =  0x40,
	HGRNIC_QP_CONTEXT_SIZE = 0x200,
	HGRNIC_RDB_ENTRY_SIZE  =  0x20,
	HGRNIC_AV_SIZE         =  0x20,
	HGRNIC_MGM_ENTRY_SIZE  = 0x100,

	HGRNIC_MPT_ENTRY_SIZE  =  0x40,
	HGRNIC_MTT_SEG_SIZE    =  0x40
};

enum {
	HGRNIC_EQ_CMD,
	HGRNIC_EQ_ASYNC,
	HGRNIC_EQ_COMP,
	HGRNIC_NUM_EQ
};

enum {
    HGRNIC_OPCODE_NOP            = 0x00,
    HGRNIC_OPCODE_RDMA_WRITE     = 0x08,
    HGRNIC_OPCODE_RDMA_WRITE_IMM = 0x09,
    HGRNIC_OPCODE_SEND           = 0x0a,
    HGRNIC_OPCODE_SEND_IMM       = 0x0b,
    HGRNIC_OPCODE_RDMA_READ      = 0x10,
    HGRNIC_OPCODE_ATOMIC_CS      = 0x11,
    HGRNIC_OPCODE_ATOMIC_FA      = 0x12,
    HGRNIC_OPCODE_BIND_MW        = 0x18,
    HGRNIC_OPCODE_RECV_ERR       = 0xfe,
    HGRNIC_OPCODE_SEND_ERR       = 0xff,
};


enum {
	HGRNIC_CMD_NUM_DBELL_DWORDS = 8
};

struct hgrnic_cmd {
	struct dma_pool          *pool;
	struct mutex              hcr_mutex;
	struct semaphore 	  poll_sem;
	struct semaphore 	  event_sem;
	int              	  max_cmds;
	spinlock_t                context_lock;
	int                       free_head;
	// struct hgrnic_cmd_context *context;
	u16                       token_mask;
	// u32                       flags;
	void __iomem             *dbell_map;
	u16                       dbell_offsets[HGRNIC_CMD_NUM_DBELL_DWORDS];
};

struct hgrnic_limits {
    int      num_ports;
    int      vl_cap;
    int      mtu_cap;
    int      gid_table_len;
    int      pkey_table_len;
    int      local_ca_ack_delay;
    int      num_uars;
    int      max_sg;
    int      num_qps;
    int      max_wqes;    // Supported max number of WQEs in SQ (or RQ)
    int	     max_desc_sz; // Supported maximum descriptor size
    int      reserved_qps;
    int      num_cqs;
    int      max_cqes;      /* maximum number of cqe in one CQ */
    int      reserved_cqs;
    int      num_eqs;
    int      reserved_eqs;
    int      num_mpts;
    int      num_mtt_segs;
    int	     mtt_seg_size; // unused now
    int      reserved_mtts;
    int      reserved_mrws;
    int      num_mgms;
    int      num_amgms;
    int      reserved_mcgs;
    int      num_pds;
    int      reserved_pds;
    u32      page_size_cap;
    u16      stat_rate_support;
    u8       port_width_cap;
};

struct hgrnic_alloc {
    u32            last; /* last allocated resource. */
    u32            top;  /* prefix of resource number */
    u32            max;  /* Maximum number of resources. */
    u32            mask; /* Maximum allowed resource number Mask,
                          * Mask may be bigger than max. */
    spinlock_t     lock;
    unsigned long *table; /* bitmap table */
};

/* This struct is used to fnd resource 
 * pointer based on resource ID */
struct hgrnic_array {
    struct {
        void    **page;
        int       used;
    } *page_list;
};

struct hgrnic_uar_table {
    struct hgrnic_alloc alloc;
};

struct hgrnic_pd_table {
    struct hgrnic_alloc alloc;
};

struct hgrnic_buddy {

    /**
     * We take max_order = 2 as an example:
     * Initial state:
     * bits[0] : 0 0 0 0
     * bits[1] :  0   0
     * bits[2] :    1
     * When allocate one entry
     * bits[0] : (0) 1 0 0
     * bits[1] :    0   1
     * bits[2] :      0
     */
    unsigned long **bits; // bitmap matrix
    int	       *num_free;
    int             max_order;
    spinlock_t      lock;
};

struct hgrnic_mr_table {
    struct hgrnic_alloc     mpt_alloc;
    struct hgrnic_buddy     mtt_buddy;
    u64                     mtt_base;
    u64                     mpt_base;
    struct hgrnic_icm_table *mtt_table;
    struct hgrnic_icm_table *mpt_table;
};

struct hgrnic_eq_table {
    struct hgrnic_alloc alloc;
    void __iomem      *clr_int;
    u32                clr_mask;
    u32                arm_mask;
    struct hgrnic_eq    eq[HGRNIC_NUM_EQ];
    u64                icm_virt;
    struct page       *icm_page;
    dma_addr_t         icm_dma; // bus addr of icm page
};

struct hgrnic_cq_table {
    struct hgrnic_alloc  alloc; /* rescource number allocation */
    spinlock_t          lock ;
    struct hgrnic_array  cq   ;
    struct hgrnic_icm_table *table; /* ICM space allocation */
};

struct hgrnic_qp_table {
    struct hgrnic_alloc     alloc;
    u32                     rdb_base;
    int                     rdb_shift;
    int                     sqp_start; // start qpn of special qp
    spinlock_t              lock;
    struct hgrnic_array     qp; // Used to find qp struct based on QPN
    struct hgrnic_icm_table *qp_table;
};

// defined in hgrnic_main.c
extern struct mutex hgrnic_device_mutex;




struct hgrnic_dev {
    struct ib_device      ib_dev;
    struct pci_dev        *pdev ;
    struct hgrnic_endev   *endev;

    int              hca_type;
    unsigned long    hgrnic_flags;

    u32              rev_id;
    u64              board_id;

    HGRNIC_DECLARE_DOORBELL_LOCK(doorbell_lock)
    struct mutex cap_mask_mutex;

    void __iomem    *hcr;
    void __iomem    *kar;

    struct hgrnic_cmd    cmd;
    struct hgrnic_limits limits;

    struct hgrnic_uar_table uar_table;
    struct hgrnic_pd_table  pd_table;
    struct hgrnic_mr_table  mr_table;
    struct hgrnic_eq_table  eq_table;
    struct hgrnic_cq_table  cq_table;
    struct hgrnic_qp_table  qp_table;

    // Resources reserved for driver uses.
    struct hgrnic_uar       driver_uar;
    struct hgrnic_pd        driver_pd;
    struct hgrnic_mr        driver_mr;

    u8                    gid[HG_GID_SIZE];
    u8                    rate[HGRNIC_MAX_PORTS];
    bool                  active;
};


extern void __buggy_use_of_HGRNIC_GET(void);
extern void __buggy_use_of_HGRNIC_PUT(void);

#define HGRNIC_GET(dest, source, offset)                       \
    do {                                                      \
        void *__p = (char *) (source) + (offset);             \
        switch (sizeof (dest)) {                              \
        case 1: (dest) = *(u8 *) __p;       break;            \
        case 2: (dest) = be16_to_cpup(__p); break;            \
        case 4: (dest) = be32_to_cpup(__p); break;            \
        case 8: (dest) = be64_to_cpup(__p); break;            \
        default: __buggy_use_of_HGRNIC_GET();                  \
        }                                                     \
    } while (0)

#define HGRNIC_PUT(dest, source, offset)                       \
    do {                                                      \
        void *__d = ((char *) (dest) + (offset));             \
        switch (sizeof(source)) {                             \
        case 1: *(u8 *) __d = (source);                break; \
        case 2:	*(__be16 *) __d = cpu_to_be16(source); break; \
        case 4:	*(__be32 *) __d = cpu_to_be32(source); break; \
        case 8:	*(__be64 *) __d = cpu_to_be64(source); break; \
        default: __buggy_use_of_HGRNIC_PUT();                  \
        }                                                     \
    } while (0)


u32 hgrnic_alloc(struct hgrnic_alloc *alloc);
void hgrnic_free(struct hgrnic_alloc *alloc, u32 obj);
int hgrnic_alloc_init(struct hgrnic_alloc *alloc, u32 num, u32 mask);
void hgrnic_alloc_cleanup(struct hgrnic_alloc *alloc);
void *hgrnic_array_get(struct hgrnic_array *array, int index);
int hgrnic_array_set(struct hgrnic_array *array, int index, void *value);
void hgrnic_array_clear(struct hgrnic_array *array, int index);
int hgrnic_array_init(struct hgrnic_array *array, int nent);
void hgrnic_array_cleanup(struct hgrnic_array *array, int nent);
int hgrnic_buf_alloc(struct hgrnic_dev *dev, int size,
		    union hgrnic_buf *buf, int *is_direct, struct hgrnic_pd *pd,
		    int hca_write, struct hgrnic_mr *mr);
void hgrnic_buf_free(struct hgrnic_dev *dev, int size, union hgrnic_buf *buf,
		    int is_direct, struct hgrnic_mr *mr);

int hgrnic_init_uar_table(struct hgrnic_dev *dev);
int hgrnic_init_pd_table(struct hgrnic_dev *dev);
int hgrnic_init_mr_table(struct hgrnic_dev *dev);
int hgrnic_init_cq_table(struct hgrnic_dev *dev);
int hgrnic_init_qp_table(struct hgrnic_dev *dev);
// int hgrnic_init_mcg_table(struct hgrnic_dev *dev);

void hgrnic_cleanup_uar_table(struct hgrnic_dev *dev);
void hgrnic_cleanup_pd_table(struct hgrnic_dev *dev);
void hgrnic_cleanup_mr_table(struct hgrnic_dev *dev);
void hgrnic_cleanup_cq_table(struct hgrnic_dev *dev);
void hgrnic_cleanup_qp_table(struct hgrnic_dev *dev);
// void hgrnic_cleanup_mcg_table(struct hgrnic_dev *dev);

int hgrnic_register_device(struct hgrnic_dev *dev);
void hgrnic_unregister_device(struct hgrnic_dev *dev);

int __hgrnic_restart_one(struct pci_dev *pdev);

int hgrnic_uar_alloc(struct hgrnic_dev *dev, struct hgrnic_uar *uar);
void hgrnic_uar_free(struct hgrnic_dev *dev, struct hgrnic_uar *uar);

int hgrnic_pd_alloc(struct hgrnic_dev *dev, int privileged, struct hgrnic_pd *pd);
void hgrnic_pd_free(struct hgrnic_dev *dev, struct hgrnic_pd *pd);

int hgrnic_write_mtt_size(struct hgrnic_dev *dev);

struct hgrnic_mtt *hgrnic_alloc_mtt(struct hgrnic_dev *dev, int size);
void hgrnic_free_mtt(struct hgrnic_dev *dev, struct hgrnic_mtt *mtt);
int hgrnic_write_mtt(struct hgrnic_dev *dev, struct hgrnic_mtt *mtt,
		    int start_index, u64 *buffer_list, int list_len);
struct ib_mr *hgrnic_reg_user_mr(struct ib_pd *pd, u64 start, 
        u64 length, u64 virt, int acc, struct ib_udata *udata);
int hgrnic_mr_alloc(struct hgrnic_dev *dev, u32 pd, int buffer_size_shift,
		   u64 iova, u64 total_size, u32 access, struct hgrnic_mr *mr);
int hgrnic_mr_alloc_notrans(struct hgrnic_dev *dev, u32 pd,
			   u32 access, struct hgrnic_mr *mr);
int hgrnic_mr_alloc_phys(struct hgrnic_dev *dev, u32 pd,
			u64 *buffer_list, int buffer_size_shift,
			int list_len, u64 iova, u64 total_size,
			u32 access, struct hgrnic_mr *mr);
void hgrnic_free_mr(struct hgrnic_dev *dev,  struct hgrnic_mr *mr);

int hgrnic_map_eq_icm(struct hgrnic_dev *dev, u64 icm_virt);
void hgrnic_unmap_eq_icm(struct hgrnic_dev *dev);
int hgrnic_create_cq(struct ib_cq *ibcq,
                    const struct ib_cq_init_attr *attr, 
                    struct ib_udata *udata);
int hgrnic_poll_cq(struct ib_cq *ibcq, int num_entries,
		  struct ib_wc *entry);
int hgrnic_arm_cq(struct ib_cq *cq, enum ib_cq_notify_flags flags);
int hgrnic_init_cq(struct hgrnic_dev *dev, int nent, 
        struct hgrnic_ucontext *ctx, u32 pdn, struct hgrnic_cq *cq);
void hgrnic_free_cq(struct hgrnic_dev *dev,
                   struct hgrnic_cq *cq);
void hgrnic_cq_clean(struct hgrnic_dev *dev, struct hgrnic_cq *cq, u32 qpn);
void hgrnic_cq_resize_copy_cqes(struct hgrnic_cq *cq);
int hgrnic_alloc_cq_buf(struct hgrnic_dev *dev, struct hgrnic_cq_buf *buf, int nent);
void hgrnic_free_cq_buf(struct hgrnic_dev *dev, struct hgrnic_cq_buf *buf, int cqe);


int hgrnic_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr, int qp_attr_mask,
		   struct ib_qp_init_attr *qp_init_attr);
struct ib_qp *hgrnic_create_qp (struct ib_pd *pd,
        struct ib_qp_init_attr *init_attr, struct ib_udata *udata);
int hgrnic_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr, int attr_mask,
		    struct ib_udata *udata);
int hgrnic_post_send(struct ib_qp *ibqp, const struct ib_send_wr *wr,
			  const struct ib_send_wr **bad_wr);
int hgrnic_post_receive(struct ib_qp *ibqp, const struct ib_recv_wr *wr,
			     const struct ib_recv_wr **bad_wr);
int hgrnic_alloc_qp(struct hgrnic_dev *dev,
		   struct hgrnic_pd *pd,
		   struct hgrnic_cq *send_cq,
		   struct hgrnic_cq *recv_cq,
		   enum ib_qp_type type,
		   struct ib_qp_cap *cap,
		   struct hgrnic_qp *qp,
		   struct ib_udata *udata);
void hgrnic_free_qp(struct hgrnic_dev *dev, struct hgrnic_qp *qp);


static inline struct hgrnic_dev *to_hgdev(struct ib_device *ibdev) {
	return container_of(ibdev, struct hgrnic_dev, ib_dev);
}

#endif /* HGRNIC_DEV_H */
