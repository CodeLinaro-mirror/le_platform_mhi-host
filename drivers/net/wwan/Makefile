# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux WWAN device drivers.
#

obj-m += wwan.o dtr_mhi.o wwan_mhi.o

KBUILD_EXTRA_SYMBOLS:= $(MHI_BUILD_ROOT)/drivers/bus/mhi/Module.symvers

all:
	make -C /lib/modules/$(MHI_KERNEL_VER)/build M=$(shell pwd) modules
clean:
	make -C /lib/modules/$(MHI_KERNEL_VER)/build M=$(shell pwd) clean

ccflags-y := -I$(MHI_BUILD_ROOT)/include -Wall
wwan-y := wwan_core.o
dtr_mhi-y := mhi_dtr.o
wwan_mhi-y := mhi_wwan_ctrl.o
