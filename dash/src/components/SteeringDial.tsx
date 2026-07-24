import React from 'react';

// Shared dial footprint (identical to BatteryDial so the two render the same size).
const W = 140;
const H = 118;
const CX = 70;
const CY = 66;
const R = 50;
const TRACK = 8;

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

// Screen coords (y down): 180° = left, 270° = straight-up (0°), 360° = right.
function needleAngle(deg: number, max: number) {
  return 270 + (deg / max) * 90;
}

interface SteeringDialProps {
  /** Measured steering angle (deg); negative = left, positive = right. */
  angleDeg: number;
  /** Commanded setpoint (deg) — drawn as a faint marker. */
  setpointDeg?: number;
  /** ± full-lock in degrees. */
  max: number;
  connected: boolean;
}

export const SteeringDial: React.FC<SteeringDialProps> = ({
  angleDeg,
  setpointDeg,
  max,
  connected,
}) => {
  const clamp = (d: number) => Math.max(-max, Math.min(max, d));
  const a = clamp(angleDeg);
  const rounded = Math.round(a);
  const tip = polar(CX, CY, R - 9, needleAngle(a, max));
  const setA = setpointDeg == null ? null : needleAngle(clamp(setpointDeg), max);
  const setPt = setA == null ? null : polar(CX, CY, R - TRACK / 2 - 1, setA);
  const setPtInner = setA == null ? null : polar(CX, CY, R - TRACK / 2 - 8, setA);

  const stroke = connected ? '#e6ddd0' : '#4b463d';
  const label = rounded === 0 ? '0°' : `${Math.abs(rounded)}° ${rounded < 0 ? 'L' : 'R'}`;

  return (
    <svg viewBox={`0 0 ${W} ${H}`} className="h-full w-auto" role="img" aria-label={`Steering ${label}`}>
      {/* track */}
      <path d={arcPath(R, 180, 360)} fill="none" stroke="#221c15" strokeWidth={TRACK} strokeLinecap="round" />

      {/* reference ticks: full-left, center, full-right */}
      {[180, 270, 360].map((ang) => {
        const o = polar(CX, CY, R - TRACK / 2 - 1, ang);
        const i = polar(CX, CY, R - TRACK / 2 - (ang === 270 ? 8 : 6), ang);
        return <line key={ang} x1={o.x} y1={o.y} x2={i.x} y2={i.y} stroke="#8a8172" strokeWidth={ang === 270 ? 2 : 1.5} />;
      })}

      {/* setpoint marker */}
      {setPt && setPtInner ? (
        <line x1={setPt.x} y1={setPt.y} x2={setPtInner.x} y2={setPtInner.y} stroke="#22d3ee" strokeWidth={2} opacity={0.7} />
      ) : null}

      {/* needle + hub */}
      <line x1={CX} y1={CY} x2={tip.x} y2={tip.y} stroke={stroke} strokeWidth={3} strokeLinecap="round" style={{ transition: 'stroke 200ms linear' }} />
      <circle cx={CX} cy={CY} r={4} fill={stroke} />

      {/* readout */}
      <text x={CX} y={98} textAnchor="middle" fontSize={16} fontWeight={800} fill={connected ? '#ffffff' : '#4b463d'} className="tabular-nums">
        {label}
      </text>
      <text x={CX} y={111} textAnchor="middle" fontSize={7} fontWeight={700} letterSpacing={1.5} fill="#8a8172">
        STEER
      </text>
    </svg>
  );
};
