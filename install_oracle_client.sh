#!/usr/bin/env bash
# install_oracle_client.sh — Install Oracle Instant Client (Basic + SQL*Plus)
# Uses Oracle's permanent 'latest' download URLs (no version pinned).
# Works on Ubuntu 22.04, 24.04, Debian, Oracle Linux, RHEL.

set -euo pipefail

IC_DIR="/opt/oracle"
IC_LINK="${IC_DIR}/instantclient"
IC_PROFILE="/etc/profile.d/oracle-ic.sh"

# Oracle permanent 'latest' URLs — always point to the current release
BASE="https://download.oracle.com/otn_software/linux/instantclient"
BASIC_ZIP="instantclient-basic-linuxx64.zip"
SQLPLUS_ZIP="instantclient-sqlplus-linuxx64.zip"

echo "==> Oracle Instant Client (latest) installer"
echo "    Destination: ${IC_DIR}"

# ── System dependencies ────────────────────────────────────────────────────────
install_deps() {
    if command -v apt-get >/dev/null 2>&1; then
        echo "==> Installing apt dependencies..."
        sudo apt-get update -qq
        # Ubuntu 24.04 renamed libaio1 → libaio1t64
        sudo apt-get install -y libaio1t64 unzip curl 2>/dev/null || \
            sudo apt-get install -y libaio1 unzip curl
        # Symlink for older binaries that look for libaio.so.1
        if [[ ! -f /usr/lib/x86_64-linux-gnu/libaio.so.1 ]]; then
            LIB=$(find /usr/lib/x86_64-linux-gnu -name 'libaio.so.1*' 2>/dev/null | head -1)
            if [[ -n "${LIB}" ]]; then
                sudo ln -sf "${LIB}" /usr/lib/x86_64-linux-gnu/libaio.so.1 2>/dev/null || true
            fi
        fi
    elif command -v dnf >/dev/null 2>&1; then
        echo "==> Installing dnf dependencies..."
        sudo dnf install -y libaio unzip curl
    elif command -v yum >/dev/null 2>&1; then
        sudo yum install -y libaio unzip curl
    fi
}

# ── Download and extract zips ──────────────────────────────────────────────────
download_and_install() {
    local TMPD
    TMPD=$(mktemp -d)
    trap 'rm -rf "${TMPD}"' EXIT

    echo "==> Downloading ${BASIC_ZIP}..."
    if ! curl -fL --retry 3 "${BASE}/${BASIC_ZIP}" -o "${TMPD}/basic.zip"; then
        echo "[ERROR] Download failed. You can download manually from:"
        echo "  https://www.oracle.com/database/technologies/instant-client/linux-x86-64-downloads.html"
        echo "Save as ${TMPD}/basic.zip and ${TMPD}/sqlplus.zip then re-run."
        exit 1
    fi

    echo "==> Downloading ${SQLPLUS_ZIP}..."
    if ! curl -fL --retry 3 "${BASE}/${SQLPLUS_ZIP}" -o "${TMPD}/sqlplus.zip"; then
        echo "[ERROR] SQLPlus download failed."
        exit 1
    fi

    echo "==> Extracting to ${IC_DIR}..."
    sudo mkdir -p "${IC_DIR}"
    sudo unzip -q -o "${TMPD}/basic.zip"  -d "${IC_DIR}"
    sudo unzip -q -o "${TMPD}/sqlplus.zip" -d "${IC_DIR}"
}

# ── Locate extracted directory and symlink ─────────────────────────────────────
setup_paths() {
    # Find the extracted directory (e.g. instantclient_21_13)
    local IC_REAL
    IC_REAL=$(find "${IC_DIR}" -maxdepth 1 -type d -name 'instantclient_*' | sort | tail -1)
    if [[ -z "${IC_REAL}" ]]; then
        echo "[ERROR] Could not find extracted instantclient directory in ${IC_DIR}"
        exit 1
    fi

    echo "==> Found: ${IC_REAL}"

    # Stable symlink
    sudo ln -snf "${IC_REAL}" "${IC_LINK}"

    # System-wide profile (PATH + LD_LIBRARY_PATH)
    sudo tee "${IC_PROFILE}" > /dev/null << EOF
# Oracle Instant Client
export PATH="\$PATH:${IC_LINK}"
export LD_LIBRARY_PATH="\$LD_LIBRARY_PATH:${IC_LINK}"
export ORACLE_HOME="${IC_LINK}"
EOF

    # Also update ldconfig
    echo "${IC_LINK}" | sudo tee /etc/ld.so.conf.d/oracle-instantclient.conf > /dev/null
    sudo ldconfig 2>/dev/null || true

    # Source for this session
    export PATH="$PATH:${IC_LINK}"
    export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}:${IC_LINK}"
    hash -r
}

# ── Run ────────────────────────────────────────────────────────────────────────
install_deps
download_and_install
setup_paths

# ── Verify ────────────────────────────────────────────────────────────────────
if command -v sqlplus >/dev/null 2>&1 || "${IC_LINK}/sqlplus" -V >/dev/null 2>&1; then
    echo "==> sqlplus is ready."
else
    echo "[WARN] sqlplus still not in PATH. Run:"
    echo "  source ${IC_PROFILE}"
    echo "  # or restart your shell"
fi

echo ""
echo "==> Done. Run:  source ${IC_PROFILE}  then:  make oracle_test"
