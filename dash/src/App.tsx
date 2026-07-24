import React, { useEffect } from 'react';
import { TelemetryProvider } from './telemetry/TelemetryProvider';
import { NotificationProvider } from './notifications/NotificationProvider';
import { NotificationHost } from './notifications/NotificationHost';
import { DashboardLayout } from './components/DashboardLayout';
import { applyCursorHidden, getCursorHidden } from './settings/cursor';

const App: React.FC = () => {
  // Apply the persisted cursor preference on startup (default = hidden/kiosk).
  useEffect(() => {
    applyCursorHidden(getCursorHidden());
  }, []);

  return (
    <TelemetryProvider>
      <NotificationProvider>
        <DashboardLayout />
        <NotificationHost />
      </NotificationProvider>
    </TelemetryProvider>
  );
};

export default App;
