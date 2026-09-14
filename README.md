# EFIS (Electronic Flight Instrument System) Project

This project is an Electronic Flight Instrument System (EFIS) developed in C++ with OpenGL and SDL2.

## Overview

The EFIS project provides a graphical interface for displaying aircraft attitude and other flight-related data. It includes components such as attitude widgets, data managers, and screens.

JSON parsing uses the vendored `nlohmann_json` tree. GRIB weather decoding uses the vendored `wgrib2` tree if that library is installed.

## Prerequisites

Before running the EFIS project, make sure you have the following dependencies installed on your system:

- OpenGL
- SDL2, SDL2_image, SDL2_ttf
- CURL (for fetching data)
- ZLIB
- Doxygen (for generating documentation)

## Optional: wgrib2

The `wgrib2` sources are included. To link them into EFIS, build and install that library first:

```
mkdir -p wgrib2/build && cd wgrib2/build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
make && sudo make install
```

Then reconfigure EFIS so `find_package(wgrib2)` succeeds. The default EFIS build does not require wgrib2.

## FlightGear terrain

The map loader reads FlightGear WS2 tiles from `resources/terrain/<10deg>/<1deg>/<index>.btg.gz`.

Download tiles for a location (Mirosławice / EPMR example):

```
./scripts/download-fg-terrain.sh --lat 50.9578 --lon 16.7703 --radius-deg 1
```

`--radius-deg 1` pulls neighbouring 1° cells so the view is not cut off at the cell edge. Existing FlightGear/TerraSync scenery can be reused instead of downloading again:

```
./scripts/link-fg-terrain.sh
./scripts/link-fg-terrain.sh --source ~/.fgfs/TerraSync/Terrain
```

Run `./efis` from the `build/` directory so `../resources/terrain` resolves.

## OpenAIP tiles

Aeronautical overlay tiles come from [OpenAIP](https://www.openaip.net) (CC BY-NC 4.0). F4 drapes Esri World Imagery plus the OpenAIP overlay onto the 3D terrain.

Put your OpenAIP client API key in `resources/openaip/api.key` or in `OPENAIP_API_KEY`.

`GET https://api.tiles.openaip.net/api/data/openaip/{z}/{x}/{y}.png` (overlay)

The prefetch script also downloads Esri World Imagery under `cache/satellite/`. Overlay goes to `cache/openaip/`. F4 drapes two zooms: **16** near the aircraft (~1.5 m/pixel) and **13** out to the FlightGear scenery window.

```
python3 scripts/download-openaip-tiles.py --lat 50.959167 --lon 16.770278 --zoom 16 --radius 7
python3 scripts/download-openaip-tiles.py --lat 50.959167 --lon 16.770278 --zoom 13 --radius 15
```

`--no-basemap` skips the imagery; `--no-openaip` skips the aviation overlay.

## Airport / runway CSV

OpenAIP country exports include small civil airfields, ultralight sites, and unnamed strips (not only ICAO airports). One CSV row per runway:

```
python3 scripts/download-airports.py --country PL --check EPMR,EPWS
```

Output: `resources/airports/airports.csv` (`icao,name,country,type,lat,lon,elev_m,runway,heading_deg,length_m,width_m,surface`). Add more countries with `--country PL,CZ,DE`. Data is OpenAIP CC BY-NC 4.0.

Runway **centerlines** come from OpenStreetMap (`aeroway=runway` ways), not from OpenAIP points or map tiles:

```
python3 scripts/download-osm-runways.py
python3 scripts/download-osm-runways.py --icao EPMR,EPWS
```

F2/F3/F4 draw nearby strips from the OpenAIP list, with OSM endpoints when `resources/airports/osm_runways.csv` is present.

## Running

```
cd build
./efis              # simulated Stratux (default)
./efis --stratux    # live Stratux at 127.0.0.1:5000
```

HTTP mock of Stratux if you want to keep the real client path:

```
python3 scripts/stratux-sim.py --port 5000
./efis --stratux
```

### Keyboard

- `Tab` cycle views (combined / AHRS / terrain / OpenAIP)
- `1` / `F1` AHRS only
- `2` / `F2` terrain only
- `3` / `F3` both
- `4` / `F4` 3D terrain with OpenAIP tiles draped on the ground
- `Esc` quit
- Sim only: arrows pitch/roll, `Q`/`E` heading, `W`/`S` speed, `+`/`-` altitude, `R` reset attitude


## Building and Running

To build the EFIS project, follow these steps:

1. Clone this repository to your local machine.
2. Navigate to the project directory.
3. Create a build directory: `mkdir build && cd build`.
4. Generate the build files: `cmake ..`.
5. Build the project: `make`.

After building the project, you can run the EFIS executable. For example:
`./efis`

## Documentation

The project includes Doxygen comments for generating documentation. To generate the documentation, run:
`make doxygen`

The documentation will be generated in the `doc` directory.

## Contributing

Contributions to this project are welcome! If you find any issues or have suggestions for improvements, please submit a pull request or open an issue on GitHub.

## License

This project is licensed under the TBD.


