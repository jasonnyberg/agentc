#ifndef AGENTC_EXTENSIONS_STDLIB_H
#define AGENTC_EXTENSIONS_STDLIB_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int ltv;

void* agentc_ext_memory_alloc(unsigned long size);
void* agentc_ext_memory_calloc(unsigned long count, unsigned long size);
void agentc_ext_memory_free(void* ptr);
void agentc_ext_memory_zero(void* ptr, unsigned long size);
void* agentc_ext_memory_slice(void* ptr, unsigned long offset);

void* agentc_ext_string_to_cstr_ltv(ltv value);
ltv agentc_ext_string_from_cstr(void* ptr);
unsigned long agentc_ext_string_length_ltv(ltv value);

unsigned long agentc_ext_type_size_ltv(ltv ctype_name);
int agentc_ext_memory_write_scalar_ltv(void* dest, unsigned long offset, ltv ctype_name, ltv value);
ltv agentc_ext_memory_read_scalar_ltv(void* src, unsigned long offset, ltv ctype_name);
int agentc_ext_memory_write_array_scalar_ltv(void* dest, unsigned long index, unsigned long stride, unsigned long field_offset, ltv ctype_name, ltv value);
ltv agentc_ext_memory_read_array_scalar_ltv(void* src, unsigned long index, unsigned long stride, unsigned long field_offset, ltv ctype_name);

ltv agentc_ext_binary_pack_scalar_ltv(ltv ctype_name, ltv value);
ltv agentc_ext_binary_concat_ltv(ltv left, ltv right);
ltv agentc_ext_binary_slice_ltv(ltv binary, unsigned long offset, unsigned long length);
ltv agentc_ext_binary_view_scalar_ltv(ltv binary, unsigned long offset, ltv ctype_name);

#ifdef __cplusplus
}
#endif

#endif
