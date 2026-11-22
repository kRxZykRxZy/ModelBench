#!/usr/bin/env python3
"""Mock stdio MCP server for testing ModelFit's MCPClient.

Implements: initialize, notifications/initialized, tools/list, tools/call,
resources/list, resources/read, prompts/list, prompts/get, ping.

Behavior is driven by environment variables:
  MOCK_MCP_MALFORMED=1   send one malformed (non-JSON) line during the session
  MOCK_MCP_SLOW_CALL=1   tools/call "slow" sleeps before answering (used for
                         client-side timeout tests)
  MOCK_MCP_NO_INIT_RESULT=1  do not answer initialize (connection hangs)

Reads JSON-RPC lines from stdin, writes responses to stdout, diagnostics to
stderr. Exit the server by sending {"jsonrpc":"2.0","method":"exit"} or by
closing stdin.
"""

import json
import os
import sys
import time


def send(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def log(msg):
    sys.stderr.write("[mock-mcp] %s\n" % msg)
    sys.stderr.flush()


TOOLS = [
    {
        "name": "echo",
        "description": "Echo back the given text",
        "inputSchema": {
            "type": "object",
            "properties": {"text": {"type": "string"}},
            "required": ["text"],
        },
    },
    {
        "name": "add",
        "description": "Add two numbers",
        "inputSchema": {
            "type": "object",
            "properties": {
                "a": {"type": "number"},
                "b": {"type": "number"},
            },
            "required": ["a", "b"],
        },
    },
    {
        "name": "slow",
        "description": "Sleep then answer (for timeout tests)",
        "inputSchema": {
            "type": "object",
            "properties": {"sleep": {"type": "number", "default": 1}},
            "required": [],
        },
    },
    {
        "name": "fails",
        "description": "Always returns a JSON-RPC error",
        "inputSchema": {"type": "object", "properties": {}, "required": []},
    },
]

RESOURCES = [
    {
        "uri": "file:///readme.txt",
        "name": "Readme",
        "description": "Mock resource",
        "mimeType": "text/plain",
    }
]

PROMPTS = [
    {"name": "greet", "description": "A greeting prompt",
     "arguments": [{"name": "who", "description": "Whom to greet"}]}
]


def handle(msg):
    method = msg.get("method")
    params = msg.get("params") or {}
    rid = msg.get("id")

    if method == "initialize":
        send({
            "jsonrpc": "2.0",
            "id": rid,
            "result": {
                "protocolVersion": "2025-03-26",
                "capabilities": {},
                "serverInfo": {"name": "mock-mcp-server", "version": "1.0.0"},
            },
        })
    elif method == "notifications/initialized":
        pass
    elif method == "tools/list":
        send({"jsonrpc": "2.0", "id": rid, "result": {"tools": TOOLS}})
    elif method == "tools/call":
        name = params.get("name")
        args = params.get("arguments") or {}

        if name == "echo":
            text = args.get("text", "")
            result = {"content": [{"type": "text", "text": text}]}
        elif name == "add":
            a = args.get("a")
            b = args.get("b")
            if a is None or b is None:
                result = None
                error = {
                    "code": -32602,
                    "message": "Invalid params: a and b are required",
                }
            else:
                result = {
                    "content": [
                        {"type": "text", "text": str(float(a) + float(b))}
                    ]
                }
                error = None
        elif name == "slow":
            delay = float(params.get("arguments", {}).get("sleep", 2))
            time.sleep(delay)
            result = {"content": [{"type": "text", "text": "slept"}]}
            error = None
        elif name == "fails":
            result = None
            error = {"code": -32000, "message": "intentional server error"}
        else:
            result = None
            error = {"code": -32601, "message": "method not found"}

        if error is None:
            send({"jsonrpc": "2.0", "id": rid, "result": result})
        else:
            send({"jsonrpc": "2.0", "id": rid, "error": error})
    elif method == "resources/list":
        send({"jsonrpc": "2.0", "id": rid, "result": {"resources": RESOURCES}})
    elif method == "resources/read":
        uri = params.get("uri")
        send({
            "jsonrpc": "2.0",
            "id": rid,
            "result": {
                "contents": [
                    {"uri": uri, "mimeType": "text/plain",
                     "text": "mock resource contents for " + str(uri)}
                ]
            },
        })
    elif method == "prompts/list":
        send({"jsonrpc": "2.0", "id": rid, "result": {"prompts": PROMPTS}})
    elif method == "prompts/get":
        send({
            "jsonrpc": "2.0", "id": rid,
            "result": {"description": "mock prompt",
                       "messages": [{"role": "user", "content": "hello"}]},
        })
    elif method == "ping":
        send({"jsonrpc": "2.0", "id": rid, "result": {}})
    elif method == "exit":
        sys.exit(0)
    else:
        if rid is not None:
            send({"jsonrpc": "2.0", "id": rid,
                  "error": {"code": -32601, "message": "method not found"}})


def main():
    malformed = os.environ.get("MOCK_MCP_MALFORMED")
    malformed_sent = False
    slow_calls = os.environ.get("MOCK_MCP_SLOW_CALL")
    no_init = os.environ.get("MOCK_MCP_NO_INIT_RESULT")

    for raw in sys.stdin:
        line = raw.strip()
        if not line:
            continue

        # Inject one malformed line before answering initialize.
        if malformed and not malformed_sent and line.startswith("{"):
            try:
                msg = json.loads(line)
            except json.JSONDecodeError:
                msg = None
            if msg is not None and msg.get("method") == "initialize":
                malformed_sent = True
                sys.stdout.write("this is not valid json\r\n")
                sys.stdout.flush()
                continue

        try:
            msg = json.loads(line)
        except json.JSONDecodeError:
            send({"jsonrpc": "2.0", "id": None,
                  "error": {"code": -32700, "message": "Parse error"}})
            continue

        if not isinstance(msg, dict):
            continue

        # Apply artificial slowdown to 'slow' tool calls when requested.
        if slow_calls and msg.get("method") == "tools/call":
            args = msg.get("params") or {}
            if args.get("name") == "slow":
                args.setdefault("arguments", {})["sleep"] = 5.0
                msg["params"] = args

        if no_init and msg.get("method") == "initialize":
            # Never answer: client-side timeout path.
            continue

        handle(msg)


if __name__ == "__main__":
    main()
