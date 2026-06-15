# MaxChat-C — Line-by-Line Code Audit (Claude)

**Date:** 2026-06-15
**Scope:** All program files under `src/` (~170 files, ~35,000 lines).
**Method:** Sequential, by subsystem. Each file read in full; findings recorded with
file:line references. Severity tags: **[BUG]** correctness defect, **[SEC]** security/safety,
**[PERF]** performance, **[DESIGN]** design/architecture smell, **[STYLE]** style/maintainability,
**[NIT]** minor.

---

## Subsystem: `core/`

### `core/FloodGuard.cpp`

- **[BUG] FloodGuard.cpp:36** — `recordMessage` appends `nowMs` *before* the size
  comparison and returns `times.size() > maxMessages_`. The just-added message is counted,
  so the guard trips one message earlier than `maxMessages_` nominally implies. Whether this
  is intended depends on semantics, but it's worth a comment: with `maxMessages_=2` the 3rd
  message in-window trips it... actually the *2nd+1*. Verify against the throttle intent.
- **[DESIGN] FloodGuard.cpp:30** — `messageTimes_[normalized]` grows a map entry per distinct
  key and entries are never removed once empty. Over a long session with many transient keys
  (e.g. per-target throttling), this is an unbounded memory leak of empty `QList`s. Consider
  erasing the entry when `times` becomes empty after pruning.
- **[NIT] FloodGuard.cpp:9-10** — `std::max(2, maxMessages)` and the window clamp silently
  override caller input with no logging; a caller passing `maxMessages=1` gets 2 with no
  feedback. Acceptable, but undocumented in the header.

### `core/UrlDetector.cpp`

- **[BUG] UrlDetector.cpp:35** — The regex `[^\s<>"']+` does not exclude backtick, `|`, or
  other shell/markup delimiters, and includes any trailing unicode. Combined with `www\.`
  matching, a bare `www.` token with no host is still matched by the `+` but trimmed later —
  fine — however `www.` alone followed by punctuation can yield a 4-char "url". Low impact.
- **[DESIGN] UrlDetector.cpp:16-27** — Balanced-bracket trimming only handles a single
  trailing run and only `)`, `]`, `}`. Mixed trailing punctuation like `).` is handled by the
  earlier loop + this loop, but interleaved cases (`].)`) trim in a fixed order that may
  over- or under-trim. Edge-case only.
- **[NIT] UrlDetector.cpp:35** — `ftp://` is detected but most downstream preview/fetch code
  is HTTP-only; detecting URLs the app can't act on may surface dead links.

### `core/CommandAlias.cpp`

- **[BUG] CommandAlias.cpp:82** — `token.left(...).toInt()` for a `$N` placeholder: if the
  numeric run overflows `int` (e.g. `$99999999999`) `toInt` returns 0 and `index` becomes -1,
  which is then skipped — harmless. But `$0` yields `index = -1` and is silently dropped; the
  Python original's 1-based convention is preserved but `$0` should arguably be documented as
  invalid rather than silently producing nothing.
- **[DESIGN] CommandAlias.cpp:94-97** — When no placeholder was used, raw `arguments` are
  appended. But if the template *did* use `$*`/`$1-` to consume args, and also has trailing
  literal text, there's no double-append guard beyond `usedPlaceholder`. Correct, but the
  `$*` branch (line 76) appends the full `arguments` string while `argumentTokens` re-splits
  on whitespace elsewhere — inconsistent whitespace normalization between `$*` (verbatim) and
  `$N` (collapsed). Document the difference.
- **[BUG] CommandAlias.cpp:142** — Loop-break condition `next == result.commandLine` catches
  immediate fixpoints, and `seen` catches command-name cycles, but a two-alias ping-pong that
  changes the *arguments* each pass while alternating commands not yet in `seen` is bounded
  only by `maxDepth`. That's the intended backstop, fine — but `seen` keys on command name
  only, so `a -> b -> a` is caught at the second `a`. OK. No action; documented for clarity.
- **[NIT] CommandAlias.cpp:57** — `placeholder` regex is rebuilt on every `applyTemplate`
  call (not `static`), unlike other files here that cache regexes. Minor per-call cost.

### `core/ConnectionPlan.cpp`

- **[DESIGN] ConnectionPlan.cpp:13-18** — `endpointKey` folds host case and includes
  `port`/`tls`, but `tls` is a bool rendered via `.arg(bool)` → "true"/"false"; fine. Dedup
  key does not normalize trailing dots in hostnames, so `irc.x.com` and `irc.x.com.` are
  treated as distinct servers.
- **[BUG] ConnectionPlan.cpp:117-118** — The primary server is appended from
  `network["host"]` without trimming before `appendUniqueServer` (which does trim internally —
  OK), but an empty host silently produces no primary server and the plan may fall back to
  `servers.first()` being a failover. If `host` is empty and `servers` is empty, the plan has
  zero servers and `networkName` stays empty — caller must handle via `hasConnectableServer`.
- **[NIT] ConnectionPlan.cpp:94** — Hardcoded default nick `"comicfan"` duplicated from
  elsewhere (DefaultNetworks?). Centralize the fallback nick constant.
- **[NIT] ConnectionPlan.cpp:109** — Default proxy port 1080 assumes SOCKS; if `proxy_type`
  is an HTTP proxy the 1080 default is wrong (should be 8080/3128). Default is type-blind.

### `core/ChatLineFormatter.cpp`

- **[BUG] ChatLineFormatter.cpp:113-122** — `linkifyUrls` uses `indexOf` on the escaped URL
  string against already-generated HTML and guards only against a preceding `"`. If a URL's
  escaped text appears as a *substring of another, longer URL* the longest-first sort (line
  106) mitigates most cases, but a URL that is a substring of a previously inserted `<a href>`
  attribute value (e.g. shortened vs full URL sharing a prefix) can still be re-linkified,
  producing nested `<a>` tags. The `"` guard only catches the exact `href="URL` boundary, not
  URLs embedded mid-attribute. Fragile; a tokenizing linkifier would be safer.
- **[BUG] ChatLineFormatter.cpp:174** — `cleanLabel.indexOf(nick)` to locate the nick within
  the label can match the wrong occurrence if the nick string also appears in the bracket
  decoration (e.g. nick `a` in label `<a>` — finds position 1, fine; but nick `-` style or a
  nick equal to a bracket char). Low risk given the nick-validity checks upstream, but the
  `indexOf` is not anchored.
- **[PERF] ChatLineFormatter.cpp:271-272** — `formatChatLine` calls `stripFormatting(line)`
  and `parseLineShape` twice (once on stripped, once on rich) plus `stripFormatting` again
  inside `messageHtml`/`labelHtml`. For every chat line that's 4+ passes over the string.
  Hot path during bulk replay; consider computing the stripped form once and threading it.
- **[DESIGN] ChatLineFormatter.cpp:26-34** — `separatorPlain`/`separatorHtml` both ignore
  `options` (`Q_UNUSED`) and return a single space — dead abstraction; inline or remove.
- **[NIT] ChatLineFormatter.cpp:256** — Hardcoded fallback timestamp color `#8a8a8a`
  duplicated as a magic constant; should be a named default alongside other theme defaults.

### `core/NetworkImport.cpp`

- **[DESIGN] NetworkImport.cpp:44** — Merge logic: an imported network matching a base entry
  copies the *base* network and overlays only non-catalog fields from the import. This means
  imported overrides to catalog fields (`host`, `port`, `tls`, `servers`) are **silently
  discarded** in favor of the base catalog values. If a user imports a config that intentionally
  changes a known network's host/port, their change is dropped. This may be intended (catalog
  is authoritative) but is surprising and undocumented — should be called out to the user.
- **[BUG] NetworkImport.cpp:44** — `!usedBaseKeys.contains(key)` means only the *first*
  imported network matching a given base key is merged; subsequent imports with the same key
  fall to the `else` branch and are appended as standalone duplicates. Duplicate-named imports
  silently produce divergent entries rather than being merged or rejected.
- **[NIT] NetworkImport.cpp:11-17** — `catalogNetworkFields` omits `website` from being
  overlaid (it's in the protected set), so imported website changes to catalog networks are
  also dropped — consistent with above but worth confirming intent.

### `core/SettingsStore.cpp`

- **[SEC] SettingsStore.cpp:179-181 / 298-307** — Secrets are persisted to `settings.json`
  in **plaintext**: SASL `password`, `server_pass`, `proxy_password`, `imgbb_api_key`,
  `imgur_client_id`, `postimages_token`, `imgbox_password`, etc. The JSON is written with
  default file permissions (no explicit `chmod 600`). On a shared machine these are
  world/group-readable depending on umask. Consider OS keychain (libsecret/DPAPI) or at least
  restrictive permissions on the settings file.
- **[BUG] SettingsStore.cpp:180** — `file.write(document.toJson(...))` return value is
  ignored; a short write (disk full) is not detected and `file.commit()` may still report
  success for a truncated file. Check the `write()` byte count before `commit()`.
- **[BUG] SettingsStore.cpp:116-117** — The stat-validated cache keys on
  `lastModified()` + `size()`. Filesystem mtime granularity is often 1s (and on some FS
  coarser); two writes within the same second that keep the file the same size (e.g. toggling
  a bool field of equal length) will **not** invalidate the cache, returning stale data. Low
  probability but a real correctness gap for the "Python app writes the same file" scenario
  the comment cites. A content hash or explicit invalidation is more robust.
- **[DESIGN] SettingsStore.cpp:161-171** — `saveRaw` reloads the whole file, erases all
  non-geometry keys, then re-inserts `settings`. This makes `saveRaw` a *full replace* (plus
  geometry), not a merge — any key present on disk but absent from `settings` is dropped. The
  `setValue` path (line 184-188) works because it loads-then-inserts first, but a caller
  passing a partial map to `saveRaw` directly would silently wipe unrelated settings. The
  geometry special-case hints this footgun has already bitten once.
- **[NIT] SettingsStore.cpp:173** — `cacheValid_ = false` is set before the write; if
  `open()` fails (line 175) the cache is needlessly invalidated even though the file is
  unchanged. Minor.
- **[NIT] SettingsStore.cpp:439** — `update_check` default `false` with a `TODO(release)`
  to flip it — track so it isn't shipped off by accident.
- **[STYLE] SettingsStore.cpp** — `defaultSettings()` is a 170-line wall of `insert` calls;
  consider a static table (key/default pairs) for readability and to make export/round-trip
  testing data-driven.

### `core/ChatBufferStore.cpp`

- **[PERF] ChatBufferStore.cpp:266-269** — `addMember` appends then re-sorts the entire
  member list (`O(n log n)`) on every single join. During a large channel's NAMES burst this
  is called per-nick → `O(n^2 log n)`. `setMembers` (bulk) exists and should be preferred for
  NAMES; verify callers use it and consider an insertion-point binary search for incremental
  adds.
- **[PERF] ChatBufferStore.cpp:218-220 / 60-64** — Trimming excess lines via repeated
  `removeFirst()` on a `QList` is `O(n)` per removal (shifts the whole buffer). When
  `setMaxLinesPerBuffer` shrinks a full 2000-line buffer it's `O(n*k)`. A `removeFirst(k)`
  range erase or a deque/ring buffer would be `O(k)`.
- **[BUG] ChatBufferStore.cpp:291-298** — `renameMember` does `removeMember` then `addMember`.
  If the new nick already exists in the channel (nick collision after a forced rename), the
  old entry is removed and `addMember` no-ops on the duplicate — net member loss is avoided,
  but any **status prefix** (`@`, `+`) carried on the old entry is dropped because `addMember`
  re-adds the bare new nick. Op/voice state is lost across renames.
- **[DESIGN] ChatBufferStore.cpp:34-44** — `sortedUniqueMembers` strips status prefixes for
  the dedup key but **keeps** the prefixed form in the list, then sorts case-insensitively on
  the prefixed string — so `@bob` and `+bob` would both be kept if status differs (dedup key
  ignores prefix → second is dropped). The dedup-by-bare-nick is correct, but mixing
  prefixed display strings with prefix-insensitive dedup is subtle; document it.
- **[NIT] ChatBufferStore.cpp:354-355** — Network is normalized with `toCaseFolded()` for the
  key; if two networks differ only by case they collide into one buffer set. Almost certainly
  intended, but worth noting for networks named e.g. "Libera" vs "libera".

### `core/ChatLogStore.cpp`

- **[SEC] ChatLogStore.cpp:20-30 / 67-83** — Path-traversal defense looks solid: each path
  component is sanitized via `safePathPart` (strips `\ / : * ? " < > |` and control chars,
  drops leading dots) and `.`/`..` components are skipped. Good. One gap: `safePathPart`
  truncates to 80 chars *after* replacement but does not collapse a name that becomes empty
  except for spaces → it trims, OK. No traversal found; noting the defense for the record.
- **[PERF] ChatLogStore.cpp:118-126** — `recentLines` reads the **entire** log file line by
  line keeping only the last `maxLines`, discarding the rest. For a multi-megabyte daily log
  this reads the whole file on every buffer open/replay. A reverse/seek-from-end read would
  bound the cost to `maxLines`.
- **[BUG] ChatLogStore.cpp:101-104** — `appendLine` opens with `QIODevice::Append` but uses a
  plain `QFile` (not `QSaveFile`) and writes via `QTextStream`; there is no `flush()` before
  the function returns. The `QTextStream` and `QFile` destructors flush, but `stream.status()`
  is checked *before* the destructor flush actually hits disk, so a write error occurring
  during the final buffered flush is not reported. Call `stream.flush()` then check status.
- **[BUG] ChatLogStore.cpp:88** — `line.trimmed()` is logged, so leading/trailing whitespace
  that is semantically meaningful (e.g. ASCII-art, code pastes) is stripped in the persisted
  log. Replay (`recentLines`) therefore can't faithfully reproduce the original spacing.
- **[DESIGN] ChatLogStore.cpp:56-65** — Date-token replacement runs a chain of
  `QString::replace` calls; a literal `%Y` typed by a user into a channel name survives into
  the mask only via `%channel` substitution which happens *first* (line 52), so a channel
  literally named `%Y` would be expanded as a date. Minor injection-into-path-mask oddity.

### `core/DefaultNetworks.cpp`

- **[BUG] DefaultNetworks.cpp:1526** — `parseServerSpec` uses `host.lastIndexOf(':')` to find
  the port separator. This **breaks IPv6 literals**: `2001:db8::1` would be split at the last
  colon, treating `1` as the port and `2001:db8:` as the host. The `amigaworld` failover is
  given as a bare IPv4 (`91.213.203.192`) so the table itself is fine, but any user/imported
  IPv6 server spec is mis-parsed. Bracketed-host parsing (`[::1]:6697`) is needed.
- **[BUG] DefaultNetworks.cpp:1530-1535** — When a port is present without a leading `+`,
  `tls` is forced to `false`, *ignoring* `defaultTls`. So a spec like `irc.x.net:6697` on a
  TLS-default network silently downgrades to plaintext. The `+` prefix is the only way to keep
  TLS, which is non-obvious and a **security-relevant default-downgrade**. Consider: absence of
  `+`/`-` should inherit `defaultTls`, not force plaintext.
- **[DESIGN] DefaultNetworks.cpp:21-1510** — ~1500-line hand-maintained network table inline
  in source. This is data, not code; it belongs in a bundled resource/JSON so it can be
  updated without recompiling and diffed cleanly. Also makes the translation unit huge.
- **[NIT] DefaultNetworks.cpp** — Every entry hardcodes nick `"comicfan"` (150+ repetitions);
  factor the default nick out. Several `website` URLs look auto-derived and wrong (e.g.
  `AyoChat` → `https://or.id/`, `BrasIRC` → `https://com.br/`) — these are TLD-only stubs from
  a bad split and should be fixed or blanked.

---

## Subsystem: `irc/`

### `irc/IrcMessage.cpp`

- **[BUG] IrcMessage.cpp:73** — Tag string is split on `;` *before* unescaping, which is
  correct (escaped `;` is `\:`), but the split does not cap the number of tags. IRCv3 mandates
  a 8191-byte tag budget; a malicious server sending thousands of tiny tags causes an
  unbounded `QHash` insert loop. Add a sane tag-count/length cap to harden against hostile
  servers.
- **[BUG] IrcMessage.cpp:9-10** — `nick()` extracts up to `!` but does not strip a leading
  server-name-only prefix (no `!`/`@`) from a *server* source, so for server-originated
  messages `nick()` returns the whole server hostname. Callers must distinguish; document.
- **[DESIGN] IrcMessage.cpp:103/106** — `command.toUpper()` upper-cases numerics harmlessly
  but also any malformed command; there is no validation that the command is alphanumeric.
  Downstream dispatch should treat unknown commands defensively (it likely does).
- **[NIT] IrcMessage.cpp:17-45** — `unescapeTagValue` handles a trailing lone backslash by
  copying it verbatim (the `i + 1 < size` guard) — matches the spec ("drop"? actually spec
  says drop a trailing `\`). Verify against IRCv3: a dangling `\` should be dropped, here it's
  kept.

### `irc/IrcFormat.cpp`

- **[BUG] IrcFormat.cpp:242-243 / 263** — The `0x11` (monospace, IRCv3 draft) control is
  *stripped* in `stripFormatting` but in `toHtml` it is silently skipped without a `flush()`.
  Skipping mid-run without flushing is harmless for monospace (no style change), but it is
  inconsistent with every other control char and would matter if monospace rendering is ever
  added. Note the asymmetry.
- **[BUG] IrcFormat.cpp:148 / 257** — Color parsing: after reading the foreground digits, a
  background is only parsed if `text.at(i) == ','` **and** the next char `isDigit()`. A color
  code like `\x03,5` (no foreground, comma, background) is not handled — the comma is treated
  as literal text. mIRC treats a bare `\x03` as a reset; the `,5` then renders literally.
  Matches some clients, diverges from others; document the chosen behavior.
- **[DESIGN] IrcFormat.cpp:84-94** — `\x04` hex color requires *exactly* 6 hex chars; 3-digit
  CSS-style hex (`\x04F00`) is unsupported and falls through to "clear color". Fine if the
  wire format is always 6, but undocumented.
- **[PERF] IrcFormat.cpp:9-63** — `mircColors()` builds a 99-entry `QHash` on first call
  (function-local static, thread-safe init) — fine. But `colorForNumber` does a hash lookup
  per color code per line; a flat `static const char*[99]` array indexed directly would be
  faster and allocation-free.
- **[NIT] IrcFormat.cpp:131** — `& 0xFFFFFFFFu` on a `quint32` is a no-op (the type is already
  32-bit); harmless but misleading — remove or switch to a wider accumulator if overflow
  semantics were intended.

### `irc/CommandParser.cpp`

- **[SEC] CommandParser.cpp:665-721** — `/raw`, `/quote`, and the **catch-all fallback**
  (lines 717-721) forward arbitrary text to the server. If any of `rest`/`commandLine` can
  contain an embedded `\r` or `\n` (e.g. a multi-line paste that slips past the paste guard,
  or a script-built command), this is a **CRLF command-injection** vector — the user could
  unknowingly send extra IRC commands. The parser itself does no CRLF stripping; verify the
  connection/send layer (`IrcConnection`/`IrcSession`) strips control chars before writing.
  This applies equally to `/msg`, `/topic`, `/kick` reasons, etc.
- **[DESIGN] CommandParser.cpp:717-721** — Any unrecognized `/word ...` is silently sent as a
  raw IRC command (uppercased verb via server). A typo like `/mesage hi` is transmitted to the
  server as `MESAGE hi` rather than rejected. This is flexible but surprising and can leak
  intended-private text; consider a "did you mean / unknown command" guard, at least for
  verbs containing spaces or non-alpha chars.
- **[BUG] CommandParser.cpp:138 / 581** — `"umite"` is registered as a command name and
  handled as an alias of `/unmute`. This looks like a typo-tolerance alias for the *misspelling*
  of "unmute", but it's an odd thing to bake into the public command list and is undocumented.
  Confirm intent; if a forgiving alias is wanted, `"unmite"`/`"unmtue"` etc. are equally likely
  typos and the single entry is arbitrary.
- **[BUG] CommandParser.cpp:665 / 103-151** — `/quote` is accepted (line 665) but is **absent
  from `supportedCommandNames()`**, as are common aliases `m`, `q`, `j`, `action`. Since that
  list drives command highlighting/help, these working commands won't be recognized by the UI.
  Keep the two lists in sync (ideally derive one from the other).
- **[DESIGN] CommandParser.cpp:443-459** — `op`/`voice`/etc. emit a single `MODE` with one
  sign char repeated per nick (`+ooo nick1 nick2 nick3`). Servers cap modes-per-line
  (typically `MODES=4` from ISUPPORT). With >4 nicks this produces an over-long mode string
  the server will truncate/reject. Should chunk by the server's `MODES` limit.
- **[NIT] CommandParser.cpp:303** — `/action` is handled as a synonym of `/me` here but, like
  `quote`, isn't in `supportedCommandNames()`.
- **[NIT] CommandParser.cpp:266-274** — Emoticon decorations are hardcoded UTF-8 art; fine,
  but `tableflip`/`flip` share the same glyph while `unflip` differs — verify `flip` vs
  `tableflip` are meant to be identical (they are, via the `else` branch).

### `irc/IrcConnection.cpp`

- **[BUG] IrcConnection.cpp:299** — Oversized-incomplete-line guard:
  `buffer_.size() > MaxPendingBytes && !buffer_.contains("\r\n")`. `QByteArray::contains`
  rescans the whole (up to 4 MB) buffer on *every* `readyRead`. A server can hold the buffer
  near `MaxBacklogBytes` with one early `\r\n` and a huge trailing partial line; the partial
  line then never trips this guard (because a CRLF exists earlier) and only the 4 MB backlog
  cap catches it. The intent — "abort on a single oversized incomplete line" — isn't actually
  enforced when any complete line precedes the partial. Track the partial-tail length instead.
- **[PERF] IrcConnection.cpp:286-299** — Two full-buffer scans per `readyRead`
  (`contains("\r\n")` here plus the `indexOf` loop in `queueIncomingLines`, plus another
  `contains` at line 427). Under flood this is repeated O(n) scanning of a multi-MB buffer.
  Consider tracking a search offset / last-scanned position.
- **[SEC] IrcConnection.cpp:362-365** — `acceptInvalidCertificate_` → `ignoreSslErrors()`
  blindly accepts *any* TLS error (expired, hostname mismatch, self-signed, revoked). It's a
  user opt-in, but there's no granularity and the only feedback is a systemText line. At
  minimum log *which* errors were ignored so a MITM isn't invisible.
- **[BUG] IrcConnection.cpp:419** — `QString::fromUtf8(chunk)` assumes UTF-8. IRC is famously
  encoding-agnostic; legacy servers/users send Latin-1/CP1252. Invalid UTF-8 sequences become
  U+FFFD replacement chars, irreversibly corrupting non-UTF-8 messages. A fallback decode
  (try UTF-8, fall back to Latin-1) is the common IRC-client approach.
- **[DESIGN] IrcConnection.cpp:149-151** — The writer lambda returns `socket_->write(payload)`
  but the return (bytes written / -1) is passed up to `IrcSession`; confirm the session treats
  a short write or -1 as failure. `QSslSocket::write` queues into an internal buffer so it
  rarely short-writes, but an error mid-TLS-handshake returns -1.
- **[NIT] IrcConnection.cpp:104** — Default nick `"comicfan"` hardcoded a *third* place
  (also DefaultNetworks, ConnectionPlan). Centralize.
- **[NIT] IrcConnection.cpp:204-212** — `localAddress()` deliberately returns only IPv4
  (for DCC). Document that DCC is IPv4-only by design so this isn't read as a bug.

### `irc/IrcSession.cpp`

- **[SEC – mitigated] IrcSession.cpp:148-150** — `enqueueLine` strips `\r` and `\n` from
  every outbound line at the single send choke point. This is the defense that neutralizes the
  CRLF-injection concern raised for `CommandParser`/`/raw`. Good. Keep all sends routed through
  here (verify nothing writes to the socket bypassing `enqueueLine`).
- **[SEC] IrcSession.cpp:413-419** — Post-`001` NickServ fallback sends `PRIVMSG NickServ
  :IDENTIFY <password>` in cleartext. It's gated by `passwordAuthAllowed()` (TLS or explicit
  opt-in), which is correct, but on an opted-in plaintext network the password crosses the
  wire unencrypted. Ensure `redactLine` masks this for the raw-log (it must match the
  `IDENTIFY`/`account password` shape) — verify in `IrcRedaction.cpp`.
- **[BUG] IrcSession.cpp:561-564** — CTCP auto-reply throttle is **global**, not per-sender.
  A single flooder sending CTCPs at >1/s suppresses auto-replies (and the `ctcpSummary`
  status line) to *every* other user for that window — both a reply-loss bug and a way for one
  peer to blind you to others' CTCPs. Throttle per-origin (small LRU of nick→last-reply).
- **[BUG] IrcSession.cpp:566-575** — When the CTCP throttle trips (line 561), the early
  `return` also skips emitting any `replyText`/`messageReceived`, so a throttled CTCP request
  is rendered **completely invisible** to the user. At least surface the inbound request even
  when the *reply* is suppressed.
- **[DESIGN] IrcSession.cpp:1071-1119** — `handleCap` handles only `LS`/`ACK`/`NAK`. It does
  not handle `NEW`/`DEL` (cap-notify) so dynamically-advertised caps mid-session are ignored,
  and only SASL `PLAIN` is supported (no `EXTERNAL`/`SCRAM`). Acceptable for scope; document
  the limitation.
- **[BUG] IrcSession.cpp:880-888** — The `352` (WHO) handler indexes `params.at(5)`/`at(6)`
  and assumes a fixed field layout. WHO reply field order is server-dependent and `WHOX`
  (354) reorders fields; a non-standard 352 with fewer-than-expected meaningful params still
  passes the `>= 7` guard but maps the wrong columns to nick/flags. Low impact (display only).
- **[DESIGN] IrcSession.cpp:1014-1025** — `passwordAuthAllowed` warns only **once** per
  session (`insecureAuthWarned_`). If the user reconnects to several plaintext networks in one
  run, only the first warns. Reset on `configureRegistration` (it is — line 84). OK; noting.
- **[NIT] IrcSession.cpp:18 / 297** — `McDataMaxPayloadBytes = 350` magic number; the comment
  cites a spec throttle but the value's derivation (512 - framing overhead?) isn't shown.
- **[NIT] IrcSession.cpp:1006-1011** — Numeric-fallthrough prints `msg.raw` when there's no
  trailing — `msg.raw` includes the server prefix and may be noisy/wide; consider trimming to
  params for unhandled numerics.

### `irc/McDataCodec.cpp`

- **[DESIGN] McDataCodec.cpp:61-62** — `service` is lowercased and `verb` uppercased on parse,
  but `payload` (`verb.rest`) is passed through byte-exact (correct — frame ops are
  byte-counted). Just confirm the *encode* side (`mcDataCtcpBody`) and the 350-byte cap in
  `IrcSession` agree on whether the cap is bytes-of-payload or bytes-of-whole-CTCP.
- **[NIT] McDataCodec.cpp:28-40** — `validToken` allows `.` in service/verb names; combined
  with the log-path masking elsewhere this is fine, but a service literally named `..` would
  pass `validToken`. No filesystem use here, so harmless — noting for defense-in-depth.

### `irc/IrcRedaction.cpp`

- **[SEC] IrcRedaction.cpp:7-31** — `redactLine` masks `PASS`, base64 `AUTHENTICATE`, and the
  common NickServ verbs, but **does NOT mask `OPER <user> <password>`**. The `/oper` command
  builds `OPER user password` (CommandParser.cpp:687-695) which then appears **in cleartext in
  the raw log**. Add `OPER` to the redaction (mask everything after the username).
- **[SEC] IrcRedaction.cpp:22-25** — The services-password regex covers IDENTIFY/REGISTER/
  GHOST/RECOVER/RELEASE/SIDENTIFY/LOGIN but not `NS IDENTIFY`-style (where the verb is sent to
  a server alias) nor `PRIVMSG NickServ :IDENTIFY` with the account-form already handled — OK,
  but `REGISTER` typically includes an email as the 2nd arg which is also masked (fine). Just
  ensure server-alias commands like raw `NICKSERV IDENTIFY x` (no PRIVMSG) are also masked if
  the app can emit them.
- **[NIT] IrcRedaction.cpp:14** — `line.mid(13)` hardcodes the length of "AUTHENTICATE " (13).
  Correct, but a named constant or `QStringView` after a `startsWith` length would be clearer.

### `irc/ReconnectPlanner.cpp`

- **[BUG] ReconnectPlanner.cpp:11-17** — `currentServer` clamps `serverIndex` into range for
  the read but does **not** write the clamped value back (state is `const&`). If `serverIndex`
  is somehow out of range, every call re-clamps; benign, but the out-of-range state persists
  and a later non-clamping read elsewhere could index out of bounds. Normalize on mutation.
- **[DESIGN] ReconnectPlanner.cpp:37** — Single-server case increments `serverAttempt`
  without bound. Over a very long flapping reconnect loop this is a slowly growing int; no
  practical overflow, but the attempt counter has no semantic use in the 1-server branch, so
  it's dead accumulation. Reset or cap it.
- **[NIT] ReconnectPlanner.cpp** — No backoff/jitter logic here; presumably the delay schedule
  lives in the caller. Cross-check that reconnect timing applies exponential backoff + jitter
  to avoid synchronized reconnect storms after a netsplit.

---

## Subsystem: `scripting/`

### `scripting/LuaEngine.cpp`

Overall the sandbox is **well-constructed**: `os.exit` always stripped; `io`/`package`/exec
verbs gated per-permission; native C loaders (`package.loadlib`, searchers 3 & 4) removed under
"load modules"; bytecode chunks forced text-only; `io.open` replaced with a jailed/guarded
version. The findings below are the gaps.

- **[SEC – serious] LuaEngine.cpp:437-448, 778-790, 916-936** — **Re-entrancy / no execution
  watchdog.** `l_http_get` calls `hostHttpGet` **synchronously**. If the host spins a nested
  event loop while waiting for the HTTP reply (the IrcConnection drain comment at lines 393-397
  explicitly says scripts can do "a synchronous fetch" that re-enters readyRead), a `QTimer`
  `fireTimer` or an inbound-message `dispatch` can call `lua_pcall`/`lua_rawgeti` **on the same
  `lua_State` that is already suspended inside the C `http_get` call**. A single Lua state is
  **not re-entrant** — this is undefined behavior (stack corruption / crash). Guard each
  `lua_State` with a "currently executing" flag and refuse to re-enter (queue or drop), or make
  `http_get` truly async.
- **[SEC] LuaEngine.cpp:533-617** — **No CPU/instruction limit and no memory cap.** A script
  with `while true do end` in `on_load` or in a 50 ms timer **freezes the entire UI thread
  forever** — there is no `lua_sethook` count-based watchdog and `luaL_newstate` uses the
  unbounded default allocator. A buggy or hostile script is a trivial client-wide DoS. Add an
  instruction-count hook that aborts long-running chunks and a custom allocator with a ceiling.
- **[SEC] LuaEngine.cpp:496-528** — **Symlink escape in `fileAccessAllowed`.** The bound check
  uses `QFileInfo::absoluteFilePath()` + `cleanPath` but **not** `canonicalFilePath()`. A
  symlink placed inside the data dir or an allowed dir that points outside passes the
  `startsWith(root)` prefix test yet resolves elsewhere — a script with file access can read/
  write arbitrary paths via a symlink it created. Resolve to the canonical path before the
  prefix check (and re-check after open on platforms where TOCTOU matters).
- **[DESIGN/SEC] LuaEngine.cpp:227-230, 845-858** — `send_raw`, `say`, `mc_send`,
  `insert_input` are registered **unconditionally** (no permission gate). Any loaded script can
  send arbitrary IRC traffic / inject text into the input box. Only `launch` (runPrograms) and
  `http_get` (network) are gated. If scripts are meant to be fully trusted once loaded, fine —
  but then the granular perms give a false sense of containment. Consider a "talk to IRC"
  permission, or document that load==trust.
- **[BUG] LuaEngine.cpp:765-776** — **No cap on timers per script.** A script can call
  `api.timer` in a loop and create unbounded `QTimer`+registry-ref entries (each ≥50 ms but
  there can be millions). Memory/handle exhaustion. Cap active timers per script.
- **[BUG] LuaEngine.cpp:398-416** — `l_set` rewrites `prefs.json` with a plain
  `QFile`+`Truncate` (not `QSaveFile`); a crash mid-write corrupts/empties the prefs file.
  Also every `get`/`set` re-reads and re-parses the whole file (no caching) — `O(file)` per
  key access. Use `QSaveFile` and an in-memory cache.
- **[SEC] LuaEngine.cpp:152-183** — Windows `l_launch` re-quotes split args by escaping only
  `"` → `\"`. Windows command-line parsing treats backslash runs before a quote specially, so
  an argument like `a\"b` or trailing backslashes can break out of quoting. `runPrograms` is
  already full RCE so impact is bounded, but the quoting is not correct per the MS C runtime
  rules (`CommandLineToArgvW`).
- **[BUG] LuaEngine.cpp:752-760, 805-817** — `createTimer`/`cancelTimersFor` linear-scan the
  scripts/timers maps to find the owner by `lua_State*`. With many scripts/timers this is
  `O(n)` per call. A `lua_State* → ScriptState*` reverse map already exists (`stateByLua_`) —
  reuse it in `createTimer` instead of the loop.
- **[NIT] LuaEngine.cpp:1090** — The non-Lua stub `hostMcData` has a **different signature**
  (5 params) than the real one (6 params, with `network`). If `MAXCHAT_LUA` is off this won't
  match the header's declaration unless the header is also conditional — verify the stub build
  actually compiles.

---

## Subsystem: `services/`

These three are solid: a shared SSRF gate (`resolvePreviewUrlPublicAsync`) runs off-thread,
re-checks **every redirect hop**, size caps abort mid-download, and timeouts are enforced.
Findings are the residual gaps.

### `services/ImageFetcher.cpp`

- **[SEC] ImageFetcher.cpp:124-132** — **Image decompression bomb.** The byte cap (5 MB) bounds
  the *compressed* download, but `QImage::loadFromData(payload)` decodes the **full** image into
  RAM *before* the `maxWidth`/`maxHeight` check (line 129). A ~few-KB PNG/JPEG can declare
  50000×50000 pixels and allocate gigabytes, OOM-crashing the client. Use `QImageReader` on the
  buffer, read `reply`/reader `size()` first, reject oversized dimensions, and use
  `setScaledSize` so the decoder never materializes the full bitmap.
- **[BUG] ImageFetcher.cpp:112-116** — Content-type is only enforced when **non-empty**. A
  server returning an image with no `Content-Type` (or a wrong one) still reaches the decoder.
  Given the decoder is the real risk surface, also sniff magic bytes, or require a positive
  `image/*` type.
- **[NIT] ImageFetcher.cpp:118** — `reply->read(maxBytes + 1)` after a `finished()` where
  `downloadProgress` already aborted oversize bodies is belt-and-suspenders — fine, but the
  double cap check (here + progress handler) is slightly redundant; keep for safety.

### `services/OpenGraphFetcher.cpp`

- **[BUG] OpenGraphFetcher.cpp:111-112** — `reply->read(maxBytes + 1)` then `.left(maxBytes)`.
  Because there's no `downloadProgress` abort here (unlike `ImageFetcher`), a server can stream
  far more than `maxBytes` and QNAM buffers it all in RAM before `finished()`; only the *parse*
  input is bounded, not the *download*. Add the same `downloadProgress`-based abort as
  `ImageFetcher` so an attacker can't make the client buffer an unbounded HTML body.
- **[NIT] OpenGraphFetcher.cpp:16-18** — `looksLikeHtmlContent` treats an **empty**
  content-type as HTML, so a typeless binary response is fed to the HTML parser. Harmless
  (parser just finds nothing) but wasteful.

### `services/OpenGraphParser.cpp`

- **[SEC] OpenGraphParser.cpp:50-56** — Numeric entity decode casts an arbitrary `toUInt`
  result straight to `char32_t` with no range/validity check. Codepoints > U+10FFFF, surrogates
  (U+D800–DFFF), or controls are passed to `QString::fromUcs4`; Qt will substitute U+FFFD but
  noted for robustness — clamp/validate the scalar value.
- **[DESIGN] OpenGraphParser.cpp:132-161** — OG/Twitter card extraction is **regex over raw
  HTML**. This is the classic fragile-by-design approach: it breaks on `<meta>` split across
  lines with `>` inside an attribute value, CDATA, scripted tags, etc. Acceptable for a
  best-effort preview, but never trust the output as structural; values are correctly
  `normalizeText`-escaped before display (verify the renderer re-escapes for HTML — see
  `LinkPreviewRenderer`).
- **[NIT] OpenGraphParser.cpp:10-12** — Entity regex caps named entities at 8 chars; valid
  HTML5 names can be longer (e.g. `&CounterClockwiseContourIntegral;`). Those render literally.
  Negligible for og: text.

### `services/LinkPreviewClassifier.cpp`

Strong SSRF design: scheme allow-list, userInfo rejected, literal-IPv4 private-range blocklist
(incl. CGNAT 100.64/10, link-local 169.254, TEST-NETs, multicast), private-name suffixes, and
an off-thread DNS re-resolve that rejects if **any** A record is private.

- **[SEC] LinkPreviewClassifier.cpp:300-332** — **DNS-rebinding TOCTOU.** The gate resolves the
  host and approves, then `QNetworkAccessManager::get` performs its **own** DNS resolution for
  the actual socket. A hostile authoritative server can return a public IP to the gate's lookup
  and a private IP (127.0.0.1 / 169.254.169.254) to QNAM's lookup milliseconds later. The
  per-redirect re-check doesn't close this because the *initial* request re-resolves too. Full
  mitigation requires pinning the vetted IP into the connection (custom resolver / connect-to
  the literal and send SNI+Host) — at least document the residual risk.
- **[SEC] LinkPreviewClassifier.cpp:30-48, 238-243** — Only **dotted-decimal** IPv4 literals
  are parsed. Alternate IPv4 encodings (decimal `2130706433`, hex `0x7f000001`, octal,
  short-form `127.1`) don't match `ipv4Pattern`; `127.1` has a dot so it passes
  `isAllowedPreviewFetchUrl` as a "domain" and is only caught later by the DNS re-resolve.
  Defense-in-depth holds, but the literal classifier should normalize via `QHostAddress`
  (which understands these forms) rather than a strict dotted-quad regex.
- **[DESIGN] LinkPreviewClassifier.cpp:233-235** — Any host containing `:` (all IPv6 literals)
  is rejected outright, so even public IPv6 targets get no preview. Safe but increasingly
  limiting as IPv6-only hosts appear; consider parsing+vetting IPv6 like IPv4.
- **[NIT] LinkPreviewClassifier.cpp:266-281** — `resolvesToPublicOnly` (sync) is retained
  "for `canFetchPreviewUrl`" and blocks the GUI thread on `QHostInfo::fromName`. Ensure no
  GUI-thread caller uses the sync path (the async one exists precisely to avoid this).

### `services/LinkPreviewRenderer.cpp`

- **[SEC] LinkPreviewRenderer.cpp:132-138, 142-151** — `card.imageUrl` (from attacker-
  controlled `og:image`) is emitted as `<img src="...">` **without a scheme check**, unlike the
  click-target `canonicalUrl` which *is* restricted to http(s) (lines 79-83). If the chat
  widget (QTextBrowser/QTextDocument) auto-loads `<img>` resources itself, a `file://` or
  internal-`http://` og:image would be fetched **bypassing the ImageFetcher SSRF gate** — an
  SSRF/local-file-read vector. Two things to verify/fix: (1) restrict `imageUrl` to http(s)
  here as is done for `canonicalUrl`; (2) confirm image loading is routed through the gated
  `ImageFetcher` and Qt's automatic resource loading is disabled in the chat view.
- **[DESIGN] LinkPreviewRenderer.cpp:79-83** — Good: the comment and code correctly treat
  `og:url` as attacker-controlled and refuse non-http(s) click targets. Apply the same rigor to
  every other attacker-supplied URL that becomes an `href`/`src` (see above).
- **[NIT] LinkPreviewRenderer.cpp:10-21** — `cleanedText` truncates by UTF-16 code units
  (`size()`), so the "max chars" limit is really a max-code-unit limit; long graphemes / ZWJ
  emoji sequences may still be split (only the high-surrogate case is handled). Cosmetic.

### `services/LinkPreviewPolicy.cpp`

- **[NIT] LinkPreviewPolicy.cpp:27-44** — Clean exhaustive switch over `LinkPreviewKind` with a
  trailing `return false`. Fine; consider `-Werror=switch` so a new kind forces a compile error
  here rather than silently defaulting to "disabled".

---

## Subsystem: `upload/`

All four uploaders share the same shape (PNG-encode → multipart POST → parse JSON). They use
fixed HTTPS hosts (no SSRF surface) and Imgur strips CR/LF from the client-id before putting it
in a header (good). Cross-cutting issues:

- **[BUG] all uploaders** (ImgbbUploader.cpp:50, ImgurUploader.cpp:48, ImgboxUploader.cpp:47/106,
  PostimagesUploader.cpp:53) — **No transfer timeout.** None call
  `QNetworkRequest::setTransferTimeout` (or a manual `QTimer`+`abort`), unlike the
  `services/` fetchers which do. A stalled upload leaves the `QNetworkReply` (and the
  multipart, parented to it) alive indefinitely and the UI's "uploading…" state never resolves.
  Add a timeout to every request.
- **[SEC] ImgbbUploader.cpp:44-47** — The ImgBB API key is placed in the **URL query string**
  (`?key=...`). URLs are the most-logged part of any request (proxies, server access logs,
  `QNetworkReply::url()` in any raw/debug log). Prefer a header or form field if the API allows;
  at minimum ensure the key never lands in app logs via `reply->url()`.
- **[DESIGN] all uploaders** — ~80% of each file is duplicated boilerplate (PNG encode into a
  `QBuffer`, build `QHttpMultiPart`, `finished` lambda with `deleteLater` + error check + JSON
  parse). Hoist the common encode/post/parse scaffold into the `ImageUploader` base; each
  service then only supplies its endpoint, auth, and the JSON path to the result URL.
- **[BUG] all uploaders** — `reply->readAll()` with no size cap on the *response*. A malicious
  or broken endpoint can return an unbounded body that's fully buffered before JSON parsing.
  Low risk (you chose the service) but trivially cappable.
- **[BUG] ImgbbUploader.cpp:41 / ImgurUploader.cpp:42** — The image is base64-encoded
  (`pngData.toBase64()`) inside a multipart form field. That inflates the upload ~33% and is
  only needed by APIs that expect a base64 `image` param; ImgBB/Imgur both accept raw binary
  multipart. Confirm base64 is actually required (Imgbox/Postimages send raw bytes) — if not,
  it's wasted bandwidth/memory on large screenshots.
- **[NIT] ImgboxUploader.cpp:123-125** — `images.toArray().first()` on a potentially empty
  array relies on `QJsonValue(Undefined).toObject()` being empty → handled, but an explicit
  `isEmpty()` check would read more clearly.
- **[NIT] ImageUploaderFactory.cpp:40** — Returns `nullptr` for unknown/disabled service with
  no diagnostic; a typo in `image_upload_service` silently disables uploads. Consider logging
  the unrecognized value.

---

## Subsystem: `app/` and `comic/`

### `app/AppInfo.cpp`

- **[NIT] AppInfo.cpp:13-15** — `version()` returns the hardcoded string `"0.1.0-dev"`. It
  should be injected from the build system (CMake `configure_file` / `target_compile_definitions`)
  so binaries report the real build version rather than a literal that drifts.

### `comic/ComicRenderer.cpp`

- **[DESIGN] ComicRenderer.cpp:27, 32, 545, 572-575** — `g_comicFontFamily` and
  `g_actionIncludeName` are **mutable file-scope globals** set at the top of each render entry
  point, justified by "render is synchronous on the GUI thread." This is fragile: any future
  reentrant render (e.g. a nested `processEvents`, a worker thread, or recursive layout) reads
  another render's state. Thread the two values through `layout`/`fitBalloons`/`drawBalloons`
  (or pack them in a small `RenderCtx` struct) instead of globals.
- **[PERF] ComicRenderer.cpp:96-142, 543-566** — Both `panelMinFont` and `renderComicPanel`
  run the full font-fit loop calling `layout()` (which `boundingRect`s every balloon) up to ~20
  times. `imageTrimmed`/`character` lookups inside `placeCharsWidths` recompute per iteration.
  For comic mode on a busy channel this is heavy; the character-width results are independent of
  font size and could be hoisted out of the fit loop.
- **[BUG] ComicRenderer.cpp:159** — `placeCharsWidths` can be entered with `targetH` already
  small; the `std::max(20, ...)` floor (line 121) and `std::max(1, ...)` width floor avoid
  zero-division, but `im.height() > 0` is the only guard before dividing by it (line 107) —
  fine. Just confirm `targetH` is always ≥1 when no actors have images (the `*0.6` fallback).
- **[NIT] ComicRenderer.cpp:79-94** — `looksLikeShout` duplicates the all-caps/letters logic in
  `comic/ComicEmotion.cpp::guessEmotion`. Factor a shared `isAllCaps(body)` helper.

### `comic/ComicEmotion.cpp`

- **[DESIGN] ComicEmotion.cpp:18-70** — Emotion heuristic is an ordered cascade of substring
  matches. `:3` maps to happy but also appears in timestamps like `12:30` → `12:3`... actually
  needs `":3"` exact substring, and `"1:3"` contains `:3` only as `:3`? `12:30`→ contains `:3`
  → false-positive "happy". Emoticon matching on raw substrings will misfire on times/ratios.
  Low stakes (it's cosmetic), but anchoring on word boundaries would reduce false hits.
- **[NIT] ComicEmotion.cpp:35-36** — `:(` matched against the **original** `body` while most
  others match the lowercased `low`; the mixed casing source is intentional (emoticons are
  case-significant) but worth a comment so it isn't "fixed" into a bug.

### `comic/ComicCharacter.cpp` and `comic/ComicArt.cpp`

- **[GOOD] ComicArt.cpp:49-53** — Hostile-file guards (`MaxDibDim` 4096, `MaxInflateBytes`
  32 MB) are present before allocation — exactly right for decoding untrusted `.avb/.bgb`.
  Verify the stride/`pixels` size math downstream also re-checks against the decompressed
  length so a header claiming `w*h` larger than the inflated buffer can't over-read.
- **[DESIGN] ComicCharacter.cpp:201-214** — `loadCharacter` caches `Character*` in a
  function-local `static QHash` that is **never freed** and **never mutex-guarded**. Fine for a
  GUI-thread-only, app-lifetime cache, but it leaks every distinct character path until exit and
  would race if comic rendering ever moves off the GUI thread.
- **[PERF] ComicCharacter.cpp:11-31, 34-83** — `alphaBBox` and `neck` each
  `convertToFormat(ARGB32)` (a full copy) then scan every pixel. Results are cached
  (`neckCache_`, `trimmed_`, `composed_`, `base_`) so it's once-per-key — acceptable.

---

## Subsystem: `spell/`

Clean and small. A native-OS-speller stub pattern keeps non-Windows builds simple.

- **[BUG] HunspellSpellchecker.cpp:72-81** — `isCorrect` returns **`false`** when no dictionary
  is loaded. "false" means "misspelled", so with a missing/failed dictionary **every word gets
  flagged**. Callers must gate on `isLoaded()` first; safer to return `true` (don't flag) when
  `hunspell_ == nullptr` so a dictionary load failure degrades to "no spellcheck" rather than
  "everything is wrong".
- **[DESIGN] HunspellSpellchecker.cpp:83-92** — `addWord` calls `Hunspell::add`, which adds to
  the **in-memory runtime dictionary only** — it is not written to a personal `.dic`, so
  user-added words vanish on restart. If the UI exposes "Add to dictionary", persist the word
  list separately and re-add on load.
- **[NIT] HunspellSpellchecker.cpp:19-30** — `normalizedWord` strips punctuation except `'`,
  but does not strip surrounding IRC formatting codes or digits; a token like `v2` or a
  color-coded word may be checked verbatim and flagged. Likely fine since the highlighter
  tokenizes first — verify.
- **[NIT] SpellcheckDictionaryCatalog.cpp:16-76** — Language/dictionary table is hand-coded
  inline (same data-in-source smell as `DefaultNetworks`). Acceptable given its small size;
  consider a resource file if it grows.

---

## Subsystem: `ui/` (selected — see note at end of section)

> The `ui/` tree is ~20,000 lines; `MainWindow.cpp` alone is ~9,900. Per the by-subsystem
> plan, the security-critical and parser files are audited line-by-line below; `MainWindow`
> gets a structural review plus spot findings; the remaining dialogs are covered at the tier
> level. A genuinely exhaustive line-by-line of every dialog would need additional passes.

### `ui/DccManager.cpp`

This file is **well-hardened** — strict numeric host/port parsing, filename sanitization
(traversal + Windows reserved devices + drive colons + trailing dot/space), per-peer nick
verification on passive replies and resumes, peer-IP verification on passive listens, offered-
size write caps (`dccWritableChunk`), anti-flood pending caps, stall/TTL sweeps, and `.part`
staging. The comments document prior real vulnerabilities that were fixed. Residual gaps:

- **[SEC] DccManager.cpp:226-274, 518** — **No maximum file size or free-space check.** The
  offered `size` is attacker-controlled and only bounds the *write* (`dccWritableChunk` caps to
  it). With accept policy `all` (or a trusted nick), a peer can offer a multi-terabyte file and
  fill the disk; the only backstop is the 2-minute *stall* timer (a steady slow stream never
  stalls). Add a configurable max-accept size and/or check `QStorageInfo` free space before
  accepting.
- **[SEC] DccManager.cpp:85-108** — `sanitizeFileName` does not strip **Unicode bidi/RTL
  override** control chars (U+202E etc.) or other formatting controls. An offer named
  `photo_‮gnp.exe` renders in the Transfers UI as `photo_exe.png` — a classic extension-
  spoof. Strip Cf-category control characters from the display/saved name.
- **[SEC] DccManager.cpp:114-119** — Passive tokens use a **single 32-bit** value
  (`QRandomGenerator::system()->generate()`, 8 hex chars). The comment calls the token "the
  only barrier"; in practice the `sameNick` check is the real barrier, but 32 bits is well
  below Python's `secrets.token_hex(16)` (128-bit). Widen to ≥64 bits — cheap defense-in-depth
  against a passive-reply forger who can also spoof the nick (some networks allow it).
- **[BUG] DccManager.cpp:211-213, 302/338/etc.** — `advertisedIp()` is fed straight into
  `QHostAddress(advertisedIp()).toIPv4Address()`. If the user configured a **hostname** (not a
  dotted IP), `QHostAddress` is invalid and `toIPv4Address()` returns **0**, so the offer
  advertises `0.0.0.0` and silently fails. Validate/resolve the advertised IP at config time, or
  reject non-IP input.
- **[BUG] DccManager.cpp:264-273** — The same-name dedup loop stops at `i < 10000` and then
  **returns the original `candidate`**, which can clobber an existing file. Astronomically
  unlikely, but the fallthrough should fail the transfer rather than overwrite.
- **[DESIGN] DccManager.cpp:1109, 1104-1111** — `sendChatLine` writes the user's line with a
  trailing `\n` but does **not** strip embedded `\n`/`\r` from `line`. DCC CHAT is line-based;
  an embedded newline injects extra lines to the peer. Low severity (it's your own outgoing
  chat) but mirror the IRC CRLF-strip discipline.
- **[NIT] DccManager.cpp:19-21, 147-148** — Several tunables (`ChunkSize`, caps, TTLs) are good
  constants; the max-pending (32) and stall (120 s) are reasonable. Consider surfacing the
  accept size cap (above) alongside them.

### `ui/AnsiRenderer.cpp`

- **[SEC] AnsiRenderer.cpp:65-69, 143-154** — **HTML attribute injection via hotspot action.**
  `hotspotHtml` builds `<a href="mc-term:%1">` where `%1` is `rest.left(sep)` — the raw action
  string taken straight from the (server-controlled) MC-HOTSPOT OSC payload, inserted via
  `arg()` **without HTML-escaping**. The legit producer (`AnsiRenderer::hotspot`) percent-encodes
  the action, but a malicious MC DATA/BBS stream can send an action containing `"` / `>` to break
  out of the `href` attribute and inject arbitrary markup into the chat `QTextBrowser` (e.g. an
  `<img src="file://...">` for local-file/SSRF, or a spoofed link). The *label* is correctly
  `toHtmlEscaped()` — apply the same escaping (HTML-attribute-escape) to the action before it
  enters `href`. This is the most concrete injection bug found in the UI layer.
- **[DESIGN] AnsiRenderer.cpp:124-157** — An unterminated `\x1b[`/`\x1b]` (no `m`/`BEL`) falls
  through to `run += ch`, emitting the ESC literally — reasonable, but an OSC with a very late
  BEL means `indexOf('\x07')` scans far ahead on every ESC. Bounded by line length; fine.
- **[NIT] AnsiRenderer.cpp:106-108** — `const int fgCode = code >= 100 ? code - 10 : code - 10;`
  both branches are identical (`code - 10`) — the ternary is dead. Simplify to `code - 10`.
- **[NIT] AnsiRenderer.cpp:20-40** — Only the 16 basic SGR colors are mapped; 256-color
  (`38;5;n`) and truecolor (`38;2;r;g;b`) SGR sequences are ignored (the `38`/`48` codes fall
  through). Fine if the BBS profile is 16-color, but worth documenting.

### `ui/TerminalFrame.cpp`

- **[GOOD] TerminalFrame.cpp** — The opcode parser is carefully bounds-checked (`hexByte`/
  `hexWord` validate `at+1 < size`, lengths are hex-bounded, `readUtf8Text` rejects truncation
  and mid-codepoint splits). Combined with the 350-byte MC DATA payload cap upstream, the
  attack surface here is small.
- **[BUG] TerminalFrame.cpp:55-77** — `readUtf8Text` re-encodes **one `QChar` at a time** to
  UTF-8. A character outside the BMP is stored in two `QChar` surrogates; converting a **lone**
  high surrogate via `QString(ops.at(*pos)).toUtf8()` yields U+FFFD replacement bytes, so the
  byte accounting against `byteLength` can desync and the op is rejected as
  "splits a UTF-8 character" even for well-formed astral text. Decode against the full UTF-8
  byte stream rather than per-QChar.
- **[DESIGN] TerminalFrame.cpp:100-113** — `Position` row/col are parsed as raw hex bytes
  (0-255) with no clamp to the terminal grid here; the consumer (`TerminalProfile`/renderer)
  must clamp. Confirm it does, or a `P` op with out-of-range row/col can index past the cell
  grid downstream.
- **[NIT] TerminalFrame.cpp:161-163** — On an unknown opcode the whole frame is rejected
  (`return false`). For forward-compat with newer producers, consider skipping unknown ops with
  a known length prefix instead of failing the entire frame.

### `ui/MainWindow.cpp` (structural review + spot findings)

> ~9,900 lines in one translation unit. Audited structurally with targeted reads of the
> HTML-insertion, link-click, settings, and notification paths (not every line — see the
> section note).

- **[DESIGN – major] MainWindow.cpp (whole file)** — **God object.** `MainWindow` owns IRC
  connection wiring, the buffer/tree model, chat HTML rendering, link-preview orchestration,
  the DCC manager, the scripting host bridge, notifications/tray, logging, auto-away, flood
  drop, find-in-chat, update checks, and dozens of dialogs. At ~9,900 lines it's effectively
  un-unit-testable and every change risks unrelated regressions. Extract cohesive
  collaborators: a `ChatViewController` (HTML insert/format/indent), a `PreviewController`
  (the OG/image/media pipeline), a `NotificationController`, and a `BufferTreeController`.
- **[SEC] MainWindow.cpp:7443-7497** — `handleChatAnchorClicked` falls through to
  `QDesktopServices::openUrl(url)` for the `default` case **without a scheme allow-list**.
  Today chat hrefs are produced only by the http/https/ftp linkifier and http(s)-restricted
  preview cards, so the exposure is small — but combined with the `AnsiRenderer` hotspot
  attribute-injection bug (above), a crafted `<a href="file://…">` could reach this `openUrl`.
  Gate `openUrl` on an explicit `{http,https,ftp,mailto}` allow-list; reject `file:`/`smb:`/
  custom schemes.
- **[GOOD] MainWindow.cpp:6755-6759, 1860** — Image loading is deliberately routed through the
  cached/SSRF-gated `ImageFetcher` (the comment notes QTextBrowser won't fetch remote `<img>`
  itself) and `setOpenExternalLinks(false)` keeps link handling in app code. This is the right
  posture; just make sure a `file://` `og:image` can't be registered as a document resource
  (ties back to the `LinkPreviewRenderer` finding — restrict `imageUrl` scheme).
- **[PERF] MainWindow.cpp (61 call sites)** — `m_settings.loadWithDefaults()` is called **61
  times**, several on hot paths (per inbound CTCP sound at 2600, per message formatting, etc.).
  `SettingsStore` has a stat-validated cache so each call is usually a `stat` + map copy, but
  building the full default map and overlaying saved keys on every message is still wasteful.
  Cache a resolved settings snapshot in `MainWindow` and refresh it only on the settings-changed
  signal.
- **[PERF] MainWindow.cpp:6686-6730** — Every chat line is an `insertHtml` into a growing
  `QTextDocument`. With large scrollback this degrades (QTextDocument is not a virtualized
  view); ensure the scrollback trim (the `scrollback`/2000 setting) actually removes blocks from
  the document, not just the backing buffer, or memory/layout cost grows unbounded over a long
  session.
- **[NIT] MainWindow.cpp:864-890** — The Linux notification-daemon probe shells out to
  `dbus-send` via `QProcess`. Fixed args (no injection), async (good, avoids the old 3 s
  stall), but it adds a process spawn at startup and a hard dependency on `dbus-send` being on
  PATH; a direct `QDBusInterface` call would avoid the subprocess.
- **[GOOD] MainWindow.cpp:792-103 (selfTest), main.cpp** — A `--selftest` path that constructs
  the window offscreen and validates startup is a solid smoke test; keep expanding it as the
  cheapest regression guard for this monolith.

### `ui/` remaining dialogs (tier-level review)

Files: `PreferencesDialog` (2,415), `ServerListDialog`, `ChannelListDialog`, `ComicSettingsDialog`,
`AliasEditorDialog`, `ChannelModesDialog`, `DccTransfersDialog`, `BanListDialog`,
`IgnoreListDialog`, `FriendsNotifyDialog`, `UrlListDialog`, `ThemeEditorDialog`,
`ColorPickerDialog`, `QuickConnectDialog`, `RawLogDialog`, `ChatFindDialog`,
`ScriptTerminalDialog`/`Manager`, `MediaPlayerDialog`, `ImageViewerDialog`, `AudioPlayerBar`,
`ToastWidget`, `Notifier`, `SpellTextEdit`, `SpellcheckHighlighter`, `GeometryPersist`,
`AppIcon`, `SystemInfo`, `ThemeCatalog`, `TerminalProfile`, `SoundPlayer`.

These were not read line-by-line in this pass. Based on the patterns seen across the codebase
and the two already-touched in the working tree (`ServerListDialog`, `MainWindow`), the
recommended focused follow-ups are:

- **[DESIGN] PreferencesDialog.cpp (2,415 lines)** — Second-largest UI file; likely mirrors the
  `defaultSettings()` wall of keys with per-control wiring. High risk of key-name drift vs
  `SettingsStore::defaultSettings()` (a typo'd key silently no-ops). Worth a dedicated
  line-by-line pass cross-checking every settings key string against the central defaults.
- **[SEC] PreferencesDialog / QuickConnectDialog / ServerListDialog** — These edit the
  plaintext-stored secrets (SASL/server/proxy passwords, API keys). Confirm password fields use
  `QLineEdit::Password` echo mode and that secrets aren't written to the raw log or window
  titles. Ties to the `SettingsStore` plaintext-secrets finding.
- **[SEC] SoundPlayer / ScriptTerminalManager** — `SoundPlayer` plays a settings-named WAV;
  confirm the jailed-path rule the `LuaEngine` comment references ("matches SoundPlayer/DCC path
  rules") is actually enforced so a CTCP SOUND or script can't play/scan arbitrary filesystem
  paths.
- **[SEC] SpellTextEdit / input paste path** — Verify the paste guard (`paste_guard`/
  `paste_lines`) strips/normalizes CRLF before text reaches `CommandParser`/`sendRaw`
  (the CRLF strip in `IrcSession::enqueueLine` is the backstop, but multi-line paste → multiple
  PRIVMSGs should be the user's explicit choice).
- **[DESIGN] GeometryPersist + per-dialog geometry** — The `geom_*` special-casing in
  `SettingsStore::saveRaw` exists because dialogs persist geometry independently; confirm every
  dialog uses the shared `attachGeometryPersist` helper rather than re-implementing save/restore
  (drift here was the source of the geometry footgun).

---

## Cross-cutting themes & prioritized follow-ups

**Highest priority (security / correctness):**
1. **Lua re-entrancy + no execution watchdog** (`LuaEngine.cpp`) — synchronous `http_get` can
   re-enter a suspended `lua_State`; an infinite loop freezes the whole client. Add a
   re-entrancy guard, an instruction-count hook, and a memory cap.
2. **AnsiRenderer hotspot HTML-attribute injection** (`AnsiRenderer.cpp:65-69`) — a malicious
   MC DATA/BBS stream injects markup into the chat view; escape the action in the `href`.
3. **Image decompression bomb** (`ImageFetcher.cpp`) — decode dimensions before allocation.
4. **Plaintext secrets at rest** (`SettingsStore.cpp`) — SASL/proxy passwords and API keys in
   world-readable JSON; use a keychain or restrictive file permissions.
5. **DCC: no max-accept size / free-space check, weak 32-bit token, bidi filename spoof**
   (`DccManager.cpp`).
6. **`og:image` scheme not restricted + `openUrl` has no scheme allow-list**
   (`LinkPreviewRenderer.cpp`, `MainWindow.cpp:7497`) — SSRF/local-file vectors.
7. **DNS-rebinding TOCTOU** in the preview SSRF gate (`LinkPreviewClassifier.cpp`).

**Recurring quality issues:**
- **Data-in-source**: `DefaultNetworks.cpp` (~1,500 lines), `SettingsStore::defaultSettings`,
  the spell catalog — move to bundled resources.
- **Duplicated default nick `"comicfan"`** across 150+ DefaultNetworks entries + 3 other files.
- **Upload boilerplate** duplicated across 4 uploaders; no transfer timeouts.
- **Missing transfer/network timeouts** in `upload/`; **two full-buffer rescans per readyRead**
  in `IrcConnection`.
- **God object** `MainWindow` (~9,900 lines) and several files mixing data tables with logic.
- **UTF-8-only decode** of IRC lines (`IrcConnection.cpp:419`) corrupts legacy-encoded text.

**Note on coverage:** `core/`, `irc/`, `scripting/`, `services/`, `upload/`, `spell/`,
`comic/`, `app/`, and the security-critical UI files (`DccManager`, `AnsiRenderer`,
`TerminalFrame`) were audited line-by-line. `MainWindow.cpp` received a structural review with
targeted reads; the remaining ~30 UI dialogs are covered at the tier level above and warrant a
dedicated second pass (especially `PreferencesDialog`, `SoundPlayer`, and the secret-editing
dialogs).

*End of audit.*
