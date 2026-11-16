#!/usr/bin/env python3
"""Download the GUGiK NMT (1 m DTM, EPSG:2180, averaged to a coarser resolution) over the rectangle east ± range, north ± range around a point as GeoTIFF tiles for terrain_model."""

import argparse
import http.client
import math
import pathlib
import shutil
import time
import urllib.error
import urllib.parse
import urllib.request

from pyproj import Transformer

URL = "https://mapy.geoportal.gov.pl/wss/service/PZGIK/NMT/GRID1/WCS/DigitalTerrainModelFormatTIFF"
COVERAGE = "DTM_PL-KRON86-NH_TIFF"
TIFF_MAGIC = (b"II*\x00", b"MM\x00*")
DTM_DIRECTORY = pathlib.Path(__file__).resolve().parent / "dtm"
ATTEMPTS = 3
RETRY_STATUS = (429, 500, 502, 503, 504)


def tiles(east, north, extent, size):
    e0 = math.floor((east - extent) / size) * size
    e1 = math.ceil((east + extent) / size) * size
    n0 = math.floor((north - extent) / size) * size
    n1 = math.ceil((north + extent) / size) * size
    return [(e, n) for n in range(n0, n1, size) for e in range(e0, e1, size)]


def download(east, north, size, resolution, path):
    query = urllib.parse.urlencode([
        ("SERVICE", "WCS"),
        ("REQUEST", "GetCoverage"),
        ("VERSION", "2.0.1"),
        ("COVERAGEID", COVERAGE),
        ("FORMAT", "image/tiff"),
        ("SUBSET", f"x({east},{east + size})"),
        ("SUBSET", f"y({north},{north + size})"),
        ("SCALESIZE", f"x({size // resolution}),y({size // resolution})"),
        ("INTERPOLATION", "AVERAGE"),
    ])
    request = urllib.request.Request(f"{URL}?{query}", headers={"Connection": "close"})
    partial = path.with_suffix(".part")

    for attempt in range(1, ATTEMPTS + 1):
        try:
            with urllib.request.urlopen(request, timeout=600) as response, open(partial, "wb") as file:
                shutil.copyfileobj(response, file)

            break
        except (urllib.error.URLError, http.client.HTTPException, ConnectionError, TimeoutError) as error:
            if attempt == ATTEMPTS or (isinstance(error, urllib.error.HTTPError) and error.code not in RETRY_STATUS):
                partial.unlink(missing_ok=True)
                raise

            print(f"{path.name}: {error}, retrying")
            time.sleep(2 ** attempt)

    with open(partial, "rb") as file:
        head = file.read(512)

    if head[:4] not in TIFF_MAGIC:
        partial.unlink()
        raise RuntimeError(f"no GeoTIFF for {path.name}: {head.decode(errors='replace')}")

    partial.rename(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("lat", type=float, help="latitude of the centre [deg]")
    parser.add_argument("lon", type=float, help="longitude of the centre [deg]")
    parser.add_argument("--range", type=float, default=10000.0, help="half size of the rectangle along east and north [m]")
    parser.add_argument("--tile", type=int, default=2000, help="tile size [m], at most 5000 (larger requests come back partly zero)")
    parser.add_argument("--resolution", type=int, default=10, help="pixel size [m], divides --tile")
    parser.add_argument("--output", type=pathlib.Path, default=DTM_DIRECTORY, help="directory (terrain_model.dtm_directory)")
    args = parser.parse_args()

    if args.resolution <= 0 or args.tile % args.resolution != 0:
        parser.error("--resolution must be positive and divide --tile")

    east, north = Transformer.from_crs("EPSG:4326", "EPSG:2180", always_xy=True).transform(args.lon, args.lat)
    args.output.mkdir(parents=True, exist_ok=True)
    selected = tiles(east, north, args.range, args.tile)

    for index, (e, n) in enumerate(selected, 1):
        path = args.output / f"nmt_{e}_{n}_{args.resolution}m.tif"

        if path.exists():
            print(f"[{index}/{len(selected)}] {path} exists")
            continue

        download(e, n, args.tile, args.resolution, path)
        print(f"[{index}/{len(selected)}] {path}")


if __name__ == "__main__":
    main()
