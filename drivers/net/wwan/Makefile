# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux WWAN device drivers.
#

obj-m += wwan.o wwan_mhi.o

THIS_DIR        := $(shell echo $(dir $(abspath $(lastword $(MAKEFILE_LIST)))) | sed "s:/*$$::")
MHI_BUILD_ROOT  := $(THIS_DIR)/../../..
MHI_INCLUDE_DIR := $(MHI_BUILD_ROOT)/include
MHI_MODULE_VERSION ?= $(shell echo ENG-$$(whoami; date) | sed "s/ /-/g")

KBUILD_EXTRA_SYMBOLS:= $(MHI_BUILD_ROOT)/drivers/bus/mhi/Module.symvers

all:
	make -C /lib/modules/$(MHI_KERNEL_VER)/build M=$(shell pwd) modules
clean:
	make -C /lib/modules/$(MHI_KERNEL_VER)/build M=$(shell pwd) clean

ccflags-y := -I$(MHI_BUILD_ROOT)/include -Wall -DMHI_MODULE_VERSION='"$(MHI_MODULE_VERSION)"'
wwan-y := wwan_core.o
wwan_mhi-y := mhi_wwan_ctrl.o
