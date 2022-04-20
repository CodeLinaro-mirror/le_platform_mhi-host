#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2021, The Linux Foundation. All rights reserved.

usage() {
echo "Usage:"
echo ""
echo "$0 will build, load, and install MHI and related kernel modules for persistent use across reboots"
echo "Verbose logging using dynamic debug will only be enabled for verifying this installation but will not stay enabled or be enabled across reboots"
echo "In oder to have verbose logging using dynamic debug, user has to run /etc/mhi_dynamicdebug.sh after a reboot"
echo "build/uninstall.sh reverses those actions"
echo ""
echo "Please run $0 as root"
echo "$0 <-h or --help> shows this text"
}

if [[ $1 == "-h" || $1 == "--help" ]]; then
	usage
	exit 0
fi

if [[ $(id -u) -ne 0 ]] ; then echo "Please run as root" ; usage; exit 1 ; fi

MHI_SCRIPT_RELATIVE_DIR=$(dirname "${BASH_SOURCE[0]}")
source $MHI_SCRIPT_RELATIVE_DIR/envsetup.sh
verbose="-v"

if ! bash $MHI_BUILD_ROOT/build/build.sh; then
	echo "Failed installation"
	exit 1
fi

if ! bash $MHI_BUILD_ROOT/scripts/load.sh $verbose; then
	echo "Failed to load modules"
	exit 1
fi

echo "Installing drivers..."
cd $MHI_BUILD_ROOT
find . -path ./out -prune -false -o -name *.ko | sed 's:.*/::' | grep -Po '.*(?=\.)' | xargs -I % echo % >> /etc/modules
find . -path ./out -prune -false -o -name *.ko | xargs -I % cp --parents % /lib/modules/$MHI_KERNEL_VER/kernel/
mkdir -p /lib/firmware/qcom/sdx65m
cp build/*.rules /etc/udev/rules.d/
udevadm control --reload
cp scripts/debug.sh /etc/mhi_dynamicdebug.sh
(crontab -l; echo "@reboot /etc/mhi_dynamicdebug.sh -t") 2>/dev/null | sort | uniq | crontab -
cd - > /dev/null

cd /lib/modules/$MHI_KERNEL_VER/
depmod

sleep 15
bash /etc/mhi_dynamicdebug.sh -d

echo "Done"
