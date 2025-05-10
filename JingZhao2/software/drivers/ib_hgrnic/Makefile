# (Copyright) 20210301  Kang Ning, NCSG, NCIC, ICT, CAS. 
# <kangning18z@ict.ac.cn>
#
# Makefile for low-level driver. This driver is compatible with Linux V5.3.0
#
obj-m += ib_hgrnic.o

ib_hgrnic-objs :=hgrnic_main.o hgrnic_cmd.o hgrnic_icm.o \
		hgrnic_allocator.o hgrnic_eq.o hgrnic_pd.o hgrnic_cq.o \
		hgrnic_mr.o hgrnic_qp.o hgrnic_register.o \
		hgrnic_provider.o hgrnic_uar.o

LINUX_KERNEL_PATH := /lib/modules/$(shell uname -r)/build
CURRENT_PATH := $(shell pwd)

all:
	cp include/rdma/hgrnic-abi.h $(LINUX_KERNEL_PATH)/include/rdma/hgrnic-abi.h
	cp include/rdma/hgrnic-abi.h $(LINUX_KERNEL_PATH)/include/uapi/rdma/hgrnic-abi.h
	cp include/uapi/rdma/rdma_user_ioctl_cmds.h $(LINUX_KERNEL_PATH)/include/uapi/rdma/rdma_user_ioctl_cmds.h
	make -C $(LINUX_KERNEL_PATH) M=$(CURRENT_PATH) modules


clean:
	make -C $(LINUX_KERNEL_PATH) M=$(CURRENT_PATH) clean
