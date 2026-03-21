// demo_complex.h — Test header for boxing/unboxing with varied scalar types
// and a nested struct field.  Used by demo/test_boxing_ffi.sh.

#ifndef AGENTC_DEMO_COMPLEX_H
#define AGENTC_DEMO_COMPLEX_H

#include <stdint.h>

// Nested struct: a simple 2-D integer point.
struct InnerPoint {
    int x;
    int y;
};

// Top-level struct exercising every scalar width and float/double, plus a
// nested struct field.
struct ComplexStruct {
    int8_t    byte_val;
    uint8_t   ubyte_val;
    int16_t   short_val;
    uint16_t  ushort_val;
    int32_t   int_val;
    uint32_t  uint_val;
    int64_t   long_val;
    uint64_t  ulong_val;
    float     float_val;
    double    double_val;
    struct InnerPoint origin;
};

#endif /* AGENTC_DEMO_COMPLEX_H */
