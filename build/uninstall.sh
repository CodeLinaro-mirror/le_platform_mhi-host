#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2021, The Linux Foundation. All rights reserved.

usage() {
echo "Usage:"
echo ""
echo "$0 will uninstall MHI and related kernel modules from persistent use across reboots, unload modules and clean the build"
echo ""
echo "Please run $0 as root"
echo "$0 <-h or --help> shows this text"
}

if [[ $1 == "-h" || $1 == "--help" ]]; then
	usage
	exit 0
fi

if [[ $(id -u) -ne 0 ]] ; then echo "Please run as root" ; echo ""; usage ; exit 1 ; fi

MHI_SCRIPT_RELATIVE_DIR=$(dirname "${BASH_SOURCE[0]}")
source $MHI_SCRIPT_RELATIVE_DIR/envsetup.sh

bash $MHI_BUILD_ROOT/scripts/unload.sh

cd $MHI_BUILD_ROOT
find . -path ./out -prune -false -o -name *.ko | sed 's:.*/::' | grep -Po '.*(?=\.)' | xargs -I % sed -i '/\<%\>/d' /etc/modules
find . -path ./out -prune -false -o -name *.ko | sed -r 's/^.{2}//' | xargs -I % rm /lib/modules/$MHI_KERNEL_VER/kernel/%
rm /etc/udev/rules.d/99-mhi-permissions.rules
udevadm control --reload
rm /etc/mhi_dynamicdebug.sh
(crontab -l; echo "@reboot /etc/mhi_dynamicdebug.sh") 2>/dev/null | grep -v /etc/mhi_dynamicdebug.sh | sort | uniq | crontab -
cd - > /dev/null

bash $MHI_BUILD_ROOT/build/build.sh clean

cd /lib/modules/$MHI_KERNEL_VER/
depmod
cd - > /dev/null

update-initramfs -c -k $(uname -r)
echo "RAMFS updated, please reboot for sanity check if desired"
