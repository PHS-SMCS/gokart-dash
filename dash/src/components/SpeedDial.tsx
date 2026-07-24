import React from 'react';

const SIZE = 200;
const CX = SIZE / 2;
const CY = SIZE / 2;
const R = 84;
const TRACK_W = 12;
const START_ANGLE = 135; // bottom-left
const SWEEP = 270; // to bottom-right, over the top

function polar(cx: number, cy: number, r: number, angleDeg: number) {
  const a = (angleDeg * Math.PI) / 180;
  return { x: cx + r * Math.cos(a), y: cy + r * Math.sin(a) };
}

function arcPath(r: number, startAngle: number, endAngle: number): string {
  const start = polar(CX, CY, r, startAngle);
  const end = polar(CX, CY, r, endAngle);
  const large = endAngle - startAngle <= 180 ? 0 : 1;
  return `M ${start.x} ${start.y} A ${r} ${r} 0 ${large} 1 ${end.x} ${end.y}`;
}

interface SpeedDialProps {
  /** Current speed in mph. */
  value: number;
  /** Full-scale in mph. */
  max: number;
  /** Whether a live source is feeding the dial (dims when not). */
  connected: boolean;
  children?: React.ReactNode;
}

export const SpeedDial: React.FC<SpeedDialProps> = ({ value, max, connected, children }) => {
  const clamped = Math.min(max, Math.max(0, value));
  const frac = clamped / max;
  const endAngle = START_ANGLE + SWEEP * frac;
  const nearMax = frac > 0.85;

  const majors = Array.from({ length: max / 10 + 1 }, (_, i) => i * 10);
  const minors = Array.from({ length: max / 5 + 1 }, (_, i) => i * 5).filter((v) => v % 10 !== 0);

  return (
    <div className="relative flex h-full w-full items-center justify-center">
      <svg viewBox={`0 0 ${SIZE} ${SIZE}`} className="h-full w-full max-h-full" role="img" aria-label={`${Math.round(clamped)} miles per hour`}>
        {/* track */}
        <path
          d={arcPath(R, START_ANGLE, START_ANGLE + SWEEP)}
          fill="none"
          stroke="#221c15"
          strokeWidth={TRACK_W}
          strokeLinecap="round"
        />
        {/* progress */}
        {frac > 0 ? (
          <path
            d={arcPath(R, START_ANGLE, endAngle)}
            fill="none"
            stroke={nearMax ? '#f59e0b' : connected ? '#e6ddd0' : '#4b463d'}
            strokeWidth={TRACK_W}
            strokeLinecap="round"
            style={{ transition: 'stroke 200ms linear' }}
          />
        ) : null}

        {/* minor ticks */}
        {minors.map((v) => {
          const ang = START_ANGLE + SWEEP * (v / max);
          const a = polar(CX, CY, R - TRACK_W / 2 - 3, ang);
          const b = polar(CX, CY, R - TRACK_W / 2 - 9, ang);
          return <line key={v} x1={a.x} y1={a.y} x2={b.x} y2={b.y} stroke="#4b463d" strokeWidth={1.5} />;
        })}

        {/* major ticks + labels */}
        {majors.map((v) => {
          const ang = START_ANGLE + SWEEP * (v / max);
          const a = polar(CX, CY, R - TRACK_W / 2 - 2, ang);
          const b = polar(CX, CY, R - TRACK_W / 2 - 12, ang);
          const label = polar(CX, CY, R - TRACK_W - 18, ang);
          return (
            <g key={v}>
              <line x1={a.x} y1={a.y} x2={b.x} y2={b.y} stroke="#8a8172" strokeWidth={2} />
              <text
                x={label.x}
                y={label.y}
                fill="#8a8172"
                fontSize={11}
                fontWeight={700}
                textAnchor="middle"
                dominantBaseline="central"
              >
                {v}
              </text>
            </g>
          );
        })}
      </svg>

      {/* center content (gear stack + numeric speed) */}
      <div className="pointer-events-none absolute inset-0 flex items-center justify-center">
        {children}
      </div>
    </div>
  );
};
