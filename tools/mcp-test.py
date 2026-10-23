#!/usr/bin/env python3
"""Talks to `bemotion mcp` like an AI assistant would and checks every tool.

    python3 tools/mcp-test.py <bemotion executable> <scratch dir>

Exits non-zero (and says why) if anything is off. Used by tools/smoke-test.sh.
"""
import base64
import json
import os
import subprocess
import sys

exe, scratch = sys.argv[1], os.path.abspath(sys.argv[2])
os.makedirs(scratch, exist_ok=True)
env = dict(os.environ, BEMOTION_APPDATA=os.path.join(scratch, "appdata"), QT_QPA_PLATFORM="offscreen")
server = subprocess.Popen([exe, "mcp"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, env=env)
next_id = 0
failures = []


def rpc(method, params=None, notify=False):
    global next_id
    msg = {"jsonrpc": "2.0", "method": method}
    if params is not None:
        msg["params"] = params
    if not notify:
        next_id += 1
        msg["id"] = next_id
    server.stdin.write((json.dumps(msg) + "\n").encode())
    server.stdin.flush()
    if notify:
        return None
    line = server.stdout.readline()
    if not line:
        raise SystemExit("server closed the connection")
    reply = json.loads(line)
    assert reply.get("id") == next_id, reply
    return reply


def call(tool, **args):
    reply = rpc("tools/call", {"name": tool, "arguments": args})
    return reply["result"]


def text(result):
    return "\n".join(c["text"] for c in result["content"] if c["type"] == "text")


def check(ok, what):
    print(("  ok    " if ok else "  FAIL  ") + what)
    if not ok:
        failures.append(what)


project = os.path.join(scratch, "mcp.bemotion")
if os.path.exists(project):
    os.remove(project)

init = rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "test", "version": "1"}})
check(init["result"]["serverInfo"]["name"] == "bemotion", "initialize")
rpc("notifications/initialized", notify=True)
tools = [t["name"] for t in rpc("tools/list")["result"]["tools"]]
check({"describe_project", "apply_operations", "render_frames", "run_lua", "export_video"} <= set(tools), "tools/list: %d tools" % len(tools))
check("error" in rpc("no/such/method"), "unknown method is a protocol error")

check(not call("create_project", project=project, width=1280, height=720).get("isError"), "create_project")
r = call("apply_operations", project=project, operations=[
    {"op": "layer.add", "type": "rectangle", "name": "Box", "position": [640, 360]},
    {"op": "property.set", "layer": "Box", "prop": "fill_color", "value": "#E8B931"},
    {"op": "key.add", "layer": "Box", "prop": "rotation", "time": 0, "value": 0},
    {"op": "key.add", "layer": "Box", "prop": "rotation", "time": 2, "value": 180, "easing": "ease_in_out"},
])
check(not r.get("isError") and json.loads(text(r))["saved"], "apply_operations")
bad = call("apply_operations", project=project, operations={"op": "property.set", "layer": "Bx", "prop": "opacity", "value": 0})
check(bad.get("isError") and '"Box"' in text(bad), "a bad operation is an error result that suggests the name")
check('"Box" rectangle' in text(call("describe_project", project=project)), "describe_project")
q = json.loads(text(call("query_layer", project=project, layer="Box", time=1)))
check(len(q["props"]["rotation"]["keys"]) == 2, "query_layer")
lua = call("run_lua", project=project, code='local doc = ...\ndoc.apply{op="layer.add", type="text", name="Label", text="MCP"}\nprint("added")')
check(not lua.get("isError") and "added" in text(lua), "run_lua")
img = call("render_frames", project=project, count=4)
pngs = [c for c in img["content"] if c["type"] == "image"]
data = base64.b64decode(pngs[0]["data"]) if pngs else b""
check(data[:8] == b"\x89PNG\r\n\x1a\n", "render_frames returns a PNG (%d bytes)" % len(data))
with open(os.path.join(scratch, "mcp-sheet.png"), "wb") as f:
    f.write(data)
check(text(call("validate_project", project=project)).strip() == "valid", "validate_project")
out = os.path.join(scratch, "mcp.gif")
check(not call("export_video", project=project, output=out, scale=0.25).get("isError") and os.path.getsize(out) > 0, "export_video")
check("layer.add" in text(call("get_operations_reference")), "get_operations_reference")

server.stdin.close()
server.wait(timeout=30)
check(server.returncode == 0, "server exits when stdin closes")
print("mcp test %s" % ("FAILED: " + ", ".join(failures) if failures else "passed"))
sys.exit(1 if failures else 0)
