#!/usr/bin/env zsh
# demo_cognitive_core.sh — runnable demo of Cognitive Core Source Examples
# Mirrors demo/Cognitive_Core_Source_Examples.md, exercising each snippet
# via heredocs against the Edict interpreter.
#
# Note: multi-line logic { ... } blocks do not parse in stdin (-) mode.
# All logic blocks are collapsed to a single line here.

set -e

EDICT=${EDICT:-$(dirname "$0")/../build/edict/edict}

echo "=== Cognitive Core Source Examples Demo ==="
echo "Using interpreter: $EDICT"
echo

# ---------------------------------------------------------------------------
echo "--- Example 1a: Logic Query (logic block syntax) ---"
# Returns a list of 3 answers: tea, cake, jam
"$EDICT" - <<'EDICT'
logic { "fresh": ["q"], "conde": [[["==", "q", "tea"]], [["membero", "q", ["cake", "jam"]]]], "results": ["q"] }
print
EDICT

echo
echo "--- Example 1b: Logic Query (compat logic_run form) ---"
# Older compatibility form — same result
"$EDICT" - <<'EDICT'
{"fresh": ["q"], "conde": [[["==", "q", "tea"]], [["membero", "q", ["cake", "jam"]]]], "results": ["q"]} logic_run !
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
"$EDICT" - <<'EDICT'
{"pattern": ["dup", "dot", "sqrt"], "replacement": ["magnitude"]} rewrite_define ! /
logic { "fresh": ["q"], "where": [["membero", "q", ["tea", "cake"]]], "results": ["q"], "limit": "2" } @answers /
answers /
'dup 'dot 'sqrt
print
EDICT

echo
echo "=== Demo complete ==="
