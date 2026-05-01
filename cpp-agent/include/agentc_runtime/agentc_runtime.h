#ifndef AGENTC_RUNTIME_H
#define AGENTC_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void* agentc_runtime_t;

const char* agentc_runtime_version(void);

void* agentc_runtime_create_json(const char* config_json);
void* agentc_runtime_create_file(const char* config_path);

int agentc_runtime_configure_json(void* runtime, const char* config_json);
int agentc_runtime_configure_file(void* runtime, const char* config_path);

char* agentc_runtime_request_json(void* runtime, const char* request_json);
char* agentc_runtime_last_error_json(void* runtime);
char* agentc_runtime_last_trace_json(void* runtime);

void agentc_runtime_destroy(void* runtime);
void agentc_runtime_free_string(char* value);

#ifdef __cplusplus
}
#endif

#endif
