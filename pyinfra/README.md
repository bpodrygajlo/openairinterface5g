# OAI O-DU & O-RU Build/Config Sync (pyinfra)

This directory contains a minimal `pyinfra` workflow used only for:

- Building OAI binaries on local DU and Spark RU.
- Syncing selected config/script files to a persistent Spark repository.

Runtime process control is handled via `mprocs` YAML workflows.

## Directory Structure

All files related to the orchestration, network configuration, and processes are self-contained in this directory:
- `inventory.py`: Defines O-DU (local) and O-RU (Spark) hosts and variables.
- `deploy.py`: The pyinfra script orchestrating the setup.
- `oai_ptp-2.conf`: PTP configuration for the O-DU (Grandmaster).
- `gnb.2x2-for-spark.conf`: OAI gNB/DU config for the O-DU node.
- `setup_du_ifs_for_spark.sh`: SR-IOV configuration script for the O-DU node.
- `spark-ptp.conf`: PTP configuration for the O-RU (Slave).
- `spark-ru.conf`: OAI O-RU config for the O-RU node.
- `spark-setup-ifs.sh`: SR-IOV configuration script for the O-RU node.
- `test_oru.yaml`: Local mprocs simulation testing workflow config.

## Architecture & Workflow

- **O-DU (Current Machine):**
  - Builds OAI binaries for DU runtime.

- **O-RU (DGX Spark - `10.0.8.1`):**
  - Uses a persistent existing repo path (no repo clone/fetch/checkout by pyinfra).
  - Syncs selected config/script files (`spark-setup-ifs.sh`, `spark-ptp.conf`, `spark-ru.conf`).
  - Builds OAI RU/UE binaries.

---

## Installation

On the current machine, install `pyinfra` via pip:

```bash
pip install pyinfra
```

---

## Configuration

Before running, inspect and modify the configurations inside `pyinfra/inventory.py`:

- **O-DU Host (`@local`):**
  - `ptp_interface`: Physical interface used for the PTP Grandmaster connection (e.g., `enp5s0f0np0`).
  - `ptp_config`: Local/dest path to the PTP configuration file.
  - `gnb_config`: Local/dest path to the OAI gNB configuration file.

- **O-RU Host (`10.0.8.1`):**
  - `oai_dir`: Directory where the OAI repo will be stored on Spark.
  - `oru_config`: Remote path where the O-RU configuration file will reside.
  - `ptp_interface`: Spark PTP physical interface name (e.g. `enp1s0f0np0`).
  - `ptp_config`: Remote path where the PTP configuration file will reside.
  - `ssh_user`: Remote username used to log in to Spark.

---

## Pyinfra Execution (Build/Sync Only)

Run the deployment using the following command from the root of your `openairinterface5g` repository:

```bash
# Dry run to see what pyinfra will do
pyinfra pyinfra/inventory.py pyinfra/deploy.py --dry

# Build and sync (default mode)
pyinfra pyinfra/inventory.py pyinfra/deploy.py

# Sync Spark config files only
pyinfra pyinfra/inventory.py pyinfra/deploy.py --data deployment_mode=sync

# Build only (no config sync)
pyinfra pyinfra/inventory.py pyinfra/deploy.py --data deployment_mode=build
```

## mprocs Workflows (No pyinfra)

If you prefer fast iterative testing without pyinfra orchestration, use these split `mprocs` configs:

- [pyinfra/mprocs_ptp.yaml](pyinfra/mprocs_ptp.yaml): local+Spark `ptp4l`, plus local+Spark `phc2sys` clock synchronizers.
- [pyinfra/mprocs_sriov.yaml](pyinfra/mprocs_sriov.yaml): local+Spark SR-IOV setup scripts.
- [pyinfra/mprocs_runtime.yaml](pyinfra/mprocs_runtime.yaml): local `nr-softmodem` and Spark `nr-oru` + `nr-uesoftmodem`.
- [pyinfra/mprocs_sync.yaml](pyinfra/mprocs_sync.yaml): run pyinfra config sync only.

> [!NOTE]
> **Clock Synchronization & Stepping Configuration:**
> To prevent timing drift between the O-DU (gNB) and O-RU nodes, `local_phc2sys` is run alongside `spark_phc2sys` to align both host clocks (`CLOCK_REALTIME`) to their respective NIC hardware clocks.
> Both `ptp4l` configurations (`spark-ptp.conf` and `oai_ptp-2.conf`) utilize a `step_threshold` and `first_step_threshold` of `0.00002` (20µs), and `phc2sys` commands utilize the `-F 0.00002` flag. This ensures the clocks step immediately to resolve large offsets (e.g. the initial 37s offset between CPU and NIC oscillators) rather than attempting slow, infinite frequency adjustments.

All Spark commands use a persistent repo path:

- `/home/bpodrygajlo/openairinterface5g`

Example usage:

```bash
mprocs --config pyinfra/mprocs_sync.yaml
mprocs --config pyinfra/mprocs_sriov.yaml
mprocs --config pyinfra/mprocs_ptp.yaml
mprocs --config pyinfra/mprocs_runtime.yaml
```

---
