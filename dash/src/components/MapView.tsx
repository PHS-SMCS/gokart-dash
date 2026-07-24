import React, { useEffect, useRef, useState } from 'react';
import L from 'leaflet';
import 'leaflet/dist/leaflet.css';
import { Crosshair, Satellite, WifiOff } from 'lucide-react';
import { useTelemetry } from '../telemetry/context';

const FALLBACK_CENTER: [number, number] = [38.8977, -77.0365];
const DEFAULT_ZOOM = 17;
const TRAIL_MAX_POINTS = 600;
const FOLLOW_PAN_DURATION_S = 0.4;

export const MapView: React.FC = () => {
  const containerRef = useRef<HTMLDivElement | null>(null);
  const mapRef = useRef<L.Map | null>(null);
  const markerRef = useRef<L.Marker | null>(null);
  const trailRef = useRef<L.Polyline | null>(null);
  const followRef = useRef(true);
  const programmaticMoveRef = useRef(false);
  const hasFirstFixRef = useRef(false);

  const [follow, setFollow] = useState(true);
  const t = useTelemetry();
  const { gps, link } = t;

  useEffect(() => {
    if (!containerRef.current || mapRef.current) return;

    const map = L.map(containerRef.current, {
      center: FALLBACK_CENTER,
      zoom: DEFAULT_ZOOM,
      zoomControl: false,
      attributionControl: false,
      preferCanvas: true,
    });

    L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
      maxZoom: 19,
      crossOrigin: true,
    }).addTo(map);

    L.control
      .attribution({ position: 'bottomleft', prefix: false })
      .addAttribution('© OpenStreetMap')
      .addTo(map);
    L.control.zoom({ position: 'bottomright' }).addTo(map);

    const onUserMove = () => {
      if (programmaticMoveRef.current) return;
      if (followRef.current) {
        followRef.current = false;
        setFollow(false);
      }
    };
    map.on('dragstart', onUserMove);
    map.on('zoomstart', onUserMove);

    trailRef.current = L.polyline([], { color: '#e6ddd0', weight: 3, opacity: 0.75 }).addTo(map);
    mapRef.current = map;

    return () => {
      map.remove();
      mapRef.current = null;
      markerRef.current = null;
      trailRef.current = null;
    };
  }, []);

  useEffect(() => {
    const map = mapRef.current;
    if (!map || gps.lat == null || gps.lon == null) return;
    const ll = L.latLng(gps.lat, gps.lon);

    if (!markerRef.current) {
      markerRef.current = L.marker(ll, { icon: kartIcon(gps.headingDeg) }).addTo(map);
    } else {
      markerRef.current.setLatLng(ll);
      markerRef.current.setIcon(kartIcon(gps.headingDeg));
    }

    if (gps.fix && trailRef.current) {
      const poly = trailRef.current;
      const pts = poly.getLatLngs() as L.LatLng[];
      const last = pts[pts.length - 1];
      if (!last || last.lat !== ll.lat || last.lng !== ll.lng) {
        pts.push(ll);
        while (pts.length > TRAIL_MAX_POINTS) pts.shift();
        poly.setLatLngs(pts);
      }
    }

    if (followRef.current) {
      programmaticMoveRef.current = true;
      if (!hasFirstFixRef.current) {
        map.setView(ll, DEFAULT_ZOOM);
        hasFirstFixRef.current = true;
      } else {
        map.panTo(ll, { animate: true, duration: FOLLOW_PAN_DURATION_S });
      }
      window.setTimeout(() => {
        programmaticMoveRef.current = false;
      }, FOLLOW_PAN_DURATION_S * 1000 + 50);
    }
  }, [gps.lat, gps.lon, gps.fix, gps.headingDeg]);

  const recenter = () => {
    followRef.current = true;
    setFollow(true);
    const map = mapRef.current;
    if (map && gps.lat != null && gps.lon != null) {
      programmaticMoveRef.current = true;
      map.setView([gps.lat, gps.lon], DEFAULT_ZOOM, { animate: true });
      window.setTimeout(() => {
        programmaticMoveRef.current = false;
      }, 500);
    }
  };

  const speedMph = gps.speedKph != null ? gps.speedKph * 0.621371 : null;

  return (
    <div className="relative h-full w-full overflow-hidden bg-[#0a0a0a]">
      <div ref={containerRef} className="map-root h-full w-full" />

      <div className="pointer-events-none absolute inset-x-2 top-2 flex items-start justify-between gap-2">
        <div className="pointer-events-auto rounded-lg border border-white/10 bg-black/70 px-3 py-1.5 backdrop-blur">
          <div className="flex items-center gap-2 text-[10px] font-semibold uppercase tracking-[0.22em]">
            <Satellite
              size={12}
              className={!link.connected ? 'text-red-400' : gps.fix ? 'text-emerald-400' : 'text-amber-300'}
            />
            <span className="text-white">
              {!link.connected ? 'No telemetry' : gps.fix ? `Fix · ${gps.sats} sats` : `Searching · ${gps.sats} sats`}
            </span>
          </div>
          <div className="mt-1 flex items-center gap-3 text-[11px] tabular-nums text-gray-300">
            <span>{gps.lat != null && gps.lon != null ? `${gps.lat.toFixed(6)}, ${gps.lon.toFixed(6)}` : '—, —'}</span>
            {speedMph != null ? (
              <span className="text-white">
                {speedMph.toFixed(1)} <span className="text-[9px] uppercase tracking-wider text-gray-500">mph</span>
              </span>
            ) : null}
          </div>
        </div>

        <button
          type="button"
          onClick={recenter}
          className={`pointer-events-auto flex h-9 items-center gap-1.5 rounded-lg border px-3 text-[10px] font-semibold uppercase tracking-[0.22em] backdrop-blur ${
            follow ? 'border-emerald-400/30 bg-emerald-500/10 text-emerald-200' : 'border-white/10 bg-black/70 text-gray-200'
          }`}
        >
          <Crosshair size={12} />
          {follow ? 'Following' : 'Recenter'}
        </button>
      </div>

      {!link.connected ? (
        <div className="pointer-events-none absolute inset-x-0 bottom-3 flex justify-center">
          <div className="pointer-events-auto flex items-center gap-2 rounded-lg border border-amber-500/30 bg-amber-500/10 px-3 py-1.5 text-[10px] font-semibold uppercase tracking-[0.22em] text-amber-200">
            <WifiOff size={12} />
            No telemetry — enable a source on the System tab
          </div>
        </div>
      ) : null}
    </div>
  );
};

function kartIcon(headingDeg: number | null): L.DivIcon {
  return L.divIcon({
    className: 'kart-marker',
    html: `<div class="kart-marker-inner" style="transform: rotate(${headingDeg ?? 0}deg)">
      <div class="kart-marker-arrow"></div>
      <div class="kart-marker-dot"></div>
    </div>`,
    iconSize: [28, 28],
    iconAnchor: [14, 14],
  });
}
