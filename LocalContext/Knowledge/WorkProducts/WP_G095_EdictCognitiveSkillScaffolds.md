# WP — G095 Edict Cognitive Skill Scaffolds

## Purpose

G095 introduces copyable Edict-level state scaffolds for agent workflows that should survive ordinary turns and remain inspectable as Listree/JSON state instead of living only in transcript memory.

The first complete scaffold is `cognitive.investigation_*`, with companion shape constructors for code review and refactor planning.

## Module

`cpp-agent/edict/modules/cognitive.edict`

Public words:

- `cognitive.investigation_new!` — `( goal -- state )`
- `cognitive.investigation_add_hypothesis!` — `( state id claim -- state )`
- `cognitive.investigation_add_evidence!` — `( state hypothesis_id evidence -- state )`
- `cognitive.investigation_mark!` — `( state hypothesis_id verdict -- state )`
- `cognitive.code_review_new!` — `( scope -- state )`
- `cognitive.refactor_plan_new!` — `( scope -- state )`
- `cognitive.refactor_plan_add_step!` — `( state id description path verify_command -- state )`

## State Shapes

### Investigation

```json
{
  "kind": "cognitive_scaffold",
  "scaffold": "investigation",
  "status": "open",
  "goal": "...",
  "hypotheses": [ {"id": "H1", "claim": "...", "status": "open", "evidence": []} ],
  "findings": [ {"hypothesis_id": "H1", "evidence": "path:line", "status": "observed"} ],
  "concerns": [],
  "refactor_steps": [],
  "success_criteria": ["record hypotheses", "attach evidence", "mark verified or rejected", "serialize state for handoff"],
  "log": []
}
```

Use this scaffold when a task needs explicit hypotheses, primary evidence, and a durable handoff object.

### Code review

The MVP code-review constructor records `scope`, `concerns`, `evidence`, and explicit success criteria. Use it when the agent is collecting review findings and wants unresolved/resolved concerns to remain inspectable.

### Refactor plan

The MVP refactor-plan constructor records ordered `steps`, `checkpoints`, and success criteria. Use it when a multi-file change needs dependency-ordered steps plus a verification command per step.

## Speculation Boundary

Scaffold states are ordinary Listree values. For rollback-safe branch exploration, use the C++ transaction API (`beginTransaction`/`rollbackTransaction`) — the same substrate that backs Edict-level `speculate`. This keeps parent state stable while allowing full scaffold mutation inside the transaction:

```edict
"branch-safety" cognitive.investigation_new! @investigation
investigation "H1" "baseline" cognitive.investigation_add_hypothesis! @investigation
```

Then from C++:
```cpp
auto checkpoint = vm.beginTransaction();
vm.execute(compiler.compile(
    "investigation \"H2\" \"branch-only\" cognitive.investigation_add_hypothesis! @investigation"
));
// investigation now has H1 and H2
vm.rollbackTransaction(checkpoint);
// investigation has only H1 again
```

## Verification Matrix

| Test | Coverage |
|---|---|
| `CognitiveScaffoldTest.InvestigationScaffoldPersistsAndSerializesAcrossTurns` | module load, investigation creation, append transitions across separate VM executes, JSON serialization |
| `CognitiveScaffoldTest.SpeculativeInvestigationBranchDoesNotCorruptParentState` | speculative branch mutation returns a branch copy without corrupting parent scaffold state |
| `CognitiveScaffoldTest.CompanionReviewAndRefactorShapesAreInspectable` | code-review and refactor-plan shape constructors plus refactor step transition |

## Future Work

- Add richer concern-resolution transitions for code review.
- Add dependency-order validation for refactor steps via the logic/miniKanren surface.
- Promote frequently used scaffolds into curated-launcher preloads if user workflows need them globally.
