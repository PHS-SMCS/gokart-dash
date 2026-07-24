import React, { useState } from 'react';
import { Activity, Radio, RotateCcw, Bell, MousePointer2 } from 'lucide-react';
import { useTelemetryControl } from '../telemetry/context';
import { useNotifications } from '../notifications/context';
import type { TelemetrySource, DriveState, SpeedGear, ContactorPhase } from '../telemetry/types';
import { telemetryWsUrl } from '../telemetry/wire';
import { getCursorHidden, setCursorHidden } from '../settings/cursor';

// --- small control primitives -------------------------------------------------

function Segmented<T extends string | number>({
  options,
  value,
  onChange,
  disabled,
}: {
  options: { value: T; label: string }[];
  value: T;
  onChange: (v: T) => void;
  disabled?: boolean;
}) {
  return (
    <div className={`flex overflow-hidden rounded-lg border border-white/10 ${disabled ? 'opacity-40' : ''}`}>
      {options.map((o) => (
        <button
          key={String(o.value)}
          type="button"
          disabled={disabled}
          onClick={() => onChange(o.value)}
          className={`flex-1 px-2 py-1.5 text-[11px] font-bold uppercase tracking-[0.1em] transition-colors ${
            o.value === value ? 'bg-[#e6ddd0] text-black' : 'bg-black/30 text-gray-400'
          }`}
        >
          {o.label}
        </button>
      ))}
    </div>
  );
}

const Toggle: React.FC<{ label: string; value: boolean; onChange: (v: boolean) => void; disabled?: boolean }> = ({
  label,
  value,
  onChange,
  disabled,
}) => (
  <button
    type="button"
    disabled={disabled}
    onClick={() => onChange(!value)}
    className={`flex items-center justify-between gap-2 rounded-lg border px-2.5 py-1.5 ${
      disabled ? 'opacity-40' : ''
    } ${value ? 'border-emerald-400/40 bg-emerald-400/10' : 'border-white/10 bg-black/30'}`}
  >
    <span className="text-[11px] font-semibold text-gray-300">{label}</span>
    <span
      className={`relative h-4 w-7 rounded-full transition-colors ${value ? 'bg-emerald-400' : 'bg-white/15'}`}
    >
      <span
        className={`absolute top-0.5 h-3 w-3 rounded-full bg-white transition-all ${value ? 'left-3.5' : 'left-0.5'}`}
      />
    </span>
  </button>
);

const Slider: React.FC<{
  label: string;
  value: number;
  min: number;
  max: number;
  step?: number;
  unit?: string;
  onChange: (v: number) => void;
  disabled?: boolean;
}> = ({ label, value, min, max, step = 1, unit, onChange, disabled }) => (
  <div className={disabled ? 'opacity-40' : ''}>
    <div className="flex items-center justify-between text-[10px] font-semibold uppercase tracking-[0.14em] text-gray-400">
      <span>{label}</span>
      <span className="tabular-nums text-white">
        {value}
        {unit ? <span className="ml-0.5 text-gray-500">{unit}</span> : null}
      </span>
    </div>
    <input
      type="range"
      min={min}
      max={max}
      step={step}
      value={value}
      disabled={disabled}
      onChange={(e) => onChange(Number(e.target.value))}
      className="mt-1 h-6 w-full appearance-none bg-transparent
        [&::-webkit-slider-runnable-track]:h-1.5 [&::-webkit-slider-runnable-track]:rounded-full [&::-webkit-slider-runnable-track]:bg-[#221c15]
        [&::-webkit-slider-thumb]:appearance-none [&::-webkit-slider-thumb]:h-5 [&::-webkit-slider-thumb]:w-5 [&::-webkit-slider-thumb]:-mt-1.5
        [&::-webkit-slider-thumb]:rounded-full [&::-webkit-slider-thumb]:bg-[#e6ddd0] [&::-webkit-slider-thumb]:border-2 [&::-webkit-slider-thumb]:border-black/40"
    />
  </div>
);

const Section: React.FC<{ title: string; children: React.ReactNode; right?: React.ReactNode }> = ({
  title,
  children,
  right,
}) => (
  <div className="rounded-xl border border-white/5 bg-black/30 p-3">
    <div className="mb-2 flex items-center justify-between">
      <h3 className="text-[10px] font-bold uppercase tracking-[0.24em] text-gray-500">{title}</h3>
      {right}
    </div>
    {children}
  </div>
);

// --- view ---------------------------------------------------------------------

const SOURCE_OPTS: { value: TelemetrySource; label: string }[] = [
  { value: 'none', label: 'Off' },
  { value: 'live', label: 'Live' },
  { value: 'sim', label: 'Sim' },
];

export const SystemView: React.FC = () => {
  const { telemetry: t, source, setSource, sim, setSim, resetSim } = useTelemetryControl();
  const { notify } = useNotifications();
  const simOn = source === 'sim';
  const [hideCursor, setHideCursor] = useState(getCursorHidden);

  const toggleCursor = (v: boolean) => {
    setHideCursor(v);
    setCursorHidden(v);
  };

  return (
    <div className="scroll-y h-full w-full p-3">
      <div className="grid grid-cols-2 gap-3">
        {/* Source + diagnostics */}
        <Section
          title="Telemetry Source"
          right={
            <span
              className={`flex items-center gap-1 text-[10px] font-semibold uppercase tracking-[0.14em] ${
                t.link.connected ? 'text-emerald-400' : 'text-gray-500'
              }`}
            >
              <Radio size={11} />
              {t.link.connected ? `${Math.round(t.link.hz)} Hz` : 'idle'}
            </span>
          }
        >
          <Segmented options={SOURCE_OPTS} value={source} onChange={setSource} />
          <div className="mt-2 grid grid-cols-2 gap-x-3 gap-y-1 text-[11px]">
            <Diag label="Source" value={t.link.source} />
            <Diag label="Rate" value={`${Math.round(t.link.hz)} Hz`} />
            <Diag label="Frame age" value={t.link.frameAgeMs == null ? '—' : `${Math.round(t.link.frameAgeMs)} ms`} />
            <Diag label="Seq" value={String(t.seq)} />
            <Diag label="Uptime" value={`${(t.uptimeMs / 1000).toFixed(1)} s`} />
            <Diag label="State" value={t.driveState} />
          </div>
          {source === 'live' ? (
            <p className="mt-2 break-all text-[9px] text-gray-600">{telemetryWsUrl()}</p>
          ) : null}
        </Section>

        {/* Notification tests */}
        <Section title="Notifications" right={<Bell size={12} className="text-gray-500" />}>
          <p className="mb-2 text-[10px] leading-snug text-gray-500">
            Fire a test toast. Live notifications are raised automatically from telemetry transitions.
          </p>
          <div className="grid grid-cols-2 gap-1.5">
            <TestBtn onClick={() => notify({ level: 'info', title: 'Precharging bus…', message: 'Resistor energized.' })} className="border-sky-400/30 text-sky-300">Info</TestBtn>
            <TestBtn onClick={() => notify({ level: 'success', title: 'Contactor closed', message: 'HV bus live.' })} className="border-emerald-400/30 text-emerald-300">Success</TestBtn>
            <TestBtn onClick={() => notify({ level: 'warning', title: 'Wheel disconnected' })} className="border-amber-400/40 text-amber-300">Warning</TestBtn>
            <TestBtn onClick={() => notify({ level: 'danger', title: 'Fault: Pedal implausible', ttl: 0 })} className="border-red-500/50 text-red-300">Danger</TestBtn>
          </div>
        </Section>

        {/* Display */}
        <Section title="Display" right={<MousePointer2 size={12} className="text-gray-500" />}>
          <Toggle label="Hide cursor (kiosk)" value={hideCursor} onChange={toggleCursor} />
          <p className="mt-2 text-[10px] leading-snug text-gray-500">
            On the kart the dash runs fullscreen with no pointer. Turn this off to show the mouse
            cursor while debugging on a workstation. Saved on this device.
          </p>
        </Section>

        {/* Simulator — drive */}
        <Section title="Simulate · Drive" right={<Activity size={12} className={simOn ? 'text-emerald-400' : 'text-gray-600'} />}>
          {!simOn ? (
            <p className="mb-2 text-[10px] text-amber-300/80">Select “Sim” above to drive these controls.</p>
          ) : null}
          <div className="flex flex-col gap-2">
            <Segmented<DriveState>
              disabled={!simOn}
              options={[
                { value: 'SAFE', label: 'Safe' },
                { value: 'ARMED', label: 'Armed' },
                { value: 'DRIVE', label: 'Drive' },
                { value: 'STOPPING', label: 'Stop' },
                { value: 'FAULT', label: 'Fault' },
              ]}
              value={sim.driveState}
              onChange={(v) => setSim({ driveState: v })}
            />
            <Segmented<SpeedGear>
              disabled={!simOn}
              options={[
                { value: 'LOW', label: 'Low' },
                { value: 'MED', label: 'Mid' },
                { value: 'HIGH', label: 'High' },
              ]}
              value={sim.gear}
              onChange={(v) => setSim({ gear: v })}
            />
            <Slider label="Speed" min={0} max={40} unit="mph" value={sim.speedMph} onChange={(v) => setSim({ speedMph: v })} disabled={!simOn} />
            <Slider label="Throttle" min={0} max={100} unit="%" value={sim.throttlePct} onChange={(v) => setSim({ throttlePct: v })} disabled={!simOn} />
            <Slider label="Brake" min={0} max={100} unit="%" value={sim.brakePct} onChange={(v) => setSim({ brakePct: v })} disabled={!simOn} />
            <Slider label="Fault code" min={0} max={8} value={sim.faultCode} onChange={(v) => setSim({ faultCode: v })} disabled={!simOn} />
            <Toggle label="Reverse" value={sim.reverse} onChange={(v) => setSim({ reverse: v })} disabled={!simOn} />
          </div>
        </Section>

        {/* Simulator — systems */}
        <Section title="Simulate · Systems">
          <div className="flex flex-col gap-2">
            <div>
              <p className="mb-1 text-[10px] font-semibold uppercase tracking-[0.14em] text-gray-500">Contactor</p>
              <Segmented<ContactorPhase>
                disabled={!simOn}
                options={[
                  { value: 'OPEN', label: 'Open' },
                  { value: 'PRECHARGE', label: 'Pre' },
                  { value: 'CLOSED', label: 'Closed' },
                  { value: 'FAULT', label: 'Fault' },
                ]}
                value={sim.contactor}
                onChange={(v) => setSim({ contactor: v })}
              />
            </div>
            <div className="grid grid-cols-2 gap-1.5">
              <Toggle label="Wheel" value={sim.wheelConnected} onChange={(v) => setSim({ wheelConnected: v })} disabled={!simOn} />
              <Toggle label="Steer link" value={sim.steerLinkOk} onChange={(v) => setSim({ steerLinkOk: v })} disabled={!simOn} />
              <Toggle label="Steer cal" value={sim.steerCalibrated} onChange={(v) => setSim({ steerCalibrated: v })} disabled={!simOn} />
              <Toggle label="ESC link" value={sim.escLinkOk} onChange={(v) => setSim({ escLinkOk: v })} disabled={!simOn} />
              <Toggle label="Bench" value={sim.benchMode} onChange={(v) => setSim({ benchMode: v })} disabled={!simOn} />
            </div>
            <Slider label="Steer set" min={-30} max={30} unit="°" value={sim.steerSetpointDeg} onChange={(v) => setSim({ steerSetpointDeg: v })} disabled={!simOn} />
            <Slider label="Motor temp" min={0} max={140} unit="°C" value={sim.motorTempC ?? 0} onChange={(v) => setSim({ motorTempC: v })} disabled={!simOn} />
            <Slider label="Batt volts" min={60} max={90} step={0.1} unit="V" value={sim.battVolts ?? 0} onChange={(v) => setSim({ battVolts: v })} disabled={!simOn} />
            <Slider label="Batt amps" min={0} max={300} unit="A" value={sim.battAmps ?? 0} onChange={(v) => setSim({ battAmps: v })} disabled={!simOn} />
          </div>
        </Section>

        {/* Simulator — position */}
        <Section
          title="Simulate · Position"
          right={
            <button
              type="button"
              onClick={resetSim}
              disabled={!simOn}
              className={`flex items-center gap-1 text-[10px] font-semibold uppercase tracking-[0.14em] ${simOn ? 'text-gray-400' : 'text-gray-700'}`}
            >
              <RotateCcw size={11} />
              Reset
            </button>
          }
        >
          <div className="flex flex-col gap-2">
            <Toggle label="GPS fix" value={sim.gpsFix} onChange={(v) => setSim({ gpsFix: v })} disabled={!simOn} />
            <Slider label="Heading" min={0} max={359} unit="°" value={sim.gpsHeadingDeg} onChange={(v) => setSim({ gpsHeadingDeg: v })} disabled={!simOn} />
            <Slider label="Sats" min={0} max={20} value={sim.gpsSats} onChange={(v) => setSim({ gpsSats: v })} disabled={!simOn} />
            <div className="grid grid-cols-2 gap-2">
              <NumberField label="Lat" value={sim.gpsLat} step={0.0005} onChange={(v) => setSim({ gpsLat: v })} disabled={!simOn} />
              <NumberField label="Lon" value={sim.gpsLon} step={0.0005} onChange={(v) => setSim({ gpsLon: v })} disabled={!simOn} />
            </div>
          </div>
        </Section>
      </div>
    </div>
  );
};

const Diag: React.FC<{ label: string; value: string }> = ({ label, value }) => (
  <div className="flex items-center justify-between border-b border-white/5 py-0.5">
    <span className="text-gray-500">{label}</span>
    <span className="tabular-nums text-gray-200">{value}</span>
  </div>
);

const TestBtn: React.FC<{ onClick: () => void; className?: string; children: React.ReactNode }> = ({
  onClick,
  className = '',
  children,
}) => (
  <button
    type="button"
    onClick={onClick}
    className={`rounded-lg border bg-black/30 px-2 py-1.5 text-[11px] font-bold uppercase tracking-[0.1em] ${className}`}
  >
    {children}
  </button>
);

const NumberField: React.FC<{
  label: string;
  value: number;
  step?: number;
  onChange: (v: number) => void;
  disabled?: boolean;
}> = ({ label, value, step = 1, onChange, disabled }) => (
  <label className={`flex flex-col gap-1 ${disabled ? 'opacity-40' : ''}`}>
    <span className="text-[10px] font-semibold uppercase tracking-[0.14em] text-gray-500">{label}</span>
    <input
      type="number"
      step={step}
      value={value}
      disabled={disabled}
      onChange={(e) => onChange(Number(e.target.value))}
      className="rounded-lg border border-white/10 bg-black/40 px-2 py-1 text-xs tabular-nums text-white outline-none focus:border-white/30"
    />
  </label>
);
