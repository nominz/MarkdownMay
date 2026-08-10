# Design QA

Source visual: user-provided Windows editor screenshots for compact paragraph-style dropdown and rounded popup menu.

Implementation target: native Win32 toolbar and find/replace panel.

## Automated/structural checks

- Find/replace buttons now receive commands through the panel host.
- Find-next selects the expected source range; close and Ctrl+F toggle are covered by integration tests.
- Paragraph style uses a compact owner-drawn dropdown with rounded clipped field and widened popup.
- Find/replace operation buttons are DPI-aware owner-drawn rounded buttons.
- Toolbar groups are separated; Find is enabled; right-side Settings uses a disabled gear command.

## Visual comparison

Native-window capture through the Computer Use runtime is unavailable in this environment (`EPERM` while initializing `@oai/sky`). The standard Release executable is also held open by an older no-window process, so a same-state post-build screenshot cannot be captured without ending that process.

final result: blocked

Blocking follow-up: close the existing MarkdownMay process, rebuild/copy the verified Release to the standard output path, capture the paragraph dropdown closed/open and find panel states, then compare at the same 896×799 viewport.
