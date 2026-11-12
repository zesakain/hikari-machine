#!/usr/bin/env python3
"""
tools/download_benchmark_assets.py

Downloads Khronos glTF Sample Assets for benchmark certification (Milestone 10G):
- Sponza (glTF with separate buffers and textures)
- FlightHelmet (glTF with separate buffers and textures)
- DamagedHelmet (glTF binary .glb)

Target directories:
- test-assets/benchmark/sponza/
- test-assets/benchmark/flight_helmet/
- test-assets/DamagedHelmet.glb
"""

import os
import sys
import urllib.request
import urllib.error
import time

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEST_ASSETS_DIR = os.path.join(BASE_DIR, "test-assets")
BENCHMARK_DIR = os.path.join(TEST_ASSETS_DIR, "benchmark")
SPONZA_DIR = os.path.join(BENCHMARK_DIR, "sponza")
FLIGHT_HELMET_DIR = os.path.join(BENCHMARK_DIR, "flight_helmet")

SPONZA_BASE_URL = "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/Sponza/glTF/"
FLIGHT_HELMET_BASE_URL = "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/FlightHelmet/glTF/"
DAMAGED_HELMET_URL = "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/DamagedHelmet/glTF-Binary/DamagedHelmet.glb"

SPONZA_FILES = [
    "Sponza.gltf",
    "Sponza.bin",
    "white.png",
    "10381718147657362067.jpg",
    "10388182081421875623.jpg",
    "11474523244911310074.jpg",
    "11490520546946913238.jpg",
    "11872827283454512094.jpg",
    "11968150294050148237.jpg",
    "1219024358953944284.jpg",
    "12501374198249454378.jpg",
    "13196865903111448057.jpg",
    "13824894030729245199.jpg",
    "13982482287905699490.jpg",
    "14118779221266351425.jpg",
    "14170708867020035030.jpg",
    "14267839433702832875.jpg",
    "14650633544276105767.jpg",
    "15295713303328085182.jpg",
    "15722799267630235092.jpg",
    "16275776544635328252.png",
    "16299174074766089871.jpg",
    "16885566240357350108.jpg",
    "17556969131407844942.jpg",
    "17876391417123941155.jpg",
    "2051777328469649772.jpg",
    "2185409758123873465.jpg",
    "2299742237651021498.jpg",
    "2374361008830720677.jpg",
    "2411100444841994089.jpg",
    "2775690330959970771.jpg",
    "2969916736137545357.jpg",
    "332936164838540657.jpg",
    "3371964815757888145.jpg",
    "3455394979645218238.jpg",
    "3628158980083700836.jpg",
    "3827035219084910048.jpg",
    "4477655471536070370.jpg",
    "4601176305987539675.jpg",
    "466164707995436622.jpg",
    "4675343432951571524.jpg",
    "4871783166746854860.jpg",
    "4910669866631290573.jpg",
    "4975155472559461469.jpg",
    "5061699253647017043.png",
    "5792855332885324923.jpg",
    "5823059166183034438.jpg",
    "6047387724914829168.jpg",
    "6151467286084645207.jpg",
    "6593109234861095314.jpg",
    "6667038893015345571.jpg",
    "6772804448157695701.jpg",
    "7056944414013900257.jpg",
    "715093869573992647.jpg",
    "7268504077753552595.jpg",
    "7441062115984513793.jpg",
    "755318871556304029.jpg",
    "759203620573749278.jpg",
    "7645212358685992005.jpg",
    "7815564343179553343.jpg",
    "8006627369776289000.png",
    "8051790464816141987.jpg",
    "8114461559286000061.jpg",
    "8481240838833932244.jpg",
    "8503262930880235456.jpg",
    "8747919177698443163.jpg",
    "8750083169368950601.jpg",
    "8773302468495022225.jpg",
    "8783994986360286082.jpg",
    "9288698199695299068.jpg",
    "9916269861720640319.jpg",
]

FLIGHT_HELMET_FILES = [
    "FlightHelmet.gltf",
    "FlightHelmet.bin",
    "FlightHelmet_Materials_GlassPlasticMat_BaseColor.png",
    "FlightHelmet_Materials_GlassPlasticMat_Normal.png",
    "FlightHelmet_Materials_GlassPlasticMat_OcclusionRoughMetal.png",
    "FlightHelmet_Materials_LeatherPartsMat_BaseColor.png",
    "FlightHelmet_Materials_LeatherPartsMat_Normal.png",
    "FlightHelmet_Materials_LeatherPartsMat_OcclusionRoughMetal.png",
    "FlightHelmet_Materials_LensesMat_BaseColor.png",
    "FlightHelmet_Materials_LensesMat_Normal.png",
    "FlightHelmet_Materials_LensesMat_OcclusionRoughMetal.png",
    "FlightHelmet_Materials_MetalPartsMat_BaseColor.png",
    "FlightHelmet_Materials_MetalPartsMat_Normal.png",
    "FlightHelmet_Materials_MetalPartsMat_OcclusionRoughMetal.png",
    "FlightHelmet_Materials_RubberWoodMat_BaseColor.png",
    "FlightHelmet_Materials_RubberWoodMat_Normal.png",
    "FlightHelmet_Materials_RubberWoodMat_OcclusionRoughMetal.png",
]


def download_file(url: str, dest_path: str, max_retries: int = 3) -> bool:
    if os.path.isfile(dest_path) and os.path.getsize(dest_path) > 0:
        return True

    temp_path = dest_path + ".tmp"
    headers = {"User-Agent": "HikariBenchmarkDownloader/1.0"}

    for attempt in range(1, max_retries + 1):
        try:
            req = urllib.request.Request(url, headers=headers)
            with urllib.request.urlopen(req, timeout=30) as resp:
                with open(temp_path, "wb") as f:
                    while True:
                        chunk = resp.read(65536)
                        if not chunk:
                            break
                        f.write(chunk)
            os.replace(temp_path, dest_path)
            return True
        except Exception as e:
            print(f"[WARN] Attempt {attempt}/{max_retries} failed for {url}: {e}")
            if os.path.exists(temp_path):
                os.remove(temp_path)
            time.sleep(1.0 * attempt)

    return False


def main():
    os.makedirs(SPONZA_DIR, exist_ok=True)
    os.makedirs(FLIGHT_HELMET_DIR, exist_ok=True)

    print("=== Downloading Benchmark Assets ===")

    # 1. DamagedHelmet
    damaged_helmet_path = os.path.join(TEST_ASSETS_DIR, "DamagedHelmet.glb")
    if not os.path.isfile(damaged_helmet_path) or os.path.getsize(damaged_helmet_path) == 0:
        print("Downloading DamagedHelmet.glb...")
        if not download_file(DAMAGED_HELMET_URL, damaged_helmet_path):
            print("[ERROR] Failed to download DamagedHelmet.glb")
            sys.exit(1)
    else:
        print("DamagedHelmet.glb already present.")

    # 2. Sponza
    print(f"Downloading Sponza ({len(SPONZA_FILES)} files)...")
    for idx, fname in enumerate(SPONZA_FILES, 1):
        url = SPONZA_BASE_URL + fname
        dest = os.path.join(SPONZA_DIR, fname)
        if not os.path.isfile(dest) or os.path.getsize(dest) == 0:
            print(f"  [{idx}/{len(SPONZA_FILES)}] {fname}")
            if not download_file(url, dest):
                print(f"[ERROR] Failed to download {fname}")
                sys.exit(1)

    # 3. FlightHelmet
    print(f"Downloading FlightHelmet ({len(FLIGHT_HELMET_FILES)} files)...")
    for idx, fname in enumerate(FLIGHT_HELMET_FILES, 1):
        url = FLIGHT_HELMET_BASE_URL + fname
        dest = os.path.join(FLIGHT_HELMET_DIR, fname)
        if not os.path.isfile(dest) or os.path.getsize(dest) == 0:
            print(f"  [{idx}/{len(FLIGHT_HELMET_FILES)}] {fname}")
            if not download_file(url, dest):
                print(f"[ERROR] Failed to download {fname}")
                sys.exit(1)

    print("=== All Benchmark Assets Downloaded Successfully ===")


if __name__ == "__main__":
    main()
