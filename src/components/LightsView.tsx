import React, { useCallback, useMemo, useState } from 'react';
import { motion } from 'framer-motion';
import { Power, WifiOff } from 'lucide-react';
import { LIGHT_PRESETS } from '../constants/lightPresets';
import { SPRING_SNAP } from '../constants/motion';
import { contrastColor, rgbToCss, scale } from '../lib/color';
import { useLed, type BridgeStatus } from '../hooks/useLed';

const DEFAULT_PRESET_ID = 'white';
const DEFAULT_BRIGHTNESS = 80;

export const LightsView: React.FC = () => {
  const [presetId, setPresetId] = useState<string>(DEFAULT_PRESET_ID);
  const [brightness, setBrightness] = useState<number>(DEFAULT_BRIGHTNESS);
  const [on, setOn] = useState<boolean>(false);
  const { status, pending, send } = useLed();

  const preset = useMemo(
    () => LIGHT_PRESETS.find((p) => p.id === presetId) ?? LIGHT_PRESETS[0],
    [presetId]
  );

  const apply = useCallback(
    (next: { presetId?: string; brightness?: number; on?: boolean }) => {
      const nextPreset =
        LIGHT_PRESETS.find((p) => p.id === (next.presetId ?? presetId)) ?? preset;
      const nextBrightness = next.brightness ?? brightness;
      const nextOn = next.on ?? on;
      send(nextOn ? scale(nextPreset.color, nextBrightness) : { r: 0, g: 0, b: 0 });
    },
    [brightness, on, preset, presetId, send]
  );

  const onPickPreset = (id: string) => {
    setPresetId(id);
    if (!on) setOn(true);
    apply({ presetId: id, on: true });
  };

  const onBrightnessChange = (value: number) => {
    setBrightness(value);
    if (on) apply({ brightness: value });
  };

  const onTogglePower = () => {
    const nextOn = !on;
    setOn(nextOn);
    apply({ on: nextOn });
  };

  return (
    <div className="flex h-full w-full flex-col gap-3 p-3">
      <Header status={status} pending={pending} />

      <PresetGrid
        presets={LIGHT_PRESETS}
        activeId={presetId}
        onPick={onPickPreset}
        previewBrightness={on ? brightness : 30}
      />

      <BrightnessSlider value={brightness} onChange={onBrightnessChange} disabled={!on} />

      <PowerButton on={on} onToggle={onTogglePower} />
    </div>
  );
};

const Header: React.FC<{ status: BridgeStatus; pending: boolean }> = ({
  status,
  pending,
}) => {
  const offline = status === 'error';
  return (
    <div className="flex h-7 shrink-0 items-center justify-between text-[10px] font-semibold uppercase tracking-[0.22em]">
      <span className="text-gray-500">Lights</span>
      <span
        className={`flex items-center gap-1.5 rounded-full border px-2 py-0.5 ${
          offline
            ? 'border-red-500/30 bg-red-500/10 text-red-300'
            : 'border-emerald-500/20 bg-emerald-500/5 text-emerald-300'
        }`}
      >
        {offline ? <WifiOff size={11} /> : <span className="h-1.5 w-1.5 rounded-full bg-emerald-400" />}
        {offline ? 'Bridge offline' : pending ? 'Sending…' : 'Bridge OK'}
      </span>
    </div>
  );
};

const PresetGrid: React.FC<{
  presets: typeof LIGHT_PRESETS;
  activeId: string;
  onPick: (id: string) => void;
  previewBrightness: number;
}> = ({ presets, activeId, onPick, previewBrightness }) => (
  <div className="grid flex-1 grid-cols-4 grid-rows-2 gap-2">
    {presets.map((p) => {
      const isActive = p.id === activeId;
      const swatch = scale(p.color, previewBrightness);
      return (
        <motion.button
          key={p.id}
          type="button"
          whileTap={{ scale: 0.96 }}
          transition={SPRING_SNAP}
          onClick={() => onPick(p.id)}
          className={`relative flex touch-manipulation select-none flex-col items-start justify-end overflow-hidden rounded-lg border p-2 ${
            isActive ? 'border-white/40' : 'border-white/5'
          }`}
          style={{ backgroundColor: rgbToCss(swatch) }}
        >
          <span
            className="text-xs font-bold uppercase tracking-wider drop-shadow-[0_1px_2px_rgba(0,0,0,0.7)]"
            style={{ color: contrastColor(p.color) }}
          >
            {p.label}
          </span>
          {isActive ? (
            <motion.span
              layoutId="lights-active-ring"
              className="pointer-events-none absolute inset-0 rounded-lg ring-2 ring-white/80"
              transition={SPRING_SNAP}
            />
          ) : null}
        </motion.button>
      );
    })}
  </div>
);


const BrightnessSlider: React.FC<{
  value: number;
  onChange: (v: number) => void;
  disabled: boolean;
}> = ({ value, onChange, disabled }) => (
  <div className={`shrink-0 rounded-lg border border-white/5 bg-black/40 px-3 py-2 ${disabled ? 'opacity-40' : ''}`}>
    <div className="flex items-center justify-between text-[10px] font-semibold uppercase tracking-[0.22em] text-gray-400">
      <span>Brightness</span>
      <span className="tabular-nums text-white">{value}%</span>
    </div>
    <input
      type="range"
      min={0}
      max={100}
      value={value}
      disabled={disabled}
      onChange={(e) => onChange(Number(e.target.value))}
      className="mt-1.5 h-9 w-full appearance-none bg-transparent
        [&::-webkit-slider-runnable-track]:h-2 [&::-webkit-slider-runnable-track]:rounded-full [&::-webkit-slider-runnable-track]:bg-[#15110d]
        [&::-webkit-slider-thumb]:appearance-none [&::-webkit-slider-thumb]:h-7 [&::-webkit-slider-thumb]:w-7 [&::-webkit-slider-thumb]:-mt-2.5
        [&::-webkit-slider-thumb]:rounded-full [&::-webkit-slider-thumb]:bg-[#e6ddd0] [&::-webkit-slider-thumb]:shadow-lg
        [&::-webkit-slider-thumb]:border-2 [&::-webkit-slider-thumb]:border-black/40"
      style={{ touchAction: 'none' }}
    />
  </div>
);

const PowerButton: React.FC<{ on: boolean; onToggle: () => void }> = ({ on, onToggle }) => (
  <motion.button
    type="button"
    whileTap={{ scale: 0.97 }}
    transition={SPRING_SNAP}
    onClick={onToggle}
    className={`flex h-12 shrink-0 touch-manipulation select-none items-center justify-center gap-2 rounded-lg border text-sm font-bold uppercase tracking-[0.28em] ${
      on
        ? 'border-amber-300/40 bg-amber-300/10 text-amber-100'
        : 'border-white/10 bg-black/40 text-gray-400'
    }`}
  >
    <Power size={16} />
    {on ? 'Lights On' : 'Lights Off'}
  </motion.button>
);
