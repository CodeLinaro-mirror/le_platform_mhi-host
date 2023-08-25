#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2021, The Linux Foundation. All rights reserved.
#
# Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
#

debug_mhi() {
echo -n "module mhi $1p" > /sys/kernel/debug/dynamic_debug/control
echo -n "module mhi_uci $1p" > /sys/kernel/debug/dynamic_debug/control
}

debug_pci() {
echo -n "module mhi_pci $1p" > /sys/kernel/debug/dynamic_debug/control
echo -n "module pci-acpi $1p" > /sys/kernel/debug/dynamic_debug/control
echo -n "module acpi $1p" > /sys/kernel/debug/dynamic_debug/control
echo -n "file pci.c $1p" > /sys/kernel/debug/dynamic_debug/control
echo -n "file pci-acpi.c $1p" > /sys/kernel/debug/dynamic_debug/control

if [[ $1 == "+" ]]
then
	echo 0xffffffff > /sys/module/acpi/parameters/debug_level
	echo 0x00C00000 > /sys/module/acpi/parameters/debug_layer
elif [[ $1 == "-" ]]
then
	echo 0x00000000 > /sys/module/acpi/parameters/debug_level
	echo 0x00000000 > /sys/module/acpi/parameters/debug_layer
fi
}

debug_net() {
echo -n "module mhi_net $1p" > /sys/kernel/debug/dynamic_debug/control
}

debug_wwan() {
echo -n "module wwan_core $1p" > /sys/kernel/debug/dynamic_debug/control
echo -n "module wwan_mhi $1p" > /sys/kernel/debug/dynamic_debug/control
echo -n "module dtr_mhi $1p" > /sys/kernel/debug/dynamic_debug/control
}

debug_ptp() {
echo -n "module mhi_ptp $1p" > /sys/kernel/debug/dynamic_debug/control
}

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

load_pci() {
if ! insmod drivers/bus/mhi/mhi_pci.ko; then
	echo "Failed to load KO"
	exit 1
fi

lsmod | grep mhi_pci
}

load_ptp() {
if ! insmod drivers/ptp/mhi_ptp.ko; then
	echo "Failed to load KO"
	exit 1
fi

lsmod | grep mhi_ptp
}

load_net() {
if ! insmod drivers/net/mhi/mhi_net.ko; then
	echo "Failed to load KO"
	exit 1
fi

lsmod | grep mhi_net
}

load_wwan() {
if ! insmod drivers/net/wwan/wwan.ko; then
	echo "Failed to load KO"
	exit 1
fi
if ! insmod drivers/net/wwan/wwan_mhi.ko; then
	echo "Failed to load KO"
	exit 1
fi

lsmod | grep wwan
}

usage() {
echo "Usage:"
echo ""
echo "$0 <mhi / pci / ptp / net / wwan / all> <-v for verbose output to enable dynamic debug>"
echo "If verbose flag is used, user must manually disable dynamic debug if needed, using scripts/debug.sh -d"
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
		debug_mhi +
	fi
elif [[ $1 == "pci" ]]
then
	load_pci
	if [[ $2 == "-v" ]]
	then
		debug_pci +
	fi
elif [[ $1 == "ptp" ]]
then
	load_ptp
	if [[ $2 == "-v" ]]
	then
		debug_ptp +
	fi
elif [[ $1 == "net" ]]
then
	load_net
	if [[ $2 == "-v" ]]
	then
		debug_net +
	fi
elif [[ $1 == "wwan" ]]
then
	load_wwan
	if [[ $2 == "-v" ]]
	then
		debug_wwan +
	fi
elif [[ $1 == "" ]] || [[ $1 == "all" ]] || [[ $1 == "-v" ]]
then
	load_mhi
	load_net
	load_wwan
	if [[ $1 == "-v" ]] || [[ $2 == "-v" ]]
	then
		debug_mhi +
		debug_net +
		debug_wwan +
	fi
	load_pci
	load_ptp
	if [[ $1 == "-v" ]] || [[ $2 == "-v" ]]
	then
		debug_pci +
		debug_ptp +
	fi
else
	usage
	exit 127
fi
