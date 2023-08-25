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
}

debug_ptp() {
echo -n "module mhi_ptp $1p" > /sys/kernel/debug/dynamic_debug/control
}

usage() {
echo "Usage:"
echo ""
echo "$0 to enable dynamic debug"
echo "$0 <-r or --run> to enable dynamic debug and run dmesg filtered for MHI with --follow"
echo "$0 <-t or --temp> to enable dynamic debug temporarily for specified number of seconds"
echo "If seconds are not specified after -t / --temp flag, 60 seconds will be the default value"
echo "$0 <-d or --disable> to disable dynamic debug"
echo "$0 <-h or --help> shows this text"
}

if [[ $1 == "-h" || $1 == "--help" ]]; then
	usage
	exit 0
fi

if [[ $(id -u) -ne 0 ]] ; then echo "Please run as root" ; echo ""; usage; exit 1 ; fi

if [[ $(cat /boot/config-$(uname -r) | grep CONFIG_DYNAMIC_DEBUG) == "CONFIG_DYNAMIC_DEBUG=y" ]]; then
	if [[ $1 == "-d" || $1 == "--disable" ]]; then
		debug_mhi -
		debug_pci -
		debug_net -
		debug_wwan -
		debug_ptp -
		echo "Disabled dynamic debug for MHI and client drivers"
	elif [[ $1 == "-t" || $1 == "--temp" ]]; then
		debug_mhi +
		debug_pci +
		debug_net +
		debug_wwan +
		debug_ptp +
		echo "Temporarily enabled dynamic debug for MHI and client drivers"
		if [[ $2 != "" ]]; then
			sleep $2
		else
			sleep 60
		fi
		debug_mhi -
		debug_pci -
		debug_net -
		debug_wwan -
		debug_ptp -
		echo "Disabled dynamic debug for MHI and client drivers"
	else
		debug_mhi +
		debug_pci +
		debug_net +
		debug_wwan +
		debug_ptp +
		echo "Enabled dynamic debug for MHI and client drivers"
	fi
else
	echo "Current kernel configuration does not support dynamic debug for MHI and client drivers"
	usage
	exit 1
fi

if [[ $1 == "-r" || $1 == "--run" ]]; then
	dmesg -w | grep mhi
fi
