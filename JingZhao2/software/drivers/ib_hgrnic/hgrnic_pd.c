/**************************************************************
 * @author Kang Ning<kangning18z@ict.ac.cn>, NCIC, ICT, CAS
 * @date 2021.09.08
 * @file hgrnic_pd.c
 * @note Initialize, Allocate, Free and Clean Up protection domain.
 *************************************************************/

#include <linux/errno.h>

#include "hgrnic_dev.h"

int hgrnic_pd_alloc (struct hgrnic_dev *dev, 
        int privileged, struct hgrnic_pd *pd)
{
    pd->privileged = privileged;

    pd->pd_num = hgrnic_alloc(&dev->pd_table.alloc);
    if (pd->pd_num == -1)
        return -ENOMEM;

    return 0;
}

void hgrnic_pd_free(struct hgrnic_dev *dev, struct hgrnic_pd *pd)
{
    hgrnic_free(&dev->pd_table.alloc, pd->pd_num);
}

// Maximum PD number is 2^24.
int hgrnic_init_pd_table(struct hgrnic_dev *dev) {
	return hgrnic_alloc_init(&dev->pd_table.alloc,
				dev->limits.num_pds,
				(1 << 24) - 1);
}

void hgrnic_cleanup_pd_table(struct hgrnic_dev *dev) {
	/* XXX check if any PDs are still allocated? */
	hgrnic_alloc_cleanup(&dev->pd_table.alloc);
}
