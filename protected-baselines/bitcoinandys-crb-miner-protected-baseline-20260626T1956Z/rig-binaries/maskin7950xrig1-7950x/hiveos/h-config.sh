#!/usr/bin/env bash
# Build the nmminer argument file from the Flight Sheet fields.
# Hive SOURCES this script, so we locate ourselves via BASH_SOURCE (not $0).
# Hive provides: CUSTOM_TEMPLATE (Wallet&Worker), CUSTOM_URL (Pool URL),
# CUSTOM_USER_CONFIG (Extra config arguments).
HCDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[[ -f "$HCDIR/h-manifest.conf" ]] && . "$HCDIR/h-manifest.conf"

# Wallet template -> -addr. The miner accepts Meow-style crb1...worker logins:
# the bare crb1 address is validated for payout, and the full value is sent to
# stratum so pools can show the worker name.
ADDR=$(echo "$CUSTOM_TEMPLATE" | tr -d ' ')

# Pool URL -> -o. Stratum is the default transport (most reliable):
#   stratum+tcp://stratum.cereblix.com:3333 (pool) | :3334 (solo)
#   ru/us/asia.cereblix.com:3333 (regions) | https://cereblix.com/pool/api (HTTP)
NODE="$CUSTOM_URL"
[[ -z "$NODE" ]] && NODE="stratum+tcp://stratum.cereblix.com:3333"

# CUSTOM_USER_CONFIG = optional extra flags (rarely needed - the miner auto-tunes
# cores/NUMA/fill). Examples: "-threads 120" or "-smt" or "-lanes auto".
EXTRA="$CUSTOM_USER_CONFIG"

echo "-o $NODE -u $ADDR $EXTRA" > "${CUSTOM_CONFIG_FILENAME:-$HCDIR/unm.conf}"
