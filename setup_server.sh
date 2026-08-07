#!/bin/bash
# Enables IP forwarding and NATs traffic from the tun0 client subnet out
# through the given egress interface (FR-5.1/5.2). Run once, after the
# server's `tunnelink --mode server` is running and tun0 exists.
set -e

if [ $# -ne 1 ]; then
    echo "Usage: $0 <egress_iface>" >&2
    exit 1
fi

IFACE=$1

sysctl -w net.ipv4.ip_forward=1
iptables -t nat -A POSTROUTING -o "$IFACE" -j MASQUERADE
iptables -A FORWARD -i tun0 -o "$IFACE" -j ACCEPT
iptables -A FORWARD -i "$IFACE" -o tun0 -m state --state ESTABLISHED,RELATED -j ACCEPT