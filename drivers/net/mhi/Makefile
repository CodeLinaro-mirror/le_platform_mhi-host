obj-m += mhi_net.o

THIS_DIR        := $(shell echo $(dir $(abspath $(lastword $(MAKEFILE_LIST)))) | sed "s:/*$$::")
MHI_BUILD_ROOT  := $(THIS_DIR)/../../..
MHI_INCLUDE_DIR := $(MHI_BUILD_ROOT)/include

KBUILD_EXTRA_SYMBOLS:= $(MHI_BUILD_ROOT)/drivers/bus/mhi/Module.symvers
MHI_MODULE_VERSION ?= $(shell echo ENG-$$(whoami; date) | sed "s/ /-/g")

all:
	make -C /lib/modules/$(MHI_KERNEL_VER)/build M=$(shell pwd) modules
clean:
	make -C /lib/modules/$(MHI_KERNEL_VER)/build M=$(shell pwd) clean

ccflags-y := -I$(MHI_BUILD_ROOT)/include -Wall -DMHI_MODULE_VERSION='"$(MHI_MODULE_VERSION)"'
mhi_net-y := net.o proto_mbim.o
