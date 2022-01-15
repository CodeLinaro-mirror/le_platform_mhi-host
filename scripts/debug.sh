#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2021, The Linux Foundation. All rights reserved.

debug_mhi() {
echo -n "file main.c +p" > /sys/kernel/debug/dynamic_debug/control
echo -n "file init.c +p" > /sys/kernel/debug/dynamic_debug/control
echo -n "file debugfs.c +p" > /sys/kernel/debug/dynamic_debug/control
echo -n "file pm.c +p" > /sys/kernel/debug/dynamic_debug/control
echo -n "file boot.c +p" > /sys/kernel/debug/dynamic_debug/control
echo -n "file uci.c +p" > /sys/kernel/debug/dynamic_debug/control
}

debug_pci() {
echo -n "file pci_generic.c +p" > /sys/kernel/debug/dynamic_debug/control
}

debug_net() {
echo -n "file net.c +p" > /sys/kernel/debug/dynamic_debug/control
echo -n "file proto_mbim.c +p" > /sys/kernel/debug/dynamic_debug/control
}

debug_wwan() {
echo -n "file wwan_core.c +p" > /sys/kernel/debug/dynamic_debug/control
echo -n "file mhi_wwan_ctrl.c +p" > /sys/kernel/debug/dynamic_debug/control
echo -n "file mhi_dtr.c +p" > /sys/kernel/debug/dynamic_debug/control
}

debug_qrtr() {
echo -n "file mhi.c +p" > /sys/kernel/debug/dynamic_debug/control
echo -n "file qrtr.c +p" > /sys/kernel/debug/dynamic_debug/control
}

usage() {
echo "Usage:"
echo ""
echo "$0 <-r or --run> to enable dynamic debug and run dmesg filtered for MHI with --follow"
echo "$0 <-h or --help> shows this text"
}

if [[ $1 == "-h" || $1 == "--help" ]]; then
	usage
	exit 0
fi

if [[ $(id -u) -ne 0 ]] ; then echo "Please run as root" ; echo ""; usage; exit 1 ; fi

if [[ $(cat /boot/config-$(uname -r) | grep CONFIG_DYNAMIC_DEBUG) == "CONFIG_DYNAMIC_DEBUG=y" ]]; then
	debug_mhi
	debug_pci
	debug_net
	debug_wwan
	debug_qrtr
else
	echo "Current kernel configuration does not support dynamic debug for MHI and client drivers"
	usage
	exit 1
fi

if [[ $1 == "-r" ]]; then
	dmesg -w | grep mhi
fi
