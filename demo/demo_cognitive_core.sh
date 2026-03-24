#!/usr/bin/env zsh
# demo_cognitive_core.sh — runnable demo of Cognitive Core Source Examples
# Mirrors demo/Cognitive_Core_Source_Examples.md, exercising each snippet
# via heredocs against the Edict interpreter.
#
# Logic examples now import the kanren capability through the normal FFI path.

set -e

EDICT=${EDICT:-$(dirname "$0")/../build/edict/edict}
KANREN_LIB=${KANREN_LIB:-$(dirname "$0")/../build/kanren/libkanren.so}
KANREN_HDR=${KANREN_HDR:-$(dirname "$0")/../cartographer/tests/kanren_runtime_ffi_poc.h}

echo "=== Cognitive Core Source Examples Demo ==="
echo "Using interpreter: $EDICT"
echo

# ---------------------------------------------------------------------------
echo "--- Example 1a: Logic Query (imported object-spec form) ---"
# Returns a list of 3 answers: tea, cake, jam
"$EDICT" - <<EDICT
[$KANREN_LIB] [$KANREN_HDR] resolver.import ! @logicffi
logicffi.agentc_logic_eval_ltv @logic
{"fresh": ["q"], "conde": [[["==", "q", "tea"]], [["membero", "q", ["cake", "jam"]]]], "results": ["q"]} logic!
print
EDICT

echo
echo "--- Example 1b: Logic Query (pure wrapper form over imported kanren) ---"
# Wrapper thunks build the canonical spec in ordinary Edict, then call imported kanren
"$EDICT" - <<EDICT
[$KANREN_LIB] [$KANREN_HDR] resolver.import ! @logicffi
[@rhs @lhs rhs lhs [] @items items ^] @pair
[@x x [] @items items ^] @fresh
[@x x [] @items items ^] @results
[@rhs @lhs rhs lhs 'membero [] @goal goal ^] @membero
[@results_list @goal_atom @fresh_list {"fresh": [], "where": [], "results": []} @spec fresh_list @spec.fresh goal_atom [] @where_clause where_clause ^ @spec.where results_list @spec.results spec] @logic_spec
[logicffi.agentc_logic_eval_ltv !] @logic_eval

logic_spec(fresh(q) membero(q pair(tea cake)) results(q))
logic_eval !
print
EDICT

echo
echo "--- Example 2: Source-Defined Rewrite Rule ---"
# Rewrites the stack suffix [dup dot sqrt] into [magnitude]
"$EDICT" - <<'EDICT'
{"pattern": ["dup", "dot", "sqrt"], "replacement": ["magnitude"]} rewrite_define ! /
'dup 'dot 'sqrt
print
EDICT

echo
echo "--- Example 3: Rewrite Introspection and Removal ---"
# Defines two rules, lists them, removes rule 0, lists remaining
"$EDICT" - <<'EDICT'
{"pattern": ["alpha"], "replacement": ["first"]} rewrite_define ! /
{"pattern": ["beta"], "replacement": ["second"]} rewrite_define ! /
rewrite_list ! /
'0 rewrite_remove ! /
rewrite_list !
print
EDICT

echo
echo "--- Example 4: Logic + Rewrite Together ---"
# Defines a rewrite rule, runs a logic query, discards the answer list,
# then runs the rewritten stack program
"$EDICT" - <<EDICT
[$KANREN_LIB] [$KANREN_HDR] resolver.import ! @logicffi
logicffi.agentc_logic_eval_ltv @logic
{"pattern": ["dup", "dot", "sqrt"], "replacement": ["magnitude"]} rewrite_define ! /
{"fresh": ["q"], "where": [["membero", "q", ["tea", "cake"]]], "results": ["q"], "limit": "2"} logic! @answers /
answers /
'dup 'dot 'sqrt
print
EDICT

echo
echo "=== Demo complete ==="
