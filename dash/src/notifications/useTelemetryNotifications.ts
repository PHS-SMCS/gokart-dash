import { useEffect, useRef } from 'react';
import type { Telemetry, DriveState, ContactorPhase } from '../telemetry/types';
import { faultName, gearSelector } from '../telemetry/types';
import { useNotifications } from './context';

const GEAR_WORD: Record<string, string> = {
  P: 'Park',
  L: 'Low speed',
  M: 'Middle speed',
  H: 'High speed',
  R: 'Reverse',
};

const STATE_TITLE: Record<DriveState, string> = {
  SAFE: 'Safe',
  ARMED: 'Armed',
  DRIVE: 'Drive engaged',
  STOPPING: 'Controlled stop',
  FAULT: 'Fault',
};

interface Prev {
  init: boolean;
  driveState: DriveState;
  faultCode: number;
  contactor: ContactorPhase;
  wheelConnected: boolean;
  steerLinkOk: boolean;
  steerCalibrated: boolean;
  linkConnected: boolean;
  gearSel: string;
}

/**
 * Watches telemetry and raises UI notifications on meaningful transitions:
 * precharge/contactor sequencing, arm/drive/stop, faults, wheel + steer link,
 * and connection changes. Purely derived — no polling, no side effects on the
 * kart. Mount once, near the app root.
 */
export function useTelemetryNotifications(t: Telemetry): void {
  const { notify } = useNotifications();
  const prev = useRef<Prev | null>(null);

  useEffect(() => {
    const gearSel = gearSelector(t);
    const p = prev.current;

    if (!p) {
      prev.current = {
        init: true,
        driveState: t.driveState,
        faultCode: t.faultCode,
        contactor: t.contactor,
        wheelConnected: t.wheelConnected,
        steerLinkOk: t.steerLinkOk,
        steerCalibrated: t.steerCalibrated,
        linkConnected: t.link.connected,
        gearSel,
      };
      return;
    }

    // Connection up/down.
    if (t.link.connected !== p.linkConnected) {
      if (t.link.connected) {
        notify({
          level: 'success',
          title: t.link.source === 'sim' ? 'Simulator active' : 'Telemetry connected',
          message:
            t.link.source === 'sim'
              ? 'Feeding simulated data from the System tab.'
              : `Live feed at ${Math.round(t.link.hz)} Hz.`,
          key: 'link',
        });
      } else {
        notify({
          level: 'warning',
          title: 'Telemetry lost',
          message: 'No frames from the kart bridge.',
          key: 'link',
          ttl: 0,
        });
      }
    }

    // Contactor sequencing.
    if (t.contactor !== p.contactor) {
      const map: Record<ContactorPhase, { level: 'info' | 'success' | 'warning' | 'danger'; title: string; msg?: string }> = {
        PRECHARGE: { level: 'info', title: 'Precharging bus…', msg: 'Resistor energized — closing contactor shortly.' },
        CLOSED: { level: 'success', title: 'Contactor closed', msg: 'HV bus live.' },
        OPEN: { level: 'info', title: 'Contactor open', msg: 'HV bus isolated.' },
        FAULT: { level: 'danger', title: 'Contactor fault', msg: 'Precharge/sequencer aborted — inspect before retry.' },
      };
      const m = map[t.contactor];
      notify({ level: m.level, title: m.title, message: m.msg, key: 'contactor' });
    }

    // Drive-state machine.
    if (t.driveState !== p.driveState) {
      const level =
        t.driveState === 'FAULT'
          ? 'danger'
          : t.driveState === 'STOPPING'
            ? 'warning'
            : t.driveState === 'DRIVE'
              ? 'success'
              : 'info';
      notify({
        level,
        title: STATE_TITLE[t.driveState],
        message:
          t.driveState === 'DRIVE'
            ? `${GEAR_WORD[gearSel] ?? ''}${t.benchMode ? ' · bench' : ''}`
            : undefined,
        key: 'drivestate',
      });
    }

    // Gear / selector changes while driving.
    if (gearSel !== p.gearSel && t.driveState === 'DRIVE') {
      notify({ level: 'info', title: GEAR_WORD[gearSel] ?? gearSel, key: 'gear', ttl: 2500 });
    }

    // Faults.
    if (t.faultCode !== p.faultCode) {
      if (t.faultCode > 0) {
        notify({ level: 'danger', title: `Fault: ${faultName(t.faultCode)}`, message: `Code ${t.faultCode}.`, key: 'fault', ttl: 0 });
      } else {
        notify({ level: 'success', title: 'Fault cleared', key: 'fault' });
      }
    }

    // Wheel + steering link.
    if (t.wheelConnected !== p.wheelConnected) {
      notify(
        t.wheelConnected
          ? { level: 'success', title: 'Wheel connected', key: 'wheel' }
          : { level: 'warning', title: 'Wheel disconnected', key: 'wheel' }
      );
    }
    if (t.steerLinkOk !== p.steerLinkOk) {
      notify(
        t.steerLinkOk
          ? { level: 'success', title: 'Steering link up', key: 'steerlink' }
          : { level: 'warning', title: 'Steering link down', key: 'steerlink' }
      );
    }
    if (t.steerCalibrated !== p.steerCalibrated && t.steerCalibrated) {
      notify({ level: 'success', title: 'Steering calibrated', key: 'steercal' });
    }

    prev.current = {
      init: true,
      driveState: t.driveState,
      faultCode: t.faultCode,
      contactor: t.contactor,
      wheelConnected: t.wheelConnected,
      steerLinkOk: t.steerLinkOk,
      steerCalibrated: t.steerCalibrated,
      linkConnected: t.link.connected,
      gearSel,
    };
  }, [t, notify]);
}
