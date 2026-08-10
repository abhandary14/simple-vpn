#!/bin/bash
# Enables IP forwarding and NATs traffic from the tun0 client subnet out
# through the given egress interface (FR-5.1/5.2). Run once, after the
# server's `tunnelink --mode server` is running. The TUN interface defaults
# to tun0, but an alternate kernel-assigned name can be supplied.
set -e

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    echo "Usage: $0 <egress_iface> [tun_iface]" >&2
    exit 1
fi

IFACE=$1
TUN_IFACE=${2:-tun0}

sysctl -w net.ipv4.ip_forward=1
iptables -t nat -A POSTROUTING -o "$IFACE" -j MASQUERADE
iptables -A FORWARD -i "$TUN_IFACE" -o "$IFACE" -j ACCEPT
iptables -A FORWARD -i "$IFACE" -o "$TUN_IFACE" -m state --state ESTABLISHED,RELATED -j ACCEPT
