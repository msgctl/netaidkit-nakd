#!/bin/sh
arping -f -q -w 5 -I $1 $(./util/gateway_ip.sh)
