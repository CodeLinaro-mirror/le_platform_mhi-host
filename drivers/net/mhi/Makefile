obj-m += mhi_net.o

KBUILD_EXTRA_SYMBOLS:= $(MHI_BUILD_ROOT)/drivers/bus/mhi/Module.symvers

all:
	make -C /lib/modules/$(MHI_KERNEL_VER)/build M=$(shell pwd) modules
clean:
	make -C /lib/modules/$(MHI_KERNEL_VER)/build M=$(shell pwd) clean

ccflags-y := -I$(MHI_BUILD_ROOT)/include -Wall
mhi_net-y := net.o proto_mbim.o
