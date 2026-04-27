# Knowledge: Pi Package Integration Pattern

**ID**: LOCAL:K030
**Category**: [J3 AgentC](index.md)
**Tags**: #j3, #pi, #extension, #skill, #package

## Summary
The correct way to deploy an AgentC-backed Pi extension+skill globally is via `pi install <path>`
pointing to a directory containing a `package.json` with a `pi` manifest.

## Verified Structure
```
pi_integration/
├── package.json          ← pi manifest: declares extensions/ and skills/ dirs
├── extensions/
│   └── agentc_extension.ts   ← ONLY .ts file here; pi loads all .ts as extensions
├── lib/
│   └── agentc.ts             ← AgentCSubstrate class; imported by extension
├── examples/
│   └── *.ts                  ← test/demo scripts; NOT in extensions/ to avoid loader crash
└── skills/
    └── agentc/
        └── SKILL.md          ← frontmatter required: name (lowercase), description
```

## package.json manifest
```json
{
  "name": "agentc-pi-tools",
  "version": "1.0.0",
  "keywords": ["pi-package"],
  "pi": {
    "extensions": ["./extensions"],
    "skills": ["./skills"]
  },
  "peerDependencies": { "@mariozechner/pi-coding-agent": "*" }
}
```

## Key Rules
- The `name` field in SKILL.md frontmatter must be **lowercase** and match the parent directory name.
- Do NOT put library/helper `.ts` files inside `extensions/` — Pi tries to load every `.ts` there as an extension.
- Do NOT symlink individual `.ts` files into `~/.pi/agent/extensions/` if they have relative imports — Node resolves relative to the symlink location, not the real file path.
- Use `pi install /absolute/path` for local package installation.
- Add `make pi_install` to the Makefile to keep the global Pi environment in sync with source changes.

## Gotcha: Node Symlink Resolution
When a file is symlinked into `~/.pi/agent/extensions/`, Node resolves its relative imports
against `~/.pi/agent/extensions/` — not the original file's directory. This causes
"Cannot find module" errors. Use `pi install` instead.
