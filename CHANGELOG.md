# Changelog

## 0.2.2

- Refreshed public release packaging and documentation for 0.2.2.
- Cleaned project links, runtime-source branding, package metadata and repository hygiene.
- Updated the repository screenshot to the current Cutter interface.
- Removed obsolete inherited package-ignore entries and stale development wording.
- Changed the default screenshot folder name to `WinterStatic Video Cutter`.
- No Cutter conversion, subtitle, audio, crop or keyframe behavior was intentionally changed from 0.2.1.

## 0.2.1

- Added ordered embedded/external audio track management.
- Added per-track audio timing offsets through AUD SYNC.
- Added managed embedded/external subtitle selection and ordering.
- Added per-track subtitle constant-offset and two-point drift correction.
- Added embedded text subtitle timing correction.
- Preserved ASS/SSA, SubRip and WebVTT formats through embedded subtitle extraction where practical.
- Added deterministic indexed adjacent-keyframe navigation for STICK KF.
- Hardened keyframe navigation state across playback and manual seeking.
- Made CROP a true on/off editing mode.
- Allowed AUD SYNC and SUBS SYNC to remain open simultaneously on separate rows.
- Improved Cutter seek-bar repaint behavior during STICK KF seeks.
- Added release hardening around subtitle drift geometry and worker-temp cleanup.
- Moved Audio mode/bitrate controls beside Resolution.

## 0.2.0

Previous public release.
