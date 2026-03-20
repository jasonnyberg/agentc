// This file is part of AgentC.
//
// AgentC is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.
//
// AgentC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with AgentC. If not, see <https://www.gnu.org/licenses/>.

// demo_ffi_libc.cpp — stress-tests the AgentC FFI mechanism by importing
// libc and libm and exercising a wide variety of function categories.

#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdint>
#include "../listree/listree.h"
#include "../cartographer/mapper.h"
#include "../cartographer/ffi.h"

using namespace agentc;
using namespace agentc::cartographer;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static FFI* g_ffi = nullptr;
static CPtr<ListreeValue> g_root;

// Wrap a single int as the FFI arg value.
static CPtr<ListreeValue> intArg(int v) {
    return createBinaryValue(&v, sizeof(int));
}

// Wrap a double.
static CPtr<ListreeValue> dblArg(double v) {
    return createBinaryValue(&v, sizeof(double));
}

// Wrap a raw pointer (e.g. char*).
static CPtr<ListreeValue> ptrArg(void* p) {
    return createBinaryValue(&p, sizeof(void*));
}

// Build a multi-arg list.
static CPtr<ListreeValue> args2(CPtr<ListreeValue> a, CPtr<ListreeValue> b) {
    auto list = createListValue();
    addListItem(list, a);
    addListItem(list, b);
    return list;
}

static CPtr<ListreeValue> args3(CPtr<ListreeValue> a, CPtr<ListreeValue> b, CPtr<ListreeValue> c) {
    auto list = createListValue();
    addListItem(list, a);
    addListItem(list, b);
    addListItem(list, c);
    return list;
}

static int callInt0(const std::string& name) {
    auto fn = g_root->find(name);
    if (!fn || !fn->getValue()) { std::cout << "  [SKIP] " << name << " not found\n"; return 0; }
    auto r = g_ffi->invoke(name, fn->getValue(), nullptr);
    return r ? *(int*)r->getData() : 0;
}

static int callInt1(const std::string& name, CPtr<ListreeValue> a) {
    auto fn = g_root->find(name);
    if (!fn || !fn->getValue()) { std::cout << "  [SKIP] " << name << " not found\n"; return 0; }
    auto r = g_ffi->invoke(name, fn->getValue(), a);
    return r ? *(int*)r->getData() : 0;
}

static int callInt2(const std::string& name, CPtr<ListreeValue> a, CPtr<ListreeValue> b) {
    auto fn = g_root->find(name);
    if (!fn || !fn->getValue()) { std::cout << "  [SKIP] " << name << " not found\n"; return 0; }
    auto r = g_ffi->invoke(name, fn->getValue(), args2(a, b));
    return r ? *(int*)r->getData() : 0;
}

static int callInt3(const std::string& name, CPtr<ListreeValue> a, CPtr<ListreeValue> b, CPtr<ListreeValue> c) {
    auto fn = g_root->find(name);
    if (!fn || !fn->getValue()) { std::cout << "  [SKIP] " << name << " not found\n"; return 0; }
    auto r = g_ffi->invoke(name, fn->getValue(), args3(a, b, c));
    return r ? *(int*)r->getData() : 0;
}

static double callDbl1(const std::string& name, CPtr<ListreeValue> a) {
    auto fn = g_root->find(name);
    if (!fn || !fn->getValue()) { std::cout << "  [SKIP] " << name << " not found\n"; return 0.0; }
    auto r = g_ffi->invoke(name, fn->getValue(), a);
    return r ? *(double*)r->getData() : 0.0;
}

static double callDbl2(const std::string& name, CPtr<ListreeValue> a, CPtr<ListreeValue> b) {
    auto fn = g_root->find(name);
    if (!fn || !fn->getValue()) { std::cout << "  [SKIP] " << name << " not found\n"; return 0.0; }
    auto r = g_ffi->invoke(name, fn->getValue(), args2(a, b));
    return r ? *(double*)r->getData() : 0.0;
}

// ---------------------------------------------------------------------------
// 1. Integer math
// ---------------------------------------------------------------------------
void demo_integer_math() {
    std::cout << "\n=== 1. Integer Math ===" << std::endl;
    std::cout << "abs(-42)   = " << callInt1("abs", intArg(-42)) << std::endl;
    std::cout << "abs(0)     = " << callInt1("abs", intArg(0))   << std::endl;
    std::cout << "abs(100)   = " << callInt1("abs", intArg(100)) << std::endl;
}

// ---------------------------------------------------------------------------
// 2. Char classification
// ---------------------------------------------------------------------------
void demo_char_classification() {
    std::cout << "\n=== 2. Char Classification ===" << std::endl;
    // Expect non-zero for true, 0 for false (same semantics as <ctype.h>).
    auto yn = [](int v){ return v ? "yes" : "no"; };
    std::cout << "toupper('a') = " << (char)callInt1("toupper", intArg('a')) << std::endl;
    std::cout << "tolower('Z') = " << (char)callInt1("tolower", intArg('Z')) << std::endl;
    std::cout << "isdigit('5') = " << yn(callInt1("isdigit", intArg('5'))) << std::endl;
    std::cout << "isdigit('x') = " << yn(callInt1("isdigit", intArg('x'))) << std::endl;
    std::cout << "isalpha('x') = " << yn(callInt1("isalpha", intArg('x'))) << std::endl;
    std::cout << "isalpha('5') = " << yn(callInt1("isalpha", intArg('5'))) << std::endl;
    std::cout << "isupper('A') = " << yn(callInt1("isupper", intArg('A'))) << std::endl;
    std::cout << "islower('a') = " << yn(callInt1("islower", intArg('a'))) << std::endl;
    std::cout << "isspace(' ') = " << yn(callInt1("isspace", intArg(' '))) << std::endl;
    std::cout << "isspace('x') = " << yn(callInt1("isspace", intArg('x'))) << std::endl;
}

// ---------------------------------------------------------------------------
// 3. Randomness
// ---------------------------------------------------------------------------
void demo_randomness() {
    std::cout << "\n=== 3. Randomness ===" << std::endl;
    // srand returns void — invoke returns null/empty; we just check no crash.
    {
        auto fn = g_root->find("srand");
        if (fn && fn->getValue()) {
            unsigned int seed = 42u;
            auto seedVal = createBinaryValue(&seed, sizeof(unsigned int));
            g_ffi->invoke("srand", fn->getValue(), seedVal);
            std::cout << "srand(42) called (void return)" << std::endl;
        } else {
            std::cout << "  [SKIP] srand not found\n";
        }
    }
    // rand() — 0 args.
    int r1 = callInt0("rand");
    int r2 = callInt0("rand");
    int r3 = callInt0("rand");
    std::cout << "rand() x3 = " << r1 << ", " << r2 << ", " << r3 << std::endl;
    // Verify they're in [0, RAND_MAX] range (non-negative).
    bool ok = (r1 >= 0) && (r2 >= 0) && (r3 >= 0);
    std::cout << "All non-negative: " << (ok ? "yes" : "NO") << std::endl;
}

// ---------------------------------------------------------------------------
// 4. Floating-point math
// ---------------------------------------------------------------------------
void demo_float_math() {
    std::cout << "\n=== 4. Float Math ===" << std::endl;
    std::cout << "sqrt(9.0)    = " << callDbl1("sqrt",  dblArg(9.0))   << std::endl;
    std::cout << "sqrt(2.0)    = " << callDbl1("sqrt",  dblArg(2.0))   << std::endl;
    std::cout << "fabs(-3.14)  = " << callDbl1("fabs",  dblArg(-3.14)) << std::endl;
    std::cout << "ceil(2.1)    = " << callDbl1("ceil",  dblArg(2.1))   << std::endl;
    std::cout << "floor(2.9)   = " << callDbl1("floor", dblArg(2.9))   << std::endl;
    std::cout << "pow(2.0,10)  = " << callDbl2("pow",   dblArg(2.0), dblArg(10.0)) << std::endl;
    std::cout << "pow(3.0,3.0) = " << callDbl2("pow",   dblArg(3.0), dblArg(3.0)) << std::endl;
}

// ---------------------------------------------------------------------------
// 5. String / memory functions
// ---------------------------------------------------------------------------
void demo_string_functions() {
    std::cout << "\n=== 5. String / Memory ===" << std::endl;

    const char* hello = "hello";
    const char* world = "world";
    const char* hello2 = "hello";
    const char* num_str = "12345";
    const char* float_str = "3.14";

    // strlen — returns int (declared that way in header to match FFI int type).
    int len = callInt1("strlen", ptrArg((void*)hello));
    std::cout << "strlen(\"hello\") = " << len << std::endl;

    // strcmp — non-commutative: verifies arg ordering.
    int cmp_hw = callInt2("strcmp", ptrArg((void*)hello), ptrArg((void*)world));
    int cmp_wh = callInt2("strcmp", ptrArg((void*)world), ptrArg((void*)hello));
    int cmp_hh = callInt2("strcmp", ptrArg((void*)hello), ptrArg((void*)hello2));
    std::cout << "strcmp(\"hello\",\"world\") < 0 : " << (cmp_hw < 0 ? "yes" : "NO") << "  (raw=" << cmp_hw << ")" << std::endl;
    std::cout << "strcmp(\"world\",\"hello\") > 0 : " << (cmp_wh > 0 ? "yes" : "NO") << "  (raw=" << cmp_wh << ")" << std::endl;
    std::cout << "strcmp(\"hello\",\"hello\") == 0: " << (cmp_hh == 0 ? "yes" : "NO") << "  (raw=" << cmp_hh << ")" << std::endl;

    // strncmp(hello, world, 3) — compares first 3 chars: "hel" vs "wor" → negative.
    int ncmp = callInt3("strncmp", ptrArg((void*)hello), ptrArg((void*)world), intArg(3));
    std::cout << "strncmp(\"hello\",\"world\",3) < 0: " << (ncmp < 0 ? "yes" : "NO") << "  (raw=" << ncmp << ")" << std::endl;

    // atoi.
    int ival = callInt1("atoi", ptrArg((void*)num_str));
    std::cout << "atoi(\"12345\") = " << ival << std::endl;

    // atof — returns double.
    double fval = callDbl1("atof", ptrArg((void*)float_str));
    std::cout << "atof(\"3.14\")  = " << fval << std::endl;
}

// ---------------------------------------------------------------------------
// Setup: write header, parse, load libraries
// ---------------------------------------------------------------------------
static bool setup() {
    const std::string headerPath = "demo_ffi_libc.h";
    {
        std::ofstream ofs(headerPath);
        if (!ofs) { std::cerr << "Failed to write " << headerPath << "\n"; return false; }
        // All types use plain C spellings understood by both clang and FFI type mapper.
        // strlen declared as int (not size_t) so FFI resolves return as ffi_type_sint.
        ofs << "int abs(int n);\n";
        ofs << "int toupper(int c);\n";
        ofs << "int tolower(int c);\n";
        ofs << "int isdigit(int c);\n";
        ofs << "int isalpha(int c);\n";
        ofs << "int isupper(int c);\n";
        ofs << "int islower(int c);\n";
        ofs << "int isspace(int c);\n";
        ofs << "void srand(int seed);\n";
        ofs << "int rand(void);\n";
        ofs << "double sqrt(double x);\n";
        ofs << "double fabs(double x);\n";
        ofs << "double ceil(double x);\n";
        ofs << "double floor(double x);\n";
        ofs << "double pow(double base, double exp);\n";
        // Parameter names must sort alphabetically in declaration order because the
        // mapper stores them in an AATree (sorted by name). Types are then assigned
        // in alphabetical-name order, so names like a1, a2, a3 guarantee correct ordering.
        ofs << "int strlen(char* a1);\n";
        ofs << "int strcmp(char* a1, char* a2);\n";
        ofs << "int strncmp(char* a1, char* a2, int a3);\n";
        ofs << "int atoi(char* a1);\n";
        ofs << "double atof(char* a1);\n";
    }

    Mapper mapper;
    g_root = mapper.parse(headerPath);
    if (!g_root) { std::cerr << "Mapper failed to parse " << headerPath << "\n"; return false; }

    g_ffi = new FFI();
    bool libc_ok =
        g_ffi->loadLibrary("libc.so.6") ||
        g_ffi->loadLibrary("/lib64/libc.so.6") ||
        g_ffi->loadLibrary("/lib/x86_64-linux-gnu/libc.so.6");
    if (!libc_ok) { std::cerr << "Could not load libc\n"; return false; }

    // libm may be separate or merged into libc on modern glibc — try both.
    g_ffi->loadLibrary("libm.so.6");
    g_ffi->loadLibrary("/lib64/libm.so.6");
    g_ffi->loadLibrary("/lib/x86_64-linux-gnu/libm.so.6");

    return true;
}

int main() {
    std::cout << "=== FFI libc/libm Stress Test ===" << std::endl;

    if (!setup()) {
        std::cerr << "Setup failed, aborting.\n";
        return 1;
    }

    demo_integer_math();
    demo_char_classification();
    demo_randomness();
    demo_float_math();
    demo_string_functions();

    std::cout << "\n=== Done ===" << std::endl;

    delete g_ffi;
    return 0;
}
