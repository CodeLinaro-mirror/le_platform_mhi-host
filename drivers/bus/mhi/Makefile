obj-m += core/ mhi_pci.o mhi_uci.o

THIS_DIR        := $(shell echo $(dir $(abspath $(lastword $(MAKEFILE_LIST)))) | sed "s:/*$$::")
MHI_BUILD_ROOT  := $(THIS_DIR)/../../..

KBUILD_EXTRA_SYMBOLS := $(MHI_BUILD_ROOT)/drivers/bus/mhi/Module.symvers
MHI_MODULE_VERSION ?= $(shell echo ENG-$$(whoami; date) | sed "s/ /-/g")

all:
	make -C /lib/modules/$(MHI_KERNEL_VER)/build M=$(shell pwd) modules
clean:
	make -C /lib/modules/$(MHI_KERNEL_VER)/build M=$(shell pwd) clean

ccflags-y := -I$(MHI_BUILD_ROOT)/include -Wall -DMHI_MODULE_VERSION='"$(MHI_MODULE_VERSION)"'
mhi_pci-y := pci_generic.o
mhi_uci-y := uci.o
