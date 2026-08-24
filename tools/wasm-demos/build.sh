#!/usr/bin/env bash
# Builds cler for the browser: liquid-dsp + GUI/plot blocks into libcler_web.a, then each
# bundled example into docs/demos/run/<name>.{html,js,wasm,worker.js}, plus the payload
# the in-browser cler-fg compiler needs (same flags, so the two cannot drift).
# Needs EMSDK pointing at an emsdk checkout with 3.1.24 activated (matches emception).
set -euo pipefail
: "${EMSDK:?set EMSDK to an emsdk checkout (3.1.24 activated)}"
source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
deps="${CLER_DEPS:-$repo/build/_deps}"        # imgui-src, implot-src, liquid-src from a native configure
out="$here/out"; run="$repo/docs/demos/run"      # served straight from GitHub Pages
payload="$repo/tools/cler-fg/app/public/payload"  # what the in-browser compiler needs (fetched on first Build)
rm -rf "$out/obj"
mkdir -p "$out/obj" "$run" "$payload"
jobs="$(nproc --ignore=1)"

# emscripten's own port of GLFW; imgui + implot + gui manager + plot blocks + liquid
CXXFLAGS=(-std=c++17 -O2 -pthread -I"$repo/include" -I"$repo" -I"$repo/desktop_blocks/gui" -I"$repo/desktop_blocks/plots"
  -I"$deps/imgui-src" -I"$deps/imgui-src/backends" -I"$deps/implot-src" -I"$out/liquid/include/liquid" -DIMGUI_IMPL_OPENGL_ES3 "-DImDrawIdx=unsigned int"
  -Wno-unused-parameter -Wno-unused-variable -Wno-missing-braces -Wno-deprecated-declarations)
# LDSHARED is what the in-browser linker reuses; -O2 and the shell live here only.
LDSHARED=(-pthread -sUSE_GLFW=3 -sUSE_WEBGL2=1 -sFULL_ES3=1 -sALLOW_MEMORY_GROWTH=1 -sPTHREAD_POOL_SIZE=8 -sASYNCIFY
  -sASYNCIFY_STACK_SIZE=65536 -sEXIT_RUNTIME=0)
LDFLAGS=(-O2 "${LDSHARED[@]}" --shell-file "$here/shell.html")

if [ ! -f "$out/liquid/lib/libliquid.a" ]; then
  echo "== liquid-dsp"
  mkdir -p "$out/liquid-build"
  (cd "$out/liquid-build" && emcmake cmake "$deps/liquid-src" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
     -DBUILD_STATIC_LIBS=ON -DBUILD_EXAMPLES=OFF -DBUILD_AUTOTESTS=OFF -DBUILD_BENCHMARKS=OFF -DENABLE_SIMD=OFF \
     -DFIND_SIMD=OFF -DFIND_FFTW=OFF -DFIND_THREADS=OFF -DCMAKE_INSTALL_PREFIX="$out/liquid" -DCMAKE_C_FLAGS="-pthread -O2" >/dev/null \
   && emmake make -j"$jobs" >/dev/null && make install >/dev/null)
fi

echo "== libcler_web.a"
lib_sources=(
  "$repo/desktop_blocks/gui/gui_manager.cpp"
  "$repo/desktop_blocks/plots/plot_timeseries.cpp"
  "$repo/desktop_blocks/plots/plot_cspectrum.cpp"
  "$repo/desktop_blocks/plots/plot_cspectrogram.cpp"
  "$deps/imgui-src/imgui.cpp" "$deps/imgui-src/imgui_draw.cpp" "$deps/imgui-src/imgui_tables.cpp" "$deps/imgui-src/imgui_widgets.cpp"
  "$deps/imgui-src/backends/imgui_impl_glfw.cpp" "$deps/imgui-src/backends/imgui_impl_opengl3.cpp"
  "$deps/implot-src/implot.cpp" "$deps/implot-src/implot_items.cpp"
)
objs=()
for src in "${lib_sources[@]}"; do
  obj="$out/obj/$(basename "${src%.cpp}").o"; objs+=("$obj")
  [ "$obj" -nt "$src" ] || em++ "${CXXFLAGS[@]}" -c "$src" -o "$obj" &
done; wait
rm -f "$out/libcler_web.a"; emar rcs "$out/libcler_web.a" "${objs[@]}"

build_example() { # name source [extra link flags...]
  local name="$1" src="$2"; shift 2
  echo "== example $name"
  em++ "${CXXFLAGS[@]}" -c "$src" -o "$out/obj/example_$name.o"
  em++ "$out/obj/example_$name.o" "$out/libcler_web.a" "$out/liquid/lib/libliquid.a" "${LDFLAGS[@]}" "$@" -o "$run/$name.html"
  chmod 644 "$run/$name".*
}
cp "$here/coi-sw.js" "$run/coi-sw.js"   # COOP/COEP for pages opened straight from docs/demos/

# earshot: no ImGui shell — the page is earshot's own browser client, its
# WebSocket bridged straight into the wasm module (web_server_wasm.cpp +
# earshot_glue.js), running the same graph as the native binary on --source sim.
build_earshot() {
  echo "== example earshot"
  local dir="$run/earshot" gen="$out/earshot_gen" src="$repo/desktop_examples/earshot"
  mkdir -p "$dir" "$gen"
  printf '#pragma once\n#define EARSHOT_VERSION "wasm demo"\n' > "$gen/earshot_version.hpp"
  printf '#pragma once\n#include "desktop_blocks/web/web_server.hpp"\nstatic const web::EmbeddedFile EARSHOT_CLIENT_FILES[] = {{"", "", 0}};\nstatic const size_t EARSHOT_CLIENT_FILES_COUNT = 0;\n' > "$gen/earshot_client_files.hpp"
  em++ "${CXXFLAGS[@]}" -I"$gen" -I"$src" -c "$src/earshot.cpp" -o "$out/obj/earshot.o"
  em++ "${CXXFLAGS[@]}" -c "$here/web_server_wasm.cpp" -o "$out/obj/web_server_wasm.o"
  em++ "$out/obj/earshot.o" "$out/obj/web_server_wasm.o" "$out/liquid/lib/libliquid.a" \
    -O2 -pthread -sPROXY_TO_PTHREAD -sPTHREAD_POOL_SIZE=16 -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=0 \
    -sEXPORTED_FUNCTIONS=_main,_malloc,_free,_earshot_ws_open,_earshot_ws_send \
    -sEXPORTED_RUNTIME_METHODS=HEAPU8,lengthBytesUTF8,stringToUTF8 \
    -o "$dir/earshot_core.js"
  cp "$src"/client/*.js "$dir/"
  cp "$here/earshot_glue.js" "$here/coi-sw.js" "$dir/"
  sed -e 's|/client/audio_worklet.js|./audio_worklet.js|' "$src/client/audio.js" > "$dir/audio.js"
  sed -e 's|<script type="module" src="/client/app.js"></script>|<script>if (window.crossOriginIsolated) { sessionStorage.removeItem("clerCoiReload"); } else if (navigator.serviceWorker \&\& !sessionStorage.getItem("clerCoiReload")) { sessionStorage.setItem("clerCoiReload","1"); navigator.serviceWorker.register("coi-sw.js").then(function(){return navigator.serviceWorker.ready;}).then(function(){location.reload();}); }</script><script src="./earshot_glue.js"></script><script async src="./earshot_core.js"></script><script type="module" src="./app.js"></script>|' \
    "$src/client/index.html" > "$dir/index.html"
  chmod 644 "$dir"/*
}
echo "== payload for the in-browser compiler"
cp "$out/libcler_web.a" "$out/liquid/lib/libliquid.a" "$payload/"
# The same flags, against the virtual repo the browser compiles in ($VIRTUAL_REPO_ROOT
# in cler-fg/app/src/web/emception.ts), so the two editions cannot drift apart.
cxxvirtual=()
for flag in "${CXXFLAGS[@]}"; do
  cxxvirtual+=("$(printf '%s' "$flag" | sed -e "s|^-I$out/liquid/include/liquid\$|-Iliquid|" \
    -e "s|^-I$deps|-Ibuild/_deps|" -e "s|^-I$repo\$|-I.|" -e "s|^-I$repo/|-I|")")
done
python3 -c 'import json, sys
count = int(sys.argv[2])
json.dump({"cxxflags": sys.argv[3:3 + count], "ldflags": sys.argv[3 + count:]}, open(sys.argv[1], "w"))' \
  "$payload/flags.json" "${#cxxvirtual[@]}" "${cxxvirtual[@]}" "${LDSHARED[@]}"
# Headers em++ needs that the app does not already bundle (it bundles desktop_blocks/**/*.hpp).
# Keys are the paths the in-browser build uses as its virtual repo root.
CLER_REPO="$repo" CLER_DEPS_DIR="$deps" CLER_LIQUID="$out/liquid/include/liquid/liquid.h" CLER_SHELL="$here/shell.html" \
python3 - "$payload/headers.json" <<'PY'
import glob, json, os, sys
repo, deps = os.environ["CLER_REPO"], os.environ["CLER_DEPS_DIR"]
out = {}
for path in glob.glob(f"{repo}/include/**/*.hpp", recursive=True):
    out[os.path.relpath(path, repo)] = open(path).read()
for sub in ("imgui-src", "imgui-src/backends", "implot-src"):
    for path in glob.glob(f"{deps}/{sub}/*.h"):
        out[f"build/_deps/{sub}/{os.path.basename(path)}"] = open(path).read()
out["liquid/liquid.h"] = open(os.environ["CLER_LIQUID"]).read()
out["shell.html"] = open(os.environ["CLER_SHELL"]).read()
json.dump(out, open(sys.argv[1], "w"))
print(f"  {len(out)} headers")
PY

build_example hello_world "$repo/desktop_examples/hello_world.cpp" &
build_example mass_spring_damper "$repo/desktop_examples/mass_spring_damper.cpp" &
build_example plots "$repo/desktop_examples/plots.cpp" &
build_example polyphase_channelizer "$repo/desktop_examples/polyphase_channelizer.cpp" &
build_example modem_loopback "$repo/desktop_examples/linear_modem_loopback.cpp" &
# the map's coastlines are read from a relative path, so they ride along in the wasm bundle
build_example ais_receiver "$repo/desktop_examples/ais_receiver/ais_receiver.cpp" \
  --embed-file "$repo/desktop_blocks/adsb/coastlines_map/ne_110m_coastline.shp@adsb_coastlines/ne_110m_coastline.shp" &
build_earshot &
wait
ls -la "$run"
