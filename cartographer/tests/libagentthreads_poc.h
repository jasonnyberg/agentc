#ifndef LIBAGENTTHREADS_POC_H
#define LIBAGENTTHREADS_POC_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int ltv;
typedef ltv (*LtvUnaryOp)(ltv);
typedef int (*PointerUnaryStatusOp)(void*);

typedef struct agentc_thread_handle agentc_thread_handle;
typedef struct agentc_shared_value agentc_shared_value;

agentc_thread_handle* agentc_thread_spawn_ltv(LtvUnaryOp entry, ltv arg);
ltv agentc_thread_join_ltv(agentc_thread_handle* handle);
agentc_thread_handle* agentc_thread_spawn_status(PointerUnaryStatusOp entry, void* arg);
int agentc_thread_join_status(agentc_thread_handle* handle);
void agentc_thread_detach(agentc_thread_handle* handle);
void agentc_thread_destroy(agentc_thread_handle* handle);

agentc_shared_value* agentc_shared_create_ltv(ltv initial);
void agentc_shared_destroy(agentc_shared_value* cell);
ltv agentc_shared_read_ltv(agentc_shared_value* cell);
int agentc_shared_write_ltv(agentc_shared_value* cell, ltv replacement);

#ifdef __cplusplus
}
#endif

#endif
