#!/usr/bin/env bash
set -euo pipefail

test_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$test_dir/build-sanitizers"
mkdir -p "$build_dir/compiler-tmp"
export TMPDIR="$build_dir/compiler-tmp"
export ASAN_OPTIONS="detect_leaks=0:abort_on_error=1:strict_string_checks=1"
export LSAN_OPTIONS="detect_leaks=0"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"

flags=(-pipe -std=gnu++11 -O1 -g -Wall -Wextra -Werror
       -fno-omit-frame-pointer "-fsanitize=${SANITIZER_SET:-undefined}" -fno-sanitize=leak)

synth_sources=(
    "$test_dir/../synth_dsp.cpp"
    "$test_dir/../synth_engine.cpp"
    "$test_dir/../synth_parameters.cpp"
    "$test_dir/synth_test_support.cpp"
)

build_run() {
    local name="$1"
    shift
    g++ "${flags[@]}" "$@" -o "$build_dir/$name"
    "$build_dir/$name"
}

build_run pcm_ring -pthread "$test_dir/test_pcm_ring.cpp"
build_run protocol "$test_dir/test_control_protocol.cpp" "$test_dir/../control_protocol.cpp" \
    "${synth_sources[@]}"
build_run protocol_fuzz "$test_dir/test_control_protocol_fuzz.cpp" \
    "$test_dir/../control_protocol.cpp" "${synth_sources[@]}"
build_run wav "$test_dir/test_wav_file.cpp" "$test_dir/../wav_file.cpp"
build_run midi "$test_dir/test_midi.cpp" "$test_dir/../midi_parser.cpp" \
    "$test_dir/../midi_transport.cpp"
build_run song_chain "$test_dir/test_song_chain.cpp"
build_run loop_stream -pthread "$test_dir/test_loop_stream_core.cpp"
build_run sampler_slots "$test_dir/test_sampler_slots.cpp" "$test_dir/../sampler_slots.cpp"
build_run sample_reference "$test_dir/test_sample_reference.cpp"
build_run sample_stream -pthread "$test_dir/test_sample_stream_core.cpp"
build_run event_looper "$test_dir/test_event_looper.cpp"
build_run motion "$test_dir/test_motion_core.cpp"
build_run ble_codec "$test_dir/test_ble_midi_codec.cpp"
build_run usb_midi_host "$test_dir/test_usb_midi_host_descriptor.cpp" \
    "$test_dir/../usb_midi_host_descriptor.cpp"
build_run boot_selector "$test_dir/test_boot_selector_core.cpp" \
    "$test_dir/../boot_selector_core.cpp"
build_run synth_engine "$test_dir/test_synth_engine.cpp" "${synth_sources[@]}"
build_run synth_offline "$test_dir/test_synth_offline_render.cpp" "${synth_sources[@]}"
build_run synth_project "$test_dir/test_synth_project.cpp" \
    "$test_dir/../synth_project.cpp" "${synth_sources[@]}"
build_run synth_ui "$test_dir/test_synth_ui_model.cpp" \
    "$test_dir/../synth_ui_model.cpp"
build_run audio_cap "$test_dir/test_audio_cap_protocol.cpp" \
    "$test_dir/../audio_cap_protocol.cpp"
build_run audio_cap_bridge "$test_dir/test_audio_cap_bridge_core.cpp" \
    "$test_dir/../audio_cap_bridge_core.cpp" "$test_dir/../audio_cap_protocol.cpp"

echo "Sanitizers (${SANITIZER_SET:-undefined}): core, protocol, synth, streaming, event, motion, MIDI and Audio Cap tests passed"
