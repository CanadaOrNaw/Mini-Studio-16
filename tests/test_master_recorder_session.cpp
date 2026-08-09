#include "../master_recorder_session.h"

#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    MasterRecorderSession session;
    assert(session.state() == MASTER_REC_UNAVAILABLE);
    session.initialize(true);
    assert(session.state() == MASTER_REC_IDLE);

    session.noteRecovery("/groovebox/recordings/RECOVER001.wav", 1234);
    assert(session.begin("/groovebox/recordings/MASTER001.wav"));
    assert(session.state() == MASTER_REC_STARTING);
    assert(!session.begin("ignored.wav"));
    assert(session.markRecording());
    assert(session.state() == MASTER_REC_RECORDING);

    session.noteProduced(256, 250, 6000);
    session.noteWrite(2048, 17500);
    session.noteWrite(1024, 9000);
    assert(session.requestStop());
    assert(!session.requestStop());
    session.complete();

    MasterRecorderSnapshot done = session.snapshot();
    assert(done.state == MASTER_REC_COMPLETE);
    assert(done.framesWritten == 3072);
    assert(done.droppedFrames == 6);
    assert(done.ringHighWater == 6000);
    assert(done.maxWriteUs == 17500);
    assert(done.recoveredFrames == 1234);
    assert(std::strcmp(done.recoveredPath,
                       "/groovebox/recordings/RECOVER001.wav") == 0);

    assert(session.begin("/groovebox/recordings/MASTER002.wav"));
    session.noteError();
    session.fail();
    done = session.snapshot();
    assert(done.state == MASTER_REC_ERROR && done.errors == 1);
    assert(done.recoveredFrames == 1234);
    std::cout << "master_recorder_session: all tests passed\n";
    return 0;
}
