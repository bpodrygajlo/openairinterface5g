#!/bin/bash
set -x
# Set these in the DU config
DU_U_PLANE_MAC=00:11:22:33:64:68
DU_C_PLANE_MAC=00:11:22:33:64:69
UPLANE_CAPTURE_MAC=00:11:22:33:64:70
CPLANE_CAPTURE_MAC=00:11:22:33:64:71
U_VLAN=3
C_VLAN=4

MTU=9216
IF=enp5s0f0np0
echo 0 | sudo tee /sys/bus/pci/devices/0000\:05\:00.0/sriov_numvfs
echo 4 | sudo tee /sys/bus/pci/devices/0000\:05\:00.0/sriov_numvfs

# this next 2 lines is for DU C/U planes
sudo ip link set $IF vf 0 mac $DU_U_PLANE_MAC vlan $U_VLAN qos 0 spoofchk off mtu $MTU
sudo ip link set $IF vf 1 mac $DU_C_PLANE_MAC vlan $C_VLAN qos 0 spoofchk off mtu $MTU
# Used for capturing U-plane traffic. To capture, configure MAC in gnb/ORU config file
sudo ip link set $IF vf 2 mac $UPLANE_CAPTURE_MAC vlan $U_VLAN qos 0 spoofchk off mtu $MTU
sudo ip link set $IF vf 2 trust on
sudo ip link set $IF vf 3 mac $CPLANE_CAPTURE_MAC vlan $C_VLAN qos 0 spoofchk off mtu $MTU
sudo ip link set $IF vf 3 trust on

# bind to vfio-pci
sleep 1
sudo /usr/local/bin/dpdk-devbind.py --unbind 05:02.0
sudo /usr/local/bin/dpdk-devbind.py --unbind 05:02.1
sudo /usr/local/bin/dpdk-devbind.py --unbind 05:02.2
sudo /usr/local/bin/dpdk-devbind.py --unbind 05:02.3
sudo modprobe vfio-pci
sudo /usr/local/bin/dpdk-devbind.py --bind vfio-pci 05:02.0
sudo /usr/local/bin/dpdk-devbind.py --bind vfio-pci 05:02.1
sudo /usr/local/bin/dpdk-devbind.py --bind iavf 05:02.2
sudo /usr/local/bin/dpdk-devbind.py --bind iavf 05:02.3
sleep 5
