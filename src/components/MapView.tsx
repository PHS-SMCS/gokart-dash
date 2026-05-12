import React, { useEffect, useRef, useState } from 'react';
import L from 'leaflet';
import 'leaflet/dist/leaflet.css';
import { Crosshair, Satellite, WifiOff } from 'lucide-react';
import { useGps, type GpsSnapshot } from '../hooks/useGps';

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
  const gps = useGps(1000);

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

    // Any user-driven move drops out of follow mode.
    const onUserMove = () => {
      if (programmaticMoveRef.current) return;
      if (followRef.current) {
        followRef.current = false;
        setFollow(false);
      }
    };
    map.on('dragstart', onUserMove);
    map.on('zoomstart', onUserMove);

    trailRef.current = L.polyline([], {
      color: '#e6ddd0',
      weight: 3,
      opacity: 0.75,
    }).addTo(map);

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
    if (!map) return;
    if (gps.lat == null || gps.lon == null) return;

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
      // Release the flag after Leaflet's move events settle.
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

  return (
    <div className="relative h-full w-full overflow-hidden bg-[#0a0a0a]">
      <div ref={containerRef} className="h-full w-full map-root" />
      <Overlay gps={gps} follow={follow} onRecenter={recenter} />
      {!gps.fix ? <NoFixHint gps={gps} /> : null}
    </div>
  );
};

function kartIcon(headingDeg: number | null): L.DivIcon {
  const rot = headingDeg ?? 0;
  return L.divIcon({
    className: 'kart-marker',
    html: `<div class="kart-marker-inner" style="transform: rotate(${rot}deg)">
      <div class="kart-marker-arrow"></div>
      <div class="kart-marker-dot"></div>
    </div>`,
    iconSize: [28, 28],
    iconAnchor: [14, 14],
  });
}

const Overlay: React.FC<{
  gps: GpsSnapshot;
  follow: boolean;
  onRecenter: () => void;
}> = ({ gps, follow, onRecenter }) => {
  const bridgeBad = !gps.bridgeOk;
  const noStream = gps.bridgeOk && !gps.streamOk;
  const speedMph = gps.speedKph != null ? gps.speedKph * 0.621371 : null;

  return (
    <>
      <div className="pointer-events-none absolute inset-x-2 top-2 flex items-start justify-between gap-2">
        <div className="pointer-events-auto rounded-lg border border-white/10 bg-black/70 px-3 py-1.5 backdrop-blur">
          <div className="flex items-center gap-2 text-[10px] font-semibold uppercase tracking-[0.22em]">
            <Satellite
              size={12}
              className={
                bridgeBad
                  ? 'text-red-400'
                  : gps.fix
                    ? 'text-emerald-400'
                    : noStream
                      ? 'text-red-400'
                      : 'text-amber-300'
              }
            />
            <span className="text-white">
              {bridgeBad
                ? 'Bridge offline'
                : gps.fix
                  ? `Fix · ${gps.sats} sats`
                  : noStream
                    ? 'No GPS stream'
                    : `Searching · ${gps.sats} sats`}
            </span>
            {gps.hdop != null ? (
              <span className="text-gray-400">HDOP {gps.hdop.toFixed(1)}</span>
            ) : null}
          </div>
          <div className="mt-1 flex items-center gap-3 text-[11px] tabular-nums text-gray-300">
            <span>
              {gps.lat != null && gps.lon != null
                ? `${gps.lat.toFixed(6)}, ${gps.lon.toFixed(6)}`
                : '—, —'}
            </span>
            {speedMph != null ? (
              <span className="text-white">
                {speedMph.toFixed(1)}{' '}
                <span className="text-gray-500 text-[9px] uppercase tracking-wider">mph</span>
              </span>
            ) : null}
            {gps.altM != null ? (
              <span className="text-gray-400">
                {Math.round(gps.altM)}{' '}
                <span className="text-[9px] uppercase tracking-wider">m</span>
              </span>
            ) : null}
          </div>
        </div>

        <button
          type="button"
          onClick={onRecenter}
          className={`pointer-events-auto flex h-9 items-center gap-1.5 rounded-lg border px-3 text-[10px] font-semibold uppercase tracking-[0.22em] backdrop-blur ${
            follow
              ? 'border-emerald-400/30 bg-emerald-500/10 text-emerald-200'
              : 'border-white/10 bg-black/70 text-gray-200'
          }`}
        >
          <Crosshair size={12} />
          {follow ? 'Following' : 'Recenter'}
        </button>
      </div>

      {bridgeBad ? (
        <div className="pointer-events-none absolute inset-x-0 bottom-3 flex justify-center">
          <div className="pointer-events-auto flex items-center gap-2 rounded-lg border border-red-500/30 bg-red-500/10 px-3 py-1.5 text-[10px] font-semibold uppercase tracking-[0.22em] text-red-200">
            <WifiOff size={12} />
            {gps.error ?? 'GPS bridge unreachable'}
          </div>
        </div>
      ) : null}
    </>
  );
};

const NoFixHint: React.FC<{ gps: GpsSnapshot }> = ({ gps }) => {
  const msg = !gps.bridgeOk
    ? null
    : !gps.streamOk
      ? 'No data from receiver — check I2C wiring'
      : 'Acquiring satellites — needs sky view';

  if (!msg) return null;

  return (
    <div className="pointer-events-none absolute inset-0 flex items-center justify-center">
      <div className="rounded-lg border border-white/10 bg-black/75 px-4 py-2 text-center text-[11px] font-semibold uppercase tracking-[0.22em] text-gray-300 backdrop-blur">
        {msg}
      </div>
    </div>
  );
};
