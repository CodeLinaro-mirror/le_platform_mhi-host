!/bin/bash
ADB=/usr/bin/adb
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
sudo rm eomloglane.txt
DEVICES=`ls /sys/bus/mhi/devices/ -l | grep -w  mhi._IP_SW1 | wc -l`

DEVICES=$((DEVICES-1))
for (( j = 0; j <= $DEVICES; j++))
do
        DEVICE_ID="192.200.101.$j:5555"
        FOLDER="DEVICE_$j/$(date +%Y%m%d%H%M%S)"
        $ADB connect $DEVICE_ID
        mkdir -p $FOLDER

				sudo mkdir -m 777 $FOLDER/positive_sequence_logs
				sudo mkdir -m 777 $FOLDER/negative_sequence_logs
				sudo chmod -R 777 DEVICE_$j

				$ADB -s $DEVICE_ID shell "echo 26 > /sys/kernel/debug/pcie-ep/case"

				for i in {0..15}
				do
					$ADB -s $DEVICE_ID pull /sys/kernel/debug/ipc_logging/ep-pcie-eom-$i/log $FOLDER/positive_sequence_logs/lane$i.txt
				done

				$ADB -s $DEVICE_ID shell "echo 27 > /sys/kernel/debug/pcie-ep/case"

				for i in {0..15}
				do
					$ADB -s $DEVICE_ID pull /sys/kernel/debug/ipc_logging/ep-pcie-eom-$i/log $FOLDER/negative_sequence_logs/lane$i.txt
				done

				sudo python3 $SCRIPT_DIR/lassen_log_parser_script.py -folder $FOLDER
				sudo chmod -R 777 DEVICE_$j

done

