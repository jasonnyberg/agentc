#!/bin/bash
ROOT=$(pwd)
./build/edict/edict - << INNER
[$ROOT/build/extensions/libagentc_extensions.so] [$ROOT/extensions/agentc_stdlib.h] resolver.import! @ext
[$ROOT/build/cpp-agent/libagent_runtime.so] [$ROOT/cpp-agent/include/agentc_runtime/agentc_runtime.h] resolver.import! @runtimeffi

"$ROOT/cpp-agent/edict/modules/agentc.edict" resolver.__native.read_text!!
"$ROOT/cpp-agent/edict/modules/agentc_stateful_loop.edict" resolver.__native.read_text!!

{} @cfg "google" @cfg.default_provider "$GEMINI_DEFAULT_MODEL" @cfg.default_model
cfg agentc_runtime_create! @rt

"You are a helpful AI assistant that replies concisely in one short sentence." agentc_state_init! @state

agentc_state_turn(rt state "What is the capital of France?") @final

"=== LLM Response ===" print
final to_json! print

rt agentc_destroy!
INNER
