#!/usr/bin/env zsh
# demo_ffi_libc.sh — exercise libc/libm FFI from Edict scripts via heredocs
# Imports real system headers directly (stdlib.h, ctype.h, math.h)
# using parser.__native.map which resolves headers through the VM's Mapper.
#
# Covers: integer math, char classification, randomness, float math
# Note: string/memory functions (strlen, strcmp, etc.) require binary char*
#       pointer values, which cannot be constructed from pure Edict strings.

set -e

EDICT=${EDICT:-$(dirname "$0")/../build/edict/edict}

echo "=== Edict libc FFI Demo ==="
echo "Using interpreter: $EDICT"
echo

echo "--- Section 1: Integer Math (abs) ---"
"$EDICT" - <<'EDICT'
unsafe_extensions_allow ! pop
'libc.so.6 resolver.load !
'/usr/include/stdlib.h parser.__native.map ! @stdlib

# abs of a positive number
'42 stdlib.abs !
print

# abs of a negative number
'-7 stdlib.abs !
print

# abs of zero
'0 stdlib.abs !
print

# abs of large negative
'-1000 stdlib.abs !
print
EDICT

echo
echo "--- Section 2: Char Classification ---"
"$EDICT" - <<'EDICT'
unsafe_extensions_allow ! pop
'libc.so.6 resolver.load !
'/usr/include/ctype.h parser.__native.map ! @ctype

# toupper('a'=97) -> 65 ('A')
'97 ctype.toupper !
print

# toupper('A'=65) -> 65 (already upper)
'65 ctype.toupper !
print

# tolower('A'=65) -> 97 ('a')
'65 ctype.tolower !
print

# tolower('z'=122) -> 122 (already lower)
'122 ctype.tolower !
print

# isdigit('5'=53) -> nonzero
'53 ctype.isdigit !
print

# isdigit('a'=97) -> 0
'97 ctype.isdigit !
print

# isalpha('z'=122) -> nonzero
'122 ctype.isalpha !
print

# isalpha('5'=53) -> 0
'53 ctype.isalpha !
print

# isspace(' '=32) -> nonzero
'32 ctype.isspace !
print

# isspace('a'=97) -> 0
'97 ctype.isspace !
print

# isupper('A'=65) -> nonzero
'65 ctype.isupper !
print

# isupper('a'=97) -> 0
'97 ctype.isupper !
print

# islower('a'=97) -> nonzero
'97 ctype.islower !
print

# islower('A'=65) -> 0
'65 ctype.islower !
print
EDICT

echo
echo "--- Section 3: Randomness (srand / rand) ---"
"$EDICT" - <<'EDICT'
unsafe_extensions_allow ! pop
'libc.so.6 resolver.load !
'/usr/include/stdlib.h parser.__native.map ! @stdlib

# seed the RNG
'42 stdlib.srand !

# draw 5 random numbers
stdlib.rand !
print

stdlib.rand !
print

stdlib.rand !
print

stdlib.rand !
print

stdlib.rand !
print
EDICT

echo
echo "--- Section 4: Float Math (sqrt, fabs, ceil, floor, pow) ---"
"$EDICT" - <<'EDICT'
unsafe_extensions_allow ! pop
'libc.so.6 resolver.load !
'libm.so.6 resolver.load !
'/usr/include/math.h parser.__native.map ! @math

# sqrt(9.0) = 3.0
'9.0 math.sqrt !
print

# sqrt(144.0) = 12.0
'144.0 math.sqrt !
print

# fabs(-3.7) = 3.7
'-3.7 math.fabs !
print

# fabs(5.5) = 5.5
'5.5 math.fabs !
print

# ceil(2.1) = 3.0
'2.1 math.ceil !
print

# ceil(-1.9) = -1.0
'-1.9 math.ceil !
print

# floor(4.9) = 4.0
'4.9 math.floor !
print

# floor(-2.1) = -3.0
'-2.1 math.floor !
print

# pow(2.0, 10.0) = 1024.0
'2.0 '10.0 math.pow !
print

# pow(3.0, 4.0) = 81.0
'3.0 '4.0 math.pow !
print
EDICT

echo
echo "--- Section 5: Double Chaining ---"
# Double return values are Edict string literals, so they chain directly
# into subsequent double-taking functions via the stod conversion path.
"$EDICT" - <<'EDICT'
unsafe_extensions_allow ! pop
'libc.so.6 resolver.load !
'libm.so.6 resolver.load !
'/usr/include/math.h parser.__native.map ! @math

# sqrt(fabs(-16.0)) = 4.0
'-16.0 math.fabs !
math.sqrt !
print

# ceil(sqrt(3.0)) = 2.0  (sqrt returns 1.7320508..., ceil rounds up)
'3.0 math.sqrt !
math.ceil !
print

# exp(log(1.0)) = 1.0
'1.0 math.log !
math.exp !
print

# ceil(sqrt(fabs(-9.0))) = 3.0  — three-deep chain
'-9.0 math.fabs !
math.sqrt !
math.ceil !
print

# log10(log2(256.0)) = log10(8.0) ≈ 0.903090025559947
'256.0 math.log2 !
math.log10 !
print
EDICT

echo
echo "--- Section 6: Two-Arg Double Functions ---"
# Functions that consume two double args (both accepted as strings or chained values).
"$EDICT" - <<'EDICT'
unsafe_extensions_allow ! pop
'libc.so.6 resolver.load !
'libm.so.6 resolver.load !
'/usr/include/math.h parser.__native.map ! @math

# hypot(3.0, 4.0) = 5.0 — Pythagorean hypotenuse
'3.0 '4.0 math.hypot !
print

# fmin(2.0, 7.0) = 2.0
'2.0 '7.0 math.fmin !
print

# fmax(2.0, 7.0) = 7.0
'2.0 '7.0 math.fmax !
print

# fmod(10.0, 3.0) = 1.0
'10.0 '3.0 math.fmod !
print

# pow(2.0, 10.0) = 1024.0
'2.0 '10.0 math.pow !
print

# sqrt(hypot(3.0, 4.0)) = sqrt(5.0) ≈ 2.2360679...
'3.0 '4.0 math.hypot !
math.sqrt !
print

# log2(8.0) = 3.0
'8.0 math.log2 !
print

# log10(1000.0) = 3.0
'1000.0 math.log10 !
print
EDICT

echo
echo "=== Demo complete ==="

echo
echo "--- Section 7: Heap Utilization (after all sections) ---"
"$EDICT" - <<'EDICT'
unsafe_extensions_allow ! pop
'libc.so.6 resolver.load !
'libm.so.6 resolver.load !
'/usr/include/stdlib.h parser.__native.map ! @stdlib
'/usr/include/ctype.h  parser.__native.map ! @ctype
'/usr/include/math.h   parser.__native.map ! @math

HeapUtilization !
EDICT
