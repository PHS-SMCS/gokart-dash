# Map Tab

The Map tab shows the kart's live position on a real map, using the on-board GPS.

## How it works

- The map is rendered with **Leaflet** using **OpenStreetMap** raster tiles.
- The kart's position comes from the **NEO-M9N GPS** module, which the
  [bridge](uart-bridge.md) reads over the Pi's I2C bus and merges into the
  telemetry stream (lat/lon, heading, fix, satellite count).
- A **kart marker** is placed at the current position and rotated to the GPS
  heading. While there is a fix, the app draws a **trail** of recent positions.

The same GPS data drives the smaller mini-map on the [Drive tab](tab-drive.md).

!!! abstract "Datasheet"
    [u-blox NEO-M9N GPS (PDF, 0.7 MB)](../reference/PDF/Mainboard/GPS/NEO-M9N-00B_DataSheet_UBX-19014285.pdf)
    — see all component datasheets on the [Datasheets](../reference/datasheets.md) page.

## Requirements & caveats

!!! note "OpenStreetMap tiles need internet"
    Leaflet fetches map tiles from `tile.openstreetmap.org`. On the kart's isolated
    network the tiles will not load unless the Pi has internet access (or a local
    tile cache/server is provided). The marker and trail still work without tiles;
    only the base map imagery is missing. A published Artifact or offline kiosk
    won't reach the tile server either.

!!! warning "GPS hardware details not in the repo"
    The GPS **module wiring, antenna, and mounting** are physical. The NEO-M9N is
    connected to the Pi's I2C bus (with a PPS line to the Teensy per the mainboard
    map); fix quality depends on antenna placement and sky view. See
    [Hardware & Open Items](../hardware-open-items.md).

In **Sim** mode ([System tab](tab-system.md)) you can set a GPS fix, heading,
satellite count, and lat/lon by hand to exercise the map with no hardware.
