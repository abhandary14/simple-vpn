#!/bin/bash
set -e

if [ $# -ne 1 ]; then
    echo "Usage: $0 <setup_server_script>" >&2
    exit 1
fi

SETUP_SCRIPT=$1

sysctl()
{
    printf 'sysctl'
    printf ' <%s>' "$@"
    printf '\n'
}

iptables()
{
    printf 'iptables'
    printf ' <%s>' "$@"
    printf '\n'
}

export -f sysctl iptables

expected_default='sysctl <-w> <net.ipv4.ip_forward=1>
iptables <-t> <nat> <-A> <POSTROUTING> <-o> <eth0> <-j> <MASQUERADE>
iptables <-A> <FORWARD> <-i> <tun0> <-o> <eth0> <-j> <ACCEPT>
iptables <-A> <FORWARD> <-i> <eth0> <-o> <tun0> <-m> <state> <--state> <ESTABLISHED,RELATED> <-j> <ACCEPT>'

actual_default=$(bash "$SETUP_SCRIPT" eth0)
if [ "$actual_default" != "$expected_default" ]; then
    echo "default-interface command test failed" >&2
    exit 1
fi

expected_custom='sysctl <-w> <net.ipv4.ip_forward=1>
iptables <-t> <nat> <-A> <POSTROUTING> <-o> <ens5> <-j> <MASQUERADE>
iptables <-A> <FORWARD> <-i> <tun7> <-o> <ens5> <-j> <ACCEPT>
iptables <-A> <FORWARD> <-i> <ens5> <-o> <tun7> <-m> <state> <--state> <ESTABLISHED,RELATED> <-j> <ACCEPT>'

actual_custom=$(bash "$SETUP_SCRIPT" ens5 tun7)
if [ "$actual_custom" != "$expected_custom" ]; then
    echo "custom-interface command test failed" >&2
    exit 1
fi

if bash "$SETUP_SCRIPT" >/dev/null 2>&1; then
    echo "missing-argument test failed" >&2
    exit 1
fi

if bash "$SETUP_SCRIPT" eth0 tun0 extra >/dev/null 2>&1; then
    echo "extra-argument test failed" >&2
    exit 1
fi

printf 'Server setup script tests passed.\n'
