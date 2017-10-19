#!/bin/bash
#
# Copyright (c) 2016 NVIDIA CORPORATION.  All Rights Reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.
#

############ PING & FAN RPM TEST ##########

target_board=`cat /proc/device-tree/model`

check_fan_rpm=0

echo "start send ping every 1sec"

if (( check_fan_rpm != 0 )); then
echo "check fan RPM every 5sec"
fi

cnt=0
cmd_out=0
fan_rpm=1400

while [ "$cmd_out" -eq 0 ]
do
    if (( cnt % 2 == 0 )); then
         ./tacp_test_app ping
    fi

    if (( cnt % 10 == 0 )) && (( check_fan_rpm != 0 )); then
        #echo "5sec task..."
        echo 1 > /sys/devices/pwm-fan/tach_enabled
        sleep 0.5
	fan_rpm=`cat /sys/devices/pwm-fan/rpm_measured`
        #echo FAN RPM is "$fan_rpm"
	echo 0 > /sys/devices/pwm-fan/tach_enabled
        if [ "$fan_rpm" -lt 1100 ] || [ "$fan_rpm" -gt 5500 ]; then
            echo "RPM is out of boundary"
            ./tacp_test_app report_fan_state bad
            cmd_out=$?
        fi
    else
        sleep 0.5
    fi

    let "cnt += 1"
done

if [ $cmd_out -ne 0 ]; then
    echo "[fail: tacp_ping_test]"
    exit 1
fi
