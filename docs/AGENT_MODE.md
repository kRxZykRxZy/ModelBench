# Agent Mode

ModelBench includes an agent mode that plans a task, executes tools, observes results,
and refines its plan until the goal is met or it is cancelled.

## Flow

1. **Plan** — `AgentPlanner` turns a natural-language goal into a sequence of
   `PlanStep`s. Each step may reference a tool (`read_file`, `git_status`, …) and
   arguments. Explicit JSON plans (`[{"tool":"...","args":{...}}]`) are also accepted.
2. **Execute** — `AgentOrchestrator` drives `ToolRegistry::execute` for the current
   step. Read-only tools run immediately; write/execute/network/dangerous tools
   pass the approval gate first.
3. **Observe** — tool output is converted into an observation string and appended
   to the run log and `AgentMemory`.
4. **Refine** — the planner may append follow-up steps based on observations.
5. **Finish** — `agentFinished(bool ok, summary)` is emitted; the run can be
   exported via `exportRun()`.

## Signals

- `agentStarted(goal)`, `planReady(steps)`, `agentStepStarted(step, description, tool, args)`
- `agentStepFinished(step, description, ok, observation)`, `agentToken(token)`
- `approvalRequired(callId, tool, args, preview, level)`, `agentFinished(ok, summary)`,
  `agentCancelled()`

## Cancellation

`AgentOrchestrator::cancel()` stops the loop after the current step. Pending tool
calls are cancelled where supported. Cancelled runs report `agentCancelled()`.

## Memory

`AgentMemory` stores per-conversation scratchpad entries, project context, durable
facts, and notes, and can recall them (keyword + optional semantic search when a
`VectorStore` is attached). Memory survives across steps within a run and can be
serialized via `toJson()` / `fromJson()`.

## Approvals

The approval gate (`ToolApproval`) supports `Prompt`, `AutoApprove` and `AutoDeny`
modes. Read-only tools never require approval. Sensitive keys are redacted and
long values truncated in the preview shown to the user.
