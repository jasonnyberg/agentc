#!/usr/bin/env python3
"""
gendocs.py — Generate multi-resolution design documentation for AgentC/J3.

Produces a static HTML site in <project>/design/ with three zoom levels:
  L1  design/index.html             — System-level component diagram
  L2  design/components/<name>.html — Per-component file+function overview
  L3  design/functions/<id>.html    — Per-function call tree (all functions)

Usage:
    python scripts/gendocs.py [--root <project-root>] [--out <output-dir>]

Requires: ctags (universal-ctags), graphviz (dot)
"""

import argparse
import json
import os
import re
import subprocess
import sys
import textwrap
from collections import defaultdict
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

# ---------------------------------------------------------------------------
# Component mapping: source file patterns → component name + description
# ---------------------------------------------------------------------------
COMPONENTS = [
    {
        "id": "slab_listree",
        "name": "Slab / Listree",
        "color": "#4e79a7",
        "description": (
            "Core memory subsystem. The slab allocator provides arena-based "
            "allocation with transaction support. Listree is the universal "
            "recursive data structure (tree of key→value dicts + lists) built "
            "on top of the slab."
        ),
        "patterns": [
            r"alloc\.(cpp|h)$",
            r"listree/listree\.(cpp|h)$",
            r"container\.h$",
            r"debug\.(cpp|h)$",
        ],
    },
    {
        "id": "cursor",
        "name": "Cursor",
        "color": "#f28e2b",
        "description": (
            "Navigation layer over Listree. The Cursor provides a positional "
            "pointer into the tree with next/prev/up/down traversal, "
            "path-based addressing, and read/write/delete operations."
        ),
        "patterns": [
            r"cursor\.(cpp|h)$",
        ],
    },
    {
        "id": "edict_vm",
        "name": "Edict VM",
        "color": "#e15759",
        "description": (
            "Stack-based virtual machine for the edict language. Manages "
            "execution contexts, opcode dispatch, scope/frame management, "
            "speculation (rollback), and runtime value operations."
        ),
        "patterns": [
            r"edict/edict_vm\.(cpp|h)$",
            r"edict/edict_types\.(cpp|h)$",
        ],
    },
    {
        "id": "edict_compiler",
        "name": "Edict Compiler",
        "color": "#76b7b2",
        "description": (
            "Tokenizer and compiler for edict source text → bytecode. "
            "Handles the full edict grammar: JSON literals, bracket thunks, "
            "keywords, identifiers, and string literals."
        ),
        "patterns": [
            r"edict/edict_compiler\.(cpp|h)$",
        ],
    },
    {
        "id": "edict_repl",
        "name": "Edict REPL / Entry",
        "color": "#59a14f",
        "description": (
            "Interactive REPL and command-line entry points. Wires the "
            "compiler and VM together, manages the prelude, and handles "
            "line-by-line user input."
        ),
        "patterns": [
            r"edict/edict_repl\.(cpp|h)$",
            r"edict/main\.cpp$",
        ],
    },
    {
        "id": "cartographer",
        "name": "Cartographer / FFI",
        "color": "#b07aa1",
        "description": (
            "Foreign Function Interface bridge. Parses C headers (via "
            "libclang), resolves symbols from shared libraries, and exposes "
            "native functions to edict programs via a type-safe call bridge."
        ),
        "patterns": [
            r"cartographer/.*\.(cpp|h)$",
        ],
    },
    {
        "id": "kanren",
        "name": "Mini-Kanren",
        "color": "#ff9da7",
        "description": (
            "Logic programming engine implementing miniKanren. Provides "
            "relational goals (fresh, conde, unify), lazy streams via "
            "snooze(), and built-in relations (membero, appendo)."
        ),
        "patterns": [
            r"kanren/kanren\.(cpp|h)$",
        ],
    },
]

# Inter-component dependency edges (hand-authored, reflects architectural intent)
# Format: (from_id, to_id, label)
COMPONENT_EDGES = [
    ("edict_vm", "slab_listree", "reads/writes Listree values"),
    ("edict_vm", "cursor", "cursor ops (nav/assign)"),
    ("edict_vm", "cartographer", "FFI calls"),
    ("edict_vm", "kanren", "logic goals"),
    ("edict_compiler", "edict_vm", "emits bytecode"),
    ("edict_repl", "edict_compiler", "compiles input"),
    ("edict_repl", "edict_vm", "executes bytecode"),
    ("cursor", "slab_listree", "navigates Listree"),
    ("cartographer", "slab_listree", "stores parsed type info"),
    ("kanren", "slab_listree", "substitution store"),
]

# ---------------------------------------------------------------------------
# HTML templates
# ---------------------------------------------------------------------------

HTML_HEAD = """\
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>{title}</title>
<style>
  :root {{
    --bg: #1e1e2e; --surface: #2a2a3d; --border: #44445a;
    --text: #cdd6f4; --muted: #7f849c; --accent: #89b4fa;
    --link: #89dceb; --warn: #fab387;
  }}
  * {{ box-sizing: border-box; margin: 0; padding: 0; }}
  body {{ background: var(--bg); color: var(--text); font: 14px/1.6 "JetBrains Mono", "Fira Code", monospace; display: flex; min-height: 100vh; }}
  nav {{ width: 240px; min-width: 240px; background: var(--surface); border-right: 1px solid var(--border); padding: 1rem; overflow-y: auto; position: sticky; top: 0; height: 100vh; }}
  nav h2 {{ color: var(--accent); font-size: 0.8rem; text-transform: uppercase; letter-spacing: 0.1em; margin-bottom: 0.75rem; }}
  nav a {{ display: block; color: var(--link); text-decoration: none; padding: 0.2rem 0.4rem; border-radius: 3px; font-size: 0.82rem; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }}
  nav a:hover {{ background: var(--border); }}
  nav .nav-section {{ margin-bottom: 1.2rem; }}
  nav .nav-label {{ color: var(--muted); font-size: 0.72rem; text-transform: uppercase; margin-bottom: 0.3rem; letter-spacing: 0.08em; }}
  main {{ flex: 1; padding: 2rem; overflow-x: auto; max-width: 1200px; }}
  h1 {{ color: var(--accent); font-size: 1.4rem; margin-bottom: 0.5rem; }}
  h2 {{ color: var(--warn); font-size: 1rem; margin: 1.5rem 0 0.5rem; border-bottom: 1px solid var(--border); padding-bottom: 0.3rem; }}
  h3 {{ color: var(--text); font-size: 0.9rem; margin: 1rem 0 0.3rem; }}
  p {{ color: var(--muted); margin-bottom: 0.8rem; font-size: 0.88rem; }}
  .breadcrumb {{ color: var(--muted); font-size: 0.8rem; margin-bottom: 1rem; }}
  .breadcrumb a {{ color: var(--link); text-decoration: none; }}
  .graph-container {{ background: var(--surface); border: 1px solid var(--border); border-radius: 6px; padding: 1rem; margin: 1rem 0; overflow-x: auto; }}
  .graph-container svg {{ max-width: 100%; height: auto; }}
  table {{ border-collapse: collapse; width: 100%; margin: 0.5rem 0 1rem; font-size: 0.83rem; }}
  th {{ background: var(--surface); color: var(--accent); text-align: left; padding: 0.4rem 0.8rem; border: 1px solid var(--border); }}
  td {{ padding: 0.35rem 0.8rem; border: 1px solid var(--border); color: var(--text); }}
  td a {{ color: var(--link); text-decoration: none; }}
  td a:hover {{ text-decoration: underline; }}
  .tag {{ display: inline-block; background: var(--border); color: var(--muted); border-radius: 3px; padding: 0.1rem 0.4rem; font-size: 0.75rem; margin-right: 0.3rem; }}
  .file-path {{ color: var(--muted); font-size: 0.8rem; }}
  code {{ background: var(--surface); padding: 0.1rem 0.3rem; border-radius: 3px; font-size: 0.85em; }}
  .empty {{ color: var(--muted); font-style: italic; font-size: 0.85rem; }}
  /* make SVG text readable on dark bg */
  .graph-container svg text {{ fill: #cdd6f4 !important; }}
  .graph-container svg .edge path {{ stroke: #7f849c !important; }}
  .graph-container svg .edge polygon {{ fill: #7f849c !important; stroke: #7f849c !important; }}
</style>
</head>
<body>
"""

HTML_FOOT = """\
</body>
</html>
"""

# ---------------------------------------------------------------------------
# Utilities
# ---------------------------------------------------------------------------


def run(cmd: List[str], cwd: Optional[str] = None) -> str:
    result = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)
    return result.stdout


def dot_to_svg(dot_src: str, timeout: int = 30) -> str:
    """Render DOT source to inline SVG string."""
    try:
        result = subprocess.run(
            ["dot", "-Tsvg"],
            input=dot_src,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return "<p style='color:#fab387'>Graph layout timed out — too many nodes/edges.</p>"
    if result.returncode != 0:
        return f"<pre style='color:red'>dot error: {result.stderr}</pre>"
    svg = result.stdout
    # Strip XML declaration and DOCTYPE — we embed inline
    svg = re.sub(r"<\?xml[^>]+\?>", "", svg)
    svg = re.sub(r"<!DOCTYPE[^>]+>", "", svg)
    svg = svg.strip()
    return svg


def safe_id(name: str) -> str:
    """Make a filesystem-safe identifier from a function name."""
    return re.sub(r"[^a-zA-Z0-9_]", "_", name)


def rel_path(from_file: Path, to_file: Path) -> str:
    return os.path.relpath(str(to_file), str(from_file.parent))


# ---------------------------------------------------------------------------
# Sidebar nav builder
# ---------------------------------------------------------------------------


def build_nav(current_file: Path, out_dir: Path, components: List[dict]) -> str:
    index = out_dir / "index.html"
    lines = ["<nav>"]
    lines.append('<div class="nav-section">')
    lines.append('<div class="nav-label">System</div>')
    lines.append(
        f'<a href="{rel_path(current_file, index)}">&#9650; System Overview</a>'
    )
    lines.append("</div>")

    lines.append('<div class="nav-section">')
    lines.append('<div class="nav-label">Components</div>')
    for c in components:
        comp_file = out_dir / "components" / f"{c['id']}.html"
        cname = c["name"]
        lines.append(
            f'<a href="{rel_path(current_file, comp_file)}" title="{cname}">{cname}</a>'
        )
    lines.append("</div>")

    lines.append("</nav>")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Step 1: Extract all functions via ctags
# ---------------------------------------------------------------------------


def extract_functions(source_files: List[Path]) -> List[dict]:
    """Run ctags over all source files, return list of function dicts."""
    if not source_files:
        return []
    cmd = [
        "ctags",
        "-R",
        "--fields=+n+S",
        "--kinds-c=f",
        "--kinds-c++=f",
        "--output-format=json",
        "--language-force=C++",
    ] + [str(f) for f in source_files]
    output = run(cmd)
    functions = []
    for line in output.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            tag = json.loads(line)
        except json.JSONDecodeError:
            continue
        # Skip anonymous lambdas
        if tag.get("name", "").startswith("__anon"):
            continue
        functions.append(tag)
    return functions


# ---------------------------------------------------------------------------
# Step 2: Build call graph via regex scan
# ---------------------------------------------------------------------------


def extract_calls_from_file(
    filepath: Path, known_names: Set[str]
) -> Dict[str, Set[str]]:
    """
    For each function in filepath, find calls to other known functions.
    Returns {caller_name: {callee_name, ...}}.

    Strategy: use ctags line numbers to slice function bodies, then regex-scan
    each body for identifiers that match known function names.
    """
    try:
        source = filepath.read_text(errors="replace")
    except Exception:
        return {}

    lines = source.splitlines()

    # Get function definitions in this file, sorted by line
    cmd = [
        "ctags",
        "--fields=+n",
        "--kinds-c=f",
        "--kinds-c++=f",
        "--output-format=json",
        "--language-force=C++",
        str(filepath),
    ]
    output = run(cmd)
    local_fns = []
    for line in output.splitlines():
        try:
            tag = json.loads(line)
            if not tag.get("name", "").startswith("__anon"):
                local_fns.append(tag)
        except json.JSONDecodeError:
            continue

    local_fns.sort(key=lambda t: t.get("line", 0))

    call_graph: Dict[str, Set[str]] = {}

    for i, fn in enumerate(local_fns):
        start = fn.get("line", 1) - 1  # 0-indexed
        end = (
            local_fns[i + 1].get("line", len(lines) + 1) - 1
            if i + 1 < len(local_fns)
            else len(lines)
        )
        body = "\n".join(lines[start:end])

        # Find all word tokens in the body that match known function names
        tokens = set(re.findall(r"\b([a-zA-Z_][a-zA-Z0-9_]*)\b", body))
        caller = fn["name"]
        callees = tokens & known_names - {caller}
        if callees:
            call_graph[caller] = callees

    return call_graph


# ---------------------------------------------------------------------------
# Step 3: Assign functions to components
# ---------------------------------------------------------------------------


def assign_component(filepath: str, components: List[dict]) -> Optional[str]:
    """Return component id for a given file path, or None if unassigned."""
    # Normalize to relative-style matching
    norm = filepath.replace("\\", "/")
    for c in components:
        for pat in c["patterns"]:
            if re.search(pat, norm):
                return c["id"]
    return None


# ---------------------------------------------------------------------------
# Step 4: Generate DOT for component-level diagram (L1)
# ---------------------------------------------------------------------------


def gen_component_dot(components: List[dict], edges: List[Tuple]) -> str:
    lines = [
        "digraph system {",
        '  graph [bgcolor="#1e1e2e" fontname="monospace" pad="0.5" nodesep="0.8" ranksep="1.2"]',
        '  node  [shape=box style="rounded,filled" fontname="monospace" fontsize="11" fontcolor="#cdd6f4" penwidth="1.5" margin="0.2,0.12"]',
        '  edge  [fontname="monospace" fontsize="9" fontcolor="#7f849c" color="#44445a" arrowsize="0.7"]',
    ]
    for c in components:
        color = c["color"]
        lines.append(
            f'  {c["id"]} [label="{c["name"]}" fillcolor="{color}33" color="{color}" fontcolor="{color}"]'
        )
    for src, dst, label in edges:
        lines.append(f'  {src} -> {dst} [label="{label}"]')
    lines.append("}")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Step 5: Generate DOT for component detail (L2)
# ---------------------------------------------------------------------------


def gen_component_detail_dot(
    comp_id: str,
    functions_in_comp: List[dict],
    call_graph: Dict[str, Set[str]],
    all_functions_by_name: Dict[str, dict],
) -> str:
    """
    Show all functions in this component as nodes.
    Edges: calls within the component (solid); calls to external components (dashed).
    """
    comp_fn_names = {f["name"] for f in functions_in_comp}

    # Group by file
    files: Dict[str, List[dict]] = defaultdict(list)
    for fn in functions_in_comp:
        files[fn.get("path", "?")].append(fn)

    lines = [
        "digraph component {",
        '  graph [bgcolor="#1e1e2e" fontname="monospace" compound=true rankdir=LR pad="0.4" nodesep="0.5" ranksep="0.8"]',
        '  node  [shape=box style="filled,rounded" fontname="monospace" fontsize="9" fontcolor="#cdd6f4" fillcolor="#2a2a3d" color="#44445a" margin="0.1,0.06"]',
        '  edge  [fontname="monospace" fontsize="8" fontcolor="#7f849c" color="#44445a" arrowsize="0.6"]',
    ]

    # Subgraph per file
    for file_path, fns in files.items():
        fname = os.path.basename(file_path)
        cluster_id = safe_id(file_path)
        lines.append(f"  subgraph cluster_{cluster_id} {{")
        lines.append(
            f'    label="{fname}" fontname="monospace" fontsize="9" fontcolor="#89b4fa"'
        )
        lines.append(f'    color="#44445a" bgcolor="#1e1e2e"')
        for fn in fns:
            nid = safe_id(fn["name"])
            lines.append(f'    {nid} [label="{fn["name"]}"]')
        lines.append("  }")

    # Edges
    for caller, callees in call_graph.items():
        if caller not in comp_fn_names:
            continue
        caller_id = safe_id(caller)
        for callee in callees:
            callee_id = safe_id(callee)
            if callee in comp_fn_names:
                lines.append(f"  {caller_id} -> {callee_id}")
            elif callee in all_functions_by_name:
                # Cross-component call — dashed
                # Add callee as external node if not already present
                ext_comp = all_functions_by_name[callee].get("_component", "?")
                lines.append(
                    f'  {callee_id} [label="{callee}\\n({ext_comp})" style="dashed,rounded" color="#7f849c" fontcolor="#7f849c"]'
                )
                lines.append(
                    f'  {caller_id} -> {callee_id} [style=dashed color="#7f849c"]'
                )

    lines.append("}")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Step 6: Generate DOT for function call tree (L3)
# ---------------------------------------------------------------------------


def gen_function_dot(
    fn_name: str,
    call_graph: Dict[str, Set[str]],
    all_functions_by_name: Dict[str, dict],
    depth: int = 2,
    max_nodes: int = 60,
) -> str:
    """BFS call tree rooted at fn_name, up to `depth` levels and `max_nodes` nodes."""
    visited: Set[str] = set()
    queue = [(fn_name, 0)]
    edges_set: Set[Tuple[str, str]] = set()
    nodes: Dict[str, str] = {}  # name → color
    truncated = False

    while queue:
        name, level = queue.pop(0)
        if name in visited or level > depth:
            continue
        if len(nodes) >= max_nodes:
            truncated = True
            break
        visited.add(name)
        info = all_functions_by_name.get(name)
        if info:
            comp_id = info.get("_component", "unknown")
            comp = next((c for c in COMPONENTS if c["id"] == comp_id), None)
            color = comp["color"] if comp else "#7f849c"
        else:
            color = "#7f849c"
        nodes[name] = color

        for callee in call_graph.get(name, set()):
            edges_set.add((name, callee))
            if callee not in visited:
                queue.append((callee, level + 1))

    lines = [
        "digraph calltree {",
        '  graph [bgcolor="#1e1e2e" fontname="monospace" pad="0.4" nodesep="0.6" ranksep="0.8"]',
        '  node  [shape=box style="filled,rounded" fontname="monospace" fontsize="9" fontcolor="#cdd6f4" margin="0.12,0.08"]',
        '  edge  [color="#44445a" arrowsize="0.6"]',
    ]
    for name, color in nodes.items():
        nid = safe_id(name)
        style = 'penwidth="2.5"' if name == fn_name else 'penwidth="1"'
        lines.append(
            f'  {nid} [label="{name}" fillcolor="{color}33" color="{color}" {style}]'
        )
    # Only include edges where both endpoints are in our node set
    for src, dst in edges_set:
        if src in nodes and dst in nodes:
            lines.append(f"  {safe_id(src)} -> {safe_id(dst)}")
    if truncated:
        lines.append(
            f'  _truncated [label="... (graph truncated at {max_nodes} nodes)" shape=plaintext fontcolor="#7f849c" fontsize="8"]'
        )
    lines.append("}")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# HTML page generators
# ---------------------------------------------------------------------------


def write_index(
    out_dir: Path,
    components: List[dict],
    component_svg: str,
    functions_by_comp: Dict[str, List[dict]],
):
    nav = build_nav(out_dir / "index.html", out_dir, components)
    rows = []
    for c in components:
        count = len(functions_by_comp.get(c["id"], []))
        comp_link = f"components/{c['id']}.html"
        rows.append(
            f'<tr><td><a href="{comp_link}">{c["name"]}</a></td>'
            f"<td>{c['description']}</td>"
            f'<td style="text-align:right">{count}</td></tr>'
        )

    html = HTML_HEAD.format(title="AgentC/J3 — System Overview")
    html += nav
    html += "<main>"
    html += '<div class="breadcrumb">AgentC / J3</div>'
    html += "<h1>System Overview</h1>"
    html += "<p>Three-level design documentation. Click a component to drill in, or a function name to see its call tree.</p>"
    html += "<h2>Component Diagram</h2>"
    html += f'<div class="graph-container">{component_svg}</div>'
    html += "<h2>Components</h2>"
    html += "<table><tr><th>Component</th><th>Description</th><th>Functions</th></tr>"
    html += "\n".join(rows)
    html += "</table>"
    html += "</main>"
    html += HTML_FOOT

    (out_dir / "index.html").write_text(html)
    print(f"  wrote {out_dir}/index.html")


def write_component_page(
    out_dir: Path,
    comp: dict,
    functions: List[dict],
    call_graph: Dict[str, Set[str]],
    all_functions_by_name: Dict[str, dict],
    components: List[dict],
):
    current = out_dir / "components" / f"{comp['id']}.html"
    nav = build_nav(current, out_dir, components)

    dot = gen_component_detail_dot(
        comp["id"], functions, call_graph, all_functions_by_name
    )
    svg = dot_to_svg(dot)

    # Group functions by file
    by_file: Dict[str, List[dict]] = defaultdict(list)
    for fn in functions:
        by_file[fn.get("path", "?")].append(fn)

    rows = []
    for filepath in sorted(by_file.keys()):
        fns = sorted(by_file[filepath], key=lambda f: f.get("line", 0))
        fname = os.path.basename(filepath)
        for fn in fns:
            fn_id = safe_id(fn["name"])
            link = f"../functions/{fn_id}.html"
            line = fn.get("line", "?")
            scope = fn.get("scope", "")
            rows.append(
                f'<tr><td><a href="{link}">{fn["name"]}</a></td>'
                f'<td class="file-path">{fname}:{line}</td>'
                f'<td class="file-path">{scope}</td></tr>'
            )

    index_link = rel_path(current, out_dir / "index.html")
    html = HTML_HEAD.format(title=f"AgentC/J3 — {comp['name']}")
    html += nav
    html += "<main>"
    html += f'<div class="breadcrumb"><a href="{index_link}">System</a> › {comp["name"]}</div>'
    html += f"<h1>{comp['name']}</h1>"
    html += f"<p>{comp['description']}</p>"
    html += "<h2>Internal Structure</h2>"
    html += f'<div class="graph-container">{svg}</div>'
    html += f"<h2>Functions ({len(functions)})</h2>"
    if rows:
        html += "<table><tr><th>Function</th><th>Location</th><th>Scope</th></tr>"
        html += "\n".join(rows)
        html += "</table>"
    else:
        html += '<p class="empty">No functions extracted for this component.</p>'
    html += "</main>"
    html += HTML_FOOT

    current.write_text(html)
    print(f"  wrote {current}")


def write_function_page(
    out_dir: Path,
    fn: dict,
    call_graph: Dict[str, Set[str]],
    all_functions_by_name: Dict[str, dict],
    components: List[dict],
):
    fn_name = fn["name"]
    fn_id = safe_id(fn_name)
    current = out_dir / "functions" / f"{fn_id}.html"
    nav = build_nav(current, out_dir, components)

    comp_id = fn.get("_component", "unknown")
    comp = next((c for c in components if c["id"] == comp_id), None)
    comp_name = comp["name"] if comp else comp_id
    comp_link = f"../components/{comp_id}.html"

    dot = gen_function_dot(fn_name, call_graph, all_functions_by_name, depth=4)
    svg = dot_to_svg(dot)

    # Callers (who calls this function)
    callers = [name for name, callees in call_graph.items() if fn_name in callees]
    # Callees (what this function calls)
    callees = sorted(call_graph.get(fn_name, set()))

    def fn_link(name: str) -> str:
        nid = safe_id(name)
        return f'<a href="{nid}.html">{name}</a>'

    index_link = rel_path(current, out_dir / "index.html")
    html = HTML_HEAD.format(title=f"AgentC/J3 — {fn_name}()")
    html += nav
    html += "<main>"
    html += (
        f'<div class="breadcrumb">'
        f'<a href="{index_link}">System</a> › '
        f'<a href="{comp_link}">{comp_name}</a> › '
        f"{fn_name}</div>"
    )
    html += f"<h1><code>{fn_name}()</code></h1>"

    filepath = fn.get("path", "?")
    line = fn.get("line", "?")
    scope = fn.get("scope", "")
    html += (
        f'<p class="file-path"><span class="tag">{comp_name}</span> {filepath}:{line}'
    )
    if scope:
        html += f" &nbsp;·&nbsp; scope: <code>{scope}</code>"
    html += "</p>"

    html += "<h2>Call Tree</h2>"
    html += f'<div class="graph-container">{svg}</div>'

    html += "<h2>Calls</h2>"
    if callees:
        html += "<table><tr><th>Callee</th><th>Component</th></tr>"
        for callee in callees:
            ci = all_functions_by_name.get(callee, {}).get("_component", "?")
            cn = next((c["name"] for c in components if c["id"] == ci), ci)
            html += f"<tr><td>{fn_link(callee)}</td><td>{cn}</td></tr>"
        html += "</table>"
    else:
        html += '<p class="empty">No tracked callees.</p>'

    html += "<h2>Called By</h2>"
    if callers:
        html += "<table><tr><th>Caller</th><th>Component</th></tr>"
        for caller in sorted(callers):
            ci = all_functions_by_name.get(caller, {}).get("_component", "?")
            cn = next((c["name"] for c in components if c["id"] == ci), ci)
            html += f"<tr><td>{fn_link(caller)}</td><td>{cn}</td></tr>"
        html += "</table>"
    else:
        html += '<p class="empty">No tracked callers.</p>'

    html += "</main>"
    html += HTML_FOOT

    current.write_text(html)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(description="Generate AgentC/J3 design docs")
    parser.add_argument("--root", default=".", help="Project root directory")
    parser.add_argument(
        "--out", default="design", help="Output directory (relative to root)"
    )
    args = parser.parse_args()

    root = Path(args.root).resolve()
    out_dir = (root / args.out).resolve()

    print(f"Project root : {root}")
    print(f"Output dir   : {out_dir}")

    # Collect source files (exclude tests, build, legacy, LocalContext)
    EXCLUDE_DIRS = {
        "build",
        "legacy",
        "LocalContext",
        ".git",
        "Testing",
        "tst",
        "design",
        "design_test",
        "design_test2",
        "design_test3",
        "scripts",
        out_dir.name,  # always exclude the output dir itself
    }
    source_files: List[Path] = []
    for pat in ["**/*.cpp", "**/*.h"]:
        for f in root.glob(pat):
            parts = set(f.relative_to(root).parts)
            if parts & EXCLUDE_DIRS:
                continue
            source_files.append(f)
    source_files.sort()
    print(f"Source files : {len(source_files)}")

    # Extract functions
    print("Extracting functions via ctags...")
    functions = extract_functions(source_files)
    print(f"  found {len(functions)} named functions")

    # Assign components
    for fn in functions:
        fn["_component"] = assign_component(fn.get("path", ""), COMPONENTS)

    all_functions_by_name: Dict[str, dict] = {}
    for fn in functions:
        # Keep the first occurrence if duplicate names exist
        if fn["name"] not in all_functions_by_name:
            all_functions_by_name[fn["name"]] = fn

    known_names: Set[str] = set(all_functions_by_name.keys())

    # Group by component
    functions_by_comp: Dict[str, List[dict]] = defaultdict(list)
    for fn in functions:
        comp_id = fn.get("_component") or "unassigned"
        functions_by_comp[comp_id].append(fn)

    # Build call graph
    print("Building call graph...")
    call_graph: Dict[str, Set[str]] = {}
    for f in source_files:
        cg = extract_calls_from_file(f, known_names)
        for caller, callees in cg.items():
            if caller not in call_graph:
                call_graph[caller] = set()
            call_graph[caller] |= callees
    total_edges = sum(len(v) for v in call_graph.values())
    print(f"  {len(call_graph)} callers, {total_edges} edges")

    # Create output directories
    (out_dir / "components").mkdir(parents=True, exist_ok=True)
    (out_dir / "functions").mkdir(parents=True, exist_ok=True)

    # L1 — System overview
    print("Generating L1: system overview...")
    comp_dot = gen_component_dot(COMPONENTS, COMPONENT_EDGES)
    comp_svg = dot_to_svg(comp_dot)
    write_index(out_dir, COMPONENTS, comp_svg, functions_by_comp)

    # L2 — Component pages
    print("Generating L2: component pages...")
    for comp in COMPONENTS:
        fns = functions_by_comp.get(comp["id"], [])
        write_component_page(
            out_dir, comp, fns, call_graph, all_functions_by_name, COMPONENTS
        )

    # L3 — Function pages
    print("Generating L3: function pages...", flush=True)
    count = 0
    for fn in functions:
        write_function_page(out_dir, fn, call_graph, all_functions_by_name, COMPONENTS)
        count += 1
        if count % 50 == 0:
            print(f"  {count}/{len(functions)}...", flush=True)
    print(f"  wrote {count} function pages", flush=True)

    print(f"\nDone. Open {out_dir}/index.html in a browser.")


if __name__ == "__main__":
    main()
