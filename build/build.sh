#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2021, The Linux Foundation. All rights reserved.

MHI_SCRIPT_RELATIVE_DIR=$(dirname "${BASH_SOURCE[0]}")

usage() {
echo "Usage:"
echo ""
echo "$0 <mhi / net / wwan / qrtr / all / clean> <all / clean>"
echo "$0 (with \"all\" or no arguments) will build all modules"
echo "$0 clean or $0 all clean: will clean all modules"
echo "$0 <module_name> clean will clean that module"
echo "Ensure environment variables are setup using 'source $MHI_SCRIPT_RELATIVE_DIR/envsetup.sh'"
echo "$0 <-h or --help> shows this text"
}

if [[ $1 == "-h" || $1 == "--help" ]]; then
	usage
	exit 0
fi

if [[ $MHI_BUILD_ROOT == "" ]]
then
	echo "Have you run 'source $MHI_SCRIPT_RELATIVE_DIR/envsetup.sh' yet?"
	usage
	exit 1
fi

if [[ ! -d "/lib/modules/$(uname -r)/build" ]]
then
	echo "Headers for currently running kernel version $(uname -r) not found."
	if [[ $(id -u) -ne 0 ]] ; then echo "Please run as root to install headers" ; exit 1 ; fi
	sudo apt-get -y install linux-headers-$(uname -r)
fi

cd $MHI_BUILD_ROOT

if [[ $1 == "mhi" ]]
then
	make -C drivers/bus/mhi $2
elif [[ $1 == "net" ]]
then
	make -C drivers/bus/mhi $2
	make -C drivers/net/wwan $2
	make -C drivers/net/mhi $2
elif [[ $1 == "wwan" ]]
then
	make -C drivers/bus/mhi $2
	make -C drivers/net/wwan $2
elif [[ $1 == "qrtr" ]]
then
	make -C net/qrtr $2
elif [[ $1 == "clean" ]]
then
	make -C drivers/bus/mhi $1
	make -C drivers/net/wwan $1
	make -C drivers/net/mhi $1
	make -C net/qrtr $1
elif [[ $1 == "" ]] || [[ $1 == "all" ]]
then
	make -C drivers/bus/mhi $2
	make -C drivers/net/wwan $2
	make -C drivers/net/mhi $2
	make -C net/qrtr $2
else
	usage
	exit 127
fi
