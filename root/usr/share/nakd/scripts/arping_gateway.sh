#!/bin/sh
arping -c 3 -I $1 $(./gateway_ip.sh)
