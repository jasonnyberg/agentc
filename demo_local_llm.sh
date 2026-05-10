#!/bin/bash
ROOT=$(pwd)
./build/edict/edict - << INNER
[$ROOT/build/extensions/libagentc_extensions.so] [$ROOT/extensions/agentc_stdlib.h] resolver.import! @ext
[$ROOT/build/cpp-agent/libagent_runtime.so] [$ROOT/cpp-agent/include/agentc_runtime/agentc_runtime.h] resolver.import! @runtimeffi

"$ROOT/cpp-agent/edict/modules/agentc.edict" resolver.__native.read_text! !
"$ROOT/cpp-agent/edict/modules/agentc_stateful_loop.edict" resolver.__native.read_text! !

"Configuring AI runtime for local qwen3.6-27b..." print
{"default_provider": "local", "default_model": "qwen"} agentc_runtime_create! @rt

"Initializing agent state..." print
"You are Qwen, created by Alibaba Cloud. You are a helpful assistant. <|think_on|>" agentc_state_init! @state

"Sending request to local model: 'What is the capital of France?'" print
agentc_state_turn(rt state "What is the capital of France?") @next_state

"LLM Response:" print
next_state.last_response.message.text print

rt agentc_destroy!
INNER
