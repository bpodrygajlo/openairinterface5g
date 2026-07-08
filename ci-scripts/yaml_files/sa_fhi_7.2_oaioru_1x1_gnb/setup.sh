#!/bin/sh
# SPDX-License-Identifier: MIT

set -e
# Set these in the DU config
RU_U_PLANE_MAC=00:11:22:33:64:66
RU_C_PLANE_MAC=00:11:22:33:64:67
DU_U_PLANE_MAC=00:11:22:33:64:68
DU_C_PLANE_MAC=00:11:22:33:64:69
U_VLAN=3
C_VLAN=4

sudo cpupower idle-set -D 0 > /dev/null
sudo sysctl kernel.sched_rt_runtime_us=-1
sudo sysctl kernel.timer_migration=0

MTU=9216
IF=enp5s0f0np0
sudo ethtool -G $IF rx 8160 tx 8160
echo 0 | sudo tee /sys/bus/pci/devices/0000\:05\:00.0/sriov_numvfs
echo 4 | sudo tee /sys/bus/pci/devices/0000\:05\:00.0/sriov_numvfs

# this next 2 lines is for C/U planes
sudo ip link set $IF vf 0 mac $RU_U_PLANE_MAC vlan $U_VLAN qos 0 spoofchk off mtu $MTU
sudo ip link set $IF vf 1 mac $RU_C_PLANE_MAC vlan $C_VLAN qos 0 spoofchk off mtu $MTU
# this next 2 lines is for DU C/U planes
sudo ip link set $IF vf 2 mac $DU_U_PLANE_MAC vlan $U_VLAN qos 0 spoofchk off mtu $MTU
sudo ip link set $IF vf 3 mac $DU_C_PLANE_MAC vlan $C_VLAN qos 0 spoofchk off mtu $MTU

sleep 1
sudo /usr/local/bin/dpdk-devbind.py --unbind 05:02.0
sudo /usr/local/bin/dpdk-devbind.py --unbind 05:02.1
sudo /usr/local/bin/dpdk-devbind.py --unbind 05:02.2
sudo /usr/local/bin/dpdk-devbind.py --unbind 05:02.3
sudo modprobe vfio-pci
sudo /usr/local/bin/dpdk-devbind.py --bind vfio-pci 05:02.0
sudo /usr/local/bin/dpdk-devbind.py --bind vfio-pci 05:02.1
sudo /usr/local/bin/dpdk-devbind.py --bind vfio-pci 05:02.2
sudo /usr/local/bin/dpdk-devbind.py --bind vfio-pci 05:02.3
exit 0
