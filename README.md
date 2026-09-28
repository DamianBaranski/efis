# EFIS

Electronic flight instrument system. Desktop builds an OpenGL window. Android builds the same view as an APK.

Work from the repository root. The examples below use Mirosławice (EPMR), about 50.959°N 16.770°E. Change the coordinates and country codes for another field.

## 1. Download resources

These files do not need an API key. Map tiles do, and they come after the key is registered.

FlightGear WS2 terrain goes to `resources/terrain/<10deg>/<1deg>/`. A 1° radius pulls the neighbouring cells so the view is not cut off at the cell edge.

```bash
./scripts/download-fg-terrain.sh --lat 50.959 --lon 16.770 --radius-deg 1
```

An existing FlightGear or TerraSync scenery tree can be linked instead:

```bash
./scripts/link-fg-terrain.sh
./scripts/link-fg-terrain.sh --source ~/.fgfs/TerraSync/Terrain
```

OpenAIP publishes country exports. Airports and runways become one CSV row per runway. Airspaces, visual reporting points, and obstacles are one file per country.

```bash
python3 scripts/download-airports.py --country PL,CZ --check EPMR,EPWS
python3 scripts/download-airspaces.py --country PL,CZ
python3 scripts/download-vrp.py --country PL,CZ

mkdir -p resources/obstacles
curl -L --fail -o resources/obstacles/pl_obs.geojson \
  https://storage.openaip.net/openaip-system-exports/pl_obs.geojson
curl -L --fail -o resources/obstacles/cz_obs.geojson \
  https://storage.openaip.net/openaip-system-exports/cz_obs.geojson
```

Outputs:

| Script | Path |
| --- | --- |
| airports | `resources/airports/airports.csv` |
| airspaces | `resources/airspaces/pl_asp.geojson`, `cz_asp.geojson` |
| reporting points | `resources/vrp/pl_rpp.json`, `cz_rpp.json` |
| obstacles | `resources/obstacles/pl_obs.geojson`, `cz_obs.geojson` |

Runway centerlines come from OpenStreetMap (`aeroway=runway`), after the airport CSV exists. A full country list is one request per field. Limit it with `--icao` while testing.

```bash
python3 scripts/download-osm-runways.py --icao EPMR,EPWS
```

That writes `resources/airports/osm_runways.csv`.

## 2. Create an OpenAIP API key

Satellite imagery is public. The OpenAIP chart overlay is not.

1. Create an account at [openaip.net](https://www.openaip.net) and log in.
2. Open the user profile **API Clients** page.
3. Request a new API client. Each client has its own key.
4. Copy the key. Treat it like a password. The same page can show or revoke it later.

Requests send the key in the `x-openaip-api-key` header. The tile URL is `https://api.tiles.openaip.net/api/data/openaip/{z}/{x}/{y}.png`.

## 3. Register the API key

Write the key as a single line. Leave this file untracked.

```bash
mkdir -p resources/openaip
printf '%s\n' 'YOUR_KEY' > resources/openaip/api.key
```

`OPENAIP_API_KEY` overrides the file for one shell:

```bash
export OPENAIP_API_KEY='YOUR_KEY'
```

The desktop client and the tile script use `OPENAIP_API_KEY` when it is set, and otherwise read `resources/openaip/api.key`. Android reads that same file after scenery is pushed to the device.

The 3D view drapes three rings: zoom 16 close in, zoom 13 in the middle, zoom 11 wide. Prefetch a disk around the field. `--radius 7` covers the ring the app draws and a margin to move inside.

```bash
python3 scripts/download-openaip-tiles.py --lat 50.959 --lon 16.770 --zoom 16 --radius 7
python3 scripts/download-openaip-tiles.py --lat 50.959 --lon 16.770 --zoom 13 --radius 7
python3 scripts/download-openaip-tiles.py --lat 50.959 --lon 16.770 --zoom 11 --radius 7
```

Tiles land in `resources/openaip/cache/`. Esri World Imagery is stored as `satellite/`. The OpenAIP overlay is stored as `openaip/`. `--no-basemap` skips imagery. `--no-openaip` skips the chart.

## 4. Initialize the Android SDK

`android/setup.sh` downloads a local JDK 21, the Android command-line tools, platform 34, build-tools, NDK 26.3, CMake 3.22.1, the emulator and x86_64 system image, SDL2, SDL2_image, SDL2_ttf, GLM, and the Gradle 8.7 wrapper. It writes `android/local.properties` and packs the small assets into the APK tree.

Host tools it expects: `curl`, `tar`, `unzip`, and `python3`.

```bash
./android/setup.sh
```

The SDK is `android/sdk`. The JDK is `android/jdk`. Re-run the script after a clean checkout. Files that are already present are left in place.

## 5. Install build prerequisites

Desktop links system libraries. On Debian or Ubuntu:

```bash
sudo apt install build-essential cmake pkg-config \
  libgl1-mesa-dev libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev \
  libcurl4-openssl-dev zlib1g-dev libglm-dev python3 curl unzip
```

CMake needs 3.10 or newer and a C++17 compiler. Doxygen is optional (`sudo apt install doxygen`, then `make doxygen` in `build/`).

The Android SDK from the previous step supplies the NDK, the Android CMake, and the JDK. The desktop packages above are still what the Linux `efis` binary links against.

## 6. Build

Desktop, from the repository root:

```bash
cmake -S . -B build
cmake --build build
```

Run the binary from `build/` so `../resources` and `../shader` resolve.

Android debug APK (arm64-v8a and x86_64):

```bash
export JAVA_HOME="$PWD/android/jdk"
cd android && ./gradlew assembleDebug
```

The APK is `android/app/build/outputs/apk/debug/app-debug.apk`. Fonts, shaders, airports, airspaces, reporting points, and obstacles are inside it. FlightGear tiles and the imagery cache stay out, because they are large.

## 7. Deploy

Desktop, simulated flight:

```bash
cd build
./efis
```

Live Stratux at `http://127.0.0.1:5000/getSituation`:

```bash
./efis --stratux
```

`python3 scripts/stratux-sim.py --port 5000` serves that same HTTP path when the receiver is absent. Android always uses the in-process simulator.

Desktop keys: `1`–`4` or `Tab` change view, `5` toggles airspace walls, `Esc` quits. In the simulator, arrows change pitch and roll, `Q`/`E` heading, `W`/`S` speed, `+`/`-` altitude, `R` resets attitude. On a phone or tablet, a tap cycles the views and a tap on the top strip toggles airspaces.

Local emulator (KVM, host GPU). This boots `android/avd/efis`, installs the APK, and launches EFIS:

```bash
./android/run-emulator.sh
```

Phone. Wi-Fi `192.168.1.7` is tried first. A USB phone is used when that address does not answer. Override with `EFIS_PHONE_HOST`.

```bash
./android/deploy-phone.sh
```

Tablet. The last address is remembered in `android/.tablet-wlan`. Set `EFIS_TABLET_HOST` the first time, or plug the tablet in.

```bash
EFIS_TABLET_HOST=192.168.1.x ./android/deploy-tablet.sh
```

Each deploy script builds the debug APK, installs it, pushes scenery, and starts `com.efis.app`. Scenery is terrain, landclass textures, the imagery cache, and `resources/openaip/api.key`. Push scenery alone, with a device already connected:

```bash
./android/push-scenery.sh
```

Manual install:

```bash
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

## Data

OpenAIP airports, airspaces, reporting points, obstacles, and chart tiles are [CC BY-NC 4.0](https://www.openaip.net). The ground image is Esri World Imagery. Terrain meshes are FlightGear WS2. Runway centerlines are OpenStreetMap.
