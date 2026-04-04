# Session 1700-1720

## Summary

Started G051 as an exploratory child of G049 and evaluated whether cursor-visited Listree items should become read-only as a concurrency control mechanism.

## Work Completed

- Reviewed `Cursor`, `ListreeValue`, and `ListreeItem` mutation surfaces.
- Confirmed that mutation is not cursor-exclusive: direct `ListreeValue` / `ListreeItem` APIs still mutate live structures outside cursor-only control.
- Confirmed the current cursor lifetime mechanism is pinning, not write exclusion, and overlapping cursor copies/iterators would require lease bookkeeping rather than a simple read-only bit.
- Recorded G051 as a proposed goal and captured the current recommendation: do not adopt cursor-visited read-only marking as the primary G049 safety model.

## Outcome

The stronger recommendation remains the existing G049 model: fresh VM per thread plus explicit protected shared-value cells. Cursor-visited read-only marking may still be worth revisiting later as a debug/hardening feature, but not as the first-line concurrency boundary.
