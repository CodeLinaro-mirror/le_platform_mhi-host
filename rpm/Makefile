obj-m += mhi-host/drivers/bus/mhi/ mhi-host/drivers/net/mhi/ mhi-host/drivers/net/wwan/

KERNELRELEASE=$(shell uname -r)

M ?= $(shell pwd)
MHI_MODULE_VERSION ?= $(shell echo $$(whoami; date) | sed "s/ /-/g")

all:
	make -C /lib/modules/$(KERNELRELEASE)/build M=$(M) MHI_MODULE_VERSION="$(MHI_MODULE_VERSION)" MHI_HOST_INCLUDE_DIR=$(realpath $(M)/mhi-host/include) modules
clean:
	make -C /lib/modules/$(KERNELRELEASE)/build M=$(M) clean

