# MC DATA + Script Terminal Phases

Current phase: Phase 10 - Audit + Polish

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

- [x] Add `ScriptTerminalDialog`.
- [x] Add `ScriptTerminalManager`.
- [x] Add `AnsiRenderer`.
- [x] Add `TerminalProfile`.
- [x] Support multiple script-owned terminal windows.
- [x] Scope terminal IDs by script name.
- [x] Implement pop-out dialog presentation.
- [x] Implement top status line.
- [x] Implement terminal display area.
- [x] Implement bottom prompt/input line.
- [x] Implement paste guard over `2 KB` or `20 lines`.
- [x] Support ANSI SGR colors/styles.
- [x] Support Unicode box/block characters.

Exit criteria:

- A host-owned terminal can open, render text, accept input, and close.
- Multiple terminals do not collide.

## Phase 5 - Terminal Profiles

- [x] Add `ibm-vga` profile: fixed `80x25`, ANSI/DOS BBS style.
- [x] Add `c64` profile: fixed `40x25`, C64 style.
- [x] Add `free` profile: flexible rows/columns.
- [x] Add fit scaling for `ibm-vga`.
- [x] Add integer scaling for `c64`.
- [x] Add resize behavior for `free`.
- [ ] Add redistributable font assets only after license check.
- [x] Add tests for profile selection and sizing.

Exit criteria:

- Terminal profile selection works and reports correct size.
- Fonts/licenses are documented if new fonts are bundled.

## Phase 6 - Lua Terminal API

- [x] Add `api.terminal_open(id, title, profile_or_cols, rows)`.
- [x] Add `api.terminal_close(id)`.
- [x] Add `api.terminal_clear(id)`.
- [x] Add `api.terminal_write(id, text)`.
- [x] Add `api.terminal_status(id, text)`.
- [x] Add `api.terminal_prompt(id, text)`.
- [x] Add `api.terminal_size(id)`.
- [x] Add `api.terminal_profile(id, profile)`.
- [x] Add `api.terminal_fit(id, mode)`.
- [x] Add `api.terminal_hotspot(action_id, label)`.
- [x] Add `on_terminal_input(api, id, text)`.
- [x] Add `on_terminal_link(api, id, action_id)`.
- [x] Add `on_terminal_closed(api, id)`.
- [x] Add Lua terminal API tests.

Exit criteria:

- Lua can control terminals without direct Qt widget access.
- Input, close, and hotspot hooks dispatch back to the owning script.

## Phase 7 - Retro-BBS Demo Script

- [x] Add `bbs.lua`.
- [x] Add defaults: `bbs_id=retro-bbs`, `bbs_name=Retro-BBS`.
- [x] Add `/bbsserve`.
- [x] Add `/bbs nick`.
- [x] Add `/bbs nick retro-bbs`.
- [x] Add `/bbsbook`.
- [x] Add `/bbsbook dial Retro-BBS`.
- [x] Store server config in script data.
- [x] Store address book in script data.
- [x] Implement welcome/about screen.
- [x] Implement main menu.
- [x] Implement message board.
- [x] Implement who online.
- [x] Implement page sysop.
- [x] Implement break-in/sysop chat.
- [x] Implement hangman door.
- [x] Implement logoff.

Exit criteria:

- Two clients can connect over an existing IRC network and use the demo BBS.
- Local server console shows useful session state.

## Phase 8 - BBS Server Console

- [x] Add stats mode.
- [x] Show sessions, connections, messages, pages, and board identity.
- [x] Add mirror mode.
- [x] Mirror the last screen sent to a selected user.
- [x] Return to stats when mirrored user logs off.
- [x] Add server terminal commands for stats/mirror/chat/logoff where practical.

Exit criteria:

- Sysop can monitor Retro-BBS from a terminal window.
- Mirror mode does not send network traffic by itself.

## Phase 9 - Future-Service Hooks

- [x] Document possible `comic` service verbs.
- [x] Keep MC DATA generic and service-neutral.
- [x] Confirm no Retro-BBS assumptions leak into the core terminal or MC DATA APIs.
- [x] Leave `MC SEC` encryption/chunking as documented TBD only.

Exit criteria:

- Future Comic Chat network layer can use MC DATA without refactoring BBS-specific code.

## Phase 10 - Audit + Polish

- [x] Run unit tests.
- [ ] Run manual two-client MC DATA/BBS test.
- [x] Audit flood behavior.
- [x] Audit Lua API safety.
- [x] Audit CTCP/DCC regressions.
- [x] Audit terminal paste behavior.
- [x] Update docs with final command/API details.
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
- Phase 2 targeted MC DATA tests passed.
- Phase 5 profile IDs/sizing are implemented with existing fonts. The current
  terminal font candidate is Spleen OTF because it is blocky, terminal-focused,
  CP437-capable in larger sizes, and BSD-2-Clause licensed. It is documented in
  `DEVDOCS/MC_DATA.md`; asset bundling remains open until license files and
  Qt rendering are verified.
- Compact terminal frame draft is documented in `DEVDOCS/MC_DATA.md`. It uses
  single-letter ops, fixed-width hex fields, and byte-length writes to reduce
  CTCP payload size. Treat it as a working protocol until implementation proves
  the final op set.
- Phase 10 full offscreen CTest is `51/51` passing.
- Initial full CTest without `QT_QPA_PLATFORM=offscreen` also aborts GUI/media
  tests in this headless environment due missing display/plugin setup; offscreen
  removes those environment-only aborts.
