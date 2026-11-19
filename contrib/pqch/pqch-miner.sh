#!/bin/sh
# PQCH one-click mining app (testnet until the main network launches).
exec "$(dirname "$(readlink -f "$0")")/bin/pqch-qt" -testnet -minerapp "$@"
