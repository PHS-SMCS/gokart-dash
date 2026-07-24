import React, { useCallback, useMemo, useRef, useState } from 'react';
import { NotificationContext, type Notice, type NotifyInput } from './context';

const DEFAULT_TTL = 4500;
const MAX_NOTICES = 4;

export const NotificationProvider: React.FC<{ children: React.ReactNode }> = ({ children }) => {
  const [notices, setNotices] = useState<Notice[]>([]);
  const idRef = useRef(1);
  const timersRef = useRef(new Map<number, number>());

  const dismiss = useCallback((id: number) => {
    setNotices((prev) => prev.filter((n) => n.id !== id));
    const t = timersRef.current.get(id);
    if (t !== undefined) {
      window.clearTimeout(t);
      timersRef.current.delete(id);
    }
  }, []);

  const notify = useCallback(
    (input: NotifyInput): number => {
      const id = idRef.current++;
      const notice: Notice = {
        id,
        level: input.level ?? 'info',
        title: input.title,
        message: input.message,
        ttl: input.ttl ?? DEFAULT_TTL,
        key: input.key,
        createdAt: Date.now(),
      };

      setNotices((prev) => {
        // Replace any existing notice sharing the dedupe key.
        let next = notice.key ? prev.filter((n) => n.key !== notice.key) : prev.slice();
        next.push(notice);
        if (next.length > MAX_NOTICES) next = next.slice(next.length - MAX_NOTICES);
        return next;
      });

      if (notice.ttl > 0) {
        const t = window.setTimeout(() => dismiss(id), notice.ttl);
        timersRef.current.set(id, t);
      }
      return id;
    },
    [dismiss]
  );

  const clear = useCallback(() => {
    timersRef.current.forEach((t) => window.clearTimeout(t));
    timersRef.current.clear();
    setNotices([]);
  }, []);

  const value = useMemo(
    () => ({ notices, notify, dismiss, clear }),
    [notices, notify, dismiss, clear]
  );

  return <NotificationContext.Provider value={value}>{children}</NotificationContext.Provider>;
};
