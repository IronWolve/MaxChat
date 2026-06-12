# MC DATA + Script Terminal Phases

Current phase: Phase 4 - Terminal Core UI

Status key:

- [ ] Not started
- [~] In progress
- [x] Complete

## Phase 0 - Planning

- [x] Define objective and goals.
- [x] Define MC DATA CTCP transport concept.
- [x] Define reusable Lua terminal concept.
- [x] Define Retro-BBS as a demo script, not a built-in feature.
- [x] Define current-IRC-network-only rule.
- [x] Define flood-safety constraints.
- [x] Define terminal profiles: `ibm-vga`, `c64`, `free`.
- [x] Define future Comic Chat service and security/versioning notes.
- [x] Write protocol/design plan to `DEVDOCS/MC_DATA.md`.

## Phase 1 - Branch + Docs

- [x] Create or switch to branch `mc-data-terminal`.
- [x] Review `DEVDOCS/MC_DATA.md` for final wording before implementation.
- [x] Commit `DEVDOCS/MC_DATA.md`.
- [x] Commit `DEVDOCS/MC_DATA_PHASES.md`.

Exit criteria:

- Branch exists.
- Both docs are tracked in git.
- No implementation files changed yet.

## Phase 2 - MC DATA Core

- [x] Add `McDataMessage`.
- [x] Add `McDataCodec`.
- [x] Parse `MC DATA <service> <verb> <payload>`.
- [x] Normalize service lowercase and verb uppercase.
- [x] Route CTCP `MC DATA` separately from generic CTCP summaries.
- [x] Preserve existing `DCC`, `ACTION`, `SOUND`, `PING`, `VERSION`, `TIME`, and `CLIENTINFO`.
- [x] Add tests for request/reply parsing.
- [x] Add regression tests proving DCC and existing CTCP behavior still works.

Exit criteria:

- Core IRC layer can receive and emit MC DATA events.
- Existing CTCP tests pass.

## Phase 3 - Lua MC DATA API

- [x] Add `api.mc_send(target, service, verb, payload)`.
- [x] Add `api.mc_reply(target, service, verb, payload)`.
- [x] Add `on_mc_data(api, network, target, nick, service, verb, payload, notice)`.
- [x] Use host-side IRC send queue for MC DATA sends.
- [x] Add payload length guard around `300-350 bytes`.
- [x] Use existing CTCP/IRC send throttling.
- [x] Prevent auto-replies to MC DATA received by `NOTICE`.
- [x] Add Lua API tests.

Exit criteria:

- Lua scripts can send and receive MC DATA safely.
- Oversized payloads fail cleanly.

## Phase 4 - Terminal Core UI

- [~] Add `ScriptTerminalDialog`.
- [ ] Add `ScriptTerminalManager`.
- [ ] Add `AnsiRenderer`.
- [ ] Add `TerminalProfile`.
- [ ] Support multiple script-owned terminal windows.
- [ ] Scope terminal IDs by script name.
- [ ] Implement pop-out dialog presentation.
- [ ] Implement top status line.
- [ ] Implement terminal display area.
- [ ] Implement bottom prompt/input line.
- [ ] Implement paste guard over `2 KB` or `20 lines`.
- [ ] Support ANSI SGR colors/styles.
- [ ] Support Unicode box/block characters.

Exit criteria:

- A host-owned terminal can open, render text, accept input, and close.
- Multiple terminals do not collide.

## Phase 5 - Terminal Profiles

- [ ] Add `ibm-vga` profile: fixed `80x25`, ANSI/DOS BBS style.
- [ ] Add `c64` profile: fixed `40x25`, C64 style.
- [ ] Add `free` profile: flexible rows/columns.
- [ ] Add fit scaling for `ibm-vga`.
- [ ] Add integer scaling for `c64`.
- [ ] Add resize behavior for `free`.
- [ ] Add redistributable font assets only after license check.
- [ ] Add tests for profile selection and sizing.

Exit criteria:

- Terminal profile selection works and reports correct size.
- Fonts/licenses are documented if new fonts are bundled.

## Phase 6 - Lua Terminal API

- [ ] Add `api.terminal_open(id, title, profile_or_cols, rows)`.
- [ ] Add `api.terminal_close(id)`.
- [ ] Add `api.terminal_clear(id)`.
- [ ] Add `api.terminal_write(id, text)`.
- [ ] Add `api.terminal_status(id, text)`.
- [ ] Add `api.terminal_prompt(id, text)`.
- [ ] Add `api.terminal_size(id)`.
- [ ] Add `api.terminal_profile(id, profile)`.
- [ ] Add `api.terminal_fit(id, mode)`.
- [ ] Add `api.terminal_hotspot(action_id, label)`.
- [ ] Add `on_terminal_input(api, id, text)`.
- [ ] Add `on_terminal_link(api, id, action_id)`.
- [ ] Add `on_terminal_closed(api, id)`.
- [ ] Add Lua terminal API tests.

Exit criteria:

- Lua can control terminals without direct Qt widget access.
- Input, close, and hotspot hooks dispatch back to the owning script.

## Phase 7 - Retro-BBS Demo Script

- [ ] Add `bbs.lua`.
- [ ] Add defaults: `bbs_id=retro-bbs`, `bbs_name=Retro-BBS`.
- [ ] Add `/bbsserve`.
- [ ] Add `/bbs nick`.
- [ ] Add `/bbs nick retro-bbs`.
- [ ] Add `/bbsbook`.
- [ ] Add `/bbsbook dial Retro-BBS`.
- [ ] Store server config in script data.
- [ ] Store address book in script data.
- [ ] Implement welcome/about screen.
- [ ] Implement main menu.
- [ ] Implement message board.
- [ ] Implement who online.
- [ ] Implement page sysop.
- [ ] Implement break-in/sysop chat.
- [ ] Implement hangman door.
- [ ] Implement logoff.

Exit criteria:

- Two clients can connect over an existing IRC network and use the demo BBS.
- Local server console shows useful session state.

## Phase 8 - BBS Server Console

- [ ] Add stats mode.
- [ ] Show sessions, connections, messages, pages, and board identity.
- [ ] Add mirror mode.
- [ ] Mirror the last screen sent to a selected user.
- [ ] Return to stats when mirrored user logs off.
- [ ] Add server terminal commands for stats/mirror/chat/logoff where practical.

Exit criteria:

- Sysop can monitor Retro-BBS from a terminal window.
- Mirror mode does not send network traffic by itself.

## Phase 9 - Future-Service Hooks

- [ ] Document possible `comic` service verbs.
- [ ] Keep MC DATA generic and service-neutral.
- [ ] Confirm no Retro-BBS assumptions leak into the core terminal or MC DATA APIs.
- [ ] Leave `MC SEC` encryption/chunking as documented TBD only.

Exit criteria:

- Future Comic Chat network layer can use MC DATA without refactoring BBS-specific code.

## Phase 10 - Audit + Polish

- [ ] Run unit tests.
- [ ] Run manual two-client MC DATA/BBS test.
- [ ] Audit flood behavior.
- [ ] Audit Lua API safety.
- [ ] Audit CTCP/DCC regressions.
- [ ] Audit terminal paste behavior.
- [ ] Update docs with final command/API details.
- [ ] Build Linux test binary only when the user says implementation is ready.

Exit criteria:

- Tests pass.
- Manual BBS demo works.
- No known CTCP/DCC regressions.
- User has a testable build when requested.

## Notes

- Do not add new IRC connections for BBS. Use existing connected networks only.
- Do not auto-open unsolicited terminals.
- Do not send secrets over MC DATA v1.
- Do not bundle fonts without checking redistribution license.
- Keep each phase commit-sized so pieces can be reverted cleanly.
- Phase 2 targeted tests passed. Full `irc_session` currently has one unrelated
  `commandStatusAndErrorsEmitReadableText` expectation mismatch to handle outside
  the MC DATA core commit.
