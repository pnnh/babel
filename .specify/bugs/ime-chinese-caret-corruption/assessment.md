# Bug Assessment: Chinese IME caret jump and text corruption

- **Slug**: `ime-chinese-caret-corruption`
- **Created**: 2026-07-26
- **Source**: pasted text (CsTextEditor runtime report; feature `specs/005-notepad-syntax-editor/spec.md`)
- **Verdict**: valid
- **Severity**: high

## Report (verbatim or summarized)

User reports that CsTextEditor was implemented per Spec, but Chinese IME input is broken:

- During continuous IME composition/input, the caret jumps into the middle of the sentence.
- Characters at the caret are deleted and replaced by newly committed IME text (sometimes deleted without replacement).
- Original requirements: `specs/005-notepad-syntax-editor/spec.md`.

No external URL was supplied (no fetch policy applied).

## Symptom

**Observed**: With a Chinese IME (e.g. Microsoft Pinyin), continuous composition causes caret position to jump mid-string and deletes/replaces nearby characters incorrectly.

**Expected** (per spec US1 / FR-004 / Notepad-class behavior): Characters appear at the caret as the user types; IME composition and commit behave like Windows Notepad—no spontaneous caret jumps or partial deletions of surrounding text. Typing must remain reliable enough for SC-001 (create a short note and save).

## Reproduction

1. Launch CsTextEditor (status bar typically shows `Editor: WinUIEdit`).
2. Focus the editor on an empty or partially filled document.
3. Switch to a Chinese IME (e.g. 微软拼音).
4. Continuously type several phrases (composition → candidate → commit, without long pauses).
5. Observe: caret jumps mid-sentence; existing characters near caret disappear and/or are replaced by the latest commit.

**Also try** (to isolate control vs. app logic):

6. Confirm status is WinUIEdit vs TextBox fallback.
7. Repeat with English-only typing (ASCII) — expected stable if issue is IME-specific.
8. [NEEDS CLARIFICATION: exact IME product/version, Windows build, and whether preview pane is open]
9. [NEEDS CLARIFICATION: empty document only vs. editing mid-existing Chinese/English mix]

## Spec impact

| Spec item | Impact |
|-----------|--------|
| **US1** “type text… appears immediately”; “standard desktop text editor” | Broken for CJK users |
| **FR-004** type / select / edit | Typing path fails under IME |
| **FR-013** UTF-8 text | Corruption may produce wrong in-memory buffer before save |
| **SC-001** create note in &lt; 2 minutes without docs | Fails for Chinese input workflow |
| **SC-005** continuous typing remains responsive | Related: continuous typing unsafe under IME |

Not a “nice-to-have localization” gap: the product is a **plain text editor** (spec Assumptions). Correct IME handling is part of baseline FR-004, not a separate language feature.

## Suspected Code Paths

- `windows/CsTextEditor/MainWindow.xaml.cs` — `InitializeEditor()` (~L42–60) always prefers `WinUiEditEditorAdapter` (Scintilla-based WinUIEdit 0.0.5-prerelease).
- `windows/CsTextEditor/MainWindow.xaml.cs` — `Editor_TextChanged` (~L72–78): on every editor change, reads full `_editor.Text`, updates dirty state and window title, schedules MD preview.
- `windows/CsTextEditor/Editors/WinUiEditEditorAdapter.cs` — `HookEditorEvents` (~L41–54): subscribes to Scintilla `Editor.Modified` and raises `TextChanged` for **all** modifications (including intermediate IME/composition-related updates if the control reports them).
- `windows/CsTextEditor/Editors/WinUiEditEditorAdapter.cs` — `Text` getter (~L62–73): `GetText(TextLength + 1)` on every sync; may be heavy or disruptive during composition; risk of trailing NUL / length edge cases.
- `windows/CsTextEditor/Editors/WinUiEditEditorAdapter.cs` — `Select` / `FocusEditor` (~L121–131, L180): if ever invoked during composition (Find, etc.), would break IME; Find path is user-triggered but still relevant.
- `windows/CsTextEditor/Editors/WinUiEditEditorAdapter.cs` — no configuration of `Editor.IMEInteraction` (API exists on WinUIEdit’s Scintilla surface: window vs inline IME).
- `windows/CsTextEditor/Editors/PlainTextEditorAdapter.cs` — fallback TextBox path; less likely if status shows WinUIEdit, but Cut/Paste force full `Text` rewrite (not IME continuous path).
- Package: `WinUIEdit` **0.0.5-prerelease** (documented early WIP in spike notes) — IME quality may be incomplete on WinUI 3.

## Root Cause Hypothesis

**Confidence: medium–high**

Primary host is **WinUIEdit (Scintilla port, prerelease)**. CJK IME uses an in-progress **composition** range that must not be disturbed by full-buffer reads, selection changes, or host re-entrancy. The app wires **every** `Modified` notification to a full `GetText` + document/title update. That pattern is a common cause of “caret jumps / partial overwrite” with Scintilla-class controls under IME, especially when:

1. Intermediate composition inserts/deletes fire `Modified` and host code re-enters the control, and/or  
2. The control’s default **inline IME** interaction is buggy on WinUI (WinUIEdit exposes `IMEInteraction` and marks some experimental features as buggy), and/or  
3. Byte-oriented Scintilla positions vs. UTF-16 string handling interact poorly if any selection/API mix occurs during multi-byte Chinese text.

The symptom matches **composition interruption + incorrect replace range**, not a simple “font doesn’t support Chinese” issue. English typing may still work, which aligns with IME-only composition failure.

Secondary contributor: using prerelease WinUIEdit without an IME smoke test in quickstart validation left this gap after implement.

## Proposed Remediation

**Preferred** (layered, minimal product risk):

1. **IME-safe change pipeline** in `WinUiEditEditorAdapter` / `MainWindow`:
   - Do **not** treat every `Modified` as a full-document sync.
   - Prefer: set dirty from lightweight signals; **debounce** full `GetText` (e.g. 50–100 ms idle) for `_document.Content` sync; never call `SetText`/`SetSel`/`Focus` from the sync path.
   - If WinUIEdit/`Editor` exposes composition or IME state (or modification flags that distinguish user commit vs. provisional), **suppress host sync during composition**; flush once on commit/idle.
   - On adapter load, set `Editor.IMEInteraction` explicitly (try windowed vs inline; pick the mode that keeps Microsoft Pinyin stable—verify both on the target OS).

2. **Harden `Text` get/set**:
   - Ensure `GetText` length/encoding matches control docs; strip trailing `\0` if present.
   - Never assign `Text` setter during live typing/IME (only Open/New/programmatic load).

3. **Regression guard for fallback**:
   - Keep `PlainTextEditorAdapter` as escape hatch.
   - Optional temporary setting or debug switch: force TextBox host to confirm whether bug is WinUIEdit-only (if TextBox is clean → control/IME path; if TextBox also fails → app-level handler).

4. **Validation** against FR-004 / SC-001 with Chinese IME (see tests below). Document IME smoke in quickstart.

**Alternatives**:

| Alternative | Trade-off |
|-------------|-----------|
| **A. Default to PlainTextEditorAdapter until WinUIEdit IME is proven** | Restores reliable Chinese typing quickly; loses syntax highlighting until fixed |
| **B. Upgrade/replace editor control** (newer WinUIEdit or other net8-compatible control) | May fix upstream IME; larger dependency risk |
| **C. Host WinUI `TextBox` for input + async highlight elsewhere** | Complex dual-buffer architecture; overkill for MVP |

**Files likely to change**:

- `windows/CsTextEditor/Editors/WinUiEditEditorAdapter.cs` — IME interaction, Modified filtering, safe Text get, debounce hooks
- `windows/CsTextEditor/MainWindow.xaml.cs` — debounced document sync; avoid work on every keystroke during composition
- `windows/CsTextEditor/Editors/ICodeEditor.cs` — optional `IsComposing` / `ContentChanged` debounced API if needed
- `windows/CsTextEditor/Editors/PlainTextEditorAdapter.cs` — only if fallback also affected or for A/B toggle
- `specs/005-notepad-syntax-editor/quickstart.md` — add IME validation scenario (docs only in fix phase if desired)
- `specs/005-notepad-syntax-editor/notes-winuiedit-spike.md` — record IME findings

**Tests to add or update**:

- Manual (required): Microsoft Pinyin / 微软拼音 — empty doc, mid-line insert, mixed 中英, rapid continuous commit, undo after IME commit.
- Manual: same matrix with Word Wrap on/off and Markdown preview on/off.
- Manual A/B: force PlainText host vs WinUIEdit.
- Automated UI IME tests are generally impractical; optional unit tests only for debounce/dirty helpers if extracted.

## Risks & Considerations

- Debouncing content sync means `_document.Content` may lag slightly behind the control; Save/Close must **always flush** from editor before persist/prompt (call `SyncDocumentFromEditor()` on those paths—already used in Save).
- Changing `IMEInteraction` may help one IME and regress another (JP/KR); smoke-test at least one additional CJK IME if available.
- WinUIEdit is prerelease—app-level mitigations may not fully fix control bugs; be prepared for Alternative A or B.
- Over-filtering `Modified` could miss real edits if not flushed on idle/commit.
- No security impact beyond text corruption / data loss risk for user documents (severity driver).

## Open Questions

- [NEEDS CLARIFICATION: Which IME and Windows version? (e.g. 微软拼音 vs 搜狗, Win10 vs Win11)]
- [NEEDS CLARIFICATION: Does status bar show `Editor: WinUIEdit` or TextBox fallback when the bug occurs?]
- [NEEDS CLARIFICATION: Does English-only continuous typing ever show the same caret jump?]
- [NEEDS CLARIFICATION: Does the bug occur with Markdown Preview enabled only, or always?]

## Recommended next step

`/speckit-bug-fix slug=ime-chinese-caret-corruption`
