obj-m += drivers/bus/mhi/ drivers/net/mhi/ drivers/ptp/ drivers/net/wwan/
KERNELRELEASE=$(shell uname -r)

M ?= $(shell pwd)

all:
	make -C /lib/modules/$(KERNELRELEASE)/build M=$(M) MHI_HOST_INCLUDE_DIR=$(realpath $(M)/include) modules
clean:
	make -C /lib/modules/$(KERNELRELEASE)/build M=$(M) clean

