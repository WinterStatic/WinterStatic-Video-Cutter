# Changelog

## 0.3.0 — release candidate (14 October 2026)

- Restored and integrated supported subtitle timing adjustments while retaining source-first sync and then clip rebasing for external Burn subtitles. Subtitle processing uses Cutter-owned temporary copies.
- Improved independent A/B marker selection: B-first defaults A to 0; explicitly selected endpoints are not overwritten by changes to the other marker.
- Corrected PowerShell job completion/failure detection when diagnostic consoles remain open.
- Normalized automatic and manually selected output filename collision handling, overwrite choices and source-destination safety.
- Cleared stale output paths when Crop forces an MP4 H.264 mode change. Long-path warnings remain non-blocking.
- Addressed the reproducible odd-height crop bottom-edge flicker in the source-resolution H.264 path using an intermediate YUV 4:4:4 processing stage prior to padding and final YUV 4:2:0 output.
- Removed temporary FFmpeg diagnostic console logging used during investigation.
- Updated recovery-era documentation for the 0.3.0 release candidate.

This source candidate requires a clean Windows/MSVC release build and final distribution checks before publication. Embedded subtitle Burn, bitmap OCR and some multi-input track workflows remain unsupported.

## 0.2.2

- Refreshed public release packaging and documentation for 0.2.2.
- Cleaned project links, runtime-source branding, package metadata and repository hygiene.
- Updated the repository screenshot to the current Cutter interface.
- Changed the default screenshot folder name to `WinterStatic Video Cutter`.

## 0.2.1

- Added managed audio/subtitle tracks, synchronization controls, keyframe navigation, crop editing and release hardening.

## 0.2.0

Previous public release.
