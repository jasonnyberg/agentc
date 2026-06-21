# Goal: G090 — User-Oriented README

**Status**: COMPLETE  
**Created**: 2026-05-13  
**Completed**: 2026-05-13

## Objective
Refocus `README.md` toward potential users instead of project authors/contributors.

## Rationale
The README has useful current-state information, but too much of it reads like internal development status. It should lead with AgentC's unique value, what users can build, concrete use patterns, maturity expectations, and near/long-term directions.

## Scope
- Rewrite README framing and section order.
- Emphasize user/application value and executable patterns.
- Retain current-state and roadmap information at a product/user level.
- Move or minimize internal development details such as implementation-unit splits and cleanup history.

## Acceptance Criteria
- [x] README leads with user-facing value proposition.
- [x] README explains what AgentC enables and who it is for.
- [x] README includes practical use patterns and runnable examples.
- [x] README describes current state, maturity, and roadmap without internal implementation noise.
- [x] README links remain valid and formatting checks pass.

## Implementation Notes — 2026-05-13
- Rewrote `README.md` around potential users: why they might want AgentC, what applications it targets, a mental model of the system, quick-start commands, use patterns, current maturity, intern-worker direction, and near/long-term roadmap.
- Reduced author/development-oriented details such as implementation-unit split and cleanup history.
- Kept current-state caveats, but phrased them as user-facing maturity expectations.
- Preserved current Edict style conventions: adjacent eval (`word!`) and bare `/` / `/ /` for discard.

## Validation — 2026-05-13
- README speculate example printed `"candidate"` then `baseline`.
- README miniKanren example printed `["tea","cake"]`.
- README Cartographer FFI example printed `7`.
- `EDICT_AUTO_CHAT=0 ./edict.sh -e 'llm.catalog! to_json! print'` returned provider catalog JSON.
- Python README local-link check — passed.
- Stale/internal phrasing scan for `translation units`, `obsolete`, `dead Dictionary`, `not to replace`, and old discard forms — passed.
- `git diff --check -- README.md LocalContext/Knowledge/Goals/G090-UserOrientedReadme/index.md` — passed.
