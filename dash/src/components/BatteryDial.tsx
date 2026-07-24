import React from 'react';

// Shared dial footprint (identical to SteeringDial so the two render the same size).
const W = 140;
const H = 118;
const CX = 70;
const CY = 66;
const R = 50;
const TRACK = 8;
const START_ANGLE = 135; // bottom-left
const SWEEP = 270; // to bottom-right, over the top

// Scales (per spec): voltage 60–90 V, current 0–300 A.
const VOLT_MIN = 60;
const VOLT_MAX = 90;
const AMP_MIN = 0;
const AMP_MAX = 300;

function polar(cx: number, cy: number, r: number, deg: number) {
  const a = (deg * Math.PI) / 180;
  return { x: cx + r * Math.cos(a), y: cy + r * Math.sin(a) };
}

function arcPath(r: number, a0: number, a1: number): string {
  const s = polar(CX, CY, r, a0);
  const e = polar(CX, CY, r, a1);
  const large = a1 - a0 <= 180 ? 0 : 1;
  return `M ${s.x} ${s.y} A ${r} ${r} 0 ${large} 1 ${e.x} ${e.y}`;
}

function frac(v: number, min: number, max: number) {
  return Math.min(1, Math.max(0, (v - min) / (max - min)));
}

function needleTip(value: number, min: number, max: number, len: number) {
  return polar(CX, CY, len, START_ANGLE + SWEEP * frac(value, min, max));
}

interface BatteryDialProps {
  /** Pack voltage (V), or null when unknown. */
  volts: number | null;
  /** Pack current (A), or null when unknown. */
  amps: number | null;
  connected: boolean;
}

/**
 * Single dial with two needles sharing one 270° sweep: voltage (blue,
 * 60–90 V) and current (red, 0–300 A). No edge ticks; the numeric readouts
 * are printed below inside the lower gap. The current needle is drawn shorter
 * so the two stay distinguishable when they overlap.
 */
export const BatteryDial: React.FC<BatteryDialProps> = ({ volts, amps, connected }) => {
  const blue = connected ? '#60a5fa' : '#39414f';
  const red = connected ? '#f87171' : '#4a3838';

  const vTip = volts == null ? null : needleTip(volts, VOLT_MIN, VOLT_MAX, R - 6);
  const aTip = amps == null ? null : needleTip(amps, AMP_MIN, AMP_MAX, R - 15);

  return (
    <svg viewBox={`0 0 ${W} ${H}`} className="h-full w-auto" role="img" aria-label="Pack voltage and current">
      {/* track */}
      <path d={arcPath(R, START_ANGLE, START_ANGLE + SWEEP)} fill="none" stroke="#221c15" strokeWidth={TRACK} strokeLinecap="round" />

      {/* current needle (shorter, drawn first so voltage sits on top) */}
      {aTip ? (
        <line x1={CX} y1={CY} x2={aTip.x} y2={aTip.y} stroke={red} strokeWidth={3} strokeLinecap="round" style={{ transition: 'stroke 200ms linear' }} />
      ) : null}
      {/* voltage needle */}
      {vTip ? (
        <line x1={CX} y1={CY} x2={vTip.x} y2={vTip.y} stroke={blue} strokeWidth={3} strokeLinecap="round" style={{ transition: 'stroke 200ms linear' }} />
      ) : null}
      <circle cx={CX} cy={CY} r={4} fill="#8a8172" />

      {/* readouts, stacked in the lower gap */}
      <text x={CX} y={93} textAnchor="middle" fontSize={12} fontWeight={800} fill={connected ? blue : '#6b7280'} className="tabular-nums">
        {volts == null ? '—' : volts.toFixed(1)} V
      </text>
      <text x={CX} y={108} textAnchor="middle" fontSize={12} fontWeight={800} fill={connected ? red : '#6b7280'} className="tabular-nums">
        {amps == null ? '—' : Math.round(amps)} A
      </text>
    </svg>
  );
};
