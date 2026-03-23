#ifndef KANREN_RUNTIME_FFI_POC_H
#define KANREN_RUNTIME_FFI_POC_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct agentc_runtime_ctx agentc_runtime_ctx;
typedef unsigned int ltv;

agentc_runtime_ctx* agentc_runtime_create(void);
void agentc_runtime_destroy(agentc_runtime_ctx* ctx);

unsigned long long agentc_runtime_value_from_ltv(agentc_runtime_ctx* ctx, ltv value);
ltv agentc_runtime_value_to_ltv(agentc_runtime_ctx* ctx, unsigned long long value);

void agentc_runtime_value_retain(agentc_runtime_ctx* ctx, unsigned long long value);
void agentc_runtime_value_release(agentc_runtime_ctx* ctx, unsigned long long value);

unsigned long long agentc_runtime_copy_last_error(agentc_runtime_ctx* ctx);
unsigned long long agentc_logic_eval(agentc_runtime_ctx* ctx, unsigned long long spec);
ltv agentc_logic_eval_ltv(ltv spec);

#ifdef __cplusplus
}
#endif

#endif
