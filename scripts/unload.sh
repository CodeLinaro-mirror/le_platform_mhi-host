#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2021, The Linux Foundation. All rights reserved.

unload_pci() {
rmmod mhi_pci

lsmod | grep mhi_pci
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
rmmod dtr_mhi
rmmod wwan

lsmod | grep mhi_wwan
lsmod | grep dtr_mhi
}

unload_qrtr() {
rmmod qrtr_mhi
rmmod qrtr_ns
rmmod qrtr_main

lsmod | grep qrtr
}

usage() {
echo "Usage:"
echo ""
echo "$0 <mhi / pci / net / wwan / qrtr / all>"
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
elif [[ $1 == "pci" ]]
then
	unload_pci
elif [[ $1 == "net" ]]
then
	unload_net
elif [[ $1 == "wwan" ]]
then
	unload_wwan
elif [[ $1 == "qrtr" ]]
then
	unload_qrtr
elif [[ $1 == "" ]] || [[ $1 == "all" ]]
then
	unload_qrtr
	unload_wwan
	unload_net
	unload_pci
	unload_mhi
else
	usage
	exit 127
fi
