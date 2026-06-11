# pyinfra/deploy.py
# Minimal pyinfra workflow: sync config files and build OAI binaries.

from pyinfra import host
from pyinfra.operations import files, server

# Required host variable
if not hasattr(host.data, "oai_dir"):
    raise ValueError(f"oai_dir variable must be defined for host {host.name}")

# deployment_mode controls what pyinfra does:
# - sync: copy config/scripts to Spark only
# - build: build binaries only
# - build_and_sync: do both (default)
deployment_mode = getattr(host.data, "deployment_mode", "build_and_sync")
if deployment_mode not in ("sync", "build", "build_and_sync"):
    raise ValueError(
        "deployment_mode must be one of: sync, build, build_and_sync "
        f"(got: {deployment_mode})"
    )

should_sync = deployment_mode in ("sync", "build_and_sync")
should_build = deployment_mode in ("build", "build_and_sync")

# ============================================================================
# 1. DISABLE NTP-LIKE SERVICES (ALL HOSTS)
# ============================================================================
server.shell(
    name="Disable NTP-like services",
    commands=[
        "systemctl stop systemd-timesyncd chronyd chrony ntpd ntp openntpd || true",
        "systemctl disable systemd-timesyncd chronyd chrony ntpd ntp openntpd || true",
    ],
    _sudo=True,
)

# ============================================================================
# 2. CONFIG/SCRIPT SYNC (Spark only)
# ============================================================================
if "oru" in host.groups and should_sync:
    # Keep the remote repo persistent and only sync selected config/script files.
    server.shell(
        name="Check persistent Spark repo path exists",
        commands=[
            (
                "if [ ! -d \"{0}\" ]; then "
                "echo 'Missing persistent repo at {0}. Create/clone it first.'; "
                "exit 1; "
                "fi"
            ).format(host.data.oai_dir)
        ],
    )

    server.shell(
        name="Create pyinfra directory on Spark",
        commands=[f"mkdir -p {host.data.oai_dir}/pyinfra"],
    )

    files.put(
        name="Sync Spark SR-IOV script",
        src="pyinfra/spark-setup-ifs.sh",
        dest=f"{host.data.oai_dir}/pyinfra/spark-setup-ifs.sh",
        mode="755",
        _sudo=True,
    )

    files.put(
        name="Sync Spark PTP config",
        src="pyinfra/spark-ptp.conf",
        dest=f"{host.data.oai_dir}/pyinfra/spark-ptp.conf",
        _sudo=True,
    )

    files.put(
        name="Sync Spark RU config",
        src="pyinfra/spark-ru.conf",
        dest=f"{host.data.oai_dir}/pyinfra/spark-ru.conf",
        _sudo=True,
    )

# ============================================================================
# 3. BUILD (local DU + Spark RU)
# ============================================================================
if should_build:
    cmake_configure_flags = "-DOAI_RU_FRONTHAUL=ON" if "oru" in host.groups else "-DOAI_FHI72=ON -Dxran_DOWNLOAD=ON"
    ninja_targets = "nr-oru nr-uesoftmodem vrtsim" if "oru" in host.groups else "nr-softmodem"

    server.shell(
        name="Configure OAI build with CMake",
        commands=[
            "mkdir -p build",
            f"cd build && cmake ../ {cmake_configure_flags} -GNinja",
        ],
        _chdir=host.data.oai_dir,
    )

    server.shell(
        name="Compile OAI binaries with Ninja",
        commands=[
            f"cd build && ninja {ninja_targets} ldpc params_libconfig dfts",
        ],
        _chdir=host.data.oai_dir,
    )
