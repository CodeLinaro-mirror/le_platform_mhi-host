obj-m += mhi.o

THIS_DIR        := $(shell echo $(dir $(abspath $(lastword $(MAKEFILE_LIST)))) | sed "s:/*$$::")
MHI_BUILD_ROOT  := $(THIS_DIR)/../../../..
MHI_INCLUDE_DIR := $(MHI_BUILD_ROOT)/include

MHI_MODULE_VERSION ?= $(shell echo ENG-$$(whoami; date) | sed "s/ /-/g")

all:
	make -C /lib/modules/$(MHI_KERNEL_VER)/build M=$(shell pwd) modules
clean:
	make -C /lib/modules/$(MHI_KERNEL_VER)/build M=$(shell pwd) clean

ccflags-y := -I$(MHI_BUILD_ROOT)/include -Wall -DMHI_MODULE_VERSION='"$(MHI_MODULE_VERSION)"'
mhi-y := init.o main.o pm.o boot.o debugfs.o
