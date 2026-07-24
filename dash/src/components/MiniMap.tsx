import React, { useEffect, useRef } from 'react';
import L from 'leaflet';
import 'leaflet/dist/leaflet.css';
import { Satellite } from 'lucide-react';
import type { GpsFix } from '../telemetry/types';

const FALLBACK_CENTER: [number, number] = [38.8977, -77.0365];
const ZOOM = 17;

function kartIcon(headingDeg: number | null): L.DivIcon {
  return L.divIcon({
    className: 'kart-marker',
    html: `<div class="kart-marker-inner" style="transform: rotate(${headingDeg ?? 0}deg)">
      <div class="kart-marker-arrow"></div>
      <div class="kart-marker-dot"></div>
    </div>`,
    iconSize: [24, 24],
    iconAnchor: [12, 12],
  });
}

/**
 * Non-interactive minimap that always follows the kart. Interactive panning
 * lives on the full Map tab; this is a glanceable locator for the drive view.
 */
export const MiniMap: React.FC<{ gps: GpsFix }> = ({ gps }) => {
  const containerRef = useRef<HTMLDivElement | null>(null);
  const mapRef = useRef<L.Map | null>(null);
  const markerRef = useRef<L.Marker | null>(null);

  useEffect(() => {
    if (!containerRef.current || mapRef.current) return;
    const map = L.map(containerRef.current, {
      center: FALLBACK_CENTER,
      zoom: ZOOM,
      zoomControl: false,
      attributionControl: false,
      dragging: false,
      scrollWheelZoom: false,
      doubleClickZoom: false,
      touchZoom: false,
      keyboard: false,
      boxZoom: false,
      preferCanvas: true,
    });
    L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
      maxZoom: 19,
      crossOrigin: true,
    }).addTo(map);
    mapRef.current = map;
    return () => {
      map.remove();
      mapRef.current = null;
      markerRef.current = null;
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
    map.panTo(ll, { animate: true, duration: 0.4 });
  }, [gps.lat, gps.lon, gps.headingDeg]);

  return (
    <div className="relative h-full w-full overflow-hidden rounded-lg border border-white/5 bg-[#0a0a0a]">
      <div ref={containerRef} className="map-root h-full w-full" />
      {!gps.fix ? (
        <div className="pointer-events-none absolute inset-0 flex items-center justify-center bg-black/55">
          <div className="flex items-center gap-1.5 rounded-md border border-white/10 bg-black/70 px-2 py-1 text-[9px] font-semibold uppercase tracking-[0.18em] text-gray-400">
            <Satellite size={11} className="text-amber-300" />
            No GPS fix
          </div>
        </div>
      ) : null}
      <div className="pointer-events-none absolute left-1.5 top-1.5 rounded bg-black/60 px-1.5 py-0.5 text-[8px] font-semibold uppercase tracking-[0.16em] text-gray-400">
        {gps.fix ? `${gps.sats} sats` : 'no fix'}
      </div>
    </div>
  );
};
