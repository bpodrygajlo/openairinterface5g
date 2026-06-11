#!/bin/bash
set -x
# Set these in the DU config
RU_U_PLANE_MAC=00:11:22:33:64:66
RU_C_PLANE_MAC=00:11:22:33:64:67
U_VLAN=3
C_VLAN=4

MTU=9216
IF=enp1s0f0np0
sudo sh -c "echo 0 > /sys/class/net/$IF/device/sriov_numvfs"
sudo sh -c "echo 2 > /sys/class/net/$IF/device/sriov_numvfs"
sudo ethtool -G $IF rx 8160 tx 8160

# this next 2 lines is for C/U planes
sudo ip link set $IF vf 0 mac $RU_U_PLANE_MAC vlan $U_VLAN qos 0 spoofchk off mtu $MTU
sudo ip link set $IF vf 1 mac $RU_C_PLANE_MAC vlan $C_VLAN qos 0 spoofchk off mtu $MTU

sudo modprobe vfio-pci

# bind to vfio-pci
sleep 1
sudo /usr/local/bin/dpdk-devbind.py --unbind 0000:01:00.2
sudo /usr/local/bin/dpdk-devbind.py --unbind 0000:01:01.2
sudo /usr/local/bin/dpdk-devbind.py --bind mlx5_core 0000:01:00.2
sudo /usr/local/bin/dpdk-devbind.py --bind mlx5_core 0000:01:01.2
sleep 1
