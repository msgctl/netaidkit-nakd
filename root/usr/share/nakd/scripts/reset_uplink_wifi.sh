#!/bin/sh

# reset uplink wifi
uci set wireless.@wifi-iface[0].disabled=1
uci set wireless.@wifi-iface[0].ssid='';
uci set wireless.@wifi-iface[0].encryption='';
uci set wireless.@wifi-iface[0].key='';
uci commit wireless

/usr/share/nakd/scripts/restart_iface.sh wwan
