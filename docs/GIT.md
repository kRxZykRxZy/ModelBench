# Git Integration

ModelBench exposes git operations both as agent tools and through the Git panel UI.

## Tools

- `git_status` — porcelain status of the working tree.
- `git_diff` — unified diff (optionally for a single path).
- `git_log` — recent commits.
- `git_branch` — list/create/switch/delete branches.
- `git_commit` — commit staged (or all) changes with a message.
- `git_push` — push to the remote (network approval).
- `git_pull` — pull from the remote (network approval).

Push and pull are `Network` level and require approval unless auto-approved.
Force push is `Dangerous`.

## UI

`GitView.qml` shows the current branch, changed files, a diff viewer and a commit
composer. It falls back to sample data until the `git` context property is wired.

## Sandboxing

Git tools operate on the configured project root. Commands are executed with the
root as the working directory; path arguments are validated.
