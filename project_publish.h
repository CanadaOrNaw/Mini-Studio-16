#pragma once

// Publish a fully-written temporary project file without sacrificing the last
// known-good backup. `primaryKnownGood` is deliberately conservative: it is
// true only after this boot successfully loaded or saved the primary.
//
// Power-loss-safe checkpoints:
// - a known-good previous backup is first moved to backupPrevious;
// - a known-good primary is then promoted to backup;
// - only then is temp published as primary;
// - on a failed final rename, the prior primary/backup arrangement is restored
//   where possible, and at least one known-good fallback remains reachable.
template <typename Fs>
bool projectPublishTempFile(Fs& fs, const char* primary, const char* temp,
                            const char* backup, const char* backupPrevious,
                            bool primaryKnownGood) {
    if (!fs.exists(temp)) return false;

    if (fs.exists(backupPrevious) && !fs.remove(backupPrevious)) return false;

    const bool primaryExists = fs.exists(primary);
    const bool backupExists = fs.exists(backup);
    bool oldBackupAside = false;
    bool primaryMovedToBackup = false;

    if (primaryExists) {
        if (primaryKnownGood) {
            if (backupExists) {
                if (!fs.rename(backup, backupPrevious)) return false;
                oldBackupAside = true;
            }
            if (!fs.rename(primary, backup)) {
                if (oldBackupAside) fs.rename(backupPrevious, backup);
                return false;
            }
            primaryMovedToBackup = true;
        } else if (backupExists) {
            // The primary failed validation/load during this boot. Preserve the
            // known-good backup instead of replacing it with corrupt bytes.
            if (!fs.remove(primary)) return false;
        } else {
            // No known-good backup exists. Keep the unknown primary as the only
            // available fallback while the new file is published.
            if (!fs.rename(primary, backup)) return false;
            primaryMovedToBackup = true;
        }
    }

    if (!fs.rename(temp, primary)) {
        bool restoredPrimary = true;
        if (primaryMovedToBackup && fs.exists(backup))
            restoredPrimary = fs.rename(backup, primary);
        // If restoring the newer primary failed, leave it reachable as .bak;
        // do not overwrite it with the older backupPrevious copy.
        if (oldBackupAside && restoredPrimary && !fs.exists(backup) &&
            fs.exists(backupPrevious))
            fs.rename(backupPrevious, backup);
        return false;
    }

    if (oldBackupAside && fs.exists(backupPrevious)) fs.remove(backupPrevious);
    return true;
}
