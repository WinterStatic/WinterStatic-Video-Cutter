# WinterStatic Video Cutter 0.3.0 — public release checklist

Status: candidate prepared; do not publish until final validation.

- [x] Source candidate based on subsync6 with tested crop fix retained.
- [x] Temporary crop/FFmpeg diagnostics removed from frontend.
- [x] Public version labels checked (`0.3.0` in build and Windows resources).
- [x] GPL, third-party notices, acknowledgements and packaging scripts retained.
- [x] Internal recovery/integration reports excluded from public source ZIP.
- [ ] Run clean `build-native.bat release` on Windows/MSVC.
- [ ] Confirm portable EXE starts and that packaged libmpv dependencies load.
- [ ] Confirm exact MSYS2 runtime source ZIP is produced and matches distributed runtime packages.
- [ ] Re-test the 542×685 odd-height crop case (output 542×686), even crop, and scaled-resolution crop in final build.
- [ ] Re-test A/B marker independence, external SRT Burn ± offsets, Soft subtitles, managed audio offsets, PowerShell failure/NUKE and overwrite/collision cases.
- [ ] Inspect portable bundle's licenses, manifests, source mapping and docs.
- [ ] Check README screenshot is still representative.
- [ ] Explicit user approval before uploading source, binaries, runtime source and tagging v0.3.0 on GitHub.
