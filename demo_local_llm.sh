#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

"$SCRIPT_DIR/edict.sh" - <<'INNER'
"Initializing local provider preset 'local-qwen'..." print
llm.init([local-qwen]) @provider

"Sending request to local model: 'What is the capital of France?'" print
provider < [What is the capital of France?] request! > pop /

"LLM Response:" print
provider.assistant_text print
INNER
