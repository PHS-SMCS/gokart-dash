import React, { useMemo, useState } from 'react';
import { motion } from 'framer-motion';
import {
  Clock3,
  Gauge,
  Lightbulb,
  Plug,
  Route,
  Thermometer,
  Zap,
  type LucideIcon,
} from 'lucide-react';
import { SPRING_SNAP } from '../constants/motion';

type PerformanceMode = 'SAFE' | 'TURBO' | '2FST2BVR';
type Gear = 'P' | 'R' | 'N' | 'D';

interface ThickGaugeProps {
  value: number;
  max: number;
  size: number;
  strokeWidth: number;
  centerTopText: string;
  centerBottomText: string;
  trackColor?: string;
  fillColor?: string;
  redlineSegment?: {
    startRatio: number;
    endRatio: number;
    color?: string;
  };
}

interface TelemetrySnapshot {
  speed: number;
  speedMax: number;
  rpm: number;
  rpmMax: number;
  rangeMiles: number;
  rangeProgress: number;
  motorTemp: number;
  motorTempProgress: number;
  tripDistance: string;
  tripTime: string;
  topSpeed: string;
  consumption: string;
  exteriorTemp: string;
  mileage: string;
}

const MODE_ORDER: PerformanceMode[] = ['SAFE', 'TURBO', '2FST2BVR'];
const GEAR_ORDER: Gear[] = ['P', 'R', 'N', 'D'];

const MODE_TELEMETRY: Record<PerformanceMode, TelemetrySnapshot> = {
  SAFE: {
    speed: 43,
    speedMax: 120,
    rpm: 2850,
    rpmMax: 7000,
    rangeMiles: 240,
    rangeProgress: 0.82,
    motorTemp: 182,
    motorTempProgress: 0.56,
    tripDistance: '14.2 mi',
    tripTime: '00:24:38',
    topSpeed: '58 mph',
    consumption: '9.8 kWh',
    exteriorTemp: '72°F',
    mileage: '12,482 mi',
  },
  TURBO: {
    speed: 60,
    speedMax: 120,
    rpm: 4300,
    rpmMax: 7000,
    rangeMiles: 200,
    rangeProgress: 0.68,
    motorTemp: 195,
    motorTempProgress: 0.68,
    tripDistance: '16.8 mi',
    tripTime: '00:20:52',
    topSpeed: '72 mph',
    consumption: '12.4 kWh',
    exteriorTemp: '72°F',
    mileage: '12,482 mi',
  },
  '2FST2BVR': {
    speed: 78,
    speedMax: 120,
    rpm: 5900,
    rpmMax: 7000,
    rangeMiles: 154,
    rangeProgress: 0.52,
    motorTemp: 211,
    motorTempProgress: 0.83,
    tripDistance: '19.9 mi',
    tripTime: '00:18:16',
    topSpeed: '88 mph',
    consumption: '16.7 kWh',
    exteriorTemp: '72°F',
    mileage: '12,482 mi',
  },
};

export const ThickGauge: React.FC<ThickGaugeProps> = ({
  value,
  max,
  size,
  strokeWidth,
  centerTopText,
  centerBottomText,
  trackColor = '#2a2520',
  fillColor = '#e6ddd0',
  redlineSegment,
}) => {
  const radius = (size - strokeWidth) / 2;
  const circumference = 2 * Math.PI * radius;
  const progress = Math.min(Math.max(value / max, 0), 1);
  const dashOffset = circumference * (1 - progress);

  const redlineLength = redlineSegment
    ? circumference * Math.max(0, redlineSegment.endRatio - redlineSegment.startRatio)
    : 0;

  const redlineOffset = redlineSegment ? -circumference * redlineSegment.startRatio : 0;

  return (
    <div className="relative" style={{ width: size, height: size }}>
      <svg width={size} height={size} className="-rotate-90">
        <circle
          cx={size / 2}
          cy={size / 2}
          r={radius}
          fill="none"
          stroke={trackColor}
          strokeWidth={strokeWidth}
        />

        <motion.circle
          cx={size / 2}
          cy={size / 2}
          r={radius}
          fill="none"
          stroke={fillColor}
          strokeWidth={strokeWidth}
          strokeLinecap="round"
          strokeDasharray={circumference}
          initial={{ strokeDashoffset: circumference }}
          animate={{ strokeDashoffset: dashOffset }}
          transition={SPRING_SNAP}
        />

        {redlineSegment ? (
          <circle
            cx={size / 2}
            cy={size / 2}
            r={radius}
            fill="none"
            stroke={redlineSegment.color ?? '#ef4444'}
            strokeWidth={strokeWidth}
            strokeLinecap="round"
            strokeDasharray={`${redlineLength} ${circumference}`}
            strokeDashoffset={redlineOffset}
          />
        ) : null}
      </svg>

      <div className="pointer-events-none absolute inset-0 flex flex-col items-center justify-center text-[#e6ddd0]">
        <p className="text-[clamp(5rem,10vw,7rem)] font-extrabold leading-none tracking-tighter">
          {centerTopText}
        </p>
        <p className="mt-2 text-sm font-medium uppercase tracking-[0.24em] text-gray-400">
          {centerBottomText}
        </p>
      </div>
    </div>
  );
};

interface ArcIndicatorProps {
  icon: LucideIcon;
  value: string;
  progress: number;
}

const ArcIndicator: React.FC<ArcIndicatorProps> = ({ icon: Icon, value, progress }) => {
  const clampedProgress = Math.min(Math.max(progress, 0), 1);
  const arcPath = 'M 20 56 Q 110 10 200 56';

  return (
    <div className="relative mt-5 h-[72px] w-[220px]">
      <svg viewBox="0 0 220 72" className="h-full w-full">
        <path
          d={arcPath}
          fill="none"
          stroke="#2a2520"
          strokeWidth="10"
          strokeLinecap="round"
          pathLength="1"
        />
        <motion.path
          d={arcPath}
          fill="none"
          stroke="#9a8f82"
          strokeWidth="10"
          strokeLinecap="round"
          initial={{ pathLength: 0 }}
          animate={{ pathLength: clampedProgress }}
          transition={SPRING_SNAP}
        />
      </svg>

      <div className="absolute inset-x-0 bottom-1 flex justify-center">
        <div className="flex items-center gap-2 rounded-full border border-white/10 bg-black/45 px-3 py-1 backdrop-blur-2xl">
          <Icon size={14} className="text-gray-400" />
          <span className="text-sm font-medium text-[#e6ddd0]">{value}</span>
        </div>
      </div>
    </div>
  );
};

interface TripStatRow {
  label: string;
  value: string;
  icon: LucideIcon;
}

export const TelemetryView: React.FC = () => {
  const [mode, setMode] = useState<PerformanceMode>('TURBO');
  const activeGear: Gear = 'D';

  const telemetry = MODE_TELEMETRY[mode];

  const stats = useMemo<TripStatRow[]>(
    () => [
      { label: 'Distance', value: telemetry.tripDistance, icon: Route },
      { label: 'Time', value: telemetry.tripTime, icon: Clock3 },
      { label: 'Top Speed', value: telemetry.topSpeed, icon: Gauge },
      { label: 'Consumption', value: telemetry.consumption, icon: Zap },
    ],
    [telemetry]
  );

  const cyclePerformanceMode = () => {
    const currentIndex = MODE_ORDER.indexOf(mode);
    const nextIndex = (currentIndex + 1) % MODE_ORDER.length;
    setMode(MODE_ORDER[nextIndex]);
  };

  return (
    <div className="relative h-full w-full overflow-hidden bg-[#0a0a0a]">
      <div className="pointer-events-none absolute inset-0 bg-[radial-gradient(70%_45%_at_50%_104%,rgba(190,130,78,0.22),transparent_72%)]" />

      <div className="relative z-10 grid h-full w-full grid-rows-[auto_1fr_auto] px-7 py-6">
        <div className="flex items-center justify-center">
          <div className="flex items-center gap-6 rounded-full border border-white/10 bg-black/40 px-6 py-2 backdrop-blur-2xl">
            <p className="text-sm font-medium text-gray-400">{telemetry.exteriorTemp}</p>

            <div className="flex items-center gap-3">
              {GEAR_ORDER.map((gear) => {
                const isActive = activeGear === gear;

                return (
                  <span
                    key={gear}
                    className={`text-sm font-medium tracking-[0.28em] ${
                      isActive
                        ? 'text-[#f4ede2] drop-shadow-[0_0_10px_rgba(244,237,226,0.8)]'
                        : 'text-gray-500'
                    }`}
                  >
                    {gear}
                  </span>
                );
              })}
            </div>
          </div>
        </div>

        <div className="grid h-full grid-cols-[1fr_minmax(250px,320px)_1fr] items-center gap-5">
          <section className="flex flex-col items-center justify-center">
            <ThickGauge
              value={telemetry.speed}
              max={telemetry.speedMax}
              size={355}
              strokeWidth={38}
              centerTopText={String(telemetry.speed)}
              centerBottomText="mph"
            />

            <ArcIndicator icon={Plug} value={`${telemetry.rangeMiles} mi`} progress={telemetry.rangeProgress} />
          </section>

          <section className="flex h-full items-center justify-center">
            <div className="w-full rounded-3xl border border-white/10 bg-black/40 p-6 backdrop-blur-2xl">
              <p className="text-xs font-medium uppercase tracking-[0.26em] text-gray-400">Current Trip</p>

              <div className="mt-5 space-y-4">
                {stats.map((stat) => {
                  const Icon = stat.icon;

                  return (
                    <div key={stat.label} className="flex items-center justify-between">
                      <div className="flex items-center gap-2.5 text-gray-400">
                        <Icon size={16} />
                        <span className="text-sm font-medium">{stat.label}</span>
                      </div>
                      <span className="text-sm font-semibold text-white">{stat.value}</span>
                    </div>
                  );
                })}
              </div>
            </div>
          </section>

          <motion.button
            type="button"
            whileTap={{ scale: 0.98 }}
            transition={SPRING_SNAP}
            onClick={cyclePerformanceMode}
            className="flex h-full touch-manipulation select-none flex-col items-center justify-center"
          >
            <ThickGauge
              value={telemetry.rpm}
              max={telemetry.rpmMax}
              size={355}
              strokeWidth={38}
              centerTopText={activeGear}
              centerBottomText={mode}
              redlineSegment={{ startRatio: 0.05, endRatio: 0.12, color: '#ef4444' }}
            />

            <ArcIndicator
              icon={Thermometer}
              value={`${telemetry.motorTemp}°F`}
              progress={telemetry.motorTempProgress}
            />
          </motion.button>
        </div>

        <div className="flex items-center justify-center">
          <div className="flex items-center gap-2 rounded-full border border-white/10 bg-black/40 px-3 py-1.5 backdrop-blur-2xl">
            <Lightbulb size={14} className="text-emerald-400" />
            <span className="text-xs font-medium uppercase tracking-[0.18em] text-emerald-400">High Beam</span>
            <span className="ml-2 rounded-full bg-black/60 px-2.5 py-0.5 text-xs font-medium text-gray-300">
              {telemetry.mileage}
            </span>
          </div>
        </div>
      </div>
    </div>
  );
};
