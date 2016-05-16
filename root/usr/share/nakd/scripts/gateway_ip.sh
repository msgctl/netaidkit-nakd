#!/bin/sh
route -n | sed -n '3p' | awk '{print $2}'
