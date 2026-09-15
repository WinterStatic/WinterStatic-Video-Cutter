# WinterStatic Video Cutter 0.2.2 public release checklist

This is a release-engineering checklist, not legal advice.

## Version and repository

- `main.cpp` reports `Version 0.2.2`.
- `resource.rc` string versions report `0.2.2`.
- `resource.rc` numeric versions are `0,2,2,0`.
- `build-native.bat` uses `APP_VERSION=0.2.2`.
- About GitHub opens a valid WinterStatic GitHub destination.
- No public file describes the release as a pre-release build.

## Documentation

- README describes current behavior, not internal development history.
- CROP documentation says on/off, not lock.
- Audio control order matches the real toolbar.
- Known limitations are accurate.
- ROADMAP records bespoke subtitle A/B cue clipping/rebasing as future work.
- ACKNOWLEDGEMENTS contains development-assistance credits.
- No stale expXX wording is present in current product docs.
- No Unicode em dash characters remain in release documentation.

## Build hygiene

- `build-native.bat` is CRLF.
- `build-native.bat` has no UTF-8 BOM.
- A clean normal build succeeds.
- `build-native.bat release` succeeds.
- Portable directory contains the expected executable/runtime/docs.
- Runtime source bundle is produced for the exact bundled MSYS2 runtime packages.

## Playback and Cutter smoke test

- Open a normal video.
- Play/pause/seek/frame step.
- Mark A/B and produce a short clip.
- STICK KF Left/Right works near the start, middle and end.
- Immediate paused keyframe reversal remains correct.
- Play -> advance -> Pause -> keyframe navigation uses the real position.
- Seek bar follows STICK KF correctly.
- CROP on/off retains the saved rectangle without applying it while off.

## Audio

- TRACKS include/exclude/reorder works.
- AUD SYNC retains independent per-track offsets.
- Normal Auto audio encode works.
- Audio Copy works where supported.
- Non-zero managed offsets are rejected on incompatible copy-only paths.
- External audio still maps correctly.

## Subtitles

- SUBS OPT include/exclude/order works.
- External Soft subtitle works.
- External Burn subtitle works.
- Constant subtitle offset works.
- Valid two-point drift works.
- Invalid drift geometry is rejected.
- Embedded SRT correction works.
- Embedded WebVTT correction works.
- Embedded styled ASS correction to MKV preserves styling.
- An ASS sample with an attached custom font still renders correctly in compatible MKV output if available for testing.
- mov_text fallback to SRT correction works.
- Bitmap subtitles are not offered as text timing targets.
- MP4 mov_text styling limitations are understood and documented.

## MULTI

- Ordered multi-input join works.
- Copy join succeeds on a known compatible set.
- Re-encode join succeeds.
- Managed per-file track features remain clearly blocked where unsupported.

## Worker lifecycle

- Hidden PowerShell conversion.
- Visible PowerShell auto-close.
- Visible PowerShell stay-open.
- NUKE terminates the tracked job.
- Closing Cutter during a running job cleans up correctly.
- Completed stay-open console does not break Cutter shutdown.

## Public-source hygiene

Before publishing, review the tree for:

- stale experimental version labels or repository URLs,
- unfinished-work annotations and debug-only comments,
- personal/local filesystem paths,
- temporary build/container paths,
- obsolete inherited product branding,
- unexpected generated binaries or archives.

Also scan for Unicode code point U+2014 and require zero matches in current Cutter documentation/source.

## Release assets

- Source repository/tag for `0.2.2`.
- Full Portable binary package.
- Matching runtime source bundle required by the selected distribution model.
- Release notes / changelog.
- Optional SHA-256 checksum file for release assets.
