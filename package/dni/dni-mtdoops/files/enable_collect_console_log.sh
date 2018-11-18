#!/bin/sh

echo enable > /sys/devices/platform/serial8250/console
/bin/config set netscan_debug=1

/sbin/basic_log.sh &
/sbin/console_log.sh &
/sbin/thermal_log.sh &
/sbin/wireless_log.sh &
/sbin/logread_log.sh &
/sbin/debug_here_log.sh &
/sbin/debug_circle.sh &
if [ "`/bin/config get enable_arlo_function`" = "1" ];then
/sbin/arlo_log.sh &
fi
/sbin/wireless_client_statistic_log.sh &
/sbin/capture_packet.sh 
