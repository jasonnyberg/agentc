#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

"$SCRIPT_DIR/edict.sh" - <<'INNER'
"Initializing google provider preset 'google'..." print
llm.init([google]) @provider

"Sending request: 'What is the capital of France?'" print
provider < [What is the capital of France?] request ! > pop /

"LLM Response:" print
provider.assistant_text print
INNER
