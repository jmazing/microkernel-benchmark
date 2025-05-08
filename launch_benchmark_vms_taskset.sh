#!/usr/bin/env bash
set -euo pipefail

# — CONFIGURATION —
QNX_VM="qnx_benchmark"
UBUNTU_VM="ubuntu_benchmark"
QNX_CPU=3
UBUNTU_CPU=2

# Find a VM's UUID from its registered name
get_uuid() {
  VBoxManage list vms \
    | awk -v name="$1" '$0 ~ name { gsub(/"/,""); print $2 }'
}

launch_direct() {
  local VM=$1 CPU=$2 UUID
  UUID=$(get_uuid "$VM")
  if [[ -z "$UUID" ]]; then
    echo "VM '$VM' not found in VBoxManage list"
    exit 1
  fi

  echo "Launching headless VM '$VM' (UUID=$UUID) on core $CPU"
  taskset -c $CPU /usr/lib/virtualbox/VBoxHeadless \
    --comment "$VM" --startvm "$UUID" --vrde config \
    &>/dev/null &
}

# Launch and pin both VMs
launch_direct "$QNX_VM"    $QNX_CPU
launch_direct "$UBUNTU_VM" $UBUNTU_CPU

# Give them a moment to spin up
sleep 2

# Verification: list all VBoxHeadless threads and their PSR
echo
echo "🧪 Verification (PID  PSR  CMD):"
ps -eLo pid,psr,cmd \
  | grep -E "VBoxHeadless.*($QNX_VM|$UBUNTU_VM)" \
  | sort -u
