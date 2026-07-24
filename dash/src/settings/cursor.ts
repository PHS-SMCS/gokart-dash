// Cursor visibility. On the kart the dash runs as a fullscreen kiosk with no
// pointer, so the cursor must be hidden. On a workstation it's needed for
// debugging. The choice is persisted and applied via a `data-hide-cursor`
// attribute on <html>, which `index.css` keys off. Default = hidden (kiosk).

const KEY = 'dash.hideCursor';

export function getCursorHidden(): boolean {
  try {
    const v = localStorage.getItem(KEY);
    return v === null ? true : v === 'true';
  } catch {
    return true;
  }
}

export function applyCursorHidden(hidden: boolean): void {
  document.documentElement.dataset.hideCursor = String(hidden);
}

export function setCursorHidden(hidden: boolean): void {
  try {
    localStorage.setItem(KEY, String(hidden));
  } catch {
    /* private mode / no storage — still apply for this session */
  }
  applyCursorHidden(hidden);
}
