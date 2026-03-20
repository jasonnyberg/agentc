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
"libc.so.6" resolver.load !
"/usr/include/stdlib.h" parser.__native.map ! @stdlib

# abs of a positive number
"42" stdlib.abs !
print

# abs of a negative number
"-7" stdlib.abs !
print

# abs of zero
"0" stdlib.abs !
print

# abs of large negative
"-1000" stdlib.abs !
print
EDICT

echo
echo "--- Section 2: Char Classification ---"
"$EDICT" - <<'EDICT'
unsafe_extensions_allow ! pop
"libc.so.6" resolver.load !
"/usr/include/ctype.h" parser.__native.map ! @ctype

# toupper('a'=97) -> 65 ('A')
"97" ctype.toupper !
print

# toupper('A'=65) -> 65 (already upper)
"65" ctype.toupper !
print

# tolower('A'=65) -> 97 ('a')
"65" ctype.tolower !
print

# tolower('z'=122) -> 122 (already lower)
"122" ctype.tolower !
print

# isdigit('5'=53) -> nonzero
"53" ctype.isdigit !
print

# isdigit('a'=97) -> 0
"97" ctype.isdigit !
print

# isalpha('z'=122) -> nonzero
"122" ctype.isalpha !
print

# isalpha('5'=53) -> 0
"53" ctype.isalpha !
print

# isspace(' '=32) -> nonzero
"32" ctype.isspace !
print

# isspace('a'=97) -> 0
"97" ctype.isspace !
print

# isupper('A'=65) -> nonzero
"65" ctype.isupper !
print

# isupper('a'=97) -> 0
"97" ctype.isupper !
print

# islower('a'=97) -> nonzero
"97" ctype.islower !
print

# islower('A'=65) -> 0
"65" ctype.islower !
print
EDICT

echo
echo "--- Section 3: Randomness (srand / rand) ---"
"$EDICT" - <<'EDICT'
unsafe_extensions_allow ! pop
"libc.so.6" resolver.load !
"/usr/include/stdlib.h" parser.__native.map ! @stdlib

# seed the RNG
"42" stdlib.srand !

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
"libc.so.6" resolver.load !
"libm.so.6" resolver.load !
"/usr/include/math.h" parser.__native.map ! @math

# sqrt(9.0) -> lround -> 3
"9.0" math.sqrt !
math.lround !
print

# sqrt(144.0) -> lround -> 12
"144.0" math.sqrt !
math.lround !
print

# fabs(-3.7) -> floor -> lround -> 3
"-3.7" math.fabs !
math.floor !
math.lround !
print

# fabs(5.5) -> lround -> 6 (rounds half-up)
"5.5" math.fabs !
math.lround !
print

# ceil(2.1) -> lround -> 3
"2.1" math.ceil !
math.lround !
print

# ceil(-1.9) -> lround -> -1
"-1.9" math.ceil !
math.lround !
print

# floor(4.9) -> lround -> 4
"4.9" math.floor !
math.lround !
print

# floor(-2.1) -> lround -> -3
"-2.1" math.floor !
math.lround !
print

# pow(2.0, 10.0) -> lround -> 1024
"2.0" "10.0" math.pow !
math.lround !
print

# pow(3.0, 4.0) -> lround -> 81
"3.0" "4.0" math.pow !
math.lround !
print
EDICT

echo
echo "=== Demo complete ==="
