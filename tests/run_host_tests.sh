#!/usr/bin/env bash
set -euo pipefail

test_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$test_dir/build"
mkdir -p "$build_dir"
# Some minimal CI/sandbox images do not provide /tmp. Give the compiler a
# writable temporary directory so an interrupted syntax-only pass cannot leave
# cc*.o/cc*.s files in the repository root.
compiler_tmp="$build_dir/compiler-tmp"
mkdir -p "$compiler_tmp"
export TMPDIR="$compiler_tmp"

g++ -pipe -std=gnu++11 -O2 -Wall -Wextra -Werror -pthread \
  "$test_dir/test_pcm_ring.cpp" -o "$build_dir/test_pcm_ring"

"$build_dir/test_pcm_ring"

g++ -pipe -std=gnu++11 -O2 -Wall -Wextra -Werror \
  "$test_dir/test_control_protocol.cpp" "$test_dir/../control_protocol.cpp" \
  -o "$build_dir/test_control_protocol"

"$build_dir/test_control_protocol"

g++ -pipe -std=gnu++11 -O2 -Wall -Wextra -Werror \
  "$test_dir/test_control_protocol_fuzz.cpp" "$test_dir/../control_protocol.cpp" \
  -o "$build_dir/test_control_protocol_fuzz"

"$build_dir/test_control_protocol_fuzz"

g++ -pipe -std=gnu++11 -O2 -Wall -Wextra -Werror \
  "$test_dir/test_serial_line_buffer.cpp" -o "$build_dir/test_serial_line_buffer"

"$build_dir/test_serial_line_buffer"

g++ -pipe -std=gnu++11 -O2 -Wall -Wextra -Werror \
  "$test_dir/test_wav_file.cpp" "$test_dir/../wav_file.cpp" \
  -o "$build_dir/test_wav_file"

"$build_dir/test_wav_file"

g++ -pipe -std=gnu++11 -O2 -Wall -Wextra -Werror \
  "$test_dir/test_master_recorder_session.cpp" \
  -o "$build_dir/test_master_recorder_session"

"$build_dir/test_master_recorder_session"

g++ -pipe -std=gnu++11 -O2 -Wall -Wextra -Werror \
  "$test_dir/test_midi.cpp" "$test_dir/../midi_parser.cpp" \
  "$test_dir/../midi_transport.cpp" -o "$build_dir/test_midi"

"$build_dir/test_midi"

g++ -pipe -std=gnu++11 -O2 -Wall -Wextra -Werror \
  "$test_dir/test_loop_timeline.cpp" -o "$build_dir/test_loop_timeline"

"$build_dir/test_loop_timeline"

g++ -pipe -std=gnu++11 -O2 -Wall -Wextra -Werror \
  "$test_dir/test_stem_file.cpp" "$test_dir/../stem_file.cpp" \
  -o "$build_dir/test_stem_file"

"$build_dir/test_stem_file"

g++ -pipe -std=gnu++11 -Wall -Wextra -Werror -fsyntax-only \
  -I"$test_dir/stubs" -I"$test_dir/.." \
  "$test_dir/../sd_diagnostics.cpp"

echo "sd_diagnostics: host syntax check passed"

g++ -pipe -std=gnu++11 -Wall -Wextra -Werror -fsyntax-only \
  -I"$test_dir/stubs" -I"$test_dir/.." \
  "$test_dir/../storage.cpp"

echo "storage: GBX v1/v2/v3 layout and syntax checks passed"

g++ -pipe -std=gnu++11 -Wall -Wextra -Werror -fsyntax-only \
  -I"$test_dir/stubs" -I"$test_dir/.." \
  "$test_dir/../input.cpp" "$test_dir/../ui.cpp"

echo "input/ui: host syntax checks passed"

g++ -pipe -std=gnu++11 -Wall -Wextra -Werror -fsyntax-only \
  -I"$test_dir/stubs" -I"$test_dir/.." \
  "$test_dir/../sequencer.cpp" "$test_dir/../sampler.cpp" \
  "$test_dir/../mic_sampler.cpp" "$test_dir/../audio_engine.cpp" \
  "$test_dir/../wavetable.cpp" "$test_dir/../master_recorder.cpp" \
  "$test_dir/../serial_control.cpp" "$test_dir/../control_protocol.cpp" \
  "$test_dir/../wav_file.cpp" "$test_dir/../midi_parser.cpp" \
  "$test_dir/../midi_transport.cpp" "$test_dir/../midi_input.cpp" \
  "$test_dir/../stem_file.cpp" "$test_dir/../stem_recorder.cpp"

echo "audio/sequencer/sampler: host syntax checks passed"

g++ -pipe -x c++ -std=gnu++11 -Wall -Wextra -Werror -fsyntax-only \
  -I"$test_dir/stubs" -I"$test_dir/.." \
  "$test_dir/../Microgroove.ino"

echo "sketch integration: host syntax check passed"

PYTHONPATH="$test_dir/.." python3 -m unittest "$test_dir/test_merge_firmware.py"
PYTHONPATH="$test_dir/.." python3 -m unittest "$test_dir/test_ministudio_cli.py"
PYTHONPATH="$test_dir/.." python3 -m unittest "$test_dir/test_split_stems.py"
PYTHONPATH="$test_dir/.." python3 -m unittest "$test_dir/test_protocol_soak.py"
