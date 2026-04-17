#!/bin/bash
set -e

HIVE_VERSION="${HIVE_VERSION:-3.1.3}"
INSTALL_DIR="/opt/hive"

echo "Installing Apache Hive ${HIVE_VERSION}..."

# Download Hive
HIVE_URL="https://archive.apache.org/dist/hive/hive-${HIVE_VERSION}/apache-hive-${HIVE_VERSION}-bin.tar.gz"
TMP_DIR=$(mktemp -d)

echo "Downloading from ${HIVE_URL}..."
curl -L "${HIVE_URL}" -o "${TMP_DIR}/hive.tar.gz"

echo "Extracting to ${INSTALL_DIR}..."
sudo mkdir -p "${INSTALL_DIR}"
sudo tar -xzf "${TMP_DIR}/hive.tar.gz" -C "${INSTALL_DIR}" --strip-components=1

# Set environment variables
echo "Setting up environment variables..."
sudo tee /etc/profile.d/hive.sh > /dev/null << EOF
export HIVE_HOME=${INSTALL_DIR}
export PATH=\$HIVE_HOME/bin:\$PATH
EOF

# Create symlinks for easy access
sudo ln -sf "${INSTALL_DIR}/bin/hive" /usr/local/bin/hive
sudo ln -sf "${INSTALL_DIR}/bin/beeline" /usr/local/bin/beeline

# Source environment for current session
export HIVE_HOME="${INSTALL_DIR}"
export PATH="${HIVE_HOME}/bin:${PATH}"

# Cleanup
rm -rf "${TMP_DIR}"

echo ""
echo "============================================"
echo "Hive and Beeline installed successfully!"
echo "============================================"
echo "Installation directory: ${INSTALL_DIR}"
echo ""
echo "To use in current session, run:"
echo "  source /etc/profile.d/hive.sh"
echo ""
echo "Or use directly:"
echo "  hive --version"
echo "  beeline --version"
echo "============================================"

# Test installation
"${INSTALL_DIR}/bin/beeline" --version 2>&1 | head -5 || true
