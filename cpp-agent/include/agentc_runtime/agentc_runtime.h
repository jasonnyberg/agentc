#ifndef AGENTC_RUNTIME_H
#define AGENTC_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void* agentc_runtime_t;

const char* agentc_runtime_version(void);

agentc_runtime_t agentc_runtime_create_json(const char* config_json);
agentc_runtime_t agentc_runtime_create_file(const char* config_path);

int agentc_runtime_configure_json(agentc_runtime_t runtime, const char* config_json);
int agentc_runtime_configure_file(agentc_runtime_t runtime, const char* config_path);

char* agentc_runtime_request_json(agentc_runtime_t runtime, const char* request_json);
char* agentc_runtime_last_error_json(agentc_runtime_t runtime);
char* agentc_runtime_last_trace_json(agentc_runtime_t runtime);

void agentc_runtime_destroy(agentc_runtime_t runtime);
void agentc_runtime_free_string(char* value);

#ifdef __cplusplus
}
#endif

#endif
