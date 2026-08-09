#!/usr/bin/env bash
set -euo pipefail

test_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$test_dir/build"
mkdir -p "$build_dir"

g++ -std=c++17 -O2 -Wall -Wextra -Werror -pthread \
  "$test_dir/test_pcm_ring.cpp" -o "$build_dir/test_pcm_ring"

"$build_dir/test_pcm_ring"

g++ -std=c++17 -Wall -Wextra -Werror -fsyntax-only \
  -I"$test_dir/stubs" -I"$test_dir/.." \
  "$test_dir/../sd_diagnostics.cpp"

echo "sd_diagnostics: host syntax check passed"

g++ -std=c++17 -Wall -Wextra -Werror -fsyntax-only \
  -I"$test_dir/stubs" -I"$test_dir/.." \
  "$test_dir/../storage.cpp"

echo "storage: GBX v1/v2/v3 layout and syntax checks passed"

g++ -std=c++17 -Wall -Wextra -Werror -fsyntax-only \
  -I"$test_dir/stubs" -I"$test_dir/.." \
  "$test_dir/../input.cpp" "$test_dir/../ui.cpp"

echo "input/ui: host syntax checks passed"

g++ -std=c++17 -Wall -Wextra -Werror -fsyntax-only \
  -I"$test_dir/stubs" -I"$test_dir/.." \
  "$test_dir/../sequencer.cpp" "$test_dir/../sampler.cpp" \
  "$test_dir/../mic_sampler.cpp" "$test_dir/../audio_engine.cpp" \
  "$test_dir/../wavetable.cpp"

echo "audio/sequencer/sampler: host syntax checks passed"

g++ -x c++ -std=c++17 -Wall -Wextra -Werror -fsyntax-only \
  -I"$test_dir/stubs" -I"$test_dir/.." \
  "$test_dir/../Microgroove.ino"

echo "sketch integration: host syntax check passed"

PYTHONPATH="$test_dir/.." python3 -m unittest "$test_dir/test_merge_firmware.py"
