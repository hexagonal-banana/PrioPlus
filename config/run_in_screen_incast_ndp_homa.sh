#!/bin/bash

set -euo pipefail

CONFIGS=(
    "config/fat320-incast-load5/1ndp.json"
    "config/fat320-incast-load5/homa.json"
    "config/fat320-incast-load7/1ndp.json"
    "config/fat320-incast-load7/homa.json"
    "config/fat320-incastmix-hadoop-load4.5-incast32-interval0.0375/1ndp.json"
    "config/fat320-incastmix-hadoop-load4.5-incast32-interval0.0375/homa.json"
    "config/fat320-incastmix-hadoop-load7-incast32-interval0.0375/1ndp.json"
    "config/fat320-incastmix-hadoop-load7-incast32-interval0.0375/homa.json"
)

for config in "${CONFIGS[@]}"; do
    echo "Launching $config"
    ./config/run_in_screen.sh "$config"
done

echo "Started ${#CONFIGS[@]} experiments in screen."
