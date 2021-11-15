obj-m += core/ mhi_pci.o mhi_uci.o

all:
	make -C /lib/modules/$(MHI_KERNEL_VER)/build M=$(shell pwd) modules
clean:
	make -C /lib/modules/$(MHI_KERNEL_VER)/build M=$(shell pwd) clean

ccflags-y := -I$(MHI_BUILD_ROOT)/include -Wall
mhi_pci-y := pci_generic.o
mhi_uci-y := uci.o
