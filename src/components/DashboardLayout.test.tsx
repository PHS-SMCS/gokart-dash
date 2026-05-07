import { afterEach, describe, expect, test, vi } from 'vitest';
import { cleanup, render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { DashboardLayout } from './DashboardLayout';

// Mock useLed since LightsView calls it on mount and would attempt a fetch
// against a bridge that doesn't exist in the test environment.
vi.mock('../hooks/useLed', () => ({
  useLed: () => ({ status: 'unknown', pending: false, send: vi.fn() }),
}));

describe('DashboardLayout', () => {
  afterEach(() => cleanup());

  test('renders the Drive view by default', () => {
    render(<DashboardLayout />);
    // DriveView contains "MPH" text under the speed numeral.
    expect(screen.getByText('MPH')).toBeInTheDocument();
  });

  test('clicking Lights in the bottom dock shows the lights view', async () => {
    const user = userEvent.setup();
    render(<DashboardLayout />);

    const lightsButton = screen.getByRole('button', { name: /lights/i });
    await user.click(lightsButton);

    // LightsView's BrightnessSlider has the "Brightness" label.
    expect(await screen.findByText(/Brightness/i)).toBeInTheDocument();
  });

  test('clicking Map shows the placeholder', async () => {
    const user = userEvent.setup();
    render(<DashboardLayout />);

    const mapButton = screen.getByRole('button', { name: /map/i });
    await user.click(mapButton);

    // Placeholder renders "Coming soon".
    expect(await screen.findByText(/Coming soon/i)).toBeInTheDocument();
  });

  test('clicking System shows the placeholder', async () => {
    const user = userEvent.setup();
    render(<DashboardLayout />);

    const systemButton = screen.getByRole('button', { name: /system/i });
    await user.click(systemButton);

    expect(await screen.findByText(/Coming soon/i)).toBeInTheDocument();
  });
});
