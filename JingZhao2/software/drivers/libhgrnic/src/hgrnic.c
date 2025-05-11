#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <pthread.h>
#include <string.h>

#ifndef HAVE_IBV_REGISTER_DRIVER
#include <sysfs/libsysfs.h>
#endif

#ifndef HAVE_IBV_READ_SYSFS_FILE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

#include "hgrnic.h"
#include "hgrnic-abi.h"

#define PCI_VENDOR_ID_ICT               0x10EE
#define PCI_DEVICE_ID_ICT_HANGU_RNIC    0x7028

struct {
    unsigned        vendor;
    unsigned        device;
} hca_table[] = {
    {PCI_VENDOR_ID_ICT, PCI_DEVICE_ID_ICT_HANGU_RNIC},
};

static struct ibv_context_ops hgrnic_ctx_ops = {
    .query_device  = hgrnic_query_device , // done
    .query_port    = hgrnic_query_port   , // done
    .alloc_pd      = hgrnic_alloc_pd     , // done
    .dealloc_pd    = hgrnic_free_pd      , // done
    .reg_mr        = hgrnic_reg_mr       , // done
    .dereg_mr      = hgrnic_dereg_mr     , // done
    .create_cq     = hgrnic_create_cq    , // done
    .poll_cq       = hgrnic_poll_cq      , // done
    .resize_cq     = hgrnic_resize_cq    , // done
    .destroy_cq    = hgrnic_destroy_cq   , // done
    .create_qp     = hgrnic_create_qp    , // done
    .query_qp      = hgrnic_query_qp     , // done
    .modify_qp     = hgrnic_modify_qp    , // done
    .destroy_qp    = hgrnic_destroy_qp   , // done
    .create_ah     = hgrnic_create_ah    , // done
    .destroy_ah    = hgrnic_destroy_ah   , // done
    .attach_mcast  = ibv_cmd_attach_mcast,
    .detach_mcast  = ibv_cmd_detach_mcast,

    .post_send     = hgrnic_post_send, // done
    .post_recv     = hgrnic_post_recv, // done

    .req_notify_cq = hgrnic_notify_cq,

    .create_srq    = hgrnic_create_srq   ,
    .query_srq     = hgrnic_query_srq    ,
    .post_srq_recv = hgrnic_post_srq_recv,
    .modify_srq    = hgrnic_modify_srq   ,
    .destroy_srq   = hgrnic_destroy_srq  
};

static struct ibv_context *hgrnic_alloc_context(struct ibv_device *ibdev, int cmd_fd)
{
    struct hgrnic_context            *context;
    struct ibv_get_context           cmd;
    struct hgrnic_alloc_ucontext_resp resp;
    int                              i;

    context = calloc(1, sizeof *context);
    if (!context)
        return NULL;

    context->ibv_ctx.cmd_fd = cmd_fd;

    if (ibv_cmd_get_context(&context->ibv_ctx, &cmd, sizeof cmd,
                &resp.ibv_resp, sizeof resp))
        goto err_free;

    context->num_qps        = resp.qp_tab_size;
    context->qp_table_shift = ffs(context->num_qps) - 1 - HGRNIC_QP_TABLE_BITS;
    context->qp_table_mask  = (1 << context->qp_table_shift) - 1;

    // context->ibv_ctx.device = ibdev; // !TODO: We may don't need it now.

    pthread_mutex_init(&context->qp_table_mutex, NULL);
    for (i = 0; i < HGRNIC_QP_TABLE_SIZE; ++i)
        context->qp_table[i].refcnt = 0;

    context->uar = mmap(NULL, to_hgdev(ibdev)->page_size, PROT_WRITE,
            MAP_SHARED, cmd_fd, 0);
    if (context->uar == MAP_FAILED)
        goto err_free;

    pthread_spin_init(&context->uar_lock, PTHREAD_PROCESS_PRIVATE);

    context->pd = hgrnic_alloc_pd(&context->ibv_ctx);
    if (!context->pd)
        goto err_unmap;

    context->pd->context = &context->ibv_ctx;

    context->ibv_ctx.ops = hgrnic_ctx_ops;

    return &context->ibv_ctx;

err_unmap:
    munmap(context->uar, to_hgdev(ibdev)->page_size);

err_free:
    free(context);
    return NULL;
}

static void hgrnic_free_context(struct ibv_context *ibctx)
{
    struct hgrnic_context *context = to_hgctx(ibctx);

    hgrnic_free_pd(context->pd);
    munmap(context->uar, to_hgdev(ibctx->device)->page_size);
    free(context);
}

// assigned to ibverbs by init_func.
static struct ibv_device_ops hgrnic_dev_ops = {
    .alloc_context = hgrnic_alloc_context,
    .free_context  = hgrnic_free_context
};

/*
 * Keep a private implementation of HAVE_IBV_READ_SYSFS_FILE to handle
 * old versions of libibverbs that didn't implement it.  This can be
 * removed when libibverbs 1.0.3 or newer is available "everywhere."
 */
#ifndef HAVE_IBV_READ_SYSFS_FILE
static int ibv_read_sysfs_file(const char *dir, const char *file,
			       char *buf, size_t size)
{
	char path[256];
	int fd;
	int len;

	snprintf(path, sizeof path, "%s/%s", dir, file);

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;

	len = read(fd, buf, size);

	close(fd);

	if (len > 0 && buf[len - 1] == '\n')
		buf[--len] = '\0';

	return len;
}
#endif /* HAVE_IBV_READ_SYSFS_FILE */

// "ibv_driver_init_func init_func", defined in ibverbs
static struct ibv_device *hgrnic_driver_init(const char *uverbs_sys_path,
                                             int abi_version)
{
	char			value[8];
	struct hgrnic_device    *dev;
	unsigned                vendor, device;
	int                     i;

	if (ibv_read_sysfs_file(uverbs_sys_path, "device/vendor",
				value, sizeof value) < 0)
		return NULL;
	sscanf(value, "%i", &vendor);

	if (ibv_read_sysfs_file(uverbs_sys_path, "device/device",
				value, sizeof value) < 0)
		return NULL;
	sscanf(value, "%i", &device);

	for (i = 0; i < sizeof hca_table / sizeof hca_table[0]; ++i)
		if (vendor == hca_table[i].vendor &&
		    device == hca_table[i].device)
			goto found;

	return NULL;

found:
	if (abi_version > HGRNIC_UVERBS_ABI_VERSION) {
		fprintf(stderr, PFX "Fatal: ABI version %d of %s is too new (expected %d)\n",
			abi_version, uverbs_sys_path, HGRNIC_UVERBS_ABI_VERSION);
		return NULL;
	}

	dev = malloc(sizeof *dev);
	if (!dev) {
		fprintf(stderr, PFX "Fatal: couldn't allocate device for %s\n",
			uverbs_sys_path);
		return NULL;
	}

	dev->ibv_dev.ops = hgrnic_dev_ops;
	dev->page_size   = sysconf(_SC_PAGESIZE);

	return &dev->ibv_dev;
}

#ifdef HAVE_IBV_REGISTER_DRIVER
static __attribute__((constructor)) void hgrnic_register_driver(void)
{
    ibv_register_driver("hgrnic", hgrnic_driver_init);
}
#else
/*
 * Export the old libsysfs sysfs_class_device-based driver entry point
 * if libibverbs does not export an ibv_register_driver() function.
 */
struct ibv_device *openib_driver_init(struct sysfs_class_device *sysdev)
{
	int abi_ver = 0;
	char value[8];

	if (ibv_read_sysfs_file(sysdev->path, "abi_version",
				value, sizeof value) > 0)
		abi_ver = strtol(value, NULL, 10);

	return hgrnic_driver_init(sysdev->path, abi_ver);
}
#endif /* HAVE_IBV_REGISTER_DRIVER */
