#include "../stem_file.h"

#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    uint8_t header[STEM_FILE_HEADER_BYTES];
    stemBuildHeader(header, 22050, 123456);
    StemFileInfo info = {};
    assert(stemParseHeader(header, info));
    assert(info.version == 1 && info.channels == 5);
    assert(info.sampleRate == 22050 && info.frames == 123456);
    header[0] = 'X';
    assert(!stemParseHeader(header, info));
    const StemPcmFrame frames[2] = {{1, 2, 3, 4, 5}, {6, 7, 8, 9, 10}};
    int16_t interleaved[10] = {};
    stemInterleaveFrames(frames, 2, interleaved);
    for (int index = 0; index < 10; ++index) assert(interleaved[index] == index + 1);
    std::cout << "stem_file: header tests passed\n";
    return 0;
}
