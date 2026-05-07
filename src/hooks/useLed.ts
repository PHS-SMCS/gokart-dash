import { useCallback, useEffect, useRef, useState } from 'react';
import type { RGB } from '../lib/color';

export type { RGB };

export type BridgeStatus = 'unknown' | 'ok' | 'error';

const BRIDGE_BASE = `http://${window.location.hostname}:5174`;

async function postLed(rgb: RGB, signal?: AbortSignal): Promise<void> {
  const res = await fetch(`${BRIDGE_BASE}/api/led`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(rgb),
    signal,
  });
  if (!res.ok) {
    throw new Error(`bridge ${res.status}`);
  }
  const body = await res.json();
  if (!body.ok) {
    throw new Error(body.error ?? 'bridge error');
  }
}

/**
 * Imperative LED controller. The Teensy state is the source of truth; this
 * hook just sends edits, with a small debounce so a slider drag doesn't
 * flood the serial line.
 */
export function useLed(debounceMs = 80) {
  const [status, setStatus] = useState<BridgeStatus>('unknown');
  const [pending, setPending] = useState(false);
  const queuedRef = useRef<RGB | null>(null);
  const inflightRef = useRef<AbortController | null>(null);
  const timerRef = useRef<number | null>(null);

  // Initial health probe so the UI can show the bridge state.
  useEffect(() => {
    let cancelled = false;
    fetch(`${BRIDGE_BASE}/api/health`)
      .then((r) => r.json())
      .then(() => {
        if (!cancelled) setStatus('ok');
      })
      .catch(() => {
        if (!cancelled) setStatus('error');
      });
    return () => {
      cancelled = true;
      if (timerRef.current !== null) {
        window.clearTimeout(timerRef.current);
      }
      inflightRef.current?.abort();
    };
  }, []);

  const flush = useCallback(async () => {
    timerRef.current = null;
    const target = queuedRef.current;
    if (!target) return;
    queuedRef.current = null;

    inflightRef.current?.abort();
    const ctrl = new AbortController();
    inflightRef.current = ctrl;
    setPending(true);
    try {
      await postLed(target, ctrl.signal);
      setStatus('ok');
    } catch (err) {
      if ((err as Error).name !== 'AbortError') {
        setStatus('error');
      }
    } finally {
      setPending(false);
      // If something newer was queued during the request, flush again.
      if (queuedRef.current) {
        timerRef.current = window.setTimeout(flush, debounceMs);
      }
    }
  }, [debounceMs]);

  const send = useCallback(
    (rgb: RGB) => {
      queuedRef.current = rgb;
      if (timerRef.current === null) {
        timerRef.current = window.setTimeout(flush, debounceMs);
      }
    },
    [debounceMs, flush]
  );

  return { status, pending, send };
}
