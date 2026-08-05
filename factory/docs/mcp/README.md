# MCP integration docs

How this appliance (T-Watch S3 firmware) talks to external MCP servers.

| File | Topic |
|------|-------|
| [nextcloud-appliance-sync.md](nextcloud-appliance-sync.md) | Nextcloud calendar + tasks 7-day sync via the ServitorAssistant MCP server. Separates what exists today from the recommended structured-JSON contract. |

## Principle

The appliance calls the MCP server **directly** (JSON-RPC `tools/call` over
Streamable HTTP) and consumes **structured JSON**. It never sends a
natural-language prompt to the LLM agent and never parses the agent's
human-readable text. An embedded sync process needs deterministic JSON, stable
identifiers, explicit timezone data, and complete-snapshot semantics — none of
which a formatted-for-LLM string provides.
