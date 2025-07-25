#!/bin/bash

# Fail on any error
set -e

# Clone FutureSDR if it doesn't exist yet
if [ ! -d "futuresdr" ]; then
  echo "Cloning FutureSDR..."
  git clone https://github.com/FutureSDR/FutureSDR.git futuresdr
fi

# Go to the benchmark directory, if this file is in FutureSDR/examples/
cd futuresdr/perf/buffer_size

# Run the benchmark with various params
echo "Running benchmark..."

#stages - how many copy+rand blocks. pipes - how many parallel flowgraphs
SAMPLES=$((256 * 1000 * 1000))
cargo run --release -- \
  --samples ${SAMPLES} \
  --pipes 1 \
  --stages 4 \
  --scheduler flow \
  --buffer-size 65536
echo "Done!"
