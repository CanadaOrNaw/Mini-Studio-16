#include "../control_protocol.h"
#include "../sampler_slots.h"
#include "../motion.h"
#include "../streaming_sampler.h"
#include "../synth_parameters.h"

#include <cassert>
#include <cstring>
#include <iostream>

static ControlRequest parseOk(const char* line) {
    ControlRequest request = {};
    assert(controlParseLine(line, request) == CONTROL_PARSE_OK);
    return request;
}

int main() {
    ControlRequest request = parseOk("MS16/1 abc-12 ping\r\n");
    assert(std::strcmp(request.id, "abc-12") == 0);
    assert(request.command == CONTROL_PING);

    request = parseOk("MS16/1 2 TRANSPORT continue");
    assert(request.command == CONTROL_TRANSPORT_CONTINUE);

    request = parseOk("MS16/1 3 tempo 240");
    assert(request.command == CONTROL_TEMPO_SET && request.arg1 == 240);

    request = parseOk("MS16/1 note1 note 3 107 127");
    assert(request.command == CONTROL_NOTE_ON);
    assert(request.arg1 == 3 && request.arg2 == 107 && request.arg3 == 127);
    request = parseOk("MS16/1 note2 note_off 2 64");
    assert(request.command == CONTROL_NOTE_OFF && request.arg1 == 2 && request.arg2 == 64);

    request = parseOk("MS16/1 5 drum 8");
    assert(request.command == CONTROL_DRUM_HIT && request.arg1 == 8);

    request = parseOk("MS16/1 6 master start");
    assert(request.command == CONTROL_MASTER_START);
    request = parseOk("MS16/1 7 stems stop");
    assert(request.command == CONTROL_STEM_STOP);
    request = parseOk("MS16/1 8 loop status");
    assert(request.command == CONTROL_LOOP_STATUS);
    request = parseOk("MS16/1 9 loop 6 record");
    assert(request.command == CONTROL_LOOP_RECORD && request.arg1 == 6);
    request = parseOk("MS16/1 10 loop 2 unmute");
    assert(request.command == CONTROL_LOOP_UNMUTE && request.arg1 == 2);
    request = parseOk("MS16/1 vol loop 2 volume 65");
    assert(request.command == CONTROL_LOOP_VOLUME && request.arg1 == 2 && request.arg2 == 65);
    request = parseOk("MS16/1 11 sample status");
    assert(request.command == CONTROL_SAMPLE_STATUS);
    request = parseOk("MS16/1 12 sample 16 assign CHORD.wav melodic");
    assert(request.command == CONTROL_SAMPLE_ASSIGN && request.arg1 == 16 &&
           request.arg2 == SAMPLER_SLOT_MELODIC &&
           std::strcmp(request.text, "CHORD.wav") == 0);
    request = parseOk("MS16/1 13 sample 2 trigger 16");
    assert(request.command == CONTROL_SAMPLE_TRIGGER && request.arg1 == 2 &&
           request.arg2 == 16);
    request = parseOk("MS16/1 rec1 sample 4 record bus sliced");
    assert(request.command == CONTROL_SAMPLE_RECORD && request.arg1 == 4 &&
           request.arg2 == STREAM_SAMPLE_INPUT_BUS && request.arg3 == SAMPLER_SLOT_SLICED);
    request = parseOk("MS16/1 rec2 sample 4 stop");
    assert(request.command == CONTROL_SAMPLE_STOP && request.arg1 == 4);
    request = parseOk("MS16/1 14 event status");
    assert(request.command == CONTROL_EVENT_STATUS);
    request = parseOk("MS16/1 15 event 5 bars 128");
    assert(request.command == CONTROL_EVENT_BARS && request.arg1 == 5 &&
           request.arg2 == 128);
    request = parseOk("MS16/1 16 event 3 arm");
    assert(request.command == CONTROL_EVENT_ARM && request.arg1 == 3);
    request = parseOk("MS16/1 17 motion status");
    assert(request.command == CONTROL_MOTION_STATUS);
    request = parseOk("MS16/1 18 motion 4 map shake synth3_resonance");
    assert(request.command == CONTROL_MOTION_MAP && request.arg1 == 4 &&
           request.arg2 == MOTION_SOURCE_SHAKE &&
           request.arg3 == MOTION_TARGET_SYNTH3_RESONANCE);
    request = parseOk("MS16/1 19 midi status");
    assert(request.command == CONTROL_MIDI_STATUS);
    request = parseOk("MS16/1 20 project status");
    assert(request.command == CONTROL_PROJECT_STATUS);
    request = parseOk("MS16/1 21 project 8 save");
    assert(request.command == CONTROL_PROJECT_SAVE && request.arg1 == 8);
    request = parseOk("MS16/1 22 project 1 load");
    assert(request.command == CONTROL_PROJECT_LOAD && request.arg1 == 1);
    request = parseOk("MS16/1 bs boot status");
    assert(request.command == CONTROL_BOOT_STATUS);
    request = parseOk("MS16/1 bn boot normal");
    assert(request.command == CONTROL_BOOT_NORMAL);
    request = parseOk("MS16/1 bh boot host");
    assert(request.command == CONTROL_BOOT_USB_HOST);
    request = parseOk("MS16/1 sy synth status");
    assert(request.command == CONTROL_SYNTH_STATUS);
    request = parseOk("MS16/1 se synth 2 engine fm4");
    assert(request.command == CONTROL_SYNTH_ENGINE && request.arg1 == 2 &&
           request.arg2 == SYNTH_ENGINE_FM4);
    request = parseOk("MS16/1 sp synth 3 set fm.op4.ratio 675");
    assert(request.command == CONTROL_SYNTH_SET && request.arg1 == 3 &&
           request.arg2 == SYNTH_PARAM_FM_OP4_RATIO && request.arg3 == 675);
    request = parseOk("MS16/1 sr synth dsp_reset");
    assert(request.command == CONTROL_SYNTH_DSP_RESET);
    request = parseOk("MS16/1 cs cap status");
    assert(request.command == CONTROL_CAP_STATUS);
    request = parseOk("MS16/1 cp cap pair");
    assert(request.command == CONTROL_CAP_PAIR);
    request = parseOk("MS16/1 cm cap monitor 73");
    assert(request.command == CONTROL_CAP_MONITOR && request.arg1 == 73);

    ControlRequest invalid = {};
    assert(controlParseLine("", invalid) == CONTROL_PARSE_EMPTY);
    assert(controlParseLine("MS15/1 1 ping", invalid) == CONTROL_PARSE_BAD_PREFIX);
    assert(controlParseLine("MS16/1", invalid) == CONTROL_PARSE_MISSING_ID);
    assert(controlParseLine("MS16/1 bad/id ping", invalid) == CONTROL_PARSE_BAD_ID);
    assert(controlParseLine("MS16/1 1 tempo 39", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 note 1 23 100", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 drum 9", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 loop 0 record", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 loop 1 dance", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 sample 17 clear", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 sample 1 trigger 0", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 sample 1 assign x.wav stereo", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 sample 1 record aux melodic", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 sample 1 record mic stereo", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 event 6 arm", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 event 1 bars 129", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 motion 5 clear", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 motion 1 map banana synth1_cutoff", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 midi start", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 project 0 save", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 project 9 load", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 project 1 delete", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 boot", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 boot factory", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 boot host now", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 synth 0 engine fm4", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 synth 1 engine fake", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 synth 1 set fm.index 801", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 synth 1 set fm.op5.ratio 100", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 cap monitor 101", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 cap power external", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 wat", invalid) == CONTROL_PARSE_UNKNOWN_COMMAND);
    assert(controlParseLine("MS16/1 1 ping extra", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);

    assert(std::strcmp(controlParseStatusName(CONTROL_PARSE_BAD_PREFIX), "bad_prefix") == 0);
    std::cout << "control_protocol: all tests passed\n";
    return 0;
}
