#!/bin/sh
#
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.

dkms_tree=$1
kernelver=$2
op=$3
SRC_DIR=$dkms_tree/$module/$module_version/build
DST_DIR=/lib/modules/$kernelver
DST_FILE_PATH=$DST_DIR/Module-mhi.symvers
DRACUT_CONF_FILE=/etc/dracut.conf.d/mhi-host.conf

if [ "$op" == "build" ];
then
    cp $SRC_DIR/Module.symvers $DST_FILE_PATH
    echo 'omit_drivers+=" mhi mhi_uci mhi_pci mhi_net wwan_mhi wwan "' > $DRACUT_CONF_FILE
elif [ "$op" == "remove" ];
then
    rm -f $DST_FILE_PATH
    rm -f $DRACUT_CONF_FILE
    rmmod mhi_uci wwan_mhi mhi_pci mhi_net wwan mhi > /dev/null 2>&1
fi
