/**************************************************************
 * @author Kang Ning<kangning18z@ict.ac.cn>, NCIC, ICT, CAS
 * @date 2021.09.08
 * @file hgrnic_provider.c
 * @note Functions & Data struct related to interface with ib_uverbs.
 *************************************************************/

#include <rdma/ib_smi.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/uverbs_ioctl.h>

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/export.h>

#include "hgrnic_dev.h"
#include "hgrnic_cmd.h"
#include <rdma/hgrnic-abi.h>
#include "hgrnic_icm.h"

/**
 * @description: 
 * API function.
 * Query device information.
 */
static int hgrnic_query_device (struct ib_device *ibdev, 
                               struct ib_device_attr *props, 
                               struct ib_udata *uhw)
{
    struct hgrnic_dev *mdev = to_hgdev(ibdev);

    if (uhw->inlen || uhw->outlen)
        return -EINVAL;

    memset(props, 0, sizeof *props);

    props->vendor_id           = PCI_VENDOR_ID_ICT;
    props->vendor_part_id      = HANGU_RNIC;
    props->max_mr_size         = ~0ull;
    props->page_size_cap       = mdev->limits.page_size_cap;
    props->max_qp              = mdev->limits.num_qps - mdev->limits.reserved_qps;
    props->max_qp_wr           = mdev->limits.max_wqes;
    props->max_send_sge        = mdev->limits.max_sg;
    props->max_recv_sge        = mdev->limits.max_sg;
    props->max_cq              = mdev->limits.num_cqs - mdev->limits.reserved_cqs;
    props->max_cqe             = mdev->limits.max_cqes;
    props->max_mr              = mdev->limits.num_mpts - mdev->limits.reserved_mrws;
    props->max_pd              = mdev->limits.num_pds - mdev->limits.reserved_pds;
    props->local_ca_ack_delay  = mdev->limits.local_ca_ack_delay;
    props->max_pkeys           = mdev->limits.pkey_table_len;
    props->atomic_cap          = IB_ATOMIC_NONE;

    /* reserved */
    props->fw_ver              = 0;
    props->device_cap_flags    = 0;
    props->hw_ver              = 1; // Not involved in design
    props->sys_image_guid      = 0; // Not involved in design
    props->max_sge_rd          = 0;
    props->max_qp_rd_atom      = 0;
    props->max_qp_init_rd_atom = 0;
    props->max_res_rd_atom     = 0;
    props->max_srq             = 0;
    props->max_srq_wr          = 0;
    props->max_srq_sge         = 0;
    props->max_mcast_grp       = 0;
    props->max_mcast_qp_attach = 0;
    props->max_total_mcast_qp_attach = 0;
    props->max_map_per_fmr = 0;

    return 0;
}

/**
 * @description: 
 * API function.
 * Query port information.
 */
static int hgrnic_query_port (struct ib_device *ibdev, u8 port, 
                             struct ib_port_attr *props)
{
#define DEVICE_LID          0x1010

    props->max_msg_sz        = 0x80000000;
    props->lid               = DEVICE_LID;
    props->state             = IB_PORT_ACTIVE;

    // 1:  return "SLEEP";
    // 2:  return "POLLING";
    // 3:  return "DISABLED";
    // 4:  return "PORT_CONFIGURATION TRAINNING";
    // 5:  return "LINK_UP";
    // 6:  return "LINK_ERROR_RECOVERY";
    // 7:  return "PHY TEST";
    props->phys_state        = 5; // According to ib_core, 5 is link up

    // 1:  return "1";
    // 2:  return "4";
    // 4:  return "8";
    // 8:  return "12";
    props->active_width      = 2;

    // 1:  2.5 Gbps";
    // 2:  5.0 Gbps";
    // 4:  10.0 Gbps";
    props->active_speed      = 4;

    props->max_mtu           = IB_MTU_4096;
    props->active_mtu        = IB_MTU_4096;

    // 1:  return "1";
    // 2:  return "2";
    // 3:  return "4";
    // 4:  return "8";
    // 5:  return "15";
    props->max_vl_num        = 0;

    props->gid_tbl_len       = 1;

    /* Reserved */
    props->subnet_prefix     = 0;
    props->lmc               = 0;
    props->sm_lid            = 0;
    props->sm_sl             = 0;
    props->subnet_timeout    = 0;
    props->init_type_reply   = 0;
    props->port_cap_flags    = 0;
    props->pkey_tbl_len      = 1;
    props->bad_pkey_cntr     = 0;
    props->qkey_viol_cntr    = 0;

    return 0;
}

static int hgrnic_modify_device (struct ib_device *ibdev, int mask, 
                                struct ib_device_modify *props)
{
    if (mask & ~IB_DEVICE_MODIFY_NODE_DESC)
        return -EOPNOTSUPP;

    if (mask & IB_DEVICE_MODIFY_NODE_DESC) {
        if (mutex_lock_interruptible(&to_hgdev(ibdev)->cap_mask_mutex))
            return -ERESTARTSYS;
        memcpy(ibdev->node_desc, props->node_desc,
                IB_DEVICE_NODE_DESC_MAX);
        mutex_unlock(&to_hgdev(ibdev)->cap_mask_mutex);
    }

    return 0;
}


/**
 * @description: 
 * API function.
 * Query pkey information.
 */
static int hgrnic_query_pkey(struct ib_device *ibdev,
                            u8 port, u16 index, u16 *pkey)
{
    *pkey = 0x0001;

    return 0;
}

/**
 * @description: 
 * API function.
 * Allocate protection domain.
 * This API calls hgrnic_pd_alloc directly.
 */
static int hgrnic_alloc_pd (struct ib_pd *ibpd, struct ib_udata *udata)
{
    struct ib_device *ibdev = ibpd->device;
    struct hgrnic_pd *pd = to_hgpd(ibpd);
    int err;

    err = hgrnic_pd_alloc(to_hgdev(ibdev), !udata, pd);
    if (err)
        return err;

    if (udata) {
        if (ib_copy_to_udata(udata, &pd->pd_num, sizeof (__u32))) {
            hgrnic_pd_free(to_hgdev(ibdev), pd);
            return -EFAULT;
        }
    }

    return 0;
}

/**
 * @description: 
 * API function.
 * Deallocate protection domain.
 * This API calls hgrnic_pd_free directly.
 */
static void hgrnic_dealloc_pd (struct ib_pd *pd, 
        struct ib_udata *udata) {
    hgrnic_pd_free(to_hgdev(pd->device), to_hgpd(pd));
}


/**
 * @description: 
 * API function. Called by driver ib_core (or ib_uverbs ?? !TODO)
 * Create QP resources.
 */
struct ib_qp *hgrnic_create_qp (struct ib_pd *pd,
                               struct ib_qp_init_attr *init_attr, 
                               struct ib_udata *udata)
{

    struct hgrnic_create_qp ucmd;
    struct hgrnic_qp *qp;
    int err;

    status_dump("enter\n");
    
    if (init_attr->create_flags) { 
	    printk(KERN_INFO PFX "Fail to create qp, ENIVAL\n");
	   return ERR_PTR(-EINVAL);
	}

    switch (init_attr->qp_type) {
    case IB_QPT_RC:
    case IB_QPT_UC:
    case IB_QPT_UD: {
        qp = kzalloc(sizeof(*qp), GFP_KERNEL);
        if (!qp) {
	    	printk(KERN_INFO PFX "Fail to create qp, ENOMEM\n");
            return ERR_PTR(-ENOMEM);
		}
	

        if (udata) {
            if (ib_copy_from_udata(&ucmd, udata, sizeof ucmd)) {
                kfree(qp);
	    		printk(KERN_INFO PFX "Fail to create qp, EFAULT\n");
                return ERR_PTR(-EFAULT);
            }
            qp->rq.mr.ibmr.lkey = ucmd.rq_lkey;
            qp->sq.mr.ibmr.lkey = ucmd.sq_lkey;
        }

        err = hgrnic_alloc_qp(to_hgdev(pd->device), to_hgpd(pd),
                to_hgcq(init_attr->send_cq),
                to_hgcq(init_attr->recv_cq),
                init_attr->qp_type, 
                &init_attr->cap, qp, udata);
        qp->rq.mr.ibmr.length = qp->rq.que_size;
        qp->sq.mr.ibmr.length = qp->sq.que_size;
        qp->ibqp.qp_num = qp->qpn;
        break;
    }
    case IB_QPT_SMI:
    case IB_QPT_GSI:
    default:
        printk(KERN_INFO PFX "hgrnic_create_qp: Don't support Special QPs.\n");
	    printk(KERN_INFO PFX "Fail to create qp, ENOSYS\n");
        return ERR_PTR(-ENOSYS);
    }

    if (err) {
        kfree(qp);
        return ERR_PTR(err);
    }

    /* pass to user space to assign to hgrnic_qp in user space */
    init_attr->cap.max_send_wr     = qp->sq.max;
    init_attr->cap.max_recv_wr     = qp->rq.max;
    init_attr->cap.max_send_sge    = qp->sq.max_gs;
    init_attr->cap.max_recv_sge    = qp->rq.max_gs;
    init_attr->cap.max_inline_data = qp->max_inline_data;


	if(NULL == &qp->ibqp) {
    	status_dump("NULL QP Error\n");
	}
    	status_dump("Normal\n");

    status_dump("exit\n");
    return &qp->ibqp;
}

static int hgrnic_alloc_ucontext(struct ib_ucontext *uctx,
                                struct ib_udata *udata)
{
    struct hgrnic_ucontext *context = to_hgucontext(uctx);
    struct hgrnic_alloc_ucontext_resp uresp = {};
    struct ib_device *ibdev = uctx->device;
    int err;

    if (!(to_hgdev(ibdev)->active))
        return -EAGAIN;

    uresp.qp_tab_size = to_hgdev(ibdev)->limits.num_qps;

    err = hgrnic_uar_alloc(to_hgdev(ibdev), &context->uar);
    if (err)
        return err;

    if (ib_copy_to_udata(udata, &uresp, sizeof(uresp))) {
        hgrnic_uar_free(to_hgdev(ibdev), &context->uar);
        return -EFAULT;
    }

    return 0;
}

static void hgrnic_dealloc_ucontext(struct ib_ucontext *context)
{
    hgrnic_uar_free(to_hgdev(context->device), &to_hgucontext(context)->uar);
}

static int hgrnic_mmap_uar(struct ib_ucontext *context,
                          struct vm_area_struct *vma)
{
    if (vma->vm_end - vma->vm_start != PAGE_SIZE)
        return -EINVAL;

    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

    if (io_remap_pfn_range(vma, vma->vm_start,
                           to_hgucontext(context)->uar.pfn,
                           PAGE_SIZE, vma->vm_page_prot))
        return -EAGAIN;

    return 0;
}

static int hgrnic_destroy_qp(struct ib_qp *qp, struct ib_udata *udata)
{
    hgrnic_free_qp(to_hgdev(qp->device), to_hgqp(qp));
    kfree(to_hgqp(qp));
    return 0;
}

/**
 * @note If kernel space calls this function, it will allocate 
 * buffer for CQ.
 */
int hgrnic_create_cq(struct ib_cq *ibcq,
                    const struct ib_cq_init_attr *attr, 
                    struct ib_udata *udata)
{
    struct ib_device *ibdev = ibcq->device;
    int entries = attr->cqe; /* Required number of cqes in the cq */
    struct hgrnic_create_cq ucmd;
    struct hgrnic_cq *cq;
    int nent;
    int err;
    struct hgrnic_ucontext *context = rdma_udata_to_drv_context(
        udata, struct hgrnic_ucontext, ibucontext);
    
    status_dump("enter");
    
    if (attr->flags)
        return -EINVAL;

    if (entries < 1 || entries > to_hgdev(ibdev)->limits.max_cqes)
        return -EINVAL;

    cq = to_hgcq(ibcq);

    if (udata) {
        if (ib_copy_from_udata(&ucmd, udata, sizeof(ucmd)))
            return -EFAULT;
        cq->buf.mr.ibmr.lkey = ucmd.lkey;
    }

    /* find the most closest value in power of 2 */
    nent = roundup_pow_of_two(entries + 1);

    err = hgrnic_init_cq(to_hgdev(ibdev), nent, context,
                udata ? ucmd.pdn : to_hgdev(ibdev)->driver_pd.pd_num,
                cq);
    if (err)
        return err;

    if (udata && ib_copy_to_udata(udata, &cq->cqn, sizeof(__u32))) {
        hgrnic_free_cq(to_hgdev(ibdev), cq);
        return -EFAULT;
    }
    cq->resize_buf = NULL;

    status_dump("exit\n");
    return 0;
}

static void hgrnic_destroy_cq(struct ib_cq *cq, struct ib_udata *udata)
{
    // printk(KERN_INFO PFX "Enter hgrnic_destroy_cq\n");
    hgrnic_free_cq(to_hgdev(cq->device), to_hgcq(cq));
    // printk(KERN_INFO PFX "Enter hgrnic_destroy_cq\n");
}

static struct ib_mr *hgrnic_get_dma_mr(struct ib_pd *pd, int acc)
{
    struct hgrnic_mr *mr;
    int err;

    printk(KERN_INFO PFX "Enter hgrnic_get_dma_mr\n");
    mr = kmalloc(sizeof *mr, GFP_KERNEL);
    if (!mr)
        return ERR_PTR(-ENOMEM);

    err = hgrnic_mr_alloc_notrans(to_hgdev(pd->device),
                                 to_hgpd(pd)->pd_num,
                                 convert_access(acc), mr);
    if (err) {
        kfree(mr);
        return ERR_PTR(err);
    }

    mr->umem = NULL;

    printk(KERN_INFO PFX "Exit hgrnic_get_dma_mr\n");
    return &mr->ibmr;
}

struct ib_mr *hgrnic_reg_user_mr(struct ib_pd *pd, u64 start, 
                                u64 length, u64 virt, int acc, 
                                struct ib_udata *udata)
{
    struct hgrnic_dev *dev = to_hgdev(pd->device);
    struct sg_dma_page_iter sg_iter;
    struct hgrnic_mr *mr;
    struct hgrnic_reg_mr ucmd;
    u64 *pages;
    int n, i;
    int err = 0;
    int write_mtt_size;

    status_dump("Enter");
    printk(KERN_INFO PFX "start vaddr 0x%llx, length %lld\n", start, length);
    if (udata->inlen < sizeof(ucmd) || 
        ib_copy_from_udata(&ucmd, udata, sizeof(ucmd)))
        return ERR_PTR(-EFAULT);

    mr = kmalloc(sizeof *mr, GFP_KERNEL);
    if (!mr)
        return ERR_PTR(-ENOMEM);

    /* interface updated in later kernel version */
    mr->umem = ib_umem_get(udata, start, length, acc,
                           ucmd.mr_attrs & HGRNIC_MR_DMASYNC);
    if (IS_ERR(mr->umem)) {
        err = PTR_ERR(mr->umem);
        goto err;
    }

    n = ib_umem_num_pages(mr->umem);

    mr->mtt = hgrnic_alloc_mtt(dev, n);
    if (IS_ERR(mr->mtt)) {
        err = PTR_ERR(mr->mtt);
        goto err_umem;
    }

    pages = (u64 *) __get_free_page(GFP_KERNEL);
    if (!pages) {
        err = -ENOMEM;
        goto err_mtt;
    }

    i = 0;
    n = mr->mtt->first_index;

    write_mtt_size = min(hgrnic_write_mtt_size(dev), (int) (PAGE_SIZE / sizeof *pages));

    for_each_sg_dma_page(mr->umem->sg_head.sgl, &sg_iter, mr->umem->nmap, 0) {
        pages[i++] = sg_page_iter_dma_address(&sg_iter);
        printk(KERN_INFO PFX " paddr[%d] 0x%llx\n", i-1, pages[i-1]);

        /*
         * Be friendly to write_mtt and pass it chunks
         * of appropriate size.
         */
        if (i == write_mtt_size) {
            err = hgrnic_write_mtt(dev, mr->mtt, n, pages, i);
            if (err)
                goto mtt_done;
            n += i;
            i = 0;
        }
    }

    if (i)
        err = hgrnic_write_mtt(dev, mr->mtt, n, pages, i);
mtt_done:
    free_page((unsigned long) pages);
    if (err)
        goto err_mtt;

    err = hgrnic_mr_alloc(dev, to_hgpd(pd)->pd_num, PAGE_SHIFT, virt, length,
                         convert_access(acc), mr);

    if (err)
        goto err_mtt;

    status_dump("exit\n\n");
    return &mr->ibmr;

err_mtt:
    hgrnic_free_mtt(dev, mr->mtt);

err_umem:
    ib_umem_release(mr->umem);

err:
    kfree(mr);
    return ERR_PTR(err);
}

static int hgrnic_dereg_mr(struct ib_mr *mr, struct ib_udata *udata)
{
    struct hgrnic_mr *hgmr = to_hgmr(mr);

    printk(KERN_INFO PFX "Enter hgrnic_dereg_mr\n");
    hgrnic_free_mr(to_hgdev(mr->device), hgmr);
    ib_umem_release(hgmr->umem);
    kfree(hgmr);

    printk(KERN_INFO PFX "Exit hgrnic_dereg_mr\n");
    return 0;
}



static ssize_t hw_rev_show(struct device *device,
                           struct device_attribute *attr, char *buf)
{
    struct hgrnic_dev *dev =
        rdma_device_to_drv_device(device, struct hgrnic_dev, ib_dev);

    return sprintf(buf, "%x\n", dev->rev_id);
}
static DEVICE_ATTR_RO(hw_rev);

static ssize_t hca_type_show(struct device *device,
                             struct device_attribute *attr, 
                             char *buf)
{
    struct hgrnic_dev *dev =
        rdma_device_to_drv_device(device, struct hgrnic_dev, ib_dev);

    switch (dev->pdev->device) {
    case PCI_DEVICE_ID_ICT_HANGU_RNIC:
        return sprintf(buf, "HANGU_RNIC\n");
    default:
        return sprintf(buf, "unknown\n");
    }
}
static DEVICE_ATTR_RO(hca_type);

static ssize_t board_id_show(struct device *device,
                             struct device_attribute *attr, char *buf)
{
    struct hgrnic_dev *dev =
        rdma_device_to_drv_device(device, struct hgrnic_dev, ib_dev);

    return sprintf(buf, "%lld\n", dev->board_id);
}
static DEVICE_ATTR_RO(board_id);

static struct attribute *hgrnic_dev_attributes[] = {
    &dev_attr_hw_rev.attr,
    &dev_attr_hca_type.attr,
    &dev_attr_board_id.attr,
    NULL
};

static const struct attribute_group hgrnic_attr_group = {
    .attrs = hgrnic_dev_attributes,
};


static int hgrnic_port_immutable(struct ib_device *ibdev, u8 port_num,
                                struct ib_port_immutable *immutable)
{
    struct ib_port_attr attr;
    int err;

    printk(KERN_INFO PFX "Enter hgrnic_port_immutable\n");
    immutable->core_cap_flags = RDMA_CORE_CAP_PROT_IB;
    err = ib_query_port(ibdev, port_num, &attr);
    if (err)
        return err;

    immutable->pkey_tbl_len = attr.pkey_tbl_len;
    immutable->gid_tbl_len = attr.gid_tbl_len;
    immutable->max_mad_size = 0;

    printk(KERN_INFO PFX "Exit hgrnic_port_immutable\n");
    return 0;
}

static int hgrnic_query_gid(struct ib_device *ibdev, u8 port,
                           int index, union ib_gid *gid)
{
    struct hgrnic_dev *dev = to_hgdev(ibdev);
    // int i = 0;

    status_dump("enter");

    memcpy(gid->raw, dev->gid, sizeof(dev->gid));
    
    status_dump("exit\n\n");
    return 0;
}


static int hgrnic_alloc_resize_buf(struct hgrnic_dev *dev, struct hgrnic_cq *cq,
                                  int entries)
{
    int ret;

    spin_lock_irq(&cq->lock);
    if (cq->resize_buf) {
        ret = -EBUSY;
        goto unlock;
    }

    cq->resize_buf = kmalloc(sizeof *cq->resize_buf, GFP_ATOMIC);
    if (!cq->resize_buf) {
        ret = -ENOMEM;
        goto unlock;
    }

    cq->resize_buf->state = CQ_RESIZE_ALLOC;

    ret = 0;

unlock:
    spin_unlock_irq(&cq->lock);

    if (ret)
        return ret;

    ret = hgrnic_alloc_cq_buf(dev, &cq->resize_buf->buf, entries);
    if (ret) {
        spin_lock_irq(&cq->lock);
        kfree(cq->resize_buf);
        cq->resize_buf = NULL;
        spin_unlock_irq(&cq->lock);
        return ret;
    }

    cq->resize_buf->cqe = entries - 1;

    spin_lock_irq(&cq->lock);
    cq->resize_buf->state = CQ_RESIZE_READY;
    spin_unlock_irq(&cq->lock);

    return 0;
}

static int hgrnic_resize_cq(struct ib_cq *ibcq, int entries, 
                           struct ib_udata *udata)
{
    struct hgrnic_dev *dev = to_hgdev(ibcq->device);
    struct hgrnic_cq *cq = to_hgcq(ibcq);
    struct hgrnic_resize_cq ucmd;
    u32 lkey;
    int ret;

    printk(KERN_INFO PFX "Enter hgrnic_resize_cq\n");
    if (entries < 1 || entries > dev->limits.max_cqes)
        return -EINVAL;

    mutex_lock(&cq->mutex);

    entries = roundup_pow_of_two(entries + 1);
    if (entries == ibcq->cqe + 1) {
        ret = 0;
        goto out;
    }

    if (cq->is_kernel) {
        ret = hgrnic_alloc_resize_buf(dev, cq, entries);
        if (ret)
            goto out;
        lkey = cq->resize_buf->buf.mr.ibmr.lkey;
    } else {
        if (ib_copy_from_udata(&ucmd, udata, sizeof ucmd)) {
            ret = -EFAULT;
            goto out;
        }
        lkey = ucmd.lkey;
    }

    ret = hgrnic_RESIZE_CQ(dev, cq->cqn, lkey, ilog2(entries));

    if (ret) {
        if (cq->resize_buf) {
            hgrnic_free_cq_buf(dev, &cq->resize_buf->buf,
                        cq->resize_buf->cqe);
            kfree(cq->resize_buf);
            spin_lock_irq(&cq->lock);
            cq->resize_buf = NULL;
            spin_unlock_irq(&cq->lock);
        }
        goto out;
    }

    if (cq->is_kernel) {
        struct hgrnic_cq_buf tbuf;
        int tcqe;

        spin_lock_irq(&cq->lock);
        if (cq->resize_buf->state == CQ_RESIZE_READY) {
            hgrnic_cq_resize_copy_cqes(cq);
            tbuf         = cq->buf;
            tcqe         = cq->ibcq.cqe;
            cq->buf      = cq->resize_buf->buf;
            cq->ibcq.cqe = cq->resize_buf->cqe;
        } else {
            tbuf = cq->resize_buf->buf;
            tcqe = cq->resize_buf->cqe;
        }

        kfree(cq->resize_buf);
        cq->resize_buf = NULL;
        spin_unlock_irq(&cq->lock);

        hgrnic_free_cq_buf(dev, &tbuf, tcqe);
    } else
        ibcq->cqe = entries - 1;

out:
	mutex_unlock(&cq->mutex);

    printk(KERN_INFO PFX "Exit hgrnic_resize_cq\n");
	return ret;
}

/**
 * @note The following is the mandatory API.
 * query_device,
 * query_port
 * query_pkey
 * alloc_pd
 * dealloc_pd
 * create_qp
 * modify_qp
 * destroy_qp
 * post_send
 * post_recv
 * create_cq
 * destroy_cq
 * poll_cq
 * req_notify_cq
 * get_dma_mr
 * dereg_mr
 * get_port_immutable
 */
static const struct ib_device_ops hgrnic_dev_ops = {
    .owner = THIS_MODULE,
    .driver_id = RDMA_DRIVER_HGRNIC,
    .uverbs_abi_ver = HGRNIC_UVERBS_ABI_VERSION,
    .uverbs_no_driver_id_binding = 1,

    .query_device  = hgrnic_query_device , /* done, void */
    .modify_device = hgrnic_modify_device, /* done, void */

    .alloc_ucontext   = hgrnic_alloc_ucontext  , /* done,tested */
    .dealloc_ucontext = hgrnic_dealloc_ucontext, /* done */

    .query_port = hgrnic_query_port, /* done, void */
    .get_port_immutable = hgrnic_port_immutable, /* done, void */
    .query_gid  = hgrnic_query_gid , /* done, void */
    .query_pkey = hgrnic_query_pkey, /* done, void */
    .mmap       = hgrnic_mmap_uar  , /* done */

    .alloc_pd   = hgrnic_alloc_pd  , /* done */
    .dealloc_pd = hgrnic_dealloc_pd, /* done */

    .create_qp  = hgrnic_create_qp , /* done */
    .query_qp   = hgrnic_query_qp  , /* done */
    .modify_qp  = hgrnic_modify_qp , /* done */
    .destroy_qp = hgrnic_destroy_qp, /* done */

    .post_send = hgrnic_post_send   , /* done */
    .post_recv = hgrnic_post_receive, /* done */

    .create_cq  = hgrnic_create_cq , /* done */
    .destroy_cq = hgrnic_destroy_cq, /* done */
    .poll_cq    = hgrnic_poll_cq   , /* done */
    .req_notify_cq = hgrnic_arm_cq , /* half done */
    .resize_cq  = hgrnic_resize_cq , /* done */

    .get_dma_mr  = hgrnic_get_dma_mr ,  /* done */
    .reg_user_mr = hgrnic_reg_user_mr,  /* done */
    .dereg_mr    = hgrnic_dereg_mr   ,  /* done */

    /* tell ib_core size of driver data struct */
    INIT_RDMA_OBJ_SIZE(ib_ah, hgrnic_ah, ibah),
    INIT_RDMA_OBJ_SIZE(ib_cq, hgrnic_cq, ibcq),
    INIT_RDMA_OBJ_SIZE(ib_pd, hgrnic_pd, ibpd),
    INIT_RDMA_OBJ_SIZE(ib_ucontext, hgrnic_ucontext, ibucontext),
};



/**
 * @note Register valid API to kernel for this driver.
 */
int hgrnic_register_device (struct hgrnic_dev *dev) {
	int ret;

	dev->ib_dev.uverbs_cmd_mask	 =
		(1ull << IB_USER_VERBS_CMD_GET_CONTEXT)		|
		(1ull << IB_USER_VERBS_CMD_QUERY_DEVICE)	|
		(1ull << IB_USER_VERBS_CMD_QUERY_PORT)		|
		(1ull << IB_USER_VERBS_CMD_ALLOC_PD)		|
		(1ull << IB_USER_VERBS_CMD_DEALLOC_PD)		|
		(1ull << IB_USER_VERBS_CMD_REG_MR)		|
		(1ull << IB_USER_VERBS_CMD_DEREG_MR)		|
		(1ull << IB_USER_VERBS_CMD_CREATE_COMP_CHANNEL)	|
		(1ull << IB_USER_VERBS_CMD_CREATE_CQ)		|
		(1ull << IB_USER_VERBS_CMD_RESIZE_CQ)		|
		(1ull << IB_USER_VERBS_CMD_DESTROY_CQ)		|
		(1ull << IB_USER_VERBS_CMD_CREATE_QP)		|
		(1ull << IB_USER_VERBS_CMD_QUERY_QP)		|
		(1ull << IB_USER_VERBS_CMD_MODIFY_QP)		|
		(1ull << IB_USER_VERBS_CMD_DESTROY_QP);
	dev->ib_dev.node_type            = RDMA_NODE_IB_CA;
	dev->ib_dev.phys_port_cnt        = dev->limits.num_ports;
	dev->ib_dev.num_comp_vectors     = 1;
	dev->ib_dev.dev.parent           = &dev->pdev->dev;


	ib_set_device_ops(&dev->ib_dev, &hgrnic_dev_ops);
	
	mutex_init(&dev->cap_mask_mutex);

    rdma_set_device_sysfs_group(&dev->ib_dev, &hgrnic_attr_group);
    printk(KERN_INFO PFX "Start ib_register_device\n");
    ret = ib_register_device(&dev->ib_dev, "hgrnic%d");
    printk(KERN_INFO PFX "End ib_register_device\n");
    if (ret)
        return ret;

    return 0;
}

void hgrnic_unregister_device (struct hgrnic_dev *dev)
{
    ib_unregister_device(&dev->ib_dev);
}
