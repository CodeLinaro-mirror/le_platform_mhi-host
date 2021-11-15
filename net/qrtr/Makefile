# SPDX-License-Identifier: GPL-2.0-only
obj-m += qrtr_main.o qrtr_ns.o qrtr_mhi.o

KBUILD_EXTRA_SYMBOLS:= $(MHI_BUILD_ROOT)/drivers/bus/mhi/Module.symvers

all:
	make -C /lib/modules/$(MHI_KERNEL_VER)/build M=$(shell pwd) modules
clean:
	make -C /lib/modules/$(MHI_KERNEL_VER)/build M=$(shell pwd) clean

ccflags-y := -I$(MHI_BUILD_ROOT)/include -Wall
qrtr_main-y := qrtr.o
qrtr_ns-y := ns.o
qrtr_mhi-y := mhi.o
