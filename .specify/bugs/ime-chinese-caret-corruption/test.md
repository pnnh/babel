# Bug Verification: Chinese IME caret jump and text corruption

- **Slug**: `ime-chinese-caret-corruption`
- **Tested**: 2026-07-26
- **Assessment**: ./assessment.md
- **Fix**: ./fix.md
- **Result**: partial

## Summary

The fix is present in source and the project builds cleanly. Static review confirms the original anti-pattern (full `GetText` on every change) is gone. Interactive Chinese IME reproduction (assessment steps 1–5 / quickstart V-010) was **not** executed in this environment, so the fix cannot be marked fully **verified** end-to-end.

## Checks Performed

| Check | Command / Action | Result | Notes |
|-------|------------------|--------|-------|
| Fix code present (static) | Review `WinUiEditEditorAdapter` + `MainWindow` vs assessment | pass | IME windowed; Modified coalesce; debounced sync; flush on Save paths |
| Immediate GetText on TextChanged removed | Code review `Editor_TextChanged` | pass | Only `MarkDirty` + schedule; no `_editor.Text` on hot path |
| Save/Close flush still present | Code review `SyncDocumentFromEditor` call sites | pass | Used by Save / Save As / preview |
| Manual docs (V-010) | `specs/005-notepad-syntax-editor/quickstart.md` | pass | Scenario documented with A/B env |
| Reproduction (post-fix) | Assessment steps 1–5 with 微软拼音 continuous input | **not-run** | Requires interactive desktop IME session; agent cannot drive IME UI |
| New / updated automated tests | N/A — none added (fix notes UI IME impractical) | not-run | No unit/UI test suite for this path |
| Regression suite | No project automated test project for CsTextEditor | not-run | None configured |
| Build / compile | `dotnet build windows/CsTextEditor/CsTextEditor.csproj -c Debug -p:Platform=x64` | pass | Build succeeded; no CS errors/warnings |

## Output Excerpts

```text
dotnet build windows/CsTextEditor/CsTextEditor.csproj -c Debug -p:Platform=x64
Build succeeded.
```

Static hot path (post-fix):

```csharp
// MainWindow.Editor_TextChanged — no GetText
_document.MarkDirty();
UpdateTitle();
ScheduleDocumentSync();

// WinUiEditEditorAdapter.Modified — no GetText/SetSel
_pendingTextChanged = true;
_changeCoalesceTimer.Start();
```

## Residual Risks

- Runtime IME behavior on real 微软拼音 / 搜狗 may still fail if WinUIEdit prerelease has control-level IME bugs beyond host re-entrancy.
- Debounced content means `_document.Content` lags until idle; Save/Close flush mitigates data loss but must remain on all exit paths.
- A/B TextBox path (`CSTEXTEDITOR_FORCE_TEXTBOX=1`) was not runtime-tested here.
- UTF-16 vs Scintilla byte positions for Find/Select with CJK still a separate risk (not the original symptom).

## Recommendation

**Hold — do not close as fully verified until a human runs V-010.**

1. F5 CsTextEditor with WinUIEdit (default).
2. Continuous Chinese IME input (empty + mid-line + mixed 中/EN).
3. Confirm no caret jump / partial delete; Save file and reopen.
4. Optional: wrap on/off, Markdown preview on/off.
5. If still broken: try `CSTEXTEDITOR_FORCE_TEXTBOX=1`; if TextBox is clean, escalate to control upgrade or default TextBox (assessment Alternative A) and re-run `/speckit-bug-assess` with new evidence.

After successful manual V-010, result can be upgraded to **verified** (amend `test.md` or re-run this command with user confirmation of manual pass).
