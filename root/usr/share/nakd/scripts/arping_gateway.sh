#!/bin/sh
arping -f -q -w 5 -I $1 $(./gateway_ip.sh)
