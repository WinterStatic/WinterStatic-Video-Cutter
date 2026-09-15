# WinterStatic Video Cutter roadmap

This file records planned work, not promised release dates.

## Bespoke subtitle A/B cue clipping and rebasing

Planned future subtitle handling for A/B clips:

```text
source subtitle cue
    ->
apply constant offset or drift correction
    ->
clip cue interval to the selected A/B range
    ->
drop cues completely outside the clip
    ->
clamp cues that overlap a clip boundary
    ->
rebase the clip start to 00:00:00
    ->
write the corrected subtitle in its supported native text format
```

The implementation should reuse the existing subtitle timing parser/rewriter for both external and corrected embedded text subtitles.

It should not create a second independent timing engine.

Important cases:

- cue begins before A and continues after A,
- cue begins before B and ends after B,
- cue lies completely before A,
- cue lies completely after B,
- negative constant offsets,
- two-point drift,
- ASS/SSA style preservation,
- SRT and WebVTT,
- text MicroDVD,
- zero-length or inverted result after clipping.

This feature is deliberately deferred from 0.2.2.

## Other deferred work

- Embedded subtitle Burn.
- Bitmap/image subtitle timing correction or OCR.
- MULTI per-file audio/subtitle track matching.
