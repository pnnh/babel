# Bug Fix: Chinese IME caret jump and text corruption

- **Slug**: `ime-chinese-caret-corruption`
- **Fixed**: 2026-07-26
- **Assessment**: ./assessment.md
- **Status**: applied

## Summary

Stopped full-buffer `GetText` on every Scintilla `Modified` event during typing/IME composition, coalesced change notifications, configured windowed IME interaction, and always flush the live editor buffer only on Save/Close/preview. This addresses FR-004 typing stability for Chinese IME under WinUIEdit.

## Changes

| File | Change | Notes |
|------|--------|-------|
| `windows/CsTextEditor/Editors/WinUiEditEditorAdapter.cs` | modified | IME windowed mode; 100 ms coalesce of Modified→TextChanged; safer Text get; avoid Focus during Select when already focused |
| `windows/CsTextEditor/MainWindow.xaml.cs` | modified | Dirty mark without immediate GetText; 120 ms debounced content sync; force-TextBox env switch |
| `specs/005-notepad-syntax-editor/quickstart.md` | modified | Added V-010 Chinese IME manual scenario |
| `specs/005-notepad-syntax-editor/notes-winuiedit-spike.md` | modified | Recorded IME mitigation notes |

## Diff Highlights (optional)

**MainWindow — IME-safe dirty path:**

```csharp
private void Editor_TextChanged(...)
{
    _document.MarkDirty();      // no GetText
    UpdateTitle();
    ScheduleDocumentSync();     // debounced GetText
    SchedulePreviewRefresh();
}

// Save / Close / preview still call SyncDocumentFromEditor() to flush.
```

**WinUiEditEditorAdapter — coalesce + IME:**

```csharp
_control.Editor.IMEInteraction = (IMEInteraction)0; // SC_IME_WINDOWED
// Modified only sets _pendingTextChanged + restarts 100ms timer
// GetText strips trailing '\0'; prefers length without +1 first
```

## Tests Added or Updated

- No automated UI IME tests (impractical for composition).
- Manual matrix documented as **V-010** in `specs/005-notepad-syntax-editor/quickstart.md`.
- A/B: `CSTEXTEDITOR_FORCE_TEXTBOX=1` for PlainText host comparison.

## Local Verification

- Commands run: `dotnet build windows/CsTextEditor/CsTextEditor.csproj -c Debug -p:Platform=x64` → **Build succeeded**
- Manual checks: Not run in this session (requires interactive Chinese IME on desktop). User should F5 and run V-010.

## Deviations from Assessment

- Did not add `IsComposing` to `ICodeEditor` — WinUIEdit projection did not expose a clear composition-active flag in use; debounce + no sync-on-Modified is the practical equivalent.
- `IMEInteraction` set via cast `(IMEInteraction)0` (Scintilla windowed) rather than named enum member (docs only describe property).
- Did not change default host to PlainText (Alternative A) — preferred layered fix retained WinUIEdit for highlighting.

## Follow-ups

- User manual verify with 微软拼音 (empty, mid-line, mixed 中/EN, wrap on/off, MD preview on/off).
- If IME still breaks after this fix, escalate to Alternative A (default TextBox) or control upgrade.
- Consider unit tests for pure debounce helpers if extracted later.
- `/speckit-bug-test slug=ime-chinese-caret-corruption` after manual smoke.
