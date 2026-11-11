# Primeagen — Personal R5 Preferences

## Constitution Link

gist.md (`../../metadata/gists/preferences/gist.md`) is the compressed law list; all universal laws bind this repository. Read one on demand: `./tools/prefs law "<title>"`. Universal laws are inherited, never restated.

Tier: app (R5). Owns: personal agent CLI/TUI frontend, ncurses view, consent policy. Borrows: Vexspoke JSON string writer (consumed, not duplicated), Harness Message and API Haven request envelopes via C23 binding.

## Exclusive Laws

| Law Title | Scope | Enforcement |
| **Personal Agent Boundary Law** | Standalone R5 app and dependencies | Mandatory for `primeagen` |
| **Explicit Tool Consent Law** | Read/build/run and model output | Mandatory for `primeagen` |

### Personal Agent Boundary Law

* Primeagen lives outside the ecosystem tree, checkout anywhere — personal R5 consumer, not a replacement R4 Harness or R3 connector.
* No reverse includes, implicit fetches or duplicate ecosystem owners.
* Python: cold CLI/policy/host adapter; a C23 binding consumes real Harness Message and API Haven request envelopes.
* Dependencies come via source-root options; Func is an independent bounded tool invocation, never included.
* Standalone host supplies HTTPS transport (native send API lacks TLS) — a scoped consumer-adapter exception moving no transport ownership or reverse dependencies.
* No GUI/window/memory subsystem.
* Model output is untrusted data, never authorization; every tool needs separate host/user approval.

### Explicit Tool Consent Law

* The terminal's explicit `/update` consumes Hotcwap `Hot_updateCold` with trusted local build SHA256 references.
* Only view/command code is reloadable; terminal, Tui/session pipes, approvals, remote permission configuration stay resident.
* New prompts pause until the existing turn and candidate admission resolve.
* Failure retains running code/state; unresolved disk rollback stops the host.
* Native candidates/constructors and stable parent directories are trusted, never model-authorized code.
* No network download, broad native agent update, server/role implementation, crash recovery or sandbox.
* The exact JSON tool tags are read, list, write, edit, create, delete, bash and websearch, plus the existing Func build/run tags.
* File paths are canonical and workspace-relative; parents must exist; symlink/hardlink/nonregular aliases reject.
* Create admits an absent file (0600); write replaces an existing file via staged rename; edit replaces exactly one nonempty match; delete unlinks one regular file without recursion/undo.
* Bash is a separately approved `/bin/bash --noprofile --norc -c` action, inheriting host environment/privileges; file confinement does not sandbox Bash.
* Search requires a host-selected trusted executable receiving one literal query argument, authorized APIs; no scraper, implicit install or guessed provider.
* Build is not run permission; func run still requires a session-created artifact.

## Repo-Local Extensions

* None — no managed exceptions to universal laws.

## Readiness Cross-Reference

* Feature readiness: primeagen readiness gist.
* Open blockers: ecosystem blockers gist.
* Proof lawbook: `tests/test-preferences.md` — `./tools/prefs test "<title>"`.
* Evidence ledger: `metadata/gists/test-checklist.md` — `python3 tools/test_checklist.py check`.
