#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2021, The Linux Foundation. All rights reserved.

status=$(lsmod | grep mhi)

usage() {
echo "Usage:"
echo ""
echo "$0 run by *non-root user* will show if MHI related kernel modules are loaded or not"
echo "$0 run by *root user* will show above information along with states output"
echo "*root user* has the option to specify <-u or --update> and <-v or --verbose> flags"
echo "<-v or --verbose> shows states, channels, and events output"
echo "<-u or --update> monitors the outputs every second"
echo "<-h or --help> shows this text"
}

if [[ $1 == "-h" || $1 == "--help" ]]; then
	usage
	exit 0
fi

if [[ $status ]]; then
	echo Status: Running
	echo ""
	echo Modules loaded:
	lsmod | grep mhi
else
	echo Status: Not running
	echo ""
	if [[ $(id -u) == 0 && $1 == "-u" || $1 == "--update" || $2 == "-u" || $2 == "--update" ]]; then
		while [ ! -d "/sys/kernel/debug/mhi/mhi*" ]
		do
			sleep 1;
		done;
	else
		usage
		exit 1
	fi
fi

if [[ $1 == "-v" || $1 == "--verbose" || $2 == "-v" || $2 == "--verbose" ]]; then
	verbose=true
fi

if [[ $1 == "-u" || $1 == "--update" || $2 == "-u" || $2 == "--update" ]]; then
	update=true
fi

echo
if [[ $status != "" && $(id -u) == 0 && -d "/sys/kernel/debug/mhi/" ]]
then
	if [[ $verbose && $update ]]; then
		find /sys/kernel/debug/mhi/ -name mhi* | tail -n +2 | sed 's:.*/::' | xargs -I % sh -c 'echo "%:" && watch -n 1 "echo "states:" && cat /sys/kernel/debug/mhi/%/states && echo "channels:" && (cat /sys/kernel/debug/mhi/%/channels || true) && echo "events:" && (cat /sys/kernel/debug/mhi/%/events || true)"'
	elif [[ $verbose ]]; then
		find /sys/kernel/debug/mhi/ -name mhi* | tail -n +2 | sed 's:.*/::' | xargs -I % sh -c 'echo "%:" && echo "states:" && cat /sys/kernel/debug/mhi/%/states && echo "channels:" && cat /sys/kernel/debug/mhi/%/channels || true && echo "events:" && cat /sys/kernel/debug/mhi/%/events || true'
	elif [[ $update ]]; then
		find /sys/kernel/debug/mhi/ -name mhi* | tail -n +2 | sed 's:.*/::' | xargs -I % sh -c 'echo "%:" && watch -n 1 "echo "states:" && cat /sys/kernel/debug/mhi/%/states || true"'
	else
		find /sys/kernel/debug/mhi/ -name mhi* | tail -n +2 | sed 's:.*/::' | xargs -I % sh -c 'echo "%:" && echo "states:" && cat /sys/kernel/debug/mhi/%/states || true'
	fi
fi
