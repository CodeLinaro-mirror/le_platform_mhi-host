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

unload_pci() {
rmmod mhi_pci

lsmod | grep mhi_pci
}

unload_ptp() {
rmmod mhi_ptp

lsmod | grep mhi_ptp
}

unload_mhi() {
rmmod mhi_uci
rmmod mhi

lsmod | grep mhi
}

unload_net() {
rmmod mhi_net

lsmod | grep mhi_net
}

unload_wwan() {
rmmod wwan_mhi
rmmod wwan

lsmod | grep mhi_wwan
}

usage() {
echo "Usage:"
echo ""
echo "$0 <mhi / pci / ptp / net / wwan / all>"
echo "$0 will unload all modules"
echo "Please run $0 as root"
echo "<-h or --help> shows this text"
}

if [[ $1 == "-h" || $1 == "--help" ]]; then
	usage
	exit 0
fi

if [[ $(id -u) -ne 0 ]] ; then echo "Please run as root" ; echo ""; usage; exit 1 ; fi

if [[ $1 == "mhi" ]]
then
	unload_mhi
	debug_mhi -
elif [[ $1 == "pci" ]]
then
	unload_pci
	debug_pci -
elif [[ $1 == "ptp" ]]
then
	unload_ptp
	debug_ptp -
elif [[ $1 == "net" ]]
then
	unload_net
	debug_net -
elif [[ $1 == "wwan" ]]
then
	unload_wwan
	debug_wwan -
elif [[ $1 == "" ]] || [[ $1 == "all" ]]
then
	unload_wwan
	unload_net
	unload_pci
	unload_ptp
	unload_mhi

	debug_wwan -
	debug_net -
	debug_pci -
	debug_ptp -
	debug_mhi -
else
	usage
	exit 127
fi
