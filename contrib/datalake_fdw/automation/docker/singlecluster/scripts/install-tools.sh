#!/usr/bin/env bash
# Install common debugging and utility tools
set -euo pipefail

echo "[install-tools] Installing basic packages..."

apt-get update && apt-get install -y --no-install-recommends \
    vim \
    iputils-ping \
    telnet \
    wget \
    curl \
    net-tools \
    dnsutils \
    htop \
    less \
    tree \
    jq

echo "[install-tools] Cleaning up apt cache..."
rm -rf /var/lib/apt/lists/*

echo "[install-tools] Done!"
