#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2021, The Linux Foundation. All rights reserved.

load_mhi() {
if ! insmod drivers/bus/mhi/core/mhi.ko; then
	echo "Failed to load KO"
	exit 1
else
	if [[ ! -f "/etc/udev/rules.d/99-mhi-permissions.rules" ]]; then
		cp build/*.rules /etc/udev/rules.d/
		udevadm control --reload
	fi
fi
if ! insmod drivers/bus/mhi/mhi_uci.ko; then
	echo "Failed to load KO"
	exit 1
fi

lsmod | grep mhi
}

debug_mhi() {
echo -n "file main.c +p" > /sys/kernel/debug/dynamic_debug/control
echo -n "file init.c +p" > /sys/kernel/debug/dynamic_debug/control
echo -n "file debugfs.c +p" > /sys/kernel/debug/dynamic_debug/control
echo -n "file pm.c +p" > /sys/kernel/debug/dynamic_debug/control
echo -n "file boot.c +p" > /sys/kernel/debug/dynamic_debug/control
echo -n "file uci.c +p" > /sys/kernel/debug/dynamic_debug/control
}

load_pci() {
if ! insmod drivers/bus/mhi/mhi_pci.ko; then
	echo "Failed to load KO"
	exit 1
fi

lsmod | grep mhi_pci
}

debug_pci() {
echo -n "file pci_generic.c +p" > /sys/kernel/debug/dynamic_debug/control
}

load_net() {
if ! insmod drivers/net/mhi/mhi_net.ko; then
	echo "Failed to load KO"
	exit 1
fi

lsmod | grep mhi_net
}

debug_net() {
echo -n "file net.c +p" > /sys/kernel/debug/dynamic_debug/control
echo -n "file proto_mbim.c +p" > /sys/kernel/debug/dynamic_debug/control
}

load_wwan() {
if ! insmod drivers/net/wwan/wwan.ko; then
	echo "Failed to load KO"
	exit 1
fi
if ! insmod drivers/net/wwan/dtr_mhi.ko; then
	echo "Failed to load KO"
	exit 1
fi
if ! insmod drivers/net/wwan/wwan_mhi.ko; then
	echo "Failed to load KO"
	exit 1
fi

lsmod | grep wwan
lsmod | grep dtr
}

debug_wwan() {
echo -n "file wwan_core.c +p" > /sys/kernel/debug/dynamic_debug/control
echo -n "file mhi_wwan_ctrl.c +p" > /sys/kernel/debug/dynamic_debug/control
echo -n "file mhi_dtr.c +p" > /sys/kernel/debug/dynamic_debug/control
}

load_qrtr() {
if ! insmod net/qrtr/qrtr_main.ko; then
	echo "Failed to load KO"
	exit 1
fi
if ! insmod net/qrtr/qrtr_ns.ko; then
	echo "Failed to load KO"
	exit 1
fi
if ! insmod net/qrtr/qrtr_mhi.ko; then
	echo "Failed to load KO"
	exit 1
fi

lsmod | grep qrtr
}

debug_qrtr() {
echo -n "file mhi.c +p" > /sys/kernel/debug/dynamic_debug/control
echo -n "file qrtr.c +p" > /sys/kernel/debug/dynamic_debug/control
echo -n "file ns.c +p" > /sys/kernel/debug/dynamic_debug/control
}

usage() {
echo "Usage:"
echo ""
echo "$0 <mhi / pci / net / wwan / qrtr / all> <-v for verbose output to enable dynamic debug>"
echo "$0 will load all modules"
echo "Please run $0 as root"
echo "<-h or --help> shows this text"
}

if [[ $1 == "-h" || $1 == "--help" ]]; then
	usage
	exit 0
fi

if [[ $(id -u) -ne 0 ]] ; then echo "Please run as root" ; echo ""; usage; exit 1 ; fi

MHI_SCRIPT_RELATIVE_DIR=$(dirname "${BASH_SOURCE[0]}")

if [[ $MHI_BUILD_ROOT == "" ]]
then
	echo "Have you run 'source build/envsetup.sh' and/or 'build/build.sh' yet?"
	usage
	exit 1
fi

cd $MHI_BUILD_ROOT

if [[ ! $(find . -name *.ko) ]]; then
	echo "Have you run 'build/build.sh' yet?"
	usage
	exit 1
fi

if [[ $1 == "mhi" ]]
then
	load_mhi
	if [[ $2 == "-v" ]]
	then
		debug_mhi
	fi
elif [[ $1 == "pci" ]]
then
	load_pci
	if [[ $2 == "-v" ]]
	then
		debug_pci
	fi
elif [[ $1 == "net" ]]
then
	load_net
	if [[ $2 == "-v" ]]
	then
		debug_net
	fi
elif [[ $1 == "wwan" ]]
then
	load_wwan
	if [[ $2 == "-v" ]]
	then
		debug_wwan
	fi
elif [[ $1 == "qrtr" ]]
then
	load_qrtr
	if [[ $2 == "-v" ]]
	then
		debug_qrtr
	fi
elif [[ $1 == "" ]] || [[ $1 == "all" ]] || [[ $1 == "-v" ]]
then
	load_mhi
	load_net
	load_wwan
	load_qrtr
	if [[ $1 == "-v" ]] || [[ $2 == "-v" ]]
	then
		debug_mhi
		debug_net
		debug_wwan
		debug_qrtr
	fi
	load_pci
	if [[ $1 == "-v" ]] || [[ $2 == "-v" ]]
	then
		debug_pci
	fi
else
	usage
	exit 127
fi
