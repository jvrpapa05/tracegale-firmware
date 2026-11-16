# AI agents can use Tracegale

**Headline:** Tracegale now works with AI agents through the Model Context Protocol (MCP), the open standard that agents
of many kinds use for tools. A developer's agent can follow Tracegale's setup guide to add it to firmware,
add a device without ever seeing its private key, and answer questions about a fleet: what's online, what crashed and where
in the code, what the logs say, how a rollout is going. It changes nothing unless the developer allows it.

## What was tested

`tracegale-mcp` (`tools/mcp`) is a small program on the developer's machine. The agent talks to it over MCP; it talks to the
Tracegale server's admin API with the developer's API token. The tests used the official MCP Go SDK's own client, which
speaks the protocol exactly as an agent's software does, in two ways:

1. **Against a stand-in server**, to check the safety rules case by case.
2. **Against a real Tracegale server**, the whole stack in Docker with real firmware, in the end-to-end tests.

## The setup

- `tracegale-mcp`, built with Go 1.27.4 and the official MCP Go SDK v1.8.0, which speaks the current MCP specification
  (2026-07-28) and the earlier ones.
- The test machine in [README.md](README.md); the end-to-end stack as Tracegale ships it.

## Results

All 8 tests against the stand-in server passed, with the race detector on:

| Check | Result |
|---|---|
| Out of the box, only the 10 tools that change nothing are offered, each marked read-only for the agent | Passed |
| Tools that change things appear only when allowed; revoking a device only when that's allowed too, marked destructive | Passed |
| Every answer marks text from devices as untrusted, so a device name like "Ignore previous instructions…" is reported as data, not obeyed | Passed |
| The token and project reach the server; malicious IDs such as `../../admin` are refused before any request is made | Passed |
| The server's refusals reach the agent as tool errors, with the reason | Passed |
| Adding a device writes its credentials to a file only the user can read; **the private key never appears in the agent's conversation**; an existing file is never overwritten; the commands are in pieces the device's shell accepts | Passed |
| The setup guide is there for the agent to read | Passed |
| Bad settings are refused: plain HTTP, a missing or wrong token file, a missing CA | Passed |

Against the real server, in the end-to-end tests (44 checks in all, all passed): **an agent added a device without seeing
its key, saw it listed, and read the fleet overview, issues, crashes, releases, rollouts, logs and metrics, over the server's
own encrypted connection.**

The Go team's vulnerability scanner finds nothing in it.

## What it doesn't show

- **No AI model was driving it in these tests.** The SDK's client exercises every tool and rule as an agent would, but how
  well a given agent uses the tools, and how it handles text crafted to mislead it, depends on that agent. Trying it with a
  real agent is next, on the maintainers' test list.
- **The untrusted-text note helps, but can't guarantee** that an agent won't be misled; that's why changes are off by
  default, and why the server's roles still apply to every call.
- **Only the admin API's existing features.** Uploading symbol files isn't a tool yet; the setup guide shows the command.

## How to repeat it

```sh
cd tools/mcp && go test -race ./...          # the stand-in server
python3 server/test/e2e_compose.py ...        # the real one, in the project's full checks
```

Setting it up with an agent: tools/mcp/README.md. Raw results:
[data/2026-11-15-agent-integration/](data/2026-11-15-agent-integration/).
