#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note
# Copyright (c) 2021, The Linux Foundation. All rights reserved.

echo "Setting environment variables..."
MHI_SCRIPT_RELATIVE_DIR=$(dirname "${BASH_SOURCE[0]}")
MHI_CUR_DIR=${PWD##*/}

if [[ $MHI_SCRIPT_RELATIVE_DIR == "." ]]
then
	cd ..
	export MHI_BUILD_ROOT=$(pwd)
	echo Build root directory: $MHI_BUILD_ROOT
	cd $MHI_CUR_DIR
elif [[ $MHI_SCRIPT_RELATIVE_DIR == *"build"* ]]
then
	export MHI_BUILD_ROOT=$(pwd)
	echo Build root directory: $MHI_BUILD_ROOT
else
	echo Please source from build or parent directory
	exit 1
fi

export MHI_KERNEL_VER=$(uname -r)
echo Building for kernel version: $MHI_KERNEL_VER
echo Override and cross-compile for different kernel version using: \""export MHI_KERNEL_VER=<kernel_version>\""
echo On git branch: $(git rev-parse --abbrev-ref HEAD)
