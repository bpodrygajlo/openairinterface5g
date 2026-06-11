# pyinfra/inventory.py
# Define inventory for testing OAI O-RU and OAI gNB (O-DU) on two machines.

import os

# Dynamically resolve the local OAI repository root path
repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# O-DU (Current local machine)
# We use "@local" so pyinfra executes operations on this host directly without SSH.
odu = [
    ("@local", {
        "oai_dir": repo_root,
        "ptp_interface": "enp5s0f0np0",
        "ptp_config": os.path.join(repo_root, "pyinfra/oai_ptp-2.conf"),
        "gnb_config": os.path.join(repo_root, "pyinfra/gnb.2x2-for-spark.conf"),
        "setup_script": "pyinfra/setup_du_ifs_for_spark.sh",
    })
]

# O-RU (Remote machine, DGX Spark)
ssh_user = "bpodrygajlo"        # SSH user for DGX Spark
remote_root = f"/home/{ssh_user}/openairinterface5g"

oru = [
    ("10.0.8.1", {
        "oai_dir": remote_root,
        "oru_config": os.path.join(remote_root, "pyinfra/spark-ru.conf"),
        "ptp_interface": "enp1s0f0np0",  # Spark PTP NIC
        "ptp_config": os.path.join(remote_root, "pyinfra/spark-ptp.conf"),
        "setup_script": "pyinfra/spark-setup-ifs.sh",
        "ssh_user": ssh_user,
        # "ssh_key": "~/.ssh/id_rsa",     # Uncomment and specify if custom SSH key is needed
    })
]



