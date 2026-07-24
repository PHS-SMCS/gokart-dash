import { createContext, useContext } from 'react';

export type NoticeLevel = 'info' | 'success' | 'warning' | 'danger';

export interface Notice {
  id: number;
  level: NoticeLevel;
  title: string;
  message?: string;
  /** ms to auto-dismiss; 0 = sticky until replaced or cleared. */
  ttl: number;
  /** Dedupe key — a new notice with the same key replaces the old one. */
  key?: string;
  createdAt: number;
}

export interface NotifyInput {
  level?: NoticeLevel;
  title: string;
  message?: string;
  ttl?: number;
  key?: string;
}

export interface NotificationContextValue {
  notices: Notice[];
  notify: (n: NotifyInput) => number;
  dismiss: (id: number) => void;
  clear: () => void;
}

export const NotificationContext = createContext<NotificationContextValue | null>(null);

export function useNotifications(): NotificationContextValue {
  const ctx = useContext(NotificationContext);
  if (!ctx) throw new Error('useNotifications must be used within NotificationProvider');
  return ctx;
}
