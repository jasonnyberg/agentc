#!/usr/bin/env zsh
# demo_boxing.sh — demonstrate cartographer.box / unbox / box_free
#
# Shows the full round-trip:
#   1. Parse time.h to get struct definitions (including struct timespec)
#   2. Extract the timespec type-def from the parse result
#   3. Build a source LTV with tv_sec and tv_nsec values
#   4. Box it into a heap-allocated C struct  (cartographer.box !)
#   5. Unbox the pointer back into an Edict LTV  (cartographer.unbox !)
#   6. Free the heap allocation  (cartographer.box_free !)
#
# Stack convention for box:
#   source_ltv type_def ns cartographer.box !   -> boxed {__ptr, __type}
#   boxed ns cartographer.unbox !               -> unboxed_ltv {tv_sec, tv_nsec, __type}
#   boxed cartographer.box_free !               -> (nothing pushed; frees heap)
#
# Notes:
#   - Dict literals use JSON syntax: { "key": "value", ... }
#   - Type defs are accessed by their clang cursor spelling (e.g. "timespec")
#   - Platform typedef aliases (__time_t, __syscall_slong_t, etc.) are
#     automatically canonicalized to their underlying LP64 C types

set -e

EDICT=${EDICT:-$(dirname "$0")/../build/edict/edict}

echo "=== Edict Boxing / Unboxing Demo ==="
echo "Using interpreter: $EDICT"
echo

# ---------------------------------------------------------------------------
echo "--- Section 1: Parse time.h and inspect struct timespec fields ---"
# ---------------------------------------------------------------------------
"$EDICT" - <<'EDICT'
unsafe_extensions_allow ! pop
"/usr/include/time.h" parser.__native.map ! @timedefs

# Inspect the timespec type definition
"timespec kind:" print
timedefs.timespec.kind print
"timespec name:" print
timedefs.timespec.name print
"timespec size (bytes):" print
timedefs.timespec.size print

# Inspect the fields
"tv_sec type:" print
timedefs.timespec.children.tv_sec.type print
"tv_sec offset:" print
timedefs.timespec.children.tv_sec.offset print

"tv_nsec type:" print
timedefs.timespec.children.tv_nsec.type print
"tv_nsec offset:" print
timedefs.timespec.children.tv_nsec.offset print
EDICT

echo
# ---------------------------------------------------------------------------
echo "--- Section 2: Box a struct timespec value ---"
# ---------------------------------------------------------------------------
"$EDICT" - <<'EDICT'
unsafe_extensions_allow ! pop
"/usr/include/time.h" parser.__native.map ! @timedefs

# Build source LTV using JSON dict literal syntax
{ "tv_sec": "1234567890", "tv_nsec": "500000000" } @src

# Box: push source first, then type_def on top.
# Result: { __ptr: <binary:8>, __type: <timespec-type-def> }
src timedefs.timespec timedefs cartographer.box ! @boxed

"boxed __ptr (8-byte native pointer):" print
boxed.__ptr print
EDICT

echo
# ---------------------------------------------------------------------------
echo "--- Section 3: Round-trip — box then unbox ---"
# ---------------------------------------------------------------------------
"$EDICT" - <<'EDICT'
unsafe_extensions_allow ! pop
"/usr/include/time.h" parser.__native.map ! @timedefs

{ "tv_sec": "1234567890", "tv_nsec": "500000000" } @src

src timedefs.timespec timedefs cartographer.box ! @boxed

# Unbox back to an LTV — reads field values from the C heap struct
boxed timedefs cartographer.unbox ! @unboxed

"Unboxed tv_sec:" print
unboxed.tv_sec print

"Unboxed tv_nsec:" print
unboxed.tv_nsec print
EDICT

echo
# ---------------------------------------------------------------------------
echo "--- Section 4: Free the boxed allocation ---"
# ---------------------------------------------------------------------------
"$EDICT" - <<'EDICT'
unsafe_extensions_allow ! pop
"/usr/include/time.h" parser.__native.map ! @timedefs

{ "tv_sec": "9999999999", "tv_nsec": "123456789" } @src

src timedefs.timespec timedefs cartographer.box ! @boxed

# Free heap memory
boxed cartographer.box_free !
"box_free completed without error" print
EDICT

echo
# ---------------------------------------------------------------------------
echo "--- Section 5: struct timeval round-trip (different field names) ---"
# ---------------------------------------------------------------------------
"$EDICT" - <<'EDICT'
unsafe_extensions_allow ! pop
"/usr/include/sys/time.h" parser.__native.map ! @sysTime

# struct timeval { tv_sec: __time_t (long), tv_usec: __suseconds_t (long) }
{ "tv_sec": "1000", "tv_usec": "500" } @src

src sysTime.timeval sysTime cartographer.box ! @boxed

boxed sysTime cartographer.unbox ! @unboxed

"timeval tv_sec:" print
unboxed.tv_sec print

"timeval tv_usec:" print
unboxed.tv_usec print

boxed cartographer.box_free !
"timeval round-trip complete" print
EDICT

echo
echo "=== Boxing Demo complete ==="
