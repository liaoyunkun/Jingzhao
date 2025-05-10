/**************************************************************
 * @author Kang Ning<kangning18z@ict.ac.cn>, NCIC, ICT, CAS
 * @date 2021.09.08
 * @file hgrnic_doorbell.h
 * @note Data structs & functions related to doorbell
 *************************************************************/

#include <linux/types.h>



#if BITS_PER_LONG == 64
/*
 * Assume that we can just write a 64-bit doorbell atomically.  s390
 * actually doesn't have writeq() but S/390 systems don't even have
 * PCI so we won't worry about it.
 */

#define HGRNIC_DECLARE_DOORBELL_LOCK(name)
#define HGRNIC_INIT_DOORBELL_LOCK(ptr)    do { } while (0)
#define HGRNIC_GET_DOORBELL_LOCK(ptr)      (NULL)

static inline void hgrnic_write64_raw(__be64 val, void __iomem *dest)
{
    __raw_writeq((__force u64) val, dest);
}

static inline void hgrnic_write64(u32 hi, u32 lo, void __iomem *dest,
                                  spinlock_t *doorbell_lock)
{
    __raw_writeq((__force u64) cpu_to_le64((u64) hi << 32 | lo), dest);
}

#else

/*
 * Just fall back to a spinlock to protect the doorbell if
 * BITS_PER_LONG is 32 -- there's no portable way to do atomic 64-bit
 * MMIO writes.
 */

#define HGRNIC_DECLARE_DOORBELL_LOCK(name) spinlock_t name;
#define HGRNIC_INIT_DOORBELL_LOCK(ptr)     spin_lock_init(ptr)
#define HGRNIC_GET_DOORBELL_LOCK(ptr)      (ptr)

static inline void hgrnic_write64_raw(__be64 val, void __iomem *dest)
{
    __raw_writel(((__force u32 *) &val)[0], dest);
    __raw_writel(((__force u32 *) &val)[1], dest + 4);
}

static inline void hgrnic_write64(u32 hi, u32 lo, void __iomem *dest,
                                  spinlock_t *doorbell_lock)
{
    unsigned long flags;

    hi = (__force u32) cpu_to_le32(hi);
    lo = (__force u32) cpu_to_le32(lo);

    spin_lock_irqsave(doorbell_lock, flags);
    __raw_writel(lo, dest);
    __raw_writel(hi, dest + 4);
    spin_unlock_irqrestore(doorbell_lock, flags);
}

#endif