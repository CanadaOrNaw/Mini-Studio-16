#include "../project_publish.h"
#include <SD.h>
#include <assert.h>
#include <string.h>
#include <vector>

SDStub SD;

static std::vector<uint8_t> data(const char* text) {
    return std::vector<uint8_t>(text, text + strlen(text));
}
static void expect(const char* path, const char* text) {
    const std::vector<uint8_t>* bytes = SD.bytes(path);
    assert(bytes && *bytes == data(text));
}

int main() {
    const char* primary = "/P1.gbx";
    const char* temp = "/P1.gbx.tmp";
    const char* backup = "/P1.gbx.bak";
    const char* previous = "/P1.gbx.bak.prev";

    // Known-good primary rotates to backup; older backup retires only after
    // the new primary is safely published.
    SD.reset();
    SD.put(primary, data("primary-old"));
    SD.put(backup, data("backup-older"));
    SD.put(temp, data("primary-new"));
    assert(projectPublishTempFile(SD, primary, temp, backup, previous, true));
    expect(primary, "primary-new");
    expect(backup, "primary-old");
    assert(!SD.exists(previous));

    // A primary that failed validation must never replace a known-good .bak.
    SD.reset();
    SD.put(primary, data("CORRUPT"));
    SD.put(backup, data("known-good"));
    SD.put(temp, data("new-good"));
    assert(projectPublishTempFile(SD, primary, temp, backup, previous, false));
    expect(primary, "new-good");
    expect(backup, "known-good");

    // If publishing the temp fails, both known-good generations survive.
    SD.reset();
    SD.put(primary, data("primary-old"));
    SD.put(backup, data("backup-older"));
    SD.put(temp, data("primary-new"));
    SD.failNextRename(temp, primary);
    assert(!projectPublishTempFile(SD, primary, temp, backup, previous, true));
    expect(primary, "primary-old");
    expect(backup, "backup-older");

    // Backup-only recovery remains usable while a fresh primary is published.
    SD.reset();
    SD.put(backup, data("known-good"));
    SD.put(temp, data("primary-new"));
    assert(projectPublishTempFile(SD, primary, temp, backup, previous, false));
    expect(primary, "primary-new");
    expect(backup, "known-good");
    return 0;
}
