#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2021, The Linux Foundation. All rights reserved.

usage() {
echo "Usage:"
echo ""
echo "$0 <valid kernel version> allows cross-compiling the build and outputs the kernel modules and installer script in the \"out\" directory"
echo "If no argument is specified, compilation will take place using currently running kernel version"
echo ""
echo "Please run $0 as root if cross-compiling for a different kernel version that requires download of headers"
echo "$0 <-h or --help> shows this text"
}

if [[ $1 == "-h" || $1 == "--help" ]]; then
	usage
	exit 0
fi

if [[ $1 == "" ]]
then
	echo "Compiling for currently running kernel version: $(uname -r)"
	echo "In order to (cross)compile for a different one, please specify it as an argument"
elif [[ $1 != $(uname -r) ]]
then
	if [[ $(id -u) -ne 0 ]] ; then echo "Please run as root" ; echo ""; usage; exit 1 ; fi
	if ! apt-get -y install linux-headers-$1; then
		echo "Failed to find/install linux-headers for $1"
		exit 1
	fi
fi

uname_str="(uname -r)"
root_id="(id -u)"
script_dir="(dirname \"\${BASH_SOURCE[0]}\")"

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

export MHI_KERNEL_VER=$1
if [[ $MHI_KERNEL_VER == "" ]]; then export MHI_KERNEL_VER=$(uname -r); fi
echo Building for kernel version: $MHI_KERNEL_VER

if ! bash $MHI_BUILD_ROOT/build/build.sh; then
	echo "Failed to build for $MHI_KERNEL_VER"
	exit 1
fi

mkdir -p $MHI_BUILD_ROOT/out/$MHI_KERNEL_VER/etc/udev/rules.d
mkdir -p $MHI_BUILD_ROOT/out/$MHI_KERNEL_VER/lib/modules/$MHI_KERNEL_VER/kernel

cd $MHI_BUILD_ROOT
find . -path ./out -prune -false -o -name *.ko | sed 's:.*/::' | grep -Po '.*(?=\.)' | xargs -I % sh -c 'grep -qxF % $MHI_BUILD_ROOT/out/$MHI_KERNEL_VER/etc/modules || echo % >> $MHI_BUILD_ROOT/out/$MHI_KERNEL_VER/etc/modules'
find . -path ./out -prune -false -o -name *.ko | xargs -I % cp --parents % $MHI_BUILD_ROOT/out/$MHI_KERNEL_VER/lib/modules/$MHI_KERNEL_VER/kernel
cp build/99-mhi-permissions.rules $MHI_BUILD_ROOT/out/$MHI_KERNEL_VER/etc/udev/rules.d/
cp scripts/debug.sh $MHI_BUILD_ROOT/out/$MHI_KERNEL_VER/etc/mhi_dynamicdebug.sh

if [[ ! -f "out/install.sh" ]]; then
	echo "#!/bin/bash" > out/install.sh
	echo "# This file was auto-generated using cross_compile.sh script." >> out/install.sh
	echo "supported_kernels() {" >> out/install.sh
	echo "echo Supported kernel versions:" >> out/install.sh
	echo "echo $MHI_KERNEL_VER" >> out/install.sh
	echo "}" >> out/install.sh
	echo "script_dir=\$$script_dir" >> out/install.sh
	echo "cd \$$script_dir" >> out/install.sh
	echo "if [ ! -d "\$$uname_str" ]; then echo \"Unsupported kernel version: \$$uname_str\"; echo \"\"; supported_kernels; exit 1; fi" >> out/install.sh
	echo "if [ \$$root_id -ne 0 ] ; then echo \"Please run as root\" ; exit 1 ; fi" >> out/install.sh
	echo "echo Installing following kernel modules for \$$uname_str:" >> out/install.sh
	echo "echo \$(cat \$$uname_str/etc/modules | tr '\n' ' ')" >> out/install.sh
	echo "cat \$$uname_str/etc/modules | xargs -I % sh -c 'grep -qxF % /etc/modules || echo % >> /etc/modules'" >> out/install.sh
	echo "mkdir -p /lib/firmware/qcom/sdx65m" >> out/install.sh
	echo "cp \$$uname_str/etc/udev/rules.d/99-mhi-permissions.rules /etc/udev/rules.d/" >> out/install.sh
	echo "udevadm control --reload" >> out/install.sh
	echo "cp \$$uname_str/etc/mhi_dynamicdebug.sh /etc/mhi_dynamicdebug.sh" >> out/install.sh
	echo "(crontab -l; echo \"@reboot /etc/mhi_dynamicdebug.sh\") 2>/dev/null | sort | uniq | crontab -" >> out/install.sh
	echo "cd \$$uname_str" >> out/install.sh
	echo "find . -path ./etc -prune -false -o -name *.ko | xargs -I % cp --parents % /" >> out/install.sh
	echo "sync" >> out/install.sh
	echo "cd - > /dev/null" >> out/install.sh
	echo "cd /lib/modules/\$$uname_str/" >> out/install.sh
	echo "depmod" >> out/install.sh
	echo "cd - > /dev/null" >> out/install.sh
	echo "cat \$$uname_str/etc/modules | xargs -I % modprobe %" >> out/install.sh
	echo "cd - > /dev/null" >> out/install.sh
	echo "bash /etc/mhi_dynamicdebug.sh" >> out/install.sh
	echo "echo Intallation complete" >> out/install.sh
	chmod +x out/install.sh
else
	if [[ ! $(grep "$MHI_KERNEL_VER" out/install.sh) ]]; then
		sed -i '/Supported kernel versions:/a echo '$MHI_KERNEL_VER'' out/install.sh
	fi
fi

if [[ ! -f "out/status.sh" ]]; then
	cp $MHI_BUILD_ROOT/scripts/status.sh $MHI_BUILD_ROOT/out/
	chmod o+rx out/status.sh
fi

if [[ ! -f "out/debug.sh" ]]; then
	cp $MHI_BUILD_ROOT/scripts/debug.sh $MHI_BUILD_ROOT/out/
	chmod o+rx out/debug.sh
fi

if [[ ! -f "out/uninstall.sh" ]]; then
	echo "#!/bin/bash" > out/uninstall.sh
	echo "# This file was auto-generated using cross_compile.sh script." >> out/uninstall.sh
	echo "if [ \$$root_id -ne 0 ] ; then echo \"Please run as root\" ; exit 1 ; fi" >> out/uninstall.sh
	find . -path ./drivers/bus/mhi -prune -false -o -name *.ko | grep -v "out" | tac | sed 's:.*/::' | grep -Po '.*(?=\.)' | xargs -I % echo "modprobe -qr %" >> out/uninstall.sh
	find ./drivers/bus/mhi -name *.ko | tac | sed 's:.*/::' | grep -Po '.*(?=\.)' | xargs -I % echo "modprobe -qr %" >> out/uninstall.sh
	find . -path ./out -prune -false -o -name *.ko | sed 's:.*/::' | grep -Po '.*(?=\.)' | xargs -I % echo "sed -i '/\<%\>/d' /etc/modules" >> out/uninstall.sh
	find . -path ./out -prune -false -o -name *.ko | sed -r 's/^.{2}//' | xargs -I % echo "rm /lib/modules/\$$uname_str/kernel/%" >> out/uninstall.sh
	echo "rm /etc/udev/rules.d/99-mhi-permissions.rules" >> out/uninstall.sh
	echo "udevadm control --reload" >> out/uninstall.sh
	echo "rm /etc/mhi_dynamicdebug.sh" >> out/uninstall.sh
	echo "(crontab -l; echo \"@reboot /etc/mhi_dynamicdebug.sh\") 2>/dev/null | grep -v /etc/mhi_dynamicdebug.sh | sort | uniq | crontab -" >> out/uninstall.sh
	echo "cd /lib/modules/\$$uname_str" >> out/uninstall.sh
	echo "depmod" >> out/uninstall.sh
	echo "cd - > /dev/null" >> out/uninstall.sh
	echo "update-initramfs -c -k \$$uname_str" >> out/uninstall.sh
	echo "Updated initramfs. If possible, please reboot to ensure sanity check that drivers are indeed removed."
	chmod +x out/uninstall.sh
fi

cd - > /dev/null

bash $MHI_BUILD_ROOT/build/build.sh clean

echo "Copied KOs to $MHI_BUILD_ROOT/out/$MHI_KERNEL_VER"
