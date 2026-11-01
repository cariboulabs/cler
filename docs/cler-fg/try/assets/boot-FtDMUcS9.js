import{d as k,f as Z,a as $}from"./progress-QwEbTXKs.js";/**
 * @license
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: Apache-2.0
 */const Q=Symbol("Comlink.proxy"),pe=Symbol("Comlink.endpoint"),fe=Symbol("Comlink.releaseProxy"),M=Symbol("Comlink.finalizer"),I=Symbol("Comlink.thrown"),J=e=>typeof e=="object"&&e!==null||typeof e=="function",me={canHandle:e=>J(e)&&e[Q],serialize(e){const{port1:n,port2:t}=new MessageChannel;return ne(e,n),[t,[t]]},deserialize(e){return e.start(),se(e)}},he={canHandle:e=>J(e)&&I in e,serialize({value:e}){let n;return e instanceof Error?n={isError:!0,value:{message:e.message,name:e.name,stack:e.stack}}:n={isError:!1,value:e},[n,[]]},deserialize(e){throw e.isError?Object.assign(new Error(e.value.message),e.value):e.value}},ee=new Map([["proxy",me],["throw",he]]);function be(e,n){for(const t of e)if(n===t||t==="*"||t instanceof RegExp&&t.test(n))return!0;return!1}function ne(e,n=globalThis,t=["*"]){n.addEventListener("message",function s(r){if(!r||!r.data)return;if(!be(t,r.origin)){console.warn(`Invalid origin '${r.origin}' for comlink proxy`);return}const{id:i,type:_,path:l}=Object.assign({path:[]},r.data),d=(r.data.argumentList||[]).map(w);let o;try{const c=l.slice(0,-1).reduce((a,p)=>a[p],e),u=l.reduce((a,p)=>a[p],e);switch(_){case"GET":o=u;break;case"SET":c[l.slice(-1)[0]]=w(r.data.value),o=!0;break;case"APPLY":o=u.apply(c,d);break;case"CONSTRUCT":{const a=new u(...d);o=L(a)}break;case"ENDPOINT":{const{port1:a,port2:p}=new MessageChannel;ne(e,p),o=we(a,[a])}break;case"RELEASE":o=void 0;break;default:return}}catch(c){o={value:c,[I]:0}}Promise.resolve(o).catch(c=>({value:c,[I]:0})).then(c=>{const[u,a]=T(c);n.postMessage(Object.assign(Object.assign({},u),{id:i}),a),_==="RELEASE"&&(n.removeEventListener("message",s),te(n),M in e&&typeof e[M]=="function"&&e[M]())}).catch(c=>{const[u,a]=T({value:new TypeError("Unserializable return value"),[I]:0});n.postMessage(Object.assign(Object.assign({},u),{id:i}),a)})}),n.start&&n.start()}function ge(e){return e.constructor.name==="MessagePort"}function te(e){ge(e)&&e.close()}function se(e,n){const t=new Map;return e.addEventListener("message",function(r){const{data:i}=r;if(!i||!i.id)return;const _=t.get(i.id);if(_)try{_(i)}finally{t.delete(i.id)}}),N(e,t,[],n)}function R(e){if(e)throw new Error("Proxy has been released and is not useable")}function re(e){return S(e,new Map,{type:"RELEASE"}).then(()=>{te(e)})}const C=new WeakMap,D="FinalizationRegistry"in globalThis&&new FinalizationRegistry(e=>{const n=(C.get(e)||0)-1;C.set(e,n),n===0&&re(e)});function ye(e,n){const t=(C.get(n)||0)+1;C.set(n,t),D&&D.register(e,n,e)}function ke(e){D&&D.unregister(e)}function N(e,n,t=[],s=function(){}){let r=!1;const i=new Proxy(s,{get(_,l){if(R(r),l===fe)return()=>{ke(i),re(e),n.clear(),r=!0};if(l==="then"){if(t.length===0)return{then:()=>i};const d=S(e,n,{type:"GET",path:t.map(o=>o.toString())}).then(w);return d.then.bind(d)}return N(e,n,[...t,l])},set(_,l,d){R(r);const[o,c]=T(d);return S(e,n,{type:"SET",path:[...t,l].map(u=>u.toString()),value:o},c).then(w)},apply(_,l,d){R(r);const o=t[t.length-1];if(o===pe)return S(e,n,{type:"ENDPOINT"}).then(w);if(o==="bind")return N(e,n,t.slice(0,-1));const[c,u]=X(d);return S(e,n,{type:"APPLY",path:t.map(a=>a.toString()),argumentList:c},u).then(w)},construct(_,l){R(r);const[d,o]=X(l);return S(e,n,{type:"CONSTRUCT",path:t.map(c=>c.toString()),argumentList:d},o).then(w)}});return ye(i,e),i}function ve(e){return Array.prototype.concat.apply([],e)}function X(e){const n=e.map(T);return[n.map(t=>t[0]),ve(n.map(t=>t[1]))]}const ae=new WeakMap;function we(e,n){return ae.set(e,n),e}function L(e){return Object.assign(e,{[Q]:!0})}function T(e){for(const[n,t]of ee)if(t.canHandle(e)){const[s,r]=t.serialize(e);return[{type:"HANDLER",name:n,value:s},r]}return[{type:"RAW",value:e},ae.get(e)||[]]}function w(e){switch(e.type){case"HANDLER":return ee.get(e.name).deserialize(e.value);case"RAW":return e.value}}function S(e,n,t,s){return new Promise(r=>{const i=xe();n.set(i,r),e.start&&e.start(),e.postMessage(Object.assign({id:i},t),s)})}function xe(){return new Array(4).fill(0).map(()=>Math.floor(Math.random()*Number.MAX_SAFE_INTEGER).toString(16)).join("-")}const ze="https://jprendes.github.io/emception/",Se={"emception.worker.bundle.worker.js":"60b9f0fb7982f9395ef63872b5ed3b798377fab09a8666f28b67ccb5029c0107","f0283badd42fe745cbe4.wasm":"2c60c515eca756e80ddc752a6ac062e07f596eb70c7a1308321705f90e09b442","9d1e542b80004e27297f.wasm":"47a2b00defa938d4471ff6ffdbf4d424ee03599db7d8f56590c6223e96191631","cecdfcda360457a8f204.br":"9bd873132b4915a4da34a977a386a4ae68785df34b8cdb9c3d205fae26eeb772"},V=24992393,H="/working",ie="draft.o",Ee=["app.html","app.js","app.wasm","app.worker.js"],Be=["-O1","-sMINIFY_HTML=0","--shell-file","shell.html"];let U=null;const oe=new TextEncoder,A=()=>new URL("./",location.href).href,j=6e4;let B=null,v=()=>{},P=null;function Re(e){P=e}function Ie(){return P}function W(e,n,t,s){return Ce(e,s).then(async r=>(await G(r,n,oe.encode(t)),k({phase:"compile",detail:n}),ce(r,["em++",..._e().cxxflags,"-c",n,"-o",ie],s)))}async function Ae(e){if(!B)throw new Error("the C++ toolchain is not running — compile first");const n=await B,t=["em++",ie,"lib/libcler_web.a","lib/libliquid.a",..._e().ldflags,...Be,"-o","app.html"];k({phase:"link"});let s=!1;const r=await ce(n,t,_=>{!s&&/wasm-opt/.test(_)&&(s=!0,k({phase:"optimize"})),e(_)}),i={};if(r===0)for(const _ of Ee)i[_]=new Uint8Array(await n.fileSystem.readFile(`${H}/${_}`));return{code:r,files:i}}function Ce(e,n){return P?Promise.reject(new Error(P)):(v=n,B??=De(e).catch(t=>{throw B=null,t}),B)}async function De(e){let n=0,t=Date.now(),s=null;const r=o=>{const c=o.data;c?.toolchainError?(s=c.toolchainError,v(c.toolchainError)):c?.toolchain&&(n+=c.bytes??0,t=Date.now(),k(n>=V?{phase:"boot"}:{phase:"toolchain",bytes:n,total:V}),v(`downloading the C++ toolchain (first visit only)… ${(n/1e6).toFixed(1)} MB`))};navigator.serviceWorker.addEventListener("message",r),v("starting the in-browser C++ toolchain…"),k({phase:"boot"});const i=new Worker(`${A()}emception/emception.worker.bundle.worker.js`),_=se(i);_.onstdout=L(o=>v(o)),_.onstderr=L(o=>v(o));let l=0;const d=new Promise((o,c)=>{i.onerror=u=>c(new Error(s??`the C++ toolchain worker failed: ${u.message||"load error"}`)),l=self.setInterval(()=>{Date.now()-t<j||c(new Error(s??`the C++ toolchain stalled for ${j/1e3} s with no download progress`))},1e3)});try{await Promise.race([_.init(),d]),v("unpacking the cler headers and libraries…"),k({phase:"stage"}),await Te(_,e)}catch(o){throw i.terminate(),o}finally{clearInterval(l),navigator.serviceWorker.removeEventListener("message",r)}return _}function _e(){if(!U)throw new Error("the build flags are not staged — the toolchain never booted");return U}async function Te(e,n){U=await(await fetch(`${A()}payload/flags.json`)).json();const t=await(await fetch(`${A()}payload/headers.json`)).json();k({phase:"stage",detail:`${Object.keys(t).length} headers`});for(const[s,r]of Object.entries({...t,...n}))await G(e,s,oe.encode(r));for(const s of["libcler_web.a","libliquid.a"]){k({phase:"stage",detail:s});const r=await(await fetch(`${A()}payload/${s}`)).arrayBuffer();await G(e,`lib/${s}`,new Uint8Array(r))}}async function G(e,n,t){await e.fileSystem.mkdirTree(`${H}/${n}`.replace(/\/[^/]+$/,"")),await e.fileSystem.writeFile(`${H}/${n}`,t)}async function ce(e,n,t){return v=t,t(`$ ${n[0]} … ${n[n.length-2]} ${n[n.length-1]}`),(await e.run(...n)).returncode}const Pe=""+new URL("cler_web-CE4Yo-ES.wasm",import.meta.url).href,Me=new TextEncoder,le=new TextDecoder,F="../../demos/run/";function Fe(e){const n=()=>new DataView(e().buffer),t=()=>new Uint8Array(e().buffer);return{random_get(s,r){return crypto.getRandomValues(t().subarray(s,s+r)),0},environ_get(){return 0},environ_sizes_get(s,r){return n().setUint32(s,0,!0),n().setUint32(r,0,!0),0},clock_time_get(s,r,i){return n().setBigUint64(i,BigInt(Math.round(performance.now()*1e6)),!0),0},fd_close(){return 0},fd_seek(){return 70},fd_write(s,r,i,_){let l=0,d="";for(let o=0;o<i;o++){const c=n().getUint32(r+o*8,!0),u=n().getUint32(r+o*8+4,!0);d+=le.decode(t().subarray(c,c+u)),l+=u}return n().setUint32(_,l,!0),(s===2?console.error:console.log)(d),0},proc_exit(s){throw new Error(`cler-web.wasm exited with ${s}`)}}}async function qe(){return Oe(e=>WebAssembly.instantiateStreaming(fetch(Pe),e))}async function Oe(e){let n=null;const{instance:t}=await e({wasi_snapshot_preview1:Fe(()=>n.memory)});n=t.exports;const s=n;return(r,i)=>{const _=Me.encode(JSON.stringify({cmd:r,args:i})),l=s.cler_alloc(_.length);new Uint8Array(s.memory.buffer).set(_,l);const d=s.cler_invoke(l,_.length);s.cler_free(l,_.length);const o=new Uint8Array(s.memory.buffer);let c=d;for(;o[c]!==0;)c++;const u=JSON.parse(le.decode(o.subarray(d,c)));if(s.cler_free(d,c-d+1),"loud"in u)throw new Error(u.loud);if("err"in u)throw u.err;return u.ok}}async function Ne(e,n=[]){const t=await qe();for(const[a,p]of Object.entries(e))t("put_file",{path:a,text:p});const s=new Map,r=new Map;let i=1;const _=(a,p)=>{for(const[f,b]of s)b.event===a&&r.get(b.handler)?.({event:a,id:f,payload:p})},l=new Map,d=new Map,o=a=>t("open_document",{path:a}).source,c=(a,p,f)=>{const b=i++,y={inputs:{},recipeSha256:""};a==="build"&&d.set(p,b);const E=h=>{for(const g of String(h).split(`
`))g.trim()&&_(`${a}-output`,{jobId:b,inputKey:y,path:p,line:g})},m=h=>{a==="build"&&d.delete(p),_(`${a}-finished`,{jobId:b,inputKey:y,path:p,code:h})};return f(E).then(m,h=>{E(h instanceof Error?h.message:String(h)),m(1)}),{jobId:b,inputKey:y}},u=window;u.__TAURI_INTERNALS__={invoke:async(a,p={})=>{if(a==="plugin:dialog|open"||a==="plugin:dialog|save")return null;if(a==="plugin:event|listen"){const m=i++;return s.set(m,{event:p.event,handler:p.handler}),m}if(a==="plugin:event|unlisten")return s.delete(p.eventId),null;const f=p.path,b=n.find(m=>m.path===f&&m.source===o(f)),y=Ie();if(a==="find_target"){if(y&&!b)throw y;if(b)return{available:!0,reason:null,name:b.name,buildDir:null,binary:`${F}${b.name}.html`,artifact:{state:"ready",artifactPath:`${F}${b.name}.html`}};const m=d.get(f),h=await O(o(f)),g=`built/${h}/app.html`;return{available:!0,reason:null,name:f.split("/").pop()?.replace(/\.[^.]+$/,"")??"flowgraph",buildDir:null,binary:null,artifact:m!==void 0?{state:"building",jobId:m}:await q(h)?{state:"ready",artifactPath:g}:{state:"needs_build",reason:"compile this document in the browser first (Ctrl+B)"}}}if(a==="check_document"){if(y)throw y;return c("check",f,m=>W(e,f,o(f),m))}if(a==="build_target"){if(y)throw y;return c("build",f,async m=>{const h=o(f),g=await O(h);if(await q(g))return m(`built/${g}/app.html is already built — press Run`),0;const x=await W(e,f,h,m);if(x!==0)return x;const z=await Ae(m);return z.code!==0?z.code:(k({phase:"store"}),await Ue(g,z.files),0)})}if(a==="run_target"){let m=`${F}${b?.name}.html`;if(!b){const K=await O(o(f));if(!await q(K))throw"this edit is not built yet — press Build (Ctrl+B) first";m=`built/${K}/app.html`}const h=i++;k({phase:"launch"});const g=window.open(m,"_blank","popup,width=1280,height=800");if(!g)throw"the browser blocked the run window — allow popups for this site";const x={inputs:{},recipeSha256:""},z=window.setInterval(()=>{g.closed&&(window.clearInterval(z),l.delete(f),_("run-finished",{jobId:h,inputKey:x,path:f,code:0}))},500);return l.set(f,{win:g,jobId:h,timer:z}),_("run-output",{jobId:h,inputKey:x,path:f,line:`running ${m} in a new window — close it or press Stop`}),{jobId:h,inputKey:x}}if(a==="stop_target")return l.get(f)?.win.close(),null;const E=t(a,p);return a==="save_document"&&Ge(f,E.source),E},transformCallback:a=>{const p=i++;return r.set(p,a),p},metadata:{}},u.__TAURI_EVENT_PLUGIN_INTERNALS__={unregisterListener:(a,p)=>s.delete(p)}}const Le="cler-built",He=5,de=()=>caches.open(Le),ue=e=>new URL(e,location.href).pathname;async function q(e){return!!await de().then(n=>n.match(ue(`built/${e}/app.html`)))}async function Ue(e,n){const t=await de();for(const[i,_]of Object.entries(n))await t.put(ue(`built/${e}/${i}`),new Response(_));const s=await t.keys(),r=[...new Set(s.map(i=>new URL(i.url).pathname.split("/built/")[1]?.split("/")[0]))];for(const i of r.slice(0,Math.max(0,r.length-He)))for(const _ of s)_.url.includes(`/built/${i}/`)&&await t.delete(_)}async function O(e){const n=await crypto.subtle.digest("SHA-256",new TextEncoder().encode(e));return Array.from(new Uint8Array(n),t=>t.toString(16).padStart(2,"0")).join("").slice(0,16)}function Ge(e,n){const t=e.split("/").pop()??"flowgraph.cpp",s=URL.createObjectURL(new Blob([n],{type:"text/plain"})),r=document.createElement("a");r.href=s,r.download=t,r.click(),URL.revokeObjectURL(s)}const Ye=`#pragma once

#include "cler.hpp"
#include "adsb_types.hpp"
#include "desktop_blocks/gui/map_canvas.hpp"
#include "modes.h"
#include "cpr.h"
#include <unordered_map>
#include <imgui.h>
#include <cmath>

struct ADSBAggregateBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;
    cler::Channel<mode_s_msg> in;

    typedef void (*OnAircraftUpdateCallback)(const ADSBState&, void* context);

    ADSBAggregateBlock(const char* name,
                       float initial_map_center_lat = 32.0f,
                       float initial_map_center_lon = 34.0f,
                       OnAircraftUpdateCallback callback = nullptr,
                       void* callback_context = nullptr,
                       const char* coastline_data_path = "adsb_coastlines/ne_110m_coastline.shp")
        : BlockBase(name), in(1024),
          _callback(callback), _callback_context(callback_context),
          _map(initial_map_center_lat, initial_map_center_lon, coastline_data_path) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        auto [read_ptr, read_size] = in.read_dbf();
        if (!read_ptr || read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        uint32_t now = static_cast<uint32_t>(std::time(nullptr));

        for (size_t i = 0; i < read_size; ++i) {
            const mode_s_msg& msg = read_ptr[i];

            uint32_t icao = (msg.aa1 << 16) | (msg.aa2 << 8) | msg.aa3;
            ADSBState& state = _aircraft[icao];

            if (state.icao == 0) {
                state.icao = icao;
            }

            bool state_changed = false;

            // Update callsign if present (DF17 metype 1-4)
            if (msg.msgtype == 17 && msg.metype >= 1 && msg.metype <= 4) {
                if (msg.flight[0] != '\\0') {
                    if (std::strncmp(state.callsign, msg.flight, 8) != 0) {
                        std::strncpy(state.callsign, msg.flight, 8);
                        state.callsign[8] = '\\0';
                        state_changed = true;
                    }
                }
            }

            if (msg.altitude > 0) {
                if (state.altitude != msg.altitude) {
                    state.altitude = msg.altitude;
                    state_changed = true;
                }
            }

            // Update velocity if present (DF17 metype 19)
            if (msg.msgtype == 17 && msg.metype == 19) {
                if (msg.velocity > 0) {
                    if (static_cast<int>(state.groundspeed) != msg.velocity) {
                        state.groundspeed = static_cast<float>(msg.velocity);
                        state_changed = true;
                    }
                }
                if (msg.heading >= 0 && msg.heading <= 360) {
                    if (static_cast<int>(state.track) != msg.heading) {
                        state.track = static_cast<float>(msg.heading);
                        state_changed = true;
                    }
                }
                if (msg.vert_rate != 0) {
                    if (state.vertical_rate != msg.vert_rate) {
                        state.vertical_rate = msg.vert_rate;
                        state_changed = true;
                    }
                }
            }

            // Update position if present (DF17 metype 9-18)
            // CPR (Compact Position Reporting) requires both even and odd frames
            if (msg.msgtype == 17 && msg.metype >= 9 && msg.metype <= 18) {
                // Store the raw CPR values (msg.raw_latitude/longitude are unsigned 17-bit values)
                if (msg.fflag == 0) {  // Even frame
                    state.last_even_cprlat = msg.raw_latitude;
                    state.last_even_cprlon = msg.raw_longitude;
                    state.has_even_position = true;
                } else {  // Odd frame (fflag == 1)
                    state.last_odd_cprlat = msg.raw_latitude;
                    state.last_odd_cprlon = msg.raw_longitude;
                    state.has_odd_position = true;
                }

                if (state.has_even_position && state.has_odd_position) {
                    double lat, lon;
                    int result = decodeCPRairborne(
                        state.last_even_cprlat, state.last_even_cprlon,
                        state.last_odd_cprlat, state.last_odd_cprlon,
                        msg.fflag,
                        &lat, &lon
                    );

                    if (result == 0) {  // Success
                        state.lat = lat;
                        state.lon = lon;
                        state.position_valid = true;
                        state.position_update_time = now;
                        state_changed = true;
                    } else {
                        // CPR decode failed, reset for next attempt
                        state.has_even_position = false;
                        state.has_odd_position = false;
                    }
                }
            }

            state.last_update_time = now;
            state.message_count++;

            if (state_changed && _callback) {
                _callback(state, _callback_context);
            }
        }

        in.commit_read(read_size);
        return cler::Empty{};
    }

    size_t get_aircrafts(ADSBState* buf, size_t max_count) const {
        size_t count = 0;
        for (const auto& pair : _aircraft) {
            if (count >= max_count) break;
            buf[count++] = pair.second;
        }
        return count;
    }

    size_t aircraft_count() const {
        return _aircraft.size();
    }

    void render() {
        ImGui::SetNextWindowSize(_initial_window_size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(_initial_window_position, ImGuiCond_FirstUseEver);

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

        ImGui::Begin("ADSB Map", nullptr, window_flags);

        _map.begin();
        draw_aircraft(ImGui::GetWindowDrawList());

        ImGui::SetCursorScreenPos(ImVec2(_map.pos.x + INFO_TEXT_OFFSET_X, _map.pos.y + _map.size.y - INFO_TEXT_OFFSET_Y));
        const char latitude_hemisphere = _map.center_lat >= 0.0f ? 'N' : 'S';
        const char longitude_hemisphere = _map.center_lon >= 0.0f ? 'E' : 'W';
        ImGui::Text("Aircraft: %zu | Center: %.2f°%c, %.2f°%c | Zoom: %.1fx",
                    _aircraft.size(), std::fabs(_map.center_lat), latitude_hemisphere,
                    std::fabs(_map.center_lon), longitude_hemisphere, _map.zoom);

        _map.interact();
        ImGui::End();
    }

    void set_initial_window(float x, float y, float w, float h) {
        _initial_window_position = ImVec2(x, y);
        _initial_window_size = ImVec2(w, h);
    }

private:
    static constexpr float TRIANGLE_SIZE = 8.0f;
    static constexpr float MAX_ALTITUDE_FOR_COLOR = 40000.0f;
    static constexpr float INFO_TEXT_OFFSET_X = 10.0f;
    static constexpr float INFO_TEXT_OFFSET_Y = 30.0f;
    static constexpr float LABEL_OFFSET_X = 10.0f;
    static constexpr float LABEL_OFFSET_Y_CALLSIGN = -8.0f;
    static constexpr float INITIAL_WINDOW_SIZE_X = 1400.0f;
    static constexpr float INITIAL_WINDOW_SIZE_Y = 800.0f;

    std::unordered_map<uint32_t, ADSBState> _aircraft;
    OnAircraftUpdateCallback _callback;
    void* _callback_context;

    ImVec2 _initial_window_position{0.0f, 0.0f};
    ImVec2 _initial_window_size{INITIAL_WINDOW_SIZE_X, INITIAL_WINDOW_SIZE_Y};


    MapCanvas _map;

    void draw_aircraft(ImDrawList* draw_list) {
        for (const auto& pair : _aircraft) {
            const ADSBState& state = pair.second;
            // no fallback: unpositioned aircraft are not drawn
            if (!state.position_valid) continue;
            const ImVec2 pos = _map.to_screen(static_cast<float>(state.lat), static_cast<float>(state.lon));
            const float alt_norm = std::min(1.0f, state.altitude / MAX_ALTITUDE_FOR_COLOR);
            _map.marker(draw_list, pos, state.track, TRIANGLE_SIZE,
                        ImGui::GetColorU32(ImVec4(alt_norm, 0.5f, 1.0f - alt_norm, 1.0f)));
            if (state.callsign[0] != '\\0') {
                draw_list->AddText(ImVec2(pos.x + LABEL_OFFSET_X, pos.y + LABEL_OFFSET_Y_CALLSIGN),
                                   IM_COL32(255, 255, 255, 255), state.callsign);
            }
        }
    }
};
`,Ke=`#pragma once
// moved: the loader is map infrastructure shared by the ADS-B and AIS maps
#include "desktop_blocks/gui/coastline_loader.hpp"
`,Xe=`#pragma once

#include "cler.hpp"
#include "desktop_blocks/adsb/modes.h"
#include "desktop_blocks/adsb/modes_2400.h"
#include <cstring>
#include <algorithm>
#include <atomic>

struct ADSBDecoderBlock : public cler::BlockBase {
    cler::Channel<uint16_t> in;

    constexpr static size_t BUFFER_ELEMENTS = cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(uint16_t) * 1000;

    enum class SampleRateMode {
        RATE_2MHZ,
        RATE_2_4MHZ
    };

    // Bitmask of DFs to pass through (e.g., 1<<17 for DF17)
    // Default: 0xFFFFFFFFU accepts all 32 message types
    // Use specific bits to filter to desired message types
    ADSBDecoderBlock(const char* name, SampleRateMode mode = SampleRateMode::RATE_2MHZ, uint32_t df_filter = 0xFFFFFFFFU)
        : BlockBase(name),
        in(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(uint16_t)),
        _mode(mode),
        _df_filter(df_filter),
        _tmp_buffer(new uint16_t[BUFFER_ELEMENTS]) {
        // Validate filter: 0 is ambiguous, must use 0xFFFFFFFFU for "allow all messages"
        assert(_df_filter != 0 && "df_filter=0 is invalid. Use 0xFFFFFFFFU to allow all message types.");
        mode_s_init(&_decoder_state);
    }

    ~ADSBDecoderBlock() {
        if (_tmp_buffer) {
            delete[] _tmp_buffer;
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<mode_s_msg>* out) {
        static size_t call_count = 0;
        static size_t total_samples = 0;

        auto [read_ptr, read_size] = in.read_dbf();

        // Mode S detection requires at least MODES_LONG_MSG_SAMPLES samples
        // (preamble + longest message at 2 samples per bit)
        constexpr size_t MODES_LONG_MSG_SAMPLES = 240;  // 16 preamble + 112*2 bits
        if (read_size < MODES_LONG_MSG_SAMPLES) {
            return cler::Error::NotEnoughSamples;
        }

        size_t write_space = out->space();
        if (write_space == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t to_process = std::min(read_size, BUFFER_ELEMENTS);

        memcpy(_tmp_buffer, read_ptr, to_process * sizeof(uint16_t));
        in.commit_read(to_process);

        total_samples += to_process;
        call_count++;

        CallbackContext ctx;
        ctx.out_channel = out;
        ctx.df_filter = _df_filter.load(std::memory_order_relaxed);

        if (_mode == SampleRateMode::RATE_2MHZ) {
            mode_s_detect(&_decoder_state, _tmp_buffer, to_process, on_message_detected, &ctx);
        } else {
            mode_s_detect_2400(&_decoder_state, _tmp_buffer, to_process, on_message_detected, &ctx);
        }

        return cler::Empty{};
    }

    void set_df_filter(uint32_t df_filter) {
        _df_filter.store(df_filter, std::memory_order_relaxed);
    }

private:
    mode_s_t _decoder_state;
    SampleRateMode _mode;
    std::atomic<uint32_t> _df_filter;
    uint16_t* _tmp_buffer = nullptr;

    struct CallbackContext {
        cler::ChannelBase<mode_s_msg>* out_channel;
        uint32_t df_filter;
    };

    static void on_message_detected(mode_s_t* self, struct mode_s_msg* mm, void* context) {
        static size_t total_messages = 0;
        static size_t good_crc_messages = 0;

        total_messages++;
        if (mm->crcok) {
            good_crc_messages++;
        }

        (void)self;
        CallbackContext* ctx = static_cast<CallbackContext*>(context);

        if (!mm->crcok) {
            return;
        }

        if (ctx->df_filter != 0) {
            if ((ctx->df_filter & (1U << mm->msgtype)) == 0) {
                return;
            }
        }

        if (ctx->out_channel->space() > 0) {
            ctx->out_channel->push(*mm);
        }
    }
};
`,Ve=`#pragma once

#include <cstdint>
#include <cstring>
#include <chrono>

// aggregated aircraft state, unified across multiple Mode S messages
struct ADSBState {
    uint32_t icao;

    char callsign[9];                // 8 chars + null terminator

    double lat;                      // degrees
    double lon;                      // degrees
    uint32_t position_update_time;
    bool position_valid;

    // CPR (Compact Position Reporting) needs one even + one odd frame to decode a position
    int last_even_cprlat;            // 17 bits
    int last_even_cprlon;            // 17 bits
    int last_odd_cprlat;             // 17 bits
    int last_odd_cprlon;             // 17 bits
    bool has_even_position;
    bool has_odd_position;

    int altitude;                    // feet
    float groundspeed;                // knots
    float track;                     // degrees, 0-360
    int vertical_rate;               // feet/minute

    uint32_t last_update_time;
    int message_count;

    ADSBState()
        : icao(0), lat(0.0), lon(0.0), position_update_time(0),
          position_valid(false),
          last_even_cprlat(0), last_even_cprlon(0),
          last_odd_cprlat(0), last_odd_cprlon(0),
          has_even_position(false), has_odd_position(false),
          altitude(0), groundspeed(0.0f), track(0.0f), vertical_rate(0),
          last_update_time(0), message_count(0) {
        callsign[0] = '\\0';
    }
};
`,je=`#pragma once

#include <array>
#include <cstdint>
#include <cstring>

// AIS (ITU-R M.1371) bit level: HDLC deframing with bit unstuffing and the
// CRC-16/X-25 frame check, and the message parser for the position, static
// data and base station reports. No DSP, no allocation.
namespace ais {

struct Message {
    uint8_t type = 0;
    uint32_t mmsi = 0;
    bool has_position = false;
    double lat = 0.0, lon = 0.0;   // degrees
    float sog = 0.0f;              // knots, <0 unknown
    float cog = 0.0f;              // degrees, <0 unknown
    int heading = -1;              // degrees, -1 unknown
    int nav_status = -1;           // type 1/2/3
    char name[21] = {};            // type 5 / 24A
    char callsign[8] = {};         // type 5
    uint8_t ship_type = 0;         // type 5 / 24B
    uint8_t bits_len = 0;          // payload bits / 8 (info)
};

// CRC-16/X-25 (HDLC FCS): poly 0x1021 reflected, init and xorout 0xFFFF.
inline uint16_t crc16_x25(const uint8_t* data, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) crc = (crc & 1u) ? static_cast<uint16_t>((crc >> 1) ^ 0x8408) : static_cast<uint16_t>(crc >> 1);
    }
    return static_cast<uint16_t>(crc ^ 0xFFFF);
}

// Feed NRZI-decoded bits. Collects the bits between HDLC flags (0x7E),
// unstuffs, regroups LSB-first into octets, checks the FCS and hands a valid
// payload to the caller. Frames longer than MAX_BYTES are dropped.
class Deframer {
public:
    // AX.25 maximum: 10 addresses + control + PID + 256-octet info + FCS.
    // AIS frames are at most 126 octets.
    static constexpr size_t MAX_BYTES = 330;

    // returns true when a CRC-valid payload is ready in payload()/length()
    bool push_bit(bool bit) {
        _flag_shift = static_cast<uint8_t>((_flag_shift << 1) | (bit ? 1u : 0u));
        bool ready = false;
        if (_flag_shift == 0x7E) {
            // the flag's leading 0 and five 1s went into the buffer (the sixth
            // 1 was held back as a possible stuffing position); drop them
            if (_in_frame && _nbits >= 6 + 24) {
                _nbits -= 6;
                ready = finish();
            }
            _in_frame = true;
            _nbits = 0;
            _ones = 0;
            return ready;
        }
        if (!_in_frame) return false;
        if (_ones == 5) {          // after five ones: 0 = stuffed, 1 = sixth one (flag or abort)
            _ones = bit ? 6 : 0;
            return false;
        }
        if (_ones == 6) {          // seven ones = abort (a flag would have matched above)
            _in_frame = false;
            _ones = 0;
            return false;
        }
        _ones = bit ? _ones + 1 : 0;
        if (_nbits >= MAX_BYTES * 8) { _in_frame = false; return false; }
        _bits[_nbits++] = bit;
        return false;
    }

    const uint8_t* payload() const { return _bytes.data(); }
    size_t length() const { return _len; }
    uint32_t frames_ok() const { return _ok; }
    uint32_t frames_bad_crc() const { return _bad; }
    bool in_frame() const { return _in_frame; }

private:
    bool finish() {
        // Drop the partial trailing octet (the bits of the next flag we
        // consumed) and regroup LSB-first.
        const size_t nbytes = _nbits / 8;
        if (nbytes < 4) return false;
        for (size_t i = 0; i < nbytes; ++i) {
            uint8_t b = 0;
            for (int k = 0; k < 8; ++k) b |= static_cast<uint8_t>(_bits[8 * i + k] ? (1u << k) : 0u);
            _bytes[i] = b;
        }
        const uint16_t fcs = static_cast<uint16_t>(_bytes[nbytes - 2] | (_bytes[nbytes - 1] << 8));
        if (crc16_x25(_bytes.data(), nbytes - 2) != fcs) { ++_bad; return false; }
        _len = nbytes - 2;
        ++_ok;
        return true;
    }

    std::array<bool, MAX_BYTES * 8> _bits{};
    std::array<uint8_t, MAX_BYTES> _bytes{};
    size_t _nbits = 0, _len = 0;
    uint8_t _flag_shift = 0;
    int _ones = 0;
    bool _in_frame = false;
    uint32_t _ok = 0, _bad = 0;
};

// Big-endian bit field reader over the payload octets (message bit order).
inline uint32_t bits(const uint8_t* p, size_t len_bits, int start, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; ++i) {
        const int b = start + i;
        const uint32_t bit = (static_cast<size_t>(b) < len_bits) ? ((p[b >> 3] >> (7 - (b & 7))) & 1u) : 0u;
        v = (v << 1) | bit;
    }
    return v;
}

inline int32_t sbits(const uint8_t* p, size_t len_bits, int start, int n) {
    uint32_t v = bits(p, len_bits, start, n);
    if (v & (1u << (n - 1))) v |= ~((1u << n) - 1u);
    return static_cast<int32_t>(v);
}

inline void text(const uint8_t* p, size_t len_bits, int start, int nchars, char* out) {
    int w = 0;
    for (int i = 0; i < nchars; ++i) {
        uint32_t c = bits(p, len_bits, start + 6 * i, 6);
        char ch = static_cast<char>(c < 32 ? c + 64 : c);
        if (ch == '@') break;
        out[w++] = ch;
    }
    while (w > 0 && out[w - 1] == ' ') --w;
    out[w] = 0;
}

inline bool parse(const uint8_t* p, size_t nbytes, Message& m) {
    const size_t len_bits = nbytes * 8;
    m = Message{};
    m.bits_len = static_cast<uint8_t>(nbytes);
    m.type = static_cast<uint8_t>(bits(p, len_bits, 0, 6));
    m.mmsi = bits(p, len_bits, 8, 30);
    auto pos = [&](int lon_at, int lat_at) {
        const int32_t lon = sbits(p, len_bits, lon_at, 28), lat = sbits(p, len_bits, lat_at, 27);
        if (lon == 0x6791AC0 || lat == 0x3412140) return;   // 181 / 91 = not available
        m.lon = lon / 600000.0; m.lat = lat / 600000.0;
        m.has_position = (m.lat >= -90.0 && m.lat <= 90.0 && m.lon >= -180.0 && m.lon <= 180.0);
    };
    switch (m.type) {
        case 1: case 2: case 3:
            if (len_bits < 168) return false;
            m.nav_status = static_cast<int>(bits(p, len_bits, 38, 4));
            { const uint32_t sog = bits(p, len_bits, 50, 10); m.sog = sog == 1023 ? -1.0f : sog / 10.0f; }
            pos(61, 89);
            { const uint32_t cog = bits(p, len_bits, 116, 12); m.cog = cog == 3600 ? -1.0f : cog / 10.0f; }
            { const uint32_t hdg = bits(p, len_bits, 128, 9); m.heading = hdg == 511 ? -1 : static_cast<int>(hdg); }
            return true;
        case 4: case 11:
            if (len_bits < 168) return false;
            pos(79, 107);
            return true;
        case 18:
            if (len_bits < 168) return false;
            { const uint32_t sog = bits(p, len_bits, 46, 10); m.sog = sog == 1023 ? -1.0f : sog / 10.0f; }
            pos(57, 85);
            { const uint32_t cog = bits(p, len_bits, 112, 12); m.cog = cog == 3600 ? -1.0f : cog / 10.0f; }
            { const uint32_t hdg = bits(p, len_bits, 124, 9); m.heading = hdg == 511 ? -1 : static_cast<int>(hdg); }
            return true;
        case 5:
            if (len_bits < 420) return false;
            text(p, len_bits, 70, 7, m.callsign);
            text(p, len_bits, 112, 20, m.name);
            m.ship_type = static_cast<uint8_t>(bits(p, len_bits, 232, 8));
            return true;
        case 24:
            if (len_bits < 160) return false;
            if (bits(p, len_bits, 38, 2) == 0) text(p, len_bits, 40, 20, m.name);
            else m.ship_type = static_cast<uint8_t>(bits(p, len_bits, 40, 8));
            return true;
        case 21:
            if (len_bits < 272) return false;
            text(p, len_bits, 43, 20, m.name);
            pos(164, 192);
            return true;
        default:
            return len_bits >= 40;
    }
}

// Encoder for tests and loopback: payload octets (message bit order) ->
// transmitted bit stream: training, flag, stuffed data + FCS, flag; then
// NRZI (0 = transition). Returns the number of bits written.
inline size_t encode_frame(const uint8_t* payload, size_t nbytes, bool* out, size_t max_bits) {
    size_t n = 0;
    auto put = [&](bool b) { if (n < max_bits) out[n++] = b; };
    for (int i = 0; i < 24; ++i) put(i & 1);           // training 0101...
    auto flag = [&]() { for (int i = 0; i < 8; ++i) put((0x7E >> i) & 1); };  // LSB first = 01111110 either way
    flag();
    const uint16_t fcs = crc16_x25(payload, nbytes);
    int ones = 0;
    auto data_bit = [&](bool b) {
        put(b);
        if (b) { if (++ones == 5) { put(false); ones = 0; } } else ones = 0;
    };
    for (size_t i = 0; i < nbytes; ++i) for (int k = 0; k < 8; ++k) data_bit((payload[i] >> k) & 1);
    for (int k = 0; k < 8; ++k) data_bit((fcs >> k) & 1);
    for (int k = 0; k < 8; ++k) data_bit((fcs >> (8 + k)) & 1);
    flag();
    for (int i = 0; i < 24; ++i) put(false);          // buffer
    // NRZI: a 0 flips the line, a 1 keeps it
    bool level = false;
    for (size_t i = 0; i < n; ++i) { if (!out[i]) level = !level; out[i] = level; }
    return n;
}

// NMEA 6-bit armoring (for tests built from real sentences)
inline size_t from_nmea_payload(const char* s, uint8_t* out, size_t max) {
    size_t nbits = 0;
    std::memset(out, 0, max);
    for (const char* c = s; *c; ++c) {
        int v = *c - 48;
        if (v > 40) v -= 8;
        for (int i = 5; i >= 0; --i) {
            if (nbits / 8 >= max) return nbits / 8;
            if ((v >> i) & 1) out[nbits / 8] |= static_cast<uint8_t>(1u << (7 - nbits % 8));
            ++nbits;
        }
    }
    return (nbits + 7) / 8;
}

}  // namespace ais
`,We=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/ais/ais.hpp"
#include "liquid.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <vector>

// One AIS channel: complex baseband at \`sample_rate\` (an integer multiple of
// 9600 baud, default 48 kS/s) -> channel lowpass -> GMSK quadrature demod ->
// GMSK receive filter -> burst decoder -> parsed messages.
//
// Bursts are short (256 bits) and arrive after noise, so instead of a timing
// PLL the decoder correlates the discriminator output with the 0101 training
// sequence (passed through the same receive chain at construction). The best
// window gives the symbol phase (widest eye) and the DC offset (carrier
// error, the quartile midpoint of the eye samples); the burst is then sampled
// at a fixed rate, NRZI-decoded and handed to the HDLC deframer. An IQ power
// squelch against a tracked noise floor keeps noise from triggering.
struct AISDecoderBlock : public cler::BlockBase {
    static constexpr double BAUD = 9600.0;
    cler::Channel<std::complex<float>> in;

    AISDecoderBlock(const char* name, double sample_rate = 48e3, size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size)
    {
        if (buffer_size > 0 && buffer_size * sizeof(std::complex<float>) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        const double sps = sample_rate / BAUD;
        if (std::fabs(sps - std::round(sps)) > 1e-6 || sps < 4.0 || sps > 16.0) {
            cler::panic("AISDecoderBlock: sample_rate must be 4..16 x 9600");
        }
        _sps = static_cast<unsigned int>(std::lround(sps));
        _win = PREAMBLE_SYMBOLS * _sps;
        _ring.assign(_win, 0.0f);
        _tmpl.assign(4 * _sps, 0.0f);

        // channel lowpass +/-8 kHz (AIS channels are 25 kHz apart; a tighter
        // edge distorts the discriminator for offset carriers)
        liquid_firdes_kaiser(CH_TAPS, static_cast<float>(8e3 / sample_rate), 60.0f, 0.0f, _ch_taps.data());
        float g = 0.0f;
        for (float t : _ch_taps) g += t;
        for (float& t : _ch_taps) t /= g;
        _chf = firfilt_crcf_create(_ch_taps.data(), CH_TAPS);
        // +/-2400 Hz deviation (h = 0.5) -> +/-1
        _demod = freqdem_create(static_cast<float>(BAUD / 4.0 / sample_rate));
        _rxf = firfilt_rrrf_create_rnyquist(LIQUID_FIRFILT_GMSKRX, _sps, 3, 0.4f, 0);
        if (!_chf || !_demod || !_rxf) cler::panic("AISDecoderBlock: liquid create failed");
        _pw_alpha = 1.0f / (8.0f * _sps);
        build_template();
    }

    ~AISDecoderBlock() {
        if (_chf) firfilt_crcf_destroy(_chf);
        if (_demod) freqdem_destroy(_demod);
        if (_rxf) firfilt_rrrf_destroy(_rxf);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<ais::Message>* out) {
        auto [rptr, rsize] = in.read_dbf();
        if (rsize == 0) return cler::Error::NotEnoughSamples;
        auto [wptr, wsize] = out->write_dbf();
        if (wsize == 0) return cler::Error::NotEnoughSpace;
        // a frame needs >= 200 symbols, so this many samples cannot outrun the output
        const size_t n = std::min(rsize, wsize * 200 * _sps);
        size_t written = 0;

        for (size_t i = 0; i < n; ++i) {
            liquid_float_complex x;
            firfilt_crcf_push(_chf, rptr[i]);
            firfilt_crcf_execute(_chf, &x);
            float f;
            freqdem_demodulate(_demod, x, &f);
            f = std::clamp(f, -3.0f, 3.0f);
            firfilt_rrrf_push(_rxf, f);
            float y;
            firfilt_rrrf_execute(_rxf, &y);
            if (sample(y, std::norm(rptr[i])) && written < wsize) {
                ais::Message m;
                if (ais::parse(_framer.payload(), _framer.length(), m)) {
                    wptr[written++] = m;
                    _messages.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        in.commit_read(n);
        out->commit_write(written);
        return cler::Empty{};
    }

    uint32_t frames_ok() const { return _framer.frames_ok(); }
    uint32_t frames_bad_crc() const { return _framer.frames_bad_crc(); }
    uint64_t messages() const { return _messages.load(std::memory_order_relaxed); }
    uint64_t bursts() const { return _bursts.load(std::memory_order_relaxed); }

private:
    static constexpr unsigned int CH_TAPS = 63;
    static constexpr unsigned int PREAMBLE_SYMBOLS = 16;   // of the 24 sent; leaves a plateau to pick from
    static constexpr float CORR_THRESHOLD = 0.6f;

    void build_template() {
        gmskmod mod = gmskmod_create(_sps, 3, 0.4f);
        freqdem dem = freqdem_create(static_cast<float>(BAUD / 4.0 / (BAUD * _sps)));
        firfilt_crcf chf = firfilt_crcf_create(_ch_taps.data(), CH_TAPS);
        firfilt_rrrf rxf = firfilt_rrrf_create_rnyquist(LIQUID_FIRFILT_GMSKRX, _sps, 3, 0.4f, 0);
        std::vector<float> out;
        std::vector<liquid_float_complex> buf(_sps);
        // NRZI levels of the 0101 training: 1 1 0 0 repeating
        for (int i = 0; i < 64; ++i) {
            gmskmod_modulate(mod, ((i / 2) % 2 == 0) ? 1u : 0u, buf.data());
            for (unsigned int k = 0; k < _sps; ++k) {
                liquid_float_complex x;
                firfilt_crcf_push(chf, buf[k]);
                firfilt_crcf_execute(chf, &x);
                float f, y;
                freqdem_demodulate(dem, x, &f);
                firfilt_rrrf_push(rxf, f);
                firfilt_rrrf_execute(rxf, &y);
                out.push_back(y);
            }
        }
        std::copy_n(out.begin() + 40 * _sps, 4 * _sps, _tmpl.begin());
        float m = 0.0f;
        for (float v : _tmpl) m = std::max(m, std::fabs(v));
        for (float& v : _tmpl) v /= m;
        gmskmod_destroy(mod);
        freqdem_destroy(dem);
        firfilt_crcf_destroy(chf);
        firfilt_rrrf_destroy(rxf);
    }

    // one receive-filtered discriminator sample; returns true when a CRC-valid
    // frame completed
    bool sample(float y, float power) {
        _ring[_w] = y;
        _w = (_w + 1) % _win;
        ++_cnt;
        _pw += _pw_alpha * (power - _pw);
        if (_cnt < 4 * _win) _nf = _pw;
        else if (_pw < _nf) _nf = _pw;
        else _nf *= 1.0005f;

        if (!_decoding) {
            if (_cnt < 4 * _win) return false;
            float c = 0.0f, a = 0.0f;
            const size_t P = _tmpl.size();
            for (size_t k = 0; k < _win; ++k) {
                const float v = _ring[(_w + k) % _win];
                c += v * _tmpl[k % P];
                a += std::fabs(v);
            }
            const float nc = std::fabs(c) / (a + 1e-6f);
            const bool loud = _pw > 4.0f * _nf;
            if (loud && nc > CORR_THRESHOLD && nc >= _peak - 0.03f) {
                if (nc > _peak) _peak = nc;
                _peak_age = 0;
                pick_phase_and_dc();
                _peak_cnt = _cnt;
            } else if (_peak > CORR_THRESHOLD && (nc < _peak - 0.08f || ++_peak_age > 2 * _sps)) {
                // plateau ended: decode the kept window (the preamble), then continue live
                _decoding = true;
                _syms = 0;
                _prev = false;
                _peak = 0.0f;
                _peak_age = 0;
                _bursts.fetch_add(1, std::memory_order_relaxed);
                _sample_mod = static_cast<unsigned int>(((_peak_cnt - static_cast<int64_t>(_win) + _ph) % _sps + _sps) % _sps);
                bool done = false;
                for (size_t j = 0; j < _win; ++j) {
                    const int64_t c0 = _cnt - static_cast<int64_t>(_win) + static_cast<int64_t>(j);
                    if (static_cast<unsigned int>(((c0 % _sps) + _sps) % _sps) != _sample_mod) continue;
                    if (decide(_ring[(_w + j) % _win])) done = true;
                }
                return done;
            }
            return false;
        }

        // _cnt was incremented for this sample, so its index is _cnt - 1: the
        // same convention the replay loop above uses (c0 = _cnt - _win + j)
        if (static_cast<unsigned int>((_cnt - 1) % _sps) != _sample_mod) return false;
        const bool done = decide(y);
        const uint32_t frames = _framer.frames_ok() + _framer.frames_bad_crc();
        if (frames != _frames_seen || _syms > 1100 || (_syms > 40 && !_framer.in_frame())) {
            _frames_seen = frames;
            _decoding = false;
        }
        return done;
    }

    bool decide(float v) {
        const bool level = (v - _dc) > 0.0f;
        const bool bit = !(level ^ _prev);   // NRZI: no transition = 1
        _prev = level;
        ++_syms;
        return _framer.push_bit(bit);
    }

    // sampling phase with the widest eye over the window; DC from the quartile
    // midpoint of those eye samples (robust to the burst's onset transient)
    void pick_phase_and_dc() {
        float best = -1.0f;
        std::array<float, PREAMBLE_SYMBOLS> e{};
        for (unsigned int ph = 0; ph < _sps; ++ph) {
            size_t m = 0;
            for (size_t j = ph; j < _win; j += _sps) e[m++] = _ring[(_w + j) % _win];
            std::sort(e.begin(), e.begin() + m);
            const float q1 = e[m / 4], q3 = e[3 * m / 4];
            if (q3 - q1 > best) {
                best = q3 - q1;
                _ph = ph;
                _dc = 0.5f * (q1 + q3);
            }
        }
    }

    unsigned int _sps = 5;
    size_t _win = 80;
    std::array<float, CH_TAPS> _ch_taps{};
    firfilt_crcf _chf = nullptr;
    freqdem _demod = nullptr;
    firfilt_rrrf _rxf = nullptr;
    std::vector<float> _tmpl, _ring;
    size_t _w = 0;
    int64_t _cnt = 0;   // 32-bit would wrap after ~12 h at 48 kS/s and skew the sampling phase
    float _pw = 0.0f, _nf = 1e9f, _pw_alpha = 0.025f;
    float _peak = 0.0f;
    unsigned int _peak_age = 0;
    int64_t _peak_cnt = 0;
    unsigned int _ph = 0, _sample_mod = 0;
    float _dc = 0.0f;
    bool _decoding = false, _prev = false;
    unsigned int _syms = 0;
    uint32_t _frames_seen = 0;
    ais::Deframer _framer;
    std::atomic<uint64_t> _messages{0}, _bursts{0};
};
`,Ze=`#pragma once

#include "cler.hpp"
#include "desktop_blocks/ais/ais.hpp"
#include "desktop_blocks/gui/cler_palette.hpp"
#include "desktop_blocks/gui/map_canvas.hpp"
#include "imgui.h"

#include <cstdio>
#include <ctime>
#include <map>
#include <mutex>
#include <new>
#include <type_traits>

// Vessels from one or more AIS channels on the shared map, with a table.
struct AISMapBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;
    static constexpr size_t MAX_INPUTS = 4;
    cler::Channel<ais::Message>* in;

    struct Vessel {
        uint32_t mmsi = 0;
        char name[21] = {};
        char callsign[8] = {};
        double lat = 0.0, lon = 0.0;
        bool has_position = false;
        float sog = -1.0f, cog = -1.0f;
        int heading = -1;
        int nav_status = -1;
        uint8_t ship_type = 0;
        uint32_t last_seen = 0;
        uint32_t messages = 0;
    };

    AISMapBlock(const char* name, size_t num_inputs = 1,
                float center_lat = 32.8f, float center_lon = 35.0f,
                const char* coastline_shp = "adsb_coastlines/ne_110m_coastline.shp")
        : cler::BlockBase(name), _num_inputs(num_inputs), _map(center_lat, center_lon, coastline_shp, 2.0f) {
        if (num_inputs == 0 || num_inputs > MAX_INPUTS) cler::panic("AISMapBlock: 1..4 inputs");
        in = reinterpret_cast<cler::Channel<ais::Message>*>(_in_storage);
        for (size_t i = 0; i < num_inputs; ++i) new (&in[i]) cler::Channel<ais::Message>(256);
    }

    ~AISMapBlock() {
        for (size_t i = 0; i < _num_inputs; ++i) in[i].~Channel();
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        size_t total = 0;
        const uint32_t now = static_cast<uint32_t>(std::time(nullptr));
        std::lock_guard<std::mutex> lock(_mutex);
        for (size_t c = 0; c < _num_inputs; ++c) {
            auto [rptr, rsize] = in[c].read_dbf();
            for (size_t i = 0; i < rsize; ++i) update(rptr[i], now);
            in[c].commit_read(rsize);
            total += rsize;
        }
        return total ? cler::Result<cler::Empty, cler::Error>(cler::Empty{}) : cler::Error::NotEnoughSamples;
    }

    void render() {
        using namespace cler::palette;
        ImGui::SetNextWindowSize(_initial_size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(_initial_pos, ImGuiCond_FirstUseEver);
        ImGui::Begin("AIS");
        const uint32_t now = static_cast<uint32_t>(std::time(nullptr));

        std::lock_guard<std::mutex> lock(_mutex);
        // table on the left
        ImGui::BeginChild("vessels", ImVec2(360, 0), ImGuiChildFlags_Borders);
        ImGui::Text("%zu vessels, %u messages", _vessels.size(), _total);
        if (ImGui::BeginTable("t", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("MMSI", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("name");
            ImGui::TableSetupColumn("kn", ImGuiTableColumnFlags_WidthFixed, 40);
            ImGui::TableSetupColumn("age", ImGuiTableColumnFlags_WidthFixed, 40);
            ImGui::TableHeadersRow();
            for (auto& [mmsi, v] : _vessels) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char id[16];
                std::snprintf(id, sizeof(id), "%09u", mmsi);
                if (ImGui::Selectable(id, _selected == mmsi, ImGuiSelectableFlags_SpanAllColumns) && v.has_position) {
                    _selected = mmsi;
                    _map.center_lat = static_cast<float>(v.lat);
                    _map.center_lon = static_cast<float>(v.lon);
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(v.name[0] ? v.name : "");
                ImGui::TableNextColumn();
                if (v.sog >= 0.0f) ImGui::Text("%.1f", v.sog);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%us", now - v.last_seen);
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
        ImGui::SameLine();

        // map
        ImGui::BeginChild("map", ImVec2(0, 0));
        _map.begin();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (auto& [mmsi, v] : _vessels) {
            if (!v.has_position) continue;
            const ImVec2 p = _map.to_screen(static_cast<float>(v.lat), static_cast<float>(v.lon));
            const bool stale = now - v.last_seen > 600;
            const ImU32 col = mmsi == _selected ? ImGui::GetColorU32(accent_hi)
                            : stale ? ImGui::GetColorU32(faint)
                            : v.sog > 0.5f ? ImGui::GetColorU32(ok) : ImGui::GetColorU32(warn);
            const float hdg = v.heading >= 0 ? static_cast<float>(v.heading) : v.cog >= 0.0f ? v.cog : 0.0f;
            if (v.sog > 0.5f || v.heading >= 0) _map.marker(dl, p, hdg, 7.0f, col);
            else dl->AddCircleFilled(p, 4.0f, col);
            if (v.name[0]) dl->AddText(ImVec2(p.x + 9, p.y - 7), IM_COL32(255, 255, 255, 230), v.name);
        }
        ImGui::SetCursorScreenPos(ImVec2(_map.pos.x + 8, _map.pos.y + _map.size.y - 24));
        ImGui::TextDisabled("center %.3f, %.3f  zoom %.1fx  (drag to pan, wheel to zoom)", _map.center_lat, _map.center_lon, _map.zoom);
        _map.interact();
        ImGui::EndChild();
        ImGui::End();
    }

    void set_initial_window(float x, float y, float w, float h) {
        _initial_pos = ImVec2(x, y);
        _initial_size = ImVec2(w, h);
    }

    size_t vessel_count() const { std::lock_guard<std::mutex> lock(_mutex); return _vessels.size(); }
    Vessel vessel(uint32_t mmsi) const {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _vessels.find(mmsi);
        return it == _vessels.end() ? Vessel{} : it->second;
    }

private:
    void update(const ais::Message& m, uint32_t now) {
        Vessel& v = _vessels[m.mmsi];
        v.mmsi = m.mmsi;
        v.last_seen = now;
        ++v.messages;
        ++_total;
        if (m.has_position) { v.lat = m.lat; v.lon = m.lon; v.has_position = true; }
        if (m.type == 1 || m.type == 2 || m.type == 3 || m.type == 18) {
            v.sog = m.sog; v.cog = m.cog; v.heading = m.heading;
            if (m.type != 18) v.nav_status = m.nav_status;
        }
        if (m.name[0]) std::memcpy(v.name, m.name, sizeof(v.name));
        if (m.callsign[0]) std::memcpy(v.callsign, m.callsign, sizeof(v.callsign));
        if (m.ship_type) v.ship_type = m.ship_type;
    }

    size_t _num_inputs;
    std::aligned_storage_t<sizeof(cler::Channel<ais::Message>), alignof(cler::Channel<ais::Message>)> _in_storage[MAX_INPUTS];
    mutable std::mutex _mutex;
    std::map<uint32_t, Vessel> _vessels;
    uint32_t _total = 0;
    uint32_t _selected = 0;
    MapCanvas _map;
    ImVec2 _initial_pos{0, 0}, _initial_size{1280, 720};
};
`,$e=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/aprs/aprs.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

// Bell 202 / AFSK1200 receiver: discriminator audio (or a soundcard's line in)
// at \`sample_rate\` -> two one-baud tone correlators -> clock recovery -> NRZI
// -> the AX.25 deframer -> parsed APRS packets.
//
// The tone decision is the normalised difference of the mark (1200 Hz) and
// space (2200 Hz) correlator magnitudes, so it is independent of level: half
// amplitude or a mismatched FM demodulator gain changes both magnitudes
// together. The one-baud window is 1200 Hz wide, which swallows the few Hz of
// audio shift a real link adds.
//
// Clock recovery is direwolf's simplest slicer: a phase accumulator stepping
// one symbol per baud that samples on wrap, nudged toward the eye centre on
// every transition. APRS bursts open with many HDLC flags, so it locks well
// before the data.
struct AFSKDemodBlock : public cler::BlockBase {
    static constexpr double BAUD = 1200.0, MARK_HZ = 1200.0, SPACE_HZ = 2200.0;

    cler::Channel<float> in;

    AFSKDemodBlock(const char* name, double sample_rate = 48e3, size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float) : buffer_size)
    {
        if (buffer_size > 0 && buffer_size * sizeof(float) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        const double sps = sample_rate / BAUD;
        if (std::fabs(sps - std::round(sps)) > 1e-6 || sps < 8.0 || sps > 128.0) {
            cler::panic("AFSKDemodBlock: sample_rate must be 8..128 x 1200");
        }
        _n = static_cast<size_t>(std::lround(sps));
        _ring.assign(_n, 0.0f);
        _mark_c.resize(_n); _mark_s.resize(_n); _space_c.resize(_n); _space_s.resize(_n);
        for (size_t k = 0; k < _n; ++k) {
            const double tm = 2.0 * M_PI * MARK_HZ * k / sample_rate;
            const double ts = 2.0 * M_PI * SPACE_HZ * k / sample_rate;
            _mark_c[k] = static_cast<float>(std::cos(tm));
            _mark_s[k] = static_cast<float>(std::sin(tm));
            _space_c[k] = static_cast<float>(std::cos(ts));
            _space_s[k] = static_cast<float>(std::sin(ts));
        }
        _pll_step = static_cast<float>(2.0 * BAUD / sample_rate);
        _lp_alpha = 4.0f / static_cast<float>(_n);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<aprs::Packet>* out) {
        auto [rptr, rsize] = in.read_dbf();
        if (rsize == 0) return cler::Error::NotEnoughSamples;
        auto [wptr, wsize] = out->write_dbf();
        if (wsize == 0) return cler::Error::NotEnoughSpace;
        // the shortest APRS frame is ~160 bits, so this many samples cannot
        // produce more packets than the output has room for
        const size_t n = std::min(rsize, wsize * 150 * _n);
        size_t written = 0;

        for (size_t i = 0; i < n; ++i) {
            if (sample(rptr[i]) && written < wsize) {
                aprs::Packet p;
                if (aprs::parse(_framer.payload(), _framer.length(), p)) {
                    wptr[written++] = p;
                    _packets.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        in.commit_read(n);
        out->commit_write(written);
        return cler::Empty{};
    }

    uint32_t frames_ok() const { return _framer.frames_ok(); }
    uint32_t frames_bad_crc() const { return _framer.frames_bad_crc(); }
    uint64_t packets() const { return _packets.load(std::memory_order_relaxed); }

private:
    // one audio sample; returns true when a CRC-valid frame completed
    bool sample(float x) {
        _ring[_w] = x;
        _w = _w + 1 == _n ? 0 : _w + 1;

        float mc = 0.0f, ms = 0.0f, sc = 0.0f, ss = 0.0f;
        for (size_t k = 0; k < _n; ++k) {
            const float v = _ring[(_w + k) % _n];
            mc += v * _mark_c[k];
            ms += v * _mark_s[k];
            sc += v * _space_c[k];
            ss += v * _space_s[k];
        }
        const float m = std::sqrt(mc * mc + ms * ms), s = std::sqrt(sc * sc + ss * ss);
        const float d = (m - s) / (m + s + 1e-9f);
        _lp += _lp_alpha * (d - _lp);
        const bool level = _lp > 0.0f;

        bool done = false;
        const float prev_pll = _pll;
        _pll += _pll_step;
        if (_pll >= 1.0f) {
            _pll -= 2.0f;
            if (prev_pll > 0.0f) done = decide(level);
        }
        if (level != _prev_level) {
            _pll *= PLL_INERTIA;   // pull the sampling instant back to the eye centre
            _prev_level = level;
        }
        return done;
    }

    bool decide(bool level) {
        const bool bit = !(level ^ _prev_nrzi);   // NRZI: no transition = 1
        _prev_nrzi = level;
        return _framer.push_bit(bit);
    }

    static constexpr float PLL_INERTIA = 0.75f;

    size_t _n = 40, _w = 0;
    std::vector<float> _ring, _mark_c, _mark_s, _space_c, _space_s;
    float _pll = 0.0f, _pll_step = 0.05f, _lp = 0.0f, _lp_alpha = 0.1f;
    bool _prev_level = false, _prev_nrzi = false;
    aprs::Deframer _framer;
    std::atomic<uint64_t> _packets{0};
};
`,Qe=`#pragma once

#include "desktop_blocks/ais/ais.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <algorithm>

// APRS over AX.25 UI frames, bit level: the HDLC layer (flags, bit stuffing,
// CRC-16/X-25 FCS, LSB-first octets) is identical to AIS, so ais::Deframer and
// ais::crc16_x25 are reused as-is. On top of them: AX.25 address decoding, the
// UI control/PID check, and the APRS info-field parser. No DSP, no allocation.
namespace aprs {

using ais::crc16_x25;
using ais::Deframer;

constexpr size_t MAX_PATH = 8;      // AX.25 allows 8 digipeaters
constexpr uint8_t UI_CONTROL = 0x03, UI_PID = 0xF0;

struct Packet {
    char source[10] = {};       // CALL-SS
    char dest[10] = {};
    char path[80] = {};         // digipeaters, comma separated, '*' = used
    char type = 0;              // APRS data type identifier
    bool has_position = false;
    double lat = 0.0, lon = 0.0;    // degrees, +N +E
    float course = -1.0f;       // degrees, <0 unknown
    float speed = -1.0f;        // knots, <0 unknown
    bool has_altitude = false;
    int altitude_ft = 0;
    char symbol_table = 0, symbol_code = 0;
    char comment[64] = {};
    char info[257] = {};        // raw info field, AX.25 max 256
    uint16_t info_len = 0;
};

// ---- AX.25 addresses -------------------------------------------------------

// 6 callsign characters shifted left by one, then an SSID octet whose bit 0 is
// the end-of-address extension. Returns false on a malformed callsign.
inline bool decode_address(const uint8_t* a, char* out, bool* last, bool* repeated = nullptr) {
    int w = 0;
    for (int i = 0; i < 6; ++i) {
        const char c = static_cast<char>(a[i] >> 1);
        if (c == ' ') continue;
        if (c < '0' || c > 'Z' || (c > '9' && c < 'A')) return false;
        out[w++] = c;
    }
    if (w == 0) return false;
    const int ssid = (a[6] >> 1) & 0x0F;
    if (ssid) {
        out[w++] = '-';
        if (ssid >= 10) out[w++] = '1';
        out[w++] = static_cast<char>('0' + ssid % 10);
    }
    out[w] = 0;
    *last = (a[6] & 1) != 0;
    if (repeated) *repeated = (a[6] & 0x80) != 0;
    return true;
}

inline void encode_address(const char* call, int ssid, bool last, uint8_t* out) {
    const size_t n = std::strlen(call);
    for (int i = 0; i < 6; ++i) {
        const char c = static_cast<size_t>(i) < n ? call[i] : ' ';
        out[i] = static_cast<uint8_t>(c << 1);
    }
    out[6] = static_cast<uint8_t>(0x60 | ((ssid & 0x0F) << 1) | (last ? 1 : 0));
}

// ---- APRS info field -------------------------------------------------------

namespace detail {

inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

inline int num(const char* s, int n) {
    int v = 0;
    for (int i = 0; i < n; ++i) {
        if (!is_digit(s[i])) return -1;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

// as num(), but a position-ambiguity blank counts as a zero in its column
inline int num_amb(const char* s, int n) {
    int v = 0;
    for (int i = 0; i < n; ++i) {
        if (s[i] == ' ') { v *= 10; continue; }
        if (!is_digit(s[i])) return -1;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

// "DDMM.hhN/DDDMM.hhW$" -> lat, lon, symbol. 19 characters.
inline bool uncompressed_position(const char* s, size_t n, Packet& p) {
    if (n < 19) return false;
    // minutes and their hundredths may be ambiguity-blanked from the right
    // ("3325.6 ", "3325.  ", "332 .  ", "33  .  "), degrees never are
    const int lat_d = num(s, 2), lat_m = num_amb(s + 2, 2), lat_h = num_amb(s + 5, 2);
    if (lat_d < 0 || lat_m < 0 || lat_h < 0 || s[4] != '.') return false;
    const int lon_d = num(s + 9, 3), lon_m = num_amb(s + 12, 2), lon_h = num_amb(s + 15, 2);
    if (lon_d < 0 || lon_m < 0 || lon_h < 0 || s[14] != '.') return false;
    if ((s[7] != 'N' && s[7] != 'S') || (s[17] != 'E' && s[17] != 'W')) return false;
    if (lat_m > 59 || lon_m > 59) return false;
    const double lat = lat_d + (lat_m + lat_h / 100.0) / 60.0;
    const double lon = lon_d + (lon_m + lon_h / 100.0) / 60.0;
    if (lat > 90.0 || lon > 180.0) return false;
    p.lat = s[7] == 'S' ? -lat : lat;
    p.lon = s[17] == 'W' ? -lon : lon;
    p.symbol_table = s[8];
    p.symbol_code = s[18];
    p.has_position = true;
    return true;
}

// base-91 compressed position: "/YYYYXXXX$csT", 13 characters
inline bool compressed_position(const char* s, size_t n, Packet& p) {
    // the leading octet is the symbol table id; anything else is a malformed
    // report, not a compressed one
    if (n < 13) return false;
    if (!(s[0] == '/' || s[0] == '\\\\' || (s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'j'))) return false;
    auto b91 = [&](int at, int len) {
        long v = 0;
        for (int i = 0; i < len; ++i) {
            if (s[at + i] < '!' || s[at + i] > '{') return -1L;
            v = v * 91 + (s[at + i] - 33);
        }
        return v;
    };
    const long y = b91(1, 4), x = b91(5, 4);
    if (y < 0 || x < 0) return false;
    p.lat = 90.0 - y / 380926.0;
    p.lon = -180.0 + x / 190463.0;
    p.symbol_table = s[0];
    p.symbol_code = s[9];
    p.has_position = true;
    // cs: course/speed when c is in '!'..'z' ('{' would be a radio range, a
    // space means no data). The compression type byte's NMEA source field
    // (bits 4-3) reading GGA means cs is an altitude in feet instead.
    if (s[10] >= '!' && s[10] <= 'z' && s[11] >= '!' && s[11] <= '{') {
        if (s[12] >= '!' && s[12] <= '{' && ((s[12] - 33) & 0x18) == 0x10) {
            const int cs = (s[10] - 33) * 91 + (s[11] - 33);
            p.altitude_ft = static_cast<int>(std::pow(1.002, cs));   // the spec truncates
            p.has_altitude = true;
        } else {
            p.course = static_cast<float>((s[10] - 33) * 4);
            p.speed = static_cast<float>(std::pow(1.08, s[11] - 33) - 1.0);
        }
    }
    return true;
}

inline bool position(const char* s, size_t n, Packet& p) {
    if (n == 0) return false;
    return is_digit(s[0]) ? uncompressed_position(s, n, p) : compressed_position(s, n, p);
}

// "/A=001234" anywhere in the comment (feet), and a leading "ddd/sss"
// course/speed extension.
inline void comment_extras(const char* s, size_t n, Packet& p) {
    if (n >= 7 && s[3] == '/' && num(s, 3) >= 0 && num(s + 4, 3) >= 0) {
        const int crs = num(s, 3), spd = num(s + 4, 3);
        if (crs > 0 && crs <= 360) p.course = static_cast<float>(crs % 360);
        p.speed = static_cast<float>(spd);
        s += 7;
        n -= 7;
    }
    for (size_t i = 0; i + 9 <= n; ++i) {
        if (s[i] == '/' && s[i + 1] == 'A' && s[i + 2] == '=') {
            const int alt = num(s + i + 3, 6);
            if (alt >= 0) { p.altitude_ft = alt; p.has_altitude = true; }
            break;
        }
    }
    size_t w = 0;
    for (size_t i = 0; i < n && w + 1 < sizeof(p.comment); ++i) {
        if (static_cast<unsigned char>(s[i]) >= 0x20) p.comment[w++] = s[i];
    }
    while (w > 0 && p.comment[w - 1] == ' ') --w;
    p.comment[w] = 0;
}

// Mic-E destination character: latitude digit plus the message/hemisphere bit.
// 'K', 'L' and 'Z' are position-ambiguity blanks.
inline int mice_digit(char c, int* bit) {
    if (c >= '0' && c <= '9') { *bit = 0; return c - '0'; }
    if (c >= 'A' && c <= 'J') { *bit = 1; return c - 'A'; }   // custom message
    if (c >= 'P' && c <= 'Y') { *bit = 1; return c - 'P'; }   // standard message
    if (c == 'K' || c == 'L' || c == 'Z') { *bit = c == 'L' ? 0 : 1; return 0; }
    return -1;
}

// dest = the 6 raw callsign characters; info = the field after the '\`'/'\\''
// data type identifier (>= 8 bytes: lon d/m/h, SP/DC/SE, symbol, table).
inline bool mice(const char* dest, const char* info, size_t n, Packet& p) {
    if (n < 8) return false;
    int d[6], bit[6];
    for (int i = 0; i < 6; ++i) {
        d[i] = mice_digit(dest[i], &bit[i]);
        if (d[i] < 0) return false;
    }
    if (d[2] * 10 + d[3] > 59) return false;
    const double lat = d[0] * 10 + d[1] + (d[2] * 10 + d[3] + (d[4] * 10 + d[5]) / 100.0) / 60.0;
    if (lat > 90.0) return false;
    p.lat = bit[3] ? lat : -lat;                       // byte 4: 0-9 = south

    int lon_d = static_cast<unsigned char>(info[0]) - 28;
    if (bit[4]) lon_d += 100;                          // byte 5: P-Z = +100 deg
    if (lon_d >= 180 && lon_d <= 189) lon_d -= 80;
    else if (lon_d >= 190 && lon_d <= 199) lon_d -= 190;
    if (lon_d < 0 || lon_d > 179) return false;
    int lon_m = static_cast<unsigned char>(info[1]) - 28;
    if (lon_m >= 60) lon_m -= 60;
    const int lon_h = static_cast<unsigned char>(info[2]) - 28;
    if (lon_m < 0 || lon_m > 59 || lon_h < 0 || lon_h > 99) return false;
    p.lon = lon_d + (lon_m + lon_h / 100.0) / 60.0;
    if (bit[5]) p.lon = -p.lon;                        // byte 6: P-Z = west

    const int sp = static_cast<unsigned char>(info[3]) - 28;
    const int dc = static_cast<unsigned char>(info[4]) - 28;
    const int se = static_cast<unsigned char>(info[5]) - 28;
    if (sp < 0 || dc < 0 || se < 0) return false;
    int speed = sp * 10 + dc / 10;
    int course = (dc % 10) * 100 + se;
    if (speed >= 800) speed -= 800;
    if (course >= 400) course -= 400;
    p.speed = static_cast<float>(speed);
    p.course = static_cast<float>(course);
    p.symbol_code = info[6];
    p.symbol_table = info[7];
    p.has_position = true;
    if (n > 8) comment_extras(info + 8, n - 8, p);
    return true;
}

}  // namespace detail

// ---- frame parser ----------------------------------------------------------

// AX.25 UI frame octets (FCS already stripped by the deframer) -> Packet.
inline bool parse(const uint8_t* b, size_t n, Packet& p) {
    p = Packet{};
    if (n < 16) return false;
    char raw_dest[7] = {};
    for (int i = 0; i < 6; ++i) raw_dest[i] = static_cast<char>(b[i] >> 1);
    bool last = false;
    if (!decode_address(b, p.dest, &last) || last) return false;
    if (!decode_address(b + 7, p.source, &last)) return false;
    size_t at = 14;
    size_t w = 0;
    for (size_t i = 0; i < MAX_PATH && !last; ++i) {
        if (at + 7 > n) return false;
        char call[10];
        bool repeated = false;
        if (!decode_address(b + at, call, &last, &repeated)) return false;
        at += 7;
        const size_t len = std::strlen(call);
        if (w + len + 2 < sizeof(p.path)) {
            if (w) p.path[w++] = ',';
            std::memcpy(p.path + w, call, len);
            w += len;
            if (repeated) p.path[w++] = '*';
        }
    }
    p.path[w] = 0;
    if (!last || at + 2 > n) return false;
    if (b[at] != UI_CONTROL || b[at + 1] != UI_PID) return false;
    at += 2;

    const char* info = reinterpret_cast<const char*>(b + at);
    const size_t ilen = n - at;
    p.info_len = static_cast<uint16_t>(std::min(ilen, sizeof(p.info) - 1));
    std::memcpy(p.info, info, p.info_len);
    if (ilen == 0) return true;
    p.type = info[0];
    switch (p.type) {
        case '!': case '=':
            if (detail::position(info + 1, ilen - 1, p)) {
                const size_t used = detail::is_digit(info[1]) ? 20u : 14u;
                if (ilen > used) detail::comment_extras(info + used, ilen - used, p);
            }
            return true;
        case '@': case '/':
            if (ilen < 8) return true;
            if (detail::position(info + 8, ilen - 8, p)) {
                const size_t used = detail::is_digit(info[8]) ? 27u : 21u;
                if (ilen > used) detail::comment_extras(info + used, ilen - used, p);
            }
            return true;
        case '\`': case '\\'': case 0x1C: case 0x1D:
            detail::mice(raw_dest, info + 1, ilen - 1, p);
            return true;
        case '>':
            detail::comment_extras(info + 1, ilen - 1, p);
            return true;
        default:
            return true;
    }
}

// ---- encoder (tests, loopback and the simulator) ---------------------------

// Build the AX.25 UI frame octets: dest, source, digipeaters, 0x03, 0xF0, info.
// \`path\` is comma separated ("WIDE1-1,WIDE2-1"), may be empty.
inline size_t encode_ui(const char* dest, const char* source, const char* path,
                        const char* info, uint8_t* out, size_t max) {
    auto split_ssid = [](const char* s, char* call, int* ssid) {
        const char* dash = std::strchr(s, '-');
        const size_t n = dash ? static_cast<size_t>(dash - s) : std::strlen(s);
        std::memcpy(call, s, n);
        call[n] = 0;
        *ssid = dash ? std::atoi(dash + 1) : 0;
    };
    char call[16];
    int ssid = 0;
    size_t at = 0;
    if (max < 16) return 0;
    split_ssid(dest, call, &ssid);
    encode_address(call, ssid, false, out + at);
    at += 7;
    split_ssid(source, call, &ssid);
    encode_address(call, ssid, path && path[0] ? false : true, out + at);
    at += 7;
    for (const char* s = path; s && *s;) {
        const char* comma = std::strchr(s, ',');
        char one[16] = {};
        const size_t n = comma ? static_cast<size_t>(comma - s) : std::strlen(s);
        if (n >= sizeof(one) || at + 7 > max) return 0;
        std::memcpy(one, s, n);
        split_ssid(one, call, &ssid);
        s = comma ? comma + 1 : s + n;
        encode_address(call, ssid, *s == 0, out + at);
        at += 7;
    }
    const size_t ilen = std::strlen(info);
    if (at + 2 + ilen > max) return 0;
    out[at++] = UI_CONTROL;
    out[at++] = UI_PID;
    std::memcpy(out + at, info, ilen);
    return at + ilen;
}

// Build a Mic-E beacon: the latitude, hemispheres and longitude offset live in
// the destination callsign, everything else in the info field. Message code 0
// (M0, "off duty"); speed in knots, course in degrees.
inline void encode_mice(double lat, double lon, int speed_kn, int course_deg,
                        char sym_table, char sym_code, char* dest, char* info) {
    const double alat = std::fabs(lat);
    const int d[6] = {
        static_cast<int>(alat) / 10, static_cast<int>(alat) % 10,
        static_cast<int>(alat * 60.0) % 60 / 10, static_cast<int>(alat * 60.0) % 60 % 10,
        static_cast<int>(std::lround(alat * 6000.0)) % 100 / 10,
        static_cast<int>(std::lround(alat * 6000.0)) % 100 % 10,
    };
    const int alon_d = static_cast<int>(std::fabs(lon));
    // pick the longitude-degrees octet and the +100 offset flag that decode
    // back to alon_d (the spec's ranges are easier to search than to invert)
    int lon_byte = 28, offset = 0;
    for (offset = 0; offset < 2; ++offset) {
        for (lon_byte = 28; lon_byte < 128; ++lon_byte) {
            int v = lon_byte - 28 + (offset ? 100 : 0);
            if (v >= 180 && v <= 189) v -= 80;
            else if (v >= 190 && v <= 199) v -= 190;
            if (v == alon_d) goto found;
        }
    }
found:
    const int bit[6] = {1, 1, 1, lat >= 0.0 ? 1 : 0, offset, lon < 0.0 ? 1 : 0};
    for (int i = 0; i < 6; ++i) dest[i] = static_cast<char>((bit[i] ? 'P' : '0') + d[i]);
    dest[6] = 0;

    const double amin = (std::fabs(lon) - alon_d) * 60.0;
    const int lm = static_cast<int>(amin);
    const int lh = static_cast<int>(std::lround((amin - lm) * 100.0)) % 100;
    const int sp = speed_kn / 10, dc = (speed_kn % 10) * 10 + course_deg / 100, se = course_deg % 100;
    int w = 0;
    info[w++] = '\`';
    info[w++] = static_cast<char>(lon_byte);
    info[w++] = static_cast<char>(lm + (lm < 10 ? 88 : 28));
    info[w++] = static_cast<char>(lh + 28);
    info[w++] = static_cast<char>(sp + 28 < 33 ? sp + 108 : sp + 28);
    info[w++] = static_cast<char>(dc + 28);
    info[w++] = static_cast<char>(se + 28);
    info[w++] = sym_code;
    info[w++] = sym_table;
    info[w] = 0;
}

// Transmitted bit stream: \`nflags\` HDLC flags of preamble (what a real TNC
// keys up with, unlike AIS's 0101 training), stuffed data + FCS, closing
// flags; then NRZI (0 = transition). Returns the number of bits written.
inline size_t encode_frame(const uint8_t* payload, size_t nbytes, bool* out, size_t max_bits, int nflags = 16) {
    size_t n = 0;
    auto put = [&](bool b) { if (n < max_bits) out[n++] = b; };
    auto flag = [&]() { for (int i = 0; i < 8; ++i) put((0x7E >> i) & 1); };
    for (int i = 0; i < nflags; ++i) flag();
    const uint16_t fcs = crc16_x25(payload, nbytes);
    int ones = 0;
    auto data_bit = [&](bool b) {
        put(b);
        if (b) { if (++ones == 5) { put(false); ones = 0; } } else ones = 0;
    };
    for (size_t i = 0; i < nbytes; ++i) for (int k = 0; k < 8; ++k) data_bit((payload[i] >> k) & 1);
    for (int k = 0; k < 8; ++k) data_bit((fcs >> k) & 1);
    for (int k = 0; k < 8; ++k) data_bit((fcs >> (8 + k)) & 1);
    flag();
    flag();
    bool level = false;
    for (size_t i = 0; i < n; ++i) { if (!out[i]) level = !level; out[i] = level; }
    return n;
}

}  // namespace aprs
`,Je=`#pragma once

#include "cler.hpp"
#include "desktop_blocks/aprs/aprs.hpp"
#include "desktop_blocks/gui/cler_palette.hpp"
#include "desktop_blocks/gui/map_canvas.hpp"
#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <map>
#include <mutex>
#include <string>

// APRS stations on the shared map, with a table. An origin (the receiver's own
// position) turns on a distance column.
struct APRSMapBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;
    static constexpr size_t MAX_STATIONS = 512;

    cler::Channel<aprs::Packet> in;

    struct Station {
        char callsign[10] = {};
        char path[80] = {};
        char comment[64] = {};
        char type = 0;
        double lat = 0.0, lon = 0.0;
        bool has_position = false;
        float course = -1.0f, speed = -1.0f;
        bool has_altitude = false;
        int altitude_ft = 0;
        char symbol_table = 0, symbol_code = 0;
        uint32_t last_seen = 0;
        uint32_t packets = 0;
    };

    APRSMapBlock(const char* name, float center_lat = 32.8f, float center_lon = 35.0f,
                 bool have_origin = false,
                 const char* coastline_shp = "adsb_coastlines/ne_110m_coastline.shp")
        : cler::BlockBase(name), in(256), _have_origin(have_origin),
          _origin_lat(center_lat), _origin_lon(center_lon),
          _map(center_lat, center_lon, coastline_shp, 2.0f) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        auto [rptr, rsize] = in.read_dbf();
        if (rsize == 0) return cler::Error::NotEnoughSamples;
        const uint32_t now = static_cast<uint32_t>(std::time(nullptr));
        {
            std::lock_guard<std::mutex> lock(_mutex);
            for (size_t i = 0; i < rsize; ++i) update(rptr[i], now);
        }
        in.commit_read(rsize);
        return cler::Empty{};
    }

    void render() {
        using namespace cler::palette;
        ImGui::SetNextWindowSize(_initial_size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(_initial_pos, ImGuiCond_FirstUseEver);
        ImGui::Begin("APRS");
        const uint32_t now = static_cast<uint32_t>(std::time(nullptr));

        std::lock_guard<std::mutex> lock(_mutex);
        // table on the left
        ImGui::BeginChild("stations", ImVec2(400, 0), ImGuiChildFlags_Borders);
        ImGui::SeparatorText("Stations");
        ImGui::Text("%zu heard, %u packets", _stations.size(), _total);
        const int cols = _have_origin ? 4 : 3;
        if (ImGui::BeginTable("t", cols, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, -70))) {
            ImGui::TableSetupColumn("callsign", ImGuiTableColumnFlags_WidthFixed, 90);
            ImGui::TableSetupColumn("type", ImGuiTableColumnFlags_WidthFixed, 40);
            if (_have_origin) ImGui::TableSetupColumn("km", ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableSetupColumn("age", ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableHeadersRow();
            for (auto& [call, st] : _stations) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Selectable(st.callsign, _selected == call, ImGuiSelectableFlags_SpanAllColumns)) {
                    _selected = call;
                    if (st.has_position) {
                        _map.center_lat = static_cast<float>(st.lat);
                        _map.center_lon = static_cast<float>(st.lon);
                    }
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(type_name(st.type));
                if (_have_origin) {
                    ImGui::TableNextColumn();
                    if (st.has_position) ImGui::Text("%.1f", distance_km(st.lat, st.lon));
                }
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%us", now - st.last_seen);
            }
            ImGui::EndTable();
        }
        ImGui::SeparatorText("Selected");
        auto it = _stations.find(_selected);
        if (it == _stations.end()) {
            ImGui::TextDisabled("no station selected");
        } else {
            const Station& st = it->second;
            ImGui::TextColored(accent_hi, "%s", st.callsign);
            if (st.path[0]) { ImGui::SameLine(); ImGui::TextDisabled("via %s", st.path); }
            if (st.speed >= 0.0f) ImGui::Text("%.0f kn  %.0f deg", st.speed, st.course < 0.0f ? 0.0f : st.course);
            if (st.has_altitude) {
                if (st.speed >= 0.0f) ImGui::SameLine(0, 12);   // a GGA compressed report has an altitude but no speed
                ImGui::Text("%d ft", st.altitude_ft);
            }
            ImGui::TextWrapped("%s", st.comment[0] ? st.comment : "");
        }
        ImGui::EndChild();
        ImGui::SameLine();

        // map
        ImGui::BeginChild("map", ImVec2(0, 0));
        _map.begin();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (_have_origin) {
            const ImVec2 o = _map.to_screen(_origin_lat, _origin_lon);
            dl->AddCircle(o, 6.0f, ImGui::GetColorU32(accent_hi), 0, 2.0f);
        }
        for (auto& [call, st] : _stations) {
            if (!st.has_position) continue;
            const ImVec2 p = _map.to_screen(static_cast<float>(st.lat), static_cast<float>(st.lon));
            const bool stale = now - st.last_seen > 1800;
            const ImU32 col = call == _selected ? ImGui::GetColorU32(accent_hi)
                            : stale ? ImGui::GetColorU32(faint)
                            : st.speed > 1.0f ? ImGui::GetColorU32(ok) : ImGui::GetColorU32(warn);
            if (st.speed > 1.0f && st.course >= 0.0f) _map.marker(dl, p, st.course, 7.0f, col);
            else dl->AddCircleFilled(p, 4.0f, col);
            dl->AddText(ImVec2(p.x + 9, p.y - 7), IM_COL32(255, 255, 255, 230), st.callsign);
        }
        ImGui::SetCursorScreenPos(ImVec2(_map.pos.x + 8, _map.pos.y + _map.size.y - 24));
        ImGui::TextDisabled("center %.3f, %.3f  zoom %.1fx  (drag to pan, wheel to zoom)", _map.center_lat, _map.center_lon, _map.zoom);
        _map.interact();
        ImGui::EndChild();
        ImGui::End();
    }

    void set_initial_window(float x, float y, float w, float h) {
        _initial_pos = ImVec2(x, y);
        _initial_size = ImVec2(w, h);
    }

    size_t station_count() const { std::lock_guard<std::mutex> lock(_mutex); return _stations.size(); }
    Station station(const char* callsign) const {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _stations.find(callsign);
        return it == _stations.end() ? Station{} : it->second;
    }

private:
    static const char* type_name(char t) {
        switch (t) {
            case '!': case '=': return "pos";
            case '@': case '/': return "pos+t";
            case '\`': case '\\'': case 0x1C: case 0x1D: return "Mic-E";
            case '>': return "status";
            case ':': return "msg";
            case ';': return "obj";
            case 'T': return "tlm";
            default: return "?";
        }
    }

    float distance_km(double lat, double lon) const {
        const double dlat = (lat - _origin_lat) * 111.32;
        const double dlon = (lon - _origin_lon) * 111.32 * std::cos(lat * M_PI / 180.0);
        return static_cast<float>(std::sqrt(dlat * dlat + dlon * dlon));
    }

    void update(const aprs::Packet& p, uint32_t now) {
        auto it = _stations.find(p.source);
        if (it == _stations.end()) {
            if (_stations.size() >= MAX_STATIONS) {
                auto oldest = _stations.begin();
                for (auto i = _stations.begin(); i != _stations.end(); ++i)
                    if (i->second.last_seen < oldest->second.last_seen) oldest = i;
                if (_selected == oldest->first) _selected.clear();
                _stations.erase(oldest);
            }
            it = _stations.emplace(p.source, Station{}).first;
        }
        Station& st = it->second;
        if (_selected.empty()) _selected = p.source;
        std::memcpy(st.callsign, p.source, sizeof(st.callsign));
        std::memcpy(st.path, p.path, sizeof(st.path));
        st.type = p.type;
        st.last_seen = now;
        ++st.packets;
        ++_total;
        if (p.has_position) {
            st.lat = p.lat; st.lon = p.lon; st.has_position = true;
            st.symbol_table = p.symbol_table; st.symbol_code = p.symbol_code;
        }
        if (p.speed >= 0.0f) { st.speed = p.speed; st.course = p.course; }
        if (p.has_altitude) { st.has_altitude = true; st.altitude_ft = p.altitude_ft; }
        if (p.comment[0]) std::memcpy(st.comment, p.comment, sizeof(st.comment));
    }

    bool _have_origin;
    float _origin_lat, _origin_lon;
    mutable std::mutex _mutex;
    std::map<std::string, Station> _stations;
    std::string _selected;
    uint32_t _total = 0;
    MapCanvas _map;
    ImVec2 _initial_pos{0, 0}, _initial_size{1280, 720};
};
`,en=`#pragma once
#include "cler.hpp"
#include <memory>

struct Slab;

struct Blob {
    uint8_t* data;   // pointer to slab region
    size_t len;      // valid length
    size_t slot_idx; // slab index for recycling
    Slab* owner_slab;

    void release();
};

struct Slab {
    Slab(size_t num_slots, size_t max_blob_size);

    // Allocate a slice: pops a free slot, returns a Blob pointing into it.
    // Returns cler::Error::ProcedureError if no slot is free.
    cler::Result<Blob, cler::Error> take_slot();
    void release_slot(size_t slot_idx);

    inline size_t capacity() const { return _num_slots; }
    inline size_t available_slots() const { return _free_slots.size(); }
    inline size_t max_blob_size() const { return _max_blob_size;}

private:
    size_t _num_slots;
    size_t _max_blob_size;
    std::unique_ptr<uint8_t[]> _data;
    cler::Channel<size_t> _free_slots;
};`,nn=`#pragma once

#include "liquid.h"
#include "polyphase_transform_5.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstring>

template <size_t NUM_CHANNELS, size_t FILTER_SEMILENGTH>
class PolyphaseAnalyzer {
    static_assert(NUM_CHANNELS > 0, "Number of channels must be positive");
    static_assert(FILTER_SEMILENGTH > 0, "Filter semilength must be positive");

public:
    static constexpr size_t channels = NUM_CHANNELS;
    static constexpr size_t taps_per_subfilter = 2 * FILTER_SEMILENGTH;
    static constexpr size_t history_frames = taps_per_subfilter - 1;

    explicit PolyphaseAnalyzer(float stopband_attenuation_db)
    {
        design_folded_taps(stopband_attenuation_db);
        build_twiddles();
    }

    void execute(const std::complex<float>* frames,
                 size_t num_frames,
                 std::complex<float>* const* out_channels)
    {
        const size_t from_carry = std::min(history_frames, num_frames);
        constexpr size_t frame_bytes = channels * sizeof(std::complex<float>);

        std::memcpy(_carry.data() + history_frames * channels, frames, from_carry * frame_bytes);

        for (size_t j = 0; j < from_carry; ++j) {
            transform(_carry.data() + j * channels, out_channels, j);
        }
        for (size_t j = history_frames; j < num_frames; ++j) {
            transform(frames + (j - history_frames) * channels, out_channels, j);
        }

        if (num_frames >= history_frames) {
            std::memcpy(_carry.data(), frames + (num_frames - history_frames) * channels,
                        history_frames * frame_bytes);
        } else {
            std::memmove(_carry.data(), _carry.data() + num_frames * channels,
                         (history_frames - num_frames) * frame_bytes);
            std::memcpy(_carry.data() + (history_frames - num_frames) * channels, frames,
                        num_frames * frame_bytes);
        }
    }

private:
    static constexpr bool uses_codelet = (channels == polyphase5::channels &&
                                          taps_per_subfilter == polyphase5::taps_per_subfilter);
    static constexpr size_t folded_taps_len = channels * taps_per_subfilter;
    static constexpr size_t twiddle_len = uses_codelet ? 0 : channels;

    void design_folded_taps(float stopband_attenuation_db)
    {
        std::array<float, folded_taps_len + 1> prototype{};
        const float cutoff = 0.5f / static_cast<float>(channels);
        liquid_firdes_kaiser(static_cast<unsigned int>(prototype.size()), cutoff,
                             std::fabs(stopband_attenuation_db), 0.0f, prototype.data());
        std::reverse_copy(prototype.begin(), prototype.begin() + folded_taps_len, _taps.begin());
    }

    void build_twiddles()
    {
        const double turn = -2.0 * M_PI / static_cast<double>(channels);
        for (size_t i = 0; i < twiddle_len; ++i) {
            const double angle = turn * static_cast<double>(i);
            _twiddles[i] = std::complex<float>(static_cast<float>(std::cos(angle)),
                                               static_cast<float>(std::sin(angle)));
        }
    }

    inline void transform(const std::complex<float>* window,
                          std::complex<float>* const* out_channels,
                          size_t frame_index) const
    {
        if constexpr (uses_codelet) {
            polyphase5::transform(_taps.data(), window, out_channels, frame_index);
        } else {
            std::array<float, channels> bins_r;
            std::array<float, channels> bins_i;

            for (size_t k = 0; k < channels; ++k) {
                bins_r[k] = window[k].real() * _taps[k];
                bins_i[k] = window[k].imag() * _taps[k];
            }
            for (size_t t = 1; t < taps_per_subfilter; ++t) {
                const float* tap_row = _taps.data() + t * channels;
                const std::complex<float>* row = window + t * channels;
                for (size_t k = 0; k < channels; ++k) {
                    bins_r[k] += row[k].real() * tap_row[k];
                    bins_i[k] += row[k].imag() * tap_row[k];
                }
            }

            for (size_t k = 0; k < channels; ++k) {
                float acc_r = bins_r[0];
                float acc_i = bins_i[0];
                for (size_t n = 1; n < channels; ++n) {
                    const std::complex<float> w = _twiddles[(k * n) % channels];
                    acc_r += bins_r[n] * w.real() - bins_i[n] * w.imag();
                    acc_i += bins_r[n] * w.imag() + bins_i[n] * w.real();
                }
                out_channels[k][frame_index] = std::complex<float>(acc_r, acc_i);
            }
        }
    }

    std::array<float, folded_taps_len> _taps{};
    std::array<std::complex<float>, 2 * history_frames * channels> _carry{};
    std::array<std::complex<float>, twiddle_len> _twiddles{};
};
`,tn=`#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "polyphase_analyzer.hpp"
#include <complex>
#include <array>
#include <algorithm>
#include <limits>

template <size_t NUM_CHANNELS, size_t FILTER_SEMILENGTH>
struct PolyphaseChannelizerBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    PolyphaseChannelizerBlock(std::string name,
                              float kaiser_attenuation,
                              size_t in_buffer_size = 0)
        : cler::BlockBase(std::move(name)),
          in(input_channel_elems(in_buffer_size)),
          _analyzer(kaiser_attenuation)
    {}

    template <typename... OChannels>
    cler::Result<cler::Empty, cler::Error> procedure(OChannels*... outs) {
        static_assert(sizeof...(OChannels) == NUM_CHANNELS,
                      "Number of output channels must match the number of polyphase channels");

        auto [read_ptr, read_size] = in.read_dbf();

        if (read_size < NUM_CHANNELS) {
            return cler::Error::NotEnoughSamples;
        }

        const size_t frames_by_contig = read_size / NUM_CHANNELS;

        std::array<std::complex<float>*, NUM_CHANNELS> ports;
        size_t min_write_space = std::numeric_limits<size_t>::max();

        size_t idx = 0;
        auto get_write_ptrs = [&](auto*... chs) {
            ([&] {
                auto [write_ptr, write_space] = chs->write_dbf();
                ports[idx] = write_ptr;
                min_write_space = std::min(min_write_space, write_space);
                idx++;
            }(), ...);
        };
        get_write_ptrs(outs...);

        const size_t num_frames = std::min(frames_by_contig, min_write_space);
        if (num_frames == 0) {
            return cler::Error::NotEnoughSpace;
        }

        _analyzer.execute(read_ptr, num_frames, ports.data());

        auto commit_writes = [&](auto*... chs) {
            ((chs->commit_write(num_frames)), ...);
        };
        commit_writes(outs...);

        in.commit_read(num_frames * NUM_CHANNELS);
        return cler::Empty{};
    }

private:
    static size_t input_channel_elems(size_t requested) {
        const size_t min_elems = cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>);
        if (requested == 0) {
            return min_elems * NUM_CHANNELS;
        }
        if (requested < min_elems || (requested % NUM_CHANNELS) != 0) {
            cler::panic("Buffer size must be >= DOUBLY_MAPPED_MIN_SIZE elements and a multiple of num_channels");
        }
        return requested;
    }

    PolyphaseAnalyzer<NUM_CHANNELS, FILTER_SEMILENGTH> _analyzer;
};
`,sn=`#pragma once

#include <array>
#include <complex>
#include <cstddef>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace polyphase5 {

constexpr size_t channels = 5;
constexpr size_t taps_per_subfilter = 6;

inline void winograd_dft(const std::array<float, channels>& bins_r,
                         const std::array<float, channels>& bins_i,
                         std::complex<float>* const* out_channels,
                         size_t frame_index)
{
    constexpr float cos_mean = -0.25f;
    constexpr float cos_half_difference = 0.559016994374947f;
    constexpr float sin_first = 0.951056516295154f;
    constexpr float sin_second = 0.587785252292473f;

    const float sum_outer_r = bins_r[1] + bins_r[4], sum_outer_i = bins_i[1] + bins_i[4];
    const float diff_outer_r = bins_r[1] - bins_r[4], diff_outer_i = bins_i[1] - bins_i[4];
    const float sum_inner_r = bins_r[2] + bins_r[3], sum_inner_i = bins_i[2] + bins_i[3];
    const float diff_inner_r = bins_r[2] - bins_r[3], diff_inner_i = bins_i[2] - bins_i[3];

    const float total_r = sum_outer_r + sum_inner_r, total_i = sum_outer_i + sum_inner_i;
    const float imbalance_r = sum_outer_r - sum_inner_r, imbalance_i = sum_outer_i - sum_inner_i;

    const float mean_r = bins_r[0] + cos_mean * total_r;
    const float mean_i = bins_i[0] + cos_mean * total_i;
    const float spread_r = cos_half_difference * imbalance_r;
    const float spread_i = cos_half_difference * imbalance_i;

    const float near_r = mean_r + spread_r, near_i = mean_i + spread_i;
    const float far_r = mean_r - spread_r, far_i = mean_i - spread_i;

    const float near_quad_r = sin_first * diff_outer_r + sin_second * diff_inner_r;
    const float near_quad_i = sin_first * diff_outer_i + sin_second * diff_inner_i;
    const float far_quad_r = sin_second * diff_outer_r - sin_first * diff_inner_r;
    const float far_quad_i = sin_second * diff_outer_i - sin_first * diff_inner_i;

    out_channels[0][frame_index] = std::complex<float>(bins_r[0] + total_r, bins_i[0] + total_i);
    out_channels[1][frame_index] = std::complex<float>(near_r + near_quad_i, near_i - near_quad_r);
    out_channels[2][frame_index] = std::complex<float>(far_r + far_quad_i, far_i - far_quad_r);
    out_channels[3][frame_index] = std::complex<float>(far_r - far_quad_i, far_i + far_quad_r);
    out_channels[4][frame_index] = std::complex<float>(near_r - near_quad_i, near_i + near_quad_r);
}

#if defined(__ARM_NEON)

inline void transform(const float* taps,
                      const std::complex<float>* window,
                      std::complex<float>* const* out_channels,
                      size_t frame_index)
{
    const float* w = reinterpret_cast<const float*>(window);

    float32x4x2_t s = vld2q_f32(w);
    float32x4_t tap4 = vld1q_f32(taps);
    float32x4_t acc_r4 = vmulq_f32(s.val[0], tap4);
    float32x4_t acc_i4 = vmulq_f32(s.val[1], tap4);
    float acc_r1 = w[8] * taps[4];
    float acc_i1 = w[9] * taps[4];

    for (size_t t = 1; t < taps_per_subfilter; ++t) {
        const float* row = w + t * 2 * channels;
        const float* tap_row = taps + t * channels;
        s = vld2q_f32(row);
        tap4 = vld1q_f32(tap_row);
        acc_r4 = vmlaq_f32(acc_r4, s.val[0], tap4);
        acc_i4 = vmlaq_f32(acc_i4, s.val[1], tap4);
        acc_r1 += row[8] * tap_row[4];
        acc_i1 += row[9] * tap_row[4];
    }

    std::array<float, channels> bins_r;
    std::array<float, channels> bins_i;
    vst1q_f32(bins_r.data(), acc_r4);
    vst1q_f32(bins_i.data(), acc_i4);
    bins_r[4] = acc_r1;
    bins_i[4] = acc_i1;

    winograd_dft(bins_r, bins_i, out_channels, frame_index);
}

#else

inline void transform(const float* taps,
                      const std::complex<float>* window,
                      std::complex<float>* const* out_channels,
                      size_t frame_index)
{
    std::array<float, channels> bins_r;
    std::array<float, channels> bins_i;

    for (size_t k = 0; k < channels; ++k) {
        bins_r[k] = window[k].real() * taps[k];
        bins_i[k] = window[k].imag() * taps[k];
    }
    for (size_t t = 1; t < taps_per_subfilter; ++t) {
        const float* tap_row = taps + t * channels;
        const std::complex<float>* row = window + t * channels;
        for (size_t k = 0; k < channels; ++k) {
            bins_r[k] += row[k].real() * tap_row[k];
            bins_i[k] += row[k].imag() * tap_row[k];
        }
    }

    winograd_dft(bins_r, bins_i, out_channels, frame_index);
}

#endif

}
`,rn=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "liquid.h"

#include <atomic>
#include <cmath>
#include <algorithm>
#include <complex>

// Runtime-switchable analog demodulator: complex channel at \`channel_rate\`
// (WBFM's rate; must be audio_decim x 48 kHz) -> mono audio at 48 kHz.
//   WBFM : quadrature demod at channel rate, audio lowpass + decimate,
//          50 us de-emphasis
//   NBFM : decimate to audio rate first, quadrature demod (2.5 kHz deviation)
//   AM   : decimate, magnitude, carrier-normalised (AGC), DC block
//   USB/LSB: decimate, then a +/-1.6 kHz frequency-translated 1.6 kHz
//          lowpass (= 0..3.2 kHz for USB, -3.2..0 for LSB), real part
// Mode switches are applied between procedure() calls; each switch resets the
// per-mode state and mutes 20 ms of audio, so the filter fill and the AM
// carrier estimate settle into silence instead of a full-scale thump.
struct AnalogDemodBlock : public cler::BlockBase {
    enum class Mode { WBFM, NBFM, AM, USB, LSB };
    cler::Channel<std::complex<float>> in;

    AnalogDemodBlock(const char* name, double channel_rate, Mode mode = Mode::WBFM,
                     size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size),
          _mode(mode), _requested(mode)
    {
        if (buffer_size > 0 && buffer_size * sizeof(std::complex<float>) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        const double d = channel_rate / AUDIO_RATE;
        if (std::fabs(d - std::round(d)) > 1e-6 || d < 1.0 || d > MAX_DECIM) {
            cler::panic("AnalogDemodBlock: channel_rate must be 1..16 x 48 kHz");
        }
        _decim = static_cast<unsigned int>(std::lround(d));

        // Anti-alias lowpass shared by the WBFM audio decimator and the complex
        // predecimator: flat to 15 kHz (WBFM mono audio), >=60 dB by 24 kHz so
        // nothing folds into the 48 kHz output. liquid centres the transition
        // band on fc, so both edges are rate-derived; a fixed transition width
        // aliases as soon as channel_rate exceeds ~360 kHz.
        float taps[MAX_TAPS];
        const float df = static_cast<float>(9e3 / channel_rate);
        unsigned int h_len = estimate_req_filter_len(df, 60.0f);
        if (h_len % 2 == 0) ++h_len;
        if (h_len > MAX_TAPS) cler::panic("AnalogDemodBlock: anti-alias filter too long");
        liquid_firdes_kaiser(h_len, static_cast<float>(19.5e3 / channel_rate), 60.0f, 0.0f, taps);
        float dc = 0.0f;
        for (unsigned int i = 0; i < h_len; ++i) dc += taps[i];
        for (unsigned int i = 0; i < h_len; ++i) taps[i] /= dc;
        _audio_decim = firdecim_rrrf_create(_decim, taps, h_len);
        _iq_decim = firdecim_crcf_create(_decim, taps, h_len);
        _wbfm = freqdem_create(static_cast<float>(75e3 / channel_rate));
        _nbfm = freqdem_create(static_cast<float>(2.5e3 / AUDIO_RATE));
        _ssb_bpf = firfilt_crcf_create_kaiser(129, static_cast<float>(1.6e3 / AUDIO_RATE), 60.0f, 0.0f);
        _ssb_nco = nco_crcf_create(LIQUID_NCO);
        if (!_audio_decim || !_iq_decim || !_wbfm || !_nbfm || !_ssb_bpf || !_ssb_nco) {
            cler::panic("AnalogDemodBlock: liquid create failed");
        }
        _deemph_alpha = static_cast<float>(1.0 - std::exp(-1.0 / (50e-6 * AUDIO_RATE)));
        apply_mode(mode);
    }

    ~AnalogDemodBlock() {
        if (_audio_decim) firdecim_rrrf_destroy(_audio_decim);
        if (_iq_decim) firdecim_crcf_destroy(_iq_decim);
        if (_wbfm) freqdem_destroy(_wbfm);
        if (_nbfm) freqdem_destroy(_nbfm);
        if (_ssb_bpf) firfilt_crcf_destroy(_ssb_bpf);
        if (_ssb_nco) nco_crcf_destroy(_ssb_nco);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        const Mode want = _requested.load(std::memory_order_relaxed);
        if (want != _mode) apply_mode(want);

        auto [rptr, rsize] = in.read_dbf();
        auto [wptr, wsize] = out->write_dbf();
        const size_t frames = std::min(rsize / _decim, wsize);
        if (frames == 0) return cler::Error::NotEnoughSpaceOrSamples;

        switch (_mode) {
            case Mode::WBFM:
                for (size_t f = 0; f < frames; ++f) {
                    freqdem_demodulate_block(_wbfm,
                        const_cast<liquid_float_complex*>(rptr + f * _decim),
                        _decim, _mpx);
                    float a;
                    firdecim_rrrf_execute(_audio_decim, _mpx, &a);
                    _de += _deemph_alpha * (a - _de);
                    wptr[f] = _de;
                }
                break;
            case Mode::NBFM:
                for (size_t f = 0; f < frames; ++f) {
                    liquid_float_complex z;
                    firdecim_crcf_execute(_iq_decim,
                        const_cast<liquid_float_complex*>(rptr + f * _decim), &z);
                    float a;
                    freqdem_demodulate(_nbfm, z, &a);
                    _de += _deemph_alpha * (a - _de);
                    wptr[f] = _de;
                }
                break;
            case Mode::AM:
                for (size_t f = 0; f < frames; ++f) {
                    liquid_float_complex z;
                    firdecim_crcf_execute(_iq_decim,
                        const_cast<liquid_float_complex*>(rptr + f * _decim), &z);
                    const float mag = std::abs(z);
                    // while muted the carrier estimate is a running mean, so
                    // the 42 ms tracker is handed the carrier level (not one
                    // modulation peak) and starts settled instead of thumping
                    if (f < _settle) _am_dc += (mag - _am_dc) / static_cast<float>(++_am_n);
                    else _am_dc += 0.0005f * (mag - _am_dc);
                    // divide by the carrier so the output is the modulation
                    // depth whatever the signal level; below AM_AGC_FLOOR the
                    // gain is pinned so an empty channel does not become hiss
                    wptr[f] = (mag - _am_dc) / std::max(_am_dc, AM_AGC_FLOOR);
                }
                break;
            case Mode::USB:
            case Mode::LSB:
                for (size_t f = 0; f < frames; ++f) {
                    liquid_float_complex z;
                    firdecim_crcf_execute(_iq_decim,
                        const_cast<liquid_float_complex*>(rptr + f * _decim), &z);
                    // mix down, filter, mix up with one NCO phase per sample: the
                    // e^{-j.theta.n}/e^{+j.theta.n} cancel and the taps become
                    // h[k]e^{j.theta.k}, i.e. a lowpass translated to +/-1.6 kHz
                    nco_crcf_mix_down(_ssb_nco, z, &z);
                    firfilt_crcf_push(_ssb_bpf, z);
                    firfilt_crcf_execute(_ssb_bpf, &z);
                    nco_crcf_mix_up(_ssb_nco, z, &z);
                    nco_crcf_step(_ssb_nco);
                    wptr[f] = 2.0f * z.real();
                }
                break;
        }
        if (_settle) {
            const size_t n = std::min(_settle, frames);
            std::fill(wptr, wptr + n, 0.0f);
            _settle -= n;
        }
        in.commit_read(frames * _decim);
        out->commit_write(frames);
        return cler::Empty{};
    }

    void set_mode(Mode m) { _requested.store(m, std::memory_order_relaxed); }
    Mode mode() const { return _requested.load(std::memory_order_relaxed); }
    static const char* mode_name(Mode m) {
        switch (m) {
            case Mode::WBFM: return "WBFM";
            case Mode::NBFM: return "NBFM";
            case Mode::AM: return "AM";
            case Mode::USB: return "USB";
            default: return "LSB";
        }
    }
    double audio_rate() const { return AUDIO_RATE; }

private:
    static constexpr double AUDIO_RATE = 48e3;
    static constexpr size_t MAX_DECIM = 16;
    static constexpr unsigned int MAX_TAPS = 512;
    static constexpr float AM_AGC_FLOOR = 0.25f;

    void apply_mode(Mode m) {
        _mode = m;
        freqdem_reset(_wbfm);
        freqdem_reset(_nbfm);
        firdecim_rrrf_reset(_audio_decim);
        firdecim_crcf_reset(_iq_decim);
        firfilt_crcf_reset(_ssb_bpf);
        nco_crcf_reset(_ssb_nco);
        nco_crcf_set_frequency(_ssb_nco,
            static_cast<float>(2.0 * M_PI * 1.6e3 / AUDIO_RATE) * (m == Mode::LSB ? -1.0f : 1.0f));
        _de = 0.0f;
        _am_dc = 0.0f;
        _am_n = 0;
        _settle = static_cast<size_t>(AUDIO_RATE * 0.02);
    }

    unsigned int _decim = 5;
    Mode _mode;
    std::atomic<Mode> _requested;
    float _mpx[MAX_DECIM];
    firdecim_rrrf _audio_decim = nullptr;
    firdecim_crcf _iq_decim = nullptr;
    freqdem _wbfm = nullptr, _nbfm = nullptr;
    firfilt_crcf _ssb_bpf = nullptr;
    nco_crcf _ssb_nco = nullptr;
    float _deemph_alpha = 0.0f, _de = 0.0f, _am_dc = 0.0f;
    size_t _settle = 0, _am_n = 0;
};
`,an=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "liquid.h"
#include <complex>

extern "C" {
#include "_ezgmsk_demod.h"
}

struct EZGmskDemodBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    EZGmskDemodBlock(const char* name,
                   unsigned int k,
                   unsigned int m,
                   float BT,
                   unsigned int preamble_symbols_len,
                   const unsigned char* syncword_symbols,
                   unsigned int syncword_symbols_len,
                   unsigned int header_bytes_len,
                   unsigned int payload_max_bytes_len,
                   ezgmsk::ezgmsk_demod_callback callback,
                   void* callback_context,
                   float detector_threshold = 0.9f,
                   float detector_dphi_max = 0.1f,
                   size_t buffer_size = 0)
    : BlockBase(name),
      in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size)
    {
        if (buffer_size > 0 && buffer_size * sizeof(std::complex<float>) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("EZGmskDemodBlock: buffer_size too small for doubly-mapped buffers");
        }
        _demod = ezgmsk_demod_create_set(
            k, m, BT,
            preamble_symbols_len,
            syncword_symbols,
            syncword_symbols_len,
            header_bytes_len,
            payload_max_bytes_len,
            detector_threshold,
            detector_dphi_max,
            callback,
            callback_context
        );
    }

    ~EZGmskDemodBlock() {
        if (_demod) {
            ezgmsk::ezgmsk_demod_destroy(_demod);
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        auto [read_ptr, read_size] = in.read_dbf();
        if (!read_ptr || read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        // liquid DSP functions don't modify input, so const_cast is safe here
        ezgmsk::ezgmsk_demod_execute(_demod,
            reinterpret_cast<liquid_float_complex*>(const_cast<std::complex<float>*>(read_ptr)), 
            read_size);
        in.commit_read(read_size);
        return cler::Empty{};
    }

private:
    ezgmsk::ezgmsk_demod _demod = nullptr;
};
`,on=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "liquid.h"
#include <vector>
#include "../blob.hpp"

extern "C" {
#include "_ezgmsk_mod.h"
}

struct EZGmskModBlock : public cler::BlockBase {
    cler::Channel<Blob> in;

    EZGmskModBlock(const char* name,
                unsigned int k,
                unsigned int m,
                float BT,
                unsigned int preamble_symbols_len,
                const size_t buffer_size = 512) 
    : cler::BlockBase(name),
        in(buffer_size),
        _k(k),
        _m(m),
        _BT(BT),
        _preamble_len(preamble_symbols_len)
    {
        _mod = ezgmsk::ezgmsk_mod_create_set(k, m, BT, preamble_symbols_len);
        if (!_mod) {
            cler::panic("EZGmskModBlock: failed to create EZGMSK modulator");
        }
    }

    ~EZGmskModBlock() {
        if (_mod) {
            ezgmsk::ezgmsk_mod_destroy(_mod);
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        const Blob* ptr1, *ptr2;
        size_t size1, size2;
        size_t available = in.peek_read(ptr1, size1, ptr2, size2);
        if (available == 0) {
            return cler::Error::NotEnoughSamples;
        }

        size_t frames_written = 0;
        for (size_t i = 0; i < available; ++i) {
            Blob* blob = const_cast<Blob*>(
                (i < size1) ? (ptr1 + i) : (ptr2 + i - size1)
            );
            ezgmsk::ezgmsk_mod_assemble(_mod, blob->data, blob->len);

            unsigned int frame_len = ezgmsk::ezgmsk_mod_get_frame_len(_mod);
            auto [write_ptr, write_space] = out->write_dbf();

            if (write_space < frame_len * sizeof(liquid_float_complex)) {
                break;
            }

            ezgmsk::ezgmsk_mod_execute(
            _mod,
            reinterpret_cast<liquid_float_complex*>(write_ptr),
            frame_len
            );

            out->commit_write(frame_len);
            ezgmsk::ezgmsk_mod_reset(_mod);

            blob->release();
            in.commit_read(1);
            ++frames_written;
        }

        if (frames_written == 0) {
            return cler::Error::NotEnoughSpace;
        }
        return cler::Empty{};
    }

private:
    ezgmsk::ezgmsk_mod _mod = nullptr;
    unsigned int _k;
    unsigned int _m;
    float _BT;
    unsigned int _preamble_len;
};
`,_n=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "liquid.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

// Complex baseband in, recovered packets out (inverse of PacketFramerBlock).
//
// flexframesync reports a frame through a C callback, which can fire several
// times inside one execute() call, so payloads land in a preallocated staging
// ring that procedure() drains into the output channel. A payload is accepted
// only when the CRC passed and its length matches packet_bytes: the length comes
// out of a header that noise can corrupt.
struct PacketDeframerBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    PacketDeframerBlock(const char* name,
                        size_t packet_bytes,
                        size_t buffer_size = 8192)
        : cler::BlockBase(name), in(buffer_size), _packet_bytes(packet_bytes),
          _staging(packet_bytes * STAGING_PACKETS) {
        if (packet_bytes == 0 || packet_bytes > LIQUID_MAX_PAYLOAD_LEN) {
            cler::panic("PacketDeframerBlock: packet_bytes out of range");
        }
        _fs = flexframesync_create(&PacketDeframerBlock::on_frame, this);
        if (!_fs) {
            cler::panic("PacketDeframerBlock: flexframesync_create failed");
        }
        _samples.resize(CHUNK);
        _drain.resize(CHUNK);
    }

    ~PacketDeframerBlock() { flexframesync_destroy(_fs); }

    size_t packet_bytes() const { return _packet_bytes; }

    // Counter getters read liquid's stats struct unsynchronized: call them from
    // the thread running procedure(), or after the flowgraph has stopped.
    unsigned int frames_detected() const { return flexframesync_get_framedatastats(_fs).num_frames_detected; }
    unsigned int headers_valid() const { return flexframesync_get_framedatastats(_fs).num_headers_valid; }
    unsigned int payloads_valid() const { return flexframesync_get_framedatastats(_fs).num_payloads_valid; }
    // Payloads that passed the CRC but found no staging room; should stay zero.
    uint64_t payloads_dropped() const { return _dropped.load(std::memory_order_relaxed); }

    // Signal quality of the last accepted payload; safe to read from another thread.
    float evm_db() const { return _evm.load(std::memory_order_relaxed); }
    float rssi_db() const { return _rssi.load(std::memory_order_relaxed); }
    float cfo() const { return _cfo.load(std::memory_order_relaxed); }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<uint8_t>* out) {
        bool progress = false;

        const size_t moved = std::min({_staging.size(), out->space(), _drain.size()});
        if (moved > 0) {
            _staging.readN(_drain.data(), moved);
            out->writeN(_drain.data(), moved);
            progress = true;
        }

        // Feed only once the previous chunk's payloads have all left: one
        // execute() can complete several frames, and the staging ring is the
        // only place they can go. Backing up here backpressures the framer
        // instead of dropping a packet.
        if (_staging.size() == 0) {
            const size_t n = std::min(in.size(), _samples.size());
            if (n > 0) {
                in.readN(_samples.data(), n);
                flexframesync_execute(_fs, _samples.data(), static_cast<unsigned int>(n));
                progress = true;
            }
        }
        return progress ? cler::Result<cler::Empty, cler::Error>(cler::Empty{})
                        : cler::Result<cler::Empty, cler::Error>(cler::Error::NotEnoughSpaceOrSamples);
    }

private:
    static constexpr size_t CHUNK = 4096;
    // A CHUNK-sample execute() completes at most ~7 frames (the shortest
    // flexframe of any modulation is >600 samples), and the callback cannot
    // backpressure, so staging is sized well past that and is not tunable.
    static constexpr size_t STAGING_PACKETS = 64;

    static int on_frame(unsigned char* /*header*/, int header_valid,
                        unsigned char* payload, unsigned int payload_len, int payload_valid,
                        framesyncstats_s stats, void* userdata) {
        return static_cast<PacketDeframerBlock*>(userdata)
            ->accept(header_valid, payload, payload_len, payload_valid, stats);
    }

    int accept(int header_valid, unsigned char* payload, unsigned int payload_len,
               int payload_valid, framesyncstats_s stats) {
        if (!header_valid || !payload_valid || payload == nullptr || payload_len != _packet_bytes) {
            return 0;
        }
        if (_staging.space() < _packet_bytes) {
            _dropped.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
        _staging.writeN(payload, _packet_bytes);
        _evm.store(stats.evm, std::memory_order_relaxed);
        _rssi.store(stats.rssi, std::memory_order_relaxed);
        _cfo.store(stats.cfo, std::memory_order_relaxed);
        return 0;
    }

    size_t _packet_bytes;
    cler::Channel<uint8_t> _staging;
    flexframesync _fs = nullptr;
    std::vector<std::complex<float>> _samples;
    std::vector<uint8_t> _drain;

    std::atomic<uint64_t> _dropped{0};
    std::atomic<float> _evm{0.0f};
    std::atomic<float> _rssi{0.0f};
    std::atomic<float> _cfo{0.0f};
};
`,cn=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "liquid.h"
#include <cstdio>

// Shared helpers for the FEC and framing blocks.
//
// liquid compiles its convolutional and Reed-Solomon codecs only when it is
// built against libfec (\`LIBFEC_ENABLED\`); the copy fetched by this project is
// not, so LIQUID_FEC_CONV_* and LIQUID_FEC_RS_M8 exist in the enum but
// fec_create() returns NULL for them. Probe before creating so the failure is a
// named panic rather than a null dereference.
inline bool fec_scheme_available(fec_scheme scheme) {
    fec q = fec_create(scheme, nullptr);
    if (!q) return false;
    fec_destroy(q);
    return true;
}

inline fec fec_create_or_panic(fec_scheme scheme, const char* who) {
    fec q = fec_create(scheme, nullptr);
    if (!q) {
        std::fprintf(stderr, "%s: ", who);
        cler::panic("fec scheme unavailable in this liquid build "
                    "(convolutional and Reed-Solomon codecs need libfec)");
    }
    return q;
}
`,ln=`#pragma once

#include "cler.hpp"
#include "desktop_blocks/fec/fec.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

// Inverse of FECEncoderBlock: consumes one coded block of
// fec_get_enc_msg_length(scheme, payload_bytes) bytes and emits payload_bytes.
//
// liquid's fec_decode() reports no uncorrectable-error indication for the block
// codes, so a codeword corrupted beyond the code's capability decodes silently
// to the wrong payload; detection belongs to a CRC above this block.
struct FECDecoderBlock : public cler::BlockBase {
    cler::Channel<uint8_t> in;

    FECDecoderBlock(const char* name, size_t payload_bytes, fec_scheme scheme, size_t buffer_size = 4096)
        : cler::BlockBase(name), in(buffer_size), _payload_bytes(payload_bytes) {
        if (payload_bytes == 0) {
            cler::panic("FECDecoderBlock requires payload_bytes > 0");
        }
        _fec = fec_create_or_panic(scheme, name);
        _encoded_bytes = fec_get_enc_msg_length(scheme, static_cast<unsigned int>(payload_bytes));
        if (buffer_size < _encoded_bytes) {
            cler::panic("FECDecoderBlock input buffer smaller than one encoded block");
        }
        _dec.resize(_payload_bytes);
        _enc.resize(_encoded_bytes);
    }

    ~FECDecoderBlock() { fec_destroy(_fec); }

    size_t payload_bytes() const { return _payload_bytes; }
    size_t encoded_bytes() const { return _encoded_bytes; }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<uint8_t>* out) {
        size_t blocks = std::min(in.size() / _encoded_bytes, out->space() / _payload_bytes);
        if (blocks == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }
        for (size_t b = 0; b < blocks; ++b) {
            in.readN(_enc.data(), _encoded_bytes);
            fec_decode(_fec, static_cast<unsigned int>(_payload_bytes), _enc.data(), _dec.data());
            out->writeN(_dec.data(), _payload_bytes);
        }
        return cler::Empty{};
    }

private:
    size_t _payload_bytes;
    size_t _encoded_bytes = 0;
    fec _fec = nullptr;
    std::vector<uint8_t> _dec, _enc;
};
`,dn=`#pragma once

#include "cler.hpp"
#include "desktop_blocks/fec/fec.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

// Block FEC encoder: consumes \`payload_bytes\` at a time and emits
// fec_get_enc_msg_length(scheme, payload_bytes) coded bytes. A whole block is
// consumed only when the whole coded block fits, so a short output never splits
// a codeword.
struct FECEncoderBlock : public cler::BlockBase {
    cler::Channel<uint8_t> in;

    FECEncoderBlock(const char* name, size_t payload_bytes, fec_scheme scheme, size_t buffer_size = 4096)
        : cler::BlockBase(name), in(buffer_size), _payload_bytes(payload_bytes) {
        if (payload_bytes == 0) {
            cler::panic("FECEncoderBlock requires payload_bytes > 0");
        }
        _fec = fec_create_or_panic(scheme, name);
        _encoded_bytes = fec_get_enc_msg_length(scheme, static_cast<unsigned int>(payload_bytes));
        if (buffer_size < payload_bytes) {
            cler::panic("FECEncoderBlock input buffer smaller than one payload block");
        }
        _dec.resize(_payload_bytes);
        _enc.resize(_encoded_bytes);
    }

    ~FECEncoderBlock() { fec_destroy(_fec); }

    size_t payload_bytes() const { return _payload_bytes; }
    size_t encoded_bytes() const { return _encoded_bytes; }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<uint8_t>* out) {
        size_t blocks = std::min(in.size() / _payload_bytes, out->space() / _encoded_bytes);
        if (blocks == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }
        for (size_t b = 0; b < blocks; ++b) {
            in.readN(_dec.data(), _payload_bytes);
            fec_encode(_fec, static_cast<unsigned int>(_payload_bytes), _dec.data(), _enc.data());
            out->writeN(_enc.data(), _encoded_bytes);
        }
        return cler::Empty{};
    }

private:
    size_t _payload_bytes;
    size_t _encoded_bytes = 0;
    fec _fec = nullptr;
    std::vector<uint8_t> _dec, _enc;
};
`,un=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "liquid.h"
#include <algorithm>
#include <cstdint>
#include <vector>

// Packets in, complex baseband out, using liquid's flexframegen: preamble,
// coded header (CRC + FEC), then a coded payload of \`packet_bytes\`. Modulation,
// CRC and the two FEC layers are the flexframegen properties.
//
// One frame is in flight at a time: a packet is consumed only when the previous
// frame has been fully written out, so backpressure never truncates a frame.
struct PacketFramerBlock : public cler::BlockBase {
    cler::Channel<uint8_t> in;

    PacketFramerBlock(const char* name,
                      size_t packet_bytes,
                      modulation_scheme scheme = LIQUID_MODEM_QPSK,
                      crc_scheme check = LIQUID_CRC_32,
                      fec_scheme fec0 = LIQUID_FEC_NONE,
                      fec_scheme fec1 = LIQUID_FEC_HAMMING128,
                      size_t buffer_size = 4096)
        : cler::BlockBase(name), in(buffer_size), _packet_bytes(packet_bytes) {
        if (packet_bytes == 0 || packet_bytes > LIQUID_MAX_PAYLOAD_LEN) {
            cler::panic("PacketFramerBlock: packet_bytes out of range");
        }
        if (buffer_size < packet_bytes) {
            cler::panic("PacketFramerBlock input buffer smaller than one packet");
        }
        flexframegenprops_s props;
        flexframegenprops_init_default(&props);
        props.check = check;
        props.fec0 = fec0;
        props.fec1 = fec1;
        props.mod_scheme = scheme;
        _fg = flexframegen_create(&props);
        if (!_fg) {
            cler::panic("PacketFramerBlock: flexframegen_create failed (unsupported scheme/fec)");
        }
        _payload.resize(_packet_bytes);
        _samples.resize(CHUNK);
    }

    ~PacketFramerBlock() { flexframegen_destroy(_fg); }

    size_t packet_bytes() const { return _packet_bytes; }

    // Samples per frame; valid once a packet has been assembled.
    unsigned int frame_samples() const { return _frame_samples; }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        if (_remaining == 0) {
            if (in.size() < _packet_bytes) {
                return cler::Error::NotEnoughSamples;
            }
            // Consume the packet only once its first samples can leave, so a
            // stalled output never strands a packet inside the generator.
            if (out->space() == 0) {
                return cler::Error::NotEnoughSpace;
            }
            in.readN(_payload.data(), _packet_bytes);
            // NULL header: flexframegen zero-fills its own user header, whose
            // length (FLEXFRAME_H_USER_DEFAULT) is a liquid internal. Passing a
            // local array of a guessed size makes assemble() over-read it.
            flexframegen_assemble(_fg, nullptr, _payload.data(), static_cast<unsigned int>(_packet_bytes));
            _frame_samples = flexframegen_getframelen(_fg);
            _remaining = _frame_samples;
        }
        const size_t n = std::min({_remaining, out->space(), _samples.size()});
        if (n == 0) {
            return cler::Error::NotEnoughSpace;
        }
        flexframegen_write_samples(_fg, _samples.data(), static_cast<unsigned int>(n));
        out->writeN(_samples.data(), n);
        _remaining -= n;
        return cler::Empty{};
    }

private:
    static constexpr size_t CHUNK = 4096;

    size_t _packet_bytes;
    size_t _remaining = 0;
    unsigned int _frame_samples = 0;
    flexframegen _fg = nullptr;
    std::vector<uint8_t> _payload;
    std::vector<std::complex<float>> _samples;
};
`,pn=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "liquid.h"
#include <type_traits>
#include <cmath>

template <typename T>
struct KaiserLPFBlock : public cler::BlockBase {
    cler::Channel<T> in;

    // Kaiser low-pass filter using liquid-dsp
    // Parameters:
    //   sample_rate      : Input sample rate in Hz (e.g., 2e6 for 2 MSPS)
    //   cutoff_freq      : Cutoff frequency in Hz (e.g., 100e3 for 100 kHz)
    //   transition_bw    : Transition bandwidth in Hz (e.g., 20e3 for 20 kHz)
    //   attenuation_db   : Stopband attenuation in dB (default: 60)
    KaiserLPFBlock(const char* name,
                   double sample_rate = 1.0e6,
                   double cutoff_freq = 100.0e3,
                   double transition_bw = 20.0e3,
                   double attenuation_db = 60.0,
                   size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size),
          _sample_rate(sample_rate),
          _cutoff_freq(cutoff_freq)
    {
        if (buffer_size > 0 && buffer_size * sizeof(T) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        if (sample_rate <= 0.0) {
            cler::panic("Sample rate must be positive");
        }
        if (cutoff_freq <= 0.0 || cutoff_freq >= sample_rate / 2.0) {
            cler::panic("Cutoff frequency must be between 0 and Nyquist");
        }
        if (transition_bw <= 0.0) {
            cler::panic("Transition bandwidth must be positive");
        }
        if (attenuation_db <= 0.0) {
            cler::panic("Attenuation must be positive");
        }

        // Normalize cutoff frequency to [0, 0.5] where 0.5 is Nyquist
        float fc = static_cast<float>(cutoff_freq / sample_rate);
        if (fc >= 0.5f) {
            cler::panic("Cutoff frequency must be less than Nyquist frequency (sample_rate/2)");
        }

        // liquid-dsp order estimate: order = ceil(attenuation / (22.0 * transition_bw_normalized))
        float transition_bw_normalized = static_cast<float>(transition_bw / sample_rate);
        unsigned int filter_order = static_cast<unsigned int>(
            std::ceil(attenuation_db / (22.0f * transition_bw_normalized)));

        if (filter_order < 5) {
            cler::panic("Filter order too small. Increase transition_bw or decrease attenuation_db");
        }
        // Odd order gives better frequency response
        if (filter_order % 2 == 0) {
            filter_order++;
        }

        if constexpr (std::is_same_v<T, float>) {
            _filter_r = firfilt_rrrf_create_kaiser(
                filter_order,
                fc,
                static_cast<float>(attenuation_db),
                0.0f);  // mu (fractional sample delay, usually 0)

            if (!_filter_r) {
                cler::panic("Failed to create Kaiser LPF for float");
            }
        } else if constexpr (std::is_same_v<T, std::complex<float>>) {
            _filter_c = firfilt_crcf_create_kaiser(
                filter_order,
                fc,
                static_cast<float>(attenuation_db),
                0.0f);  // mu (fractional sample delay, usually 0)

            if (!_filter_c) {
                cler::panic("Failed to create Kaiser LPF for complex float");
            }
        }
    }

    ~KaiserLPFBlock() {
        if constexpr (std::is_same_v<T, float>) {
            if (_filter_r) {
                firfilt_rrrf_destroy(_filter_r);
            }
        } else if constexpr (std::is_same_v<T, std::complex<float>>) {
            if (_filter_c) {
                firfilt_crcf_destroy(_filter_c);
            }
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        auto [read_ptr, read_size] = in.read_dbf();
        if (!read_ptr || read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        auto [write_ptr, write_space] = out->write_dbf();
        if (!write_ptr || write_space == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t samples_to_process = std::min(read_size, write_space);

        if constexpr (std::is_same_v<T, float>) {
            firfilt_rrrf_execute_block(
                _filter_r,
                const_cast<float*>(read_ptr),
                samples_to_process,
                write_ptr
            );
        } else if constexpr (std::is_same_v<T, std::complex<float>>) {
            firfilt_crcf_execute_block(
                _filter_c,
                reinterpret_cast<liquid_float_complex*>(const_cast<std::complex<float>*>(read_ptr)),
                samples_to_process,
                reinterpret_cast<liquid_float_complex*>(write_ptr)
            );
        }

        in.commit_read(samples_to_process);
        out->commit_write(samples_to_process);

        return cler::Empty{};
    }

private:
    firfilt_rrrf _filter_r = nullptr;
    firfilt_crcf _filter_c = nullptr;
    double _sample_rate;
    double _cutoff_freq;
};
`,fn=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "liquid.h"
#include <complex>

struct FMDemodBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    // FM demodulator using liquid-dsp's freqdem
    // Parameters:
    //   sample_rate    : SDR sample rate in Hz (e.g., 2e6 for 2 MSPS)
    //   freq_deviation : FM frequency deviation in Hz (default: 75 kHz for broadcast)
    FMDemodBlock(const char* name,
                 double sample_rate,
                 double freq_deviation = 75e3,
                 size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size),
          _sample_rate(sample_rate),
          _freq_deviation(freq_deviation)
    {
        if (buffer_size > 0 && buffer_size * sizeof(std::complex<float>) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        if (sample_rate <= 0.0) {
            cler::panic("Sample rate must be positive");
        }
        if (freq_deviation <= 0.0) {
            cler::panic("Frequency deviation must be positive");
        }

        float kf = static_cast<float>(freq_deviation / sample_rate);
        _demod = freqdem_create(kf);
        if (!_demod) {
            cler::panic("Failed to create FM demodulator");
        }
    }

    ~FMDemodBlock() {
        if (_demod) {
            freqdem_destroy(_demod);
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        auto [read_ptr, read_size] = in.read_dbf();
        if (!read_ptr || read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        auto [write_ptr, write_space] = out->write_dbf();
        if (!write_ptr || write_space == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t samples_to_process = std::min({read_size, write_space});

        freqdem_demodulate_block(
            _demod,
            reinterpret_cast<liquid_float_complex*>(const_cast<std::complex<float>*>(read_ptr)),
            samples_to_process,
            reinterpret_cast<float*>(write_ptr));

        in.commit_read(samples_to_process);
        out->commit_write(samples_to_process);

        return cler::Empty{};
    }

private:

    freqdem _demod = nullptr;
    double _sample_rate;
    double _freq_deviation;
};
`,mn=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/fm/rds.hpp"
#include "liquid.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <mutex>

// Broadcast FM multiplex decoder. Input is the demodulated MPX baseband
// (float, normalised so +/-1 = +/-deviation) at mpx_rate >= 120 kHz; output is
// interleaved L,R audio at mpx_rate / audio_decim. One PLL on the 19 kHz pilot
// drives both the 38 kHz stereo subcarrier and the 57 kHz RDS carrier.
struct FMMpxDecoderBlock : public cler::BlockBase {
    static constexpr size_t MAX_DECIM = 16;
    cler::Channel<float> in;

    FMMpxDecoderBlock(const char* name,
                      double mpx_rate,
                      size_t audio_decim = 5,
                      double deemphasis_us = 50.0,
                      size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float) : buffer_size),
          _fs(mpx_rate),
          _decim(audio_decim)
    {
        if (buffer_size > 0 && buffer_size * sizeof(float) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        if (mpx_rate < 120e3) cler::panic("FMMpxDecoderBlock: mpx_rate must be >= 120 kHz");
        if (audio_decim == 0 || audio_decim > MAX_DECIM) cler::panic("FMMpxDecoderBlock: bad audio_decim");
        _audio_fs = mpx_rate / static_cast<double>(audio_decim);
        if (_audio_fs < 32e3) cler::panic("FMMpxDecoderBlock: audio rate must be >= 32 kHz");

        // pilot PLL: 2nd order, ~15 Hz loop bandwidth
        const double wn = 2.0 * M_PI * 15.0 / mpx_rate;
        _kp = static_cast<float>(2.0 * 0.707 * wn);
        _ki = static_cast<float>(wn * wn);
        _omega = static_cast<float>(2.0 * M_PI * 19e3 / mpx_rate);
        _pd_alpha = static_cast<float>(1.0 - std::exp(-2.0 * M_PI * 200.0 / mpx_rate));
        _stat_alpha = static_cast<float>(1.0 / (0.1 * mpx_rate));

        // 15 kHz audio lowpass folded into the decimator
        const float fc = static_cast<float>(15e3 / mpx_rate);
        const float df = static_cast<float>(4e3 / mpx_rate);
        unsigned int h_len = estimate_req_filter_len(df, 60.0f);
        if (h_len % 2 == 0) ++h_len;
        if (h_len > MAX_TAPS) h_len = MAX_TAPS;
        liquid_firdes_kaiser(h_len, fc, 60.0f, 0.0f, _audio_taps.data());
        float dc = 0.0f;
        for (unsigned int i = 0; i < h_len; ++i) dc += _audio_taps[i];
        for (unsigned int i = 0; i < h_len; ++i) _audio_taps[i] /= dc;
        _lpr_decim = firdecim_rrrf_create(static_cast<unsigned int>(audio_decim), _audio_taps.data(), h_len);
        _lmr_decim = firdecim_rrrf_create(static_cast<unsigned int>(audio_decim), _audio_taps.data(), h_len);
        if (!_lpr_decim || !_lmr_decim) cler::panic("FMMpxDecoderBlock: firdecim create failed");
        set_deemphasis_us(deemphasis_us);

        // RDS: 57 kHz -> baseband, integer-decimate to ~24 kHz with a real
        // anti-alias filter (57 kHz aliases to DC at 19 kHz), resample to
        // 8 samples per half-bit (19 kHz), then a polyphase symbol
        // synchroniser with an RRC matched filter.
        _rds_decim = static_cast<unsigned int>(std::max(1.0, std::floor(mpx_rate / 24e3)));
        if (_rds_decim > MAX_DECIM) cler::panic("FMMpxDecoderBlock: mpx_rate too high for RDS path");
        const float rfc = static_cast<float>(4e3 / mpx_rate);
        const float rdf = static_cast<float>((mpx_rate / _rds_decim / 2.0 - 4e3) / mpx_rate);
        unsigned int rlen = estimate_req_filter_len(rdf, 60.0f);
        if (rlen % 2 == 0) ++rlen;
        if (rlen > MAX_TAPS) rlen = MAX_TAPS;
        liquid_firdes_kaiser(rlen, rfc, 60.0f, 0.0f, _rds_taps.data());
        _rds_firdecim = firdecim_crcf_create(_rds_decim, _rds_taps.data(), rlen);
        const float r = static_cast<float>(RDS_FS * _rds_decim / mpx_rate);
        _rds_resamp = resamp_crcf_create(r, 7, std::min(0.45f, 0.45f * r), 60.0f, 64);
        _rds_sync = symsync_crcf_create_rnyquist(LIQUID_FIRFILT_RRC, RDS_SPS, 4, 0.9f, 32);
        if (!_rds_firdecim || !_rds_resamp || !_rds_sync) cler::panic("FMMpxDecoderBlock: RDS create failed");
        symsync_crcf_set_lf_bw(_rds_sync, 0.02f);
        symsync_crcf_set_output_rate(_rds_sync, 1);
    }

    ~FMMpxDecoderBlock() {
        if (_lpr_decim) firdecim_rrrf_destroy(_lpr_decim);
        if (_lmr_decim) firdecim_rrrf_destroy(_lmr_decim);
        if (_rds_firdecim) firdecim_crcf_destroy(_rds_firdecim);
        if (_rds_resamp) resamp_crcf_destroy(_rds_resamp);
        if (_rds_sync) symsync_crcf_destroy(_rds_sync);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        auto [rptr, rsize] = in.read_dbf();
        auto [wptr, wsize] = out->write_dbf();
        size_t frames = std::min(rsize / _decim, wsize / 2);
        if (frames == 0) return cler::Error::NotEnoughSpaceOrSamples;

        const bool stereo_on = _stereo_enabled.load(std::memory_order_relaxed);
        const float de = _deemph_alpha.load(std::memory_order_relaxed);
        std::array<float, MAX_DECIM> lpr{}, lmr{};

        for (size_t f = 0; f < frames; ++f) {
            const float* x = rptr + f * _decim;
            for (size_t i = 0; i < _decim; ++i) {
                const float s = x[i];
                const float sn = std::sin(_theta), cs = std::cos(_theta);

                // phase detector on the pilot
                _pd_i += _pd_alpha * (s * cs - _pd_i);
                _pd_q += _pd_alpha * (-s * sn - _pd_q);
                const float err = std::atan2(_pd_q, _pd_i);
                _freq += _ki * err;
                _theta += _omega + _freq + _kp * err;
                if (_theta > 2.0f * static_cast<float>(M_PI)) _theta -= 2.0f * static_cast<float>(M_PI);
                else if (_theta < 0.0f) _theta += 2.0f * static_cast<float>(M_PI);

                _pil_i += _stat_alpha * (_pd_i - _pil_i);
                _pil_q2 += _stat_alpha * (_pd_q * _pd_q - _pil_q2);

                lpr[i] = s;
                // pilot locks as cos(theta) = sin(phi); subcarrier is sin(2 phi) = -sin(2 theta)
                lmr[i] = -2.0f * s * (2.0f * sn * cs);

                rds_sample(s, sn, cs);
            }
            float sum = 0.0f, diff = 0.0f;
            firdecim_rrrf_execute(_lpr_decim, lpr.data(), &sum);
            firdecim_rrrf_execute(_lmr_decim, lmr.data(), &diff);
            if (!stereo_on || !_locked) diff = 0.0f;
            const float l = 0.5f * (sum + diff), r = 0.5f * (sum - diff);
            _de_l += de * (l - _de_l);
            _de_r += de * (r - _de_r);
            wptr[2 * f] = _de_l;
            wptr[2 * f + 1] = _de_r;
        }

        const float snr = _pil_i * _pil_i / (_pil_q2 + 1e-12f);
        const float snr_db = 10.0f * std::log10(snr + 1e-12f);
        _locked = (_pil_i > 0.02f) && (snr_db > 8.0f);
        _pilot_snr_db.store(snr_db, std::memory_order_relaxed);
        _pilot_level.store(2.0f * _pil_i, std::memory_order_relaxed);
        _stereo_locked.store(_locked, std::memory_order_relaxed);

        in.commit_read(frames * _decim);
        out->commit_write(frames * 2);
        return cler::Empty{};
    }

    float pilot_snr_db() const { return _pilot_snr_db.load(std::memory_order_relaxed); }
    // pilot amplitude relative to full deviation (nominal 0.08-0.10)
    float pilot_level() const { return _pilot_level.load(std::memory_order_relaxed); }
    bool stereo_locked() const { return _stereo_locked.load(std::memory_order_relaxed); }
    void set_stereo(bool on) { _stereo_enabled.store(on, std::memory_order_relaxed); }
    bool stereo() const { return _stereo_enabled.load(std::memory_order_relaxed); }
    void set_deemphasis_us(double us) {
        const float a = us <= 0.0 ? 1.0f
            : static_cast<float>(1.0 - std::exp(-1.0 / (us * 1e-6 * _audio_fs)));
        _deemph_alpha.store(a, std::memory_order_relaxed);
    }
    double audio_rate() const { return _audio_fs; }

    uint64_t rds_halfbits() const { return _rds_halfbits.load(std::memory_order_relaxed); }

    rds::Station rds_station() const {
        std::lock_guard<std::mutex> lock(_rds_mutex);
        return _rds_snapshot;
    }
    void rds_reset() {
        std::lock_guard<std::mutex> lock(_rds_mutex);
        _rds_reset_request = true;
        _rds_snapshot = rds::Station{};
    }

private:
    static constexpr size_t MAX_TAPS = 512;
    static constexpr double RDS_FS = 19000.0;   // 8 samples per half-bit at 2375 baud
    static constexpr unsigned int RDS_SPS = 8;

    void rds_sample(float s, float sn, float cs) {
        // e^{-j3 theta}
        const float c3 = cs * (4.0f * cs * cs - 3.0f);
        const float s3 = sn * (3.0f - 4.0f * sn * sn);
        _rds_mix[_rds_fill++] = {s * c3, -s * s3};
        if (_rds_fill < _rds_decim) return;
        _rds_fill = 0;
        liquid_float_complex z;
        firdecim_crcf_execute(_rds_firdecim, _rds_mix.data(), &z);
        liquid_float_complex rs[4];
        unsigned int n = 0;
        resamp_crcf_execute(_rds_resamp, z, rs, &n);
        for (unsigned int k = 0; k < n; ++k) {
            liquid_float_complex sym[2];
            unsigned int m = 0;
            symsync_crcf_execute(_rds_sync, &rs[k], 1, sym, &m);
            for (unsigned int j = 0; j < m; ++j) rds_halfbit(sym[j]);
        }
    }

    void rds_halfbit(std::complex<float> v) {
        _rds_halfbits.fetch_add(1, std::memory_order_relaxed);
        // BPSK phase: slow average of v^2 gives 2*phase
        const std::complex<float> v2 = v * v;
        _rds_ph += 0.005f * (v2 - _rds_ph);
        const float ph = 0.5f * std::atan2(_rds_ph.imag(), _rds_ph.real());
        const float y = v.real() * std::cos(ph) + v.imag() * std::sin(ph);

        // biphase: each bit is two opposite half-bits; track which pairing is right
        const float d = y - _rds_prev;
        _rds_pair[_rds_parity] += 0.01f * (std::fabs(d) - _rds_pair[_rds_parity]);
        const int best = _rds_pair[0] >= _rds_pair[1] ? 0 : 1;
        if (_rds_parity == best) {
            const bool bit = d > 0.0f;
            const bool data = bit ^ _rds_prev_bit;
            _rds_prev_bit = bit;
            if (_rds.push_bit(data)) publish_rds();
        }
        _rds_prev = y;
        _rds_parity ^= 1;
    }

    void publish_rds() {
        std::lock_guard<std::mutex> lock(_rds_mutex);
        if (_rds_reset_request) {
            _rds.reset();
            _rds_reset_request = false;
        }
        _rds_snapshot = _rds.station();
    }

    double _fs, _audio_fs;
    size_t _decim;
    float _kp, _ki, _omega, _pd_alpha, _stat_alpha;
    float _theta = 0.0f, _freq = 0.0f;
    float _pd_i = 0.0f, _pd_q = 0.0f;
    float _pil_i = 0.0f, _pil_q2 = 1.0f;
    bool _locked = false;
    float _de_l = 0.0f, _de_r = 0.0f;

    std::array<float, MAX_TAPS> _audio_taps{};
    firdecim_rrrf _lpr_decim = nullptr, _lmr_decim = nullptr;

    unsigned int _rds_decim = 1;
    std::array<float, MAX_TAPS> _rds_taps{};
    std::array<liquid_float_complex, MAX_DECIM> _rds_mix{};
    unsigned int _rds_fill = 0;
    firdecim_crcf _rds_firdecim = nullptr;
    resamp_crcf _rds_resamp = nullptr;
    symsync_crcf _rds_sync = nullptr;
    std::complex<float> _rds_ph{0.0f, 0.0f};
    float _rds_prev = 0.0f;
    float _rds_pair[2] = {0.0f, 0.0f};
    int _rds_parity = 0;
    bool _rds_prev_bit = false;
    rds::Decoder _rds;
    mutable std::mutex _rds_mutex;
    rds::Station _rds_snapshot;
    bool _rds_reset_request = false;

    std::atomic<uint64_t> _rds_halfbits{0};
    std::atomic<float> _pilot_snr_db{-99.0f};
    std::atomic<float> _pilot_level{0.0f};
    std::atomic<bool> _stereo_locked{false};
    std::atomic<bool> _stereo_enabled{true};
    std::atomic<float> _deemph_alpha{1.0f};
};
`,hn=`#pragma once

#include <array>
#include <cstdint>
#include <cstring>

// RBDS/RDS bit-level decoder: takes differentially-decoded data bits, finds
// block sync via the 10-bit syndromes, assembles groups, and keeps the
// station text (PI, PTY, PS, RadioText). No DSP here, no allocation.
namespace rds {

struct Station {
    uint16_t pi = 0;
    uint8_t pty = 0;
    bool tp = false;
    bool ta = false;
    bool synced = false;
    char ps[9] = {};       // programme service name (8 chars)
    char rt[65] = {};      // radiotext (up to 64 chars)
    uint32_t groups_ok = 0;
    uint32_t blocks_bad = 0;
    uint32_t blocks_corrected = 0;
    uint32_t blocks_total = 0;
};

class Decoder {
public:
    static constexpr uint32_t OFFSET_A = 0x0FC, OFFSET_B = 0x198, OFFSET_C = 0x168,
                              OFFSET_CP = 0x350, OFFSET_D = 0x1B4;

    // Syndrome of a 26-bit block: 16 data + 10 check bits, generator
    // g(x) = x^10 + x^8 + x^7 + x^5 + x^4 + x^3 + 1.
    static uint32_t syndrome(uint32_t block) {
        uint32_t reg = 0;
        for (int i = 25; i >= 0; --i) {
            reg = (reg << 1) | ((block >> i) & 1u);
            if (reg & 0x400u) reg ^= 0x5B9u;
        }
        return reg & 0x3FF;
    }

    // Burst error correction: the syndrome of the received block XOR the
    // expected offset is the syndrome of the error pattern alone; a table maps
    // it back to the burst. The code can correct 5-bit bursts, but with
    // random (not bursty) bit errors every extra table entry is a chance to
    // turn an uncorrectable block into a valid-looking wrong one, so only
    // bursts up to MAX_BURST bits are corrected (single-bit errors dominate).
    // Returns the corrected block, or 0 when the syndrome is not a short burst.
    static constexpr int MAX_BURST = 2;
    static uint32_t correct(uint32_t block, uint32_t offset) {
        static const BurstTable table;
        const uint32_t s = (syndrome(block) ^ offset) & 0x3FF;
        if (s == 0) return block;
        const uint32_t e = table.at(s);
        return e ? (block ^ e) : 0;
    }

    static int offset_index(uint32_t s) {
        switch (s) {
            case OFFSET_A: return 0;
            case OFFSET_B: return 1;
            case OFFSET_C: return 2;
            case OFFSET_CP: return 2;
            case OFFSET_D: return 3;
            default: return -1;
        }
    }

    // Feed one data bit (already differentially decoded). Returns true when a
    // complete group was accepted.
    bool push_bit(bool bit) {
        _shift = ((_shift << 1) | (bit ? 1u : 0u)) & 0x3FFFFFFu;
        ++_bits;
        if (!_station.synced) {
            const int idx = offset_index(syndrome(_shift));
            if (idx == 0) {
                _station.synced = true;
                _bits = 0;
                _blocks[0] = _shift >> 10;
                _valid = 1;
                _expect = 1;
                _bad_run = 0;
                ++_station.blocks_total;
            }
            return false;
        }
        if (_bits < 26) return false;
        _bits = 0;
        ++_station.blocks_total;
        if (_expect == 0) _valid = 0;
        const uint32_t s = syndrome(_shift);
        const int idx = offset_index(s);
        if (idx != _expect) {
            // a block that matches another offset is a sync slip, not noise
            const uint32_t want = _expect == 0 ? OFFSET_A : _expect == 1 ? OFFSET_B : _expect == 2 ? OFFSET_C : OFFSET_D;
            uint32_t fixed = idx < 0 ? correct(_shift, want) : 0;
            if (!fixed && idx < 0 && _expect == 2) fixed = correct(_shift, OFFSET_CP);
            if (fixed) {
                ++_station.blocks_corrected;
                _shift = fixed;
                return accept(_shift, syndrome(_shift));
            }
            ++_station.blocks_bad;
            if (++_bad_run >= 4 * 4) {
                _station.synced = false;
                _expect = 0;
            } else {
                _expect = (_expect + 1) % 4;
            }
            return false;
        }
        return accept(_shift, s);
    }

    const Station& station() const { return _station; }

    void reset() { *this = Decoder{}; }

private:
    bool accept(uint32_t block, uint32_t s) {
        const int idx = offset_index(s);
        _bad_run = 0;
        _valid |= 1u << idx;
        _blocks[idx] = block >> 10;
        _cprime = (idx == 2 && s == OFFSET_CP);
        _expect = (idx + 1) % 4;
        // a group is only as good as its worst block: a stale B would send
        // PS/RT characters to the wrong segment
        if (idx == 3 && _valid == 0xF) {
            parse_group();
            ++_station.groups_ok;
            return true;
        }
        return false;
    }

    struct BurstTable {
        std::array<uint32_t, 1024> map{};
        BurstTable() {
            // shortest burst wins a collision; bursts start and end with a set bit
            for (int len = 1; len <= MAX_BURST; ++len) {
                for (uint32_t pat = 1u << (len - 1); pat < (1u << len); ++pat) {
                    if (!(pat & 1u)) continue;
                    for (int pos = 0; pos + len <= 26; ++pos) {
                        const uint32_t e = pat << pos;
                        const uint32_t s = syndrome(e);
                        if (map[s] == 0) map[s] = e;
                    }
                }
            }
        }
        uint32_t at(uint32_t s) const { return map[s & 0x3FF]; }
    };

    void parse_group() {
        const uint32_t a = _blocks[0], b = _blocks[1], c = _blocks[2], d = _blocks[3];
        _station.pi = static_cast<uint16_t>(a);
        const uint32_t type = (b >> 12) & 0xF;
        const bool version_b = (b >> 11) & 1;
        _station.tp = (b >> 10) & 1;
        _station.pty = (b >> 5) & 0x1F;
        if (type == 0) {
            _station.ta = (b >> 4) & 1;
            const uint32_t seg = b & 0x3;
            _station.ps[2 * seg] = printable(d >> 8);
            _station.ps[2 * seg + 1] = printable(d & 0xFF);
            _station.ps[8] = 0;
        } else if (type == 2) {
            const bool ab = (b >> 4) & 1;
            if (ab != _rt_ab) {
                _rt_ab = ab;
                std::memset(_station.rt, 0, sizeof(_station.rt));
            }
            const uint32_t seg = b & 0xF;
            if (!version_b) {
                put_rt(4 * seg, c >> 8); put_rt(4 * seg + 1, c & 0xFF);
                put_rt(4 * seg + 2, d >> 8); put_rt(4 * seg + 3, d & 0xFF);
            } else {
                put_rt(2 * seg, d >> 8); put_rt(2 * seg + 1, d & 0xFF);
            }
            _station.rt[64] = 0;
        }
        (void)_cprime;
    }

    void put_rt(uint32_t pos, uint32_t ch) {
        if (pos >= 64) return;
        _station.rt[pos] = (ch == 0x0D) ? 0 : printable(ch);
    }

    static char printable(uint32_t ch) {
        return (ch >= 0x20 && ch < 0x7F) ? static_cast<char>(ch) : ' ';
    }

    Station _station;
    uint32_t _shift = 0;
    uint32_t _bits = 0;
    int _expect = 0;
    int _bad_run = 0;
    unsigned _valid = 0;
    bool _cprime = false;
    bool _rt_ab = false;
    std::array<uint32_t, 4> _blocks{};
};

// Encoder for tests and loopback: 16-bit words -> 26-bit blocks with the
// given offset, emitted MSB first.
inline uint32_t encode_block(uint16_t data, uint32_t offset) {
    const uint32_t shifted = static_cast<uint32_t>(data) << 10;
    const uint32_t check = Decoder::syndrome(shifted);
    return shifted | (check ^ offset);
}

inline const char* pty_name(uint8_t pty) {
    static const char* names[32] = {
        "None", "News", "Affairs", "Info", "Sport", "Educate", "Drama", "Culture",
        "Science", "Varied", "Pop M", "Rock M", "Easy M", "Light M", "Classics", "Other M",
        "Weather", "Finance", "Children", "Social", "Religion", "Phone In", "Travel", "Leisure",
        "Jazz", "Country", "Nation M", "Oldies", "Folk M", "Document", "TEST", "Alarm"};
    return names[pty & 31];
}

}  // namespace rds
`,bn=`#pragma once

#include "imgui.h"

namespace cler::palette {

constexpr ImVec4 rgba(unsigned r, unsigned g, unsigned b, float a = 1.0f) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

// Neutral greys with a blue accent, shared with the flowgraph GUI's tokens: a
// saturated red on every frame and title bar is tiring to sit in front of.
inline constexpr ImVec4 bg0       = rgba(0x0e, 0x11, 0x16);
inline constexpr ImVec4 bg1       = rgba(0x16, 0x1b, 0x22);
inline constexpr ImVec4 bg2       = rgba(0x1e, 0x24, 0x2d);
inline constexpr ImVec4 border    = rgba(0x2a, 0x32, 0x3d);
inline constexpr ImVec4 border_hi = rgba(0x3a, 0x44, 0x4f);
inline constexpr ImVec4 fg        = rgba(0xe6, 0xed, 0xf3);
inline constexpr ImVec4 muted     = rgba(0x8b, 0x98, 0xa9);
inline constexpr ImVec4 faint     = rgba(0x6b, 0x77, 0x87);
inline constexpr ImVec4 accent    = rgba(0x2b, 0x5f, 0xa8);
inline constexpr ImVec4 accent_hi = rgba(0x39, 0x87, 0xe5);
inline constexpr ImVec4 accent_bg = rgba(0x16, 0x28, 0x3c);
inline constexpr ImVec4 ok        = rgba(0x2e, 0x8b, 0x57);
inline constexpr ImVec4 warn      = rgba(0xb8, 0x77, 0x0a);
inline constexpr ImVec4 danger    = rgba(0xc0, 0x20, 0x2e);

inline constexpr ImVec4 plot_series[] = {
    rgba(0x39, 0x87, 0xe5),
    rgba(0x19, 0x9e, 0x70),
    rgba(0xc9, 0x85, 0x00),
    rgba(0x00, 0xa3, 0xb4),
    rgba(0x90, 0x85, 0xe9),
    rgba(0xc9, 0xb4, 0x8a),
    rgba(0xd6, 0x6a, 0x8a),
    rgba(0xe6, 0x67, 0x67),
};

}
`,gn=`#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <cmath>

/**
 * Simple shapefile (.shp) parser for Natural Earth coastlines
 *
 * Shapefiles store geometry data in binary format:
 * - Header (100 bytes)
 * - Records with geometry data
 *
 * Each record contains a record header and shape data.
 * For polylines (type 3), we extract coordinate sequences.
 */
struct CoastlineData {
    std::vector<std::vector<std::pair<float, float>>> polylines;  // lat/lon pairs

    bool load_from_shapefile(const char* shp_path) {
        std::ifstream file(shp_path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        file.seekg(0, std::ios::end);
        std::streamsize file_size = file.tellg();
        file.seekg(0, std::ios::beg);

        uint8_t header[100];
        file.read(reinterpret_cast<char*>(header), 100);
        if (file.gcount() != 100) {
            return false;
        }

        size_t offset = 100;

        while (offset < static_cast<size_t>(file_size)) {
            if (offset + 8 > static_cast<size_t>(file_size)) break;

            file.seekg(offset, std::ios::beg);

            // record header fields are big-endian; shape data below is little-endian
            uint32_t record_num_be, record_len_be;
            file.read(reinterpret_cast<char*>(&record_num_be), 4);
            file.read(reinterpret_cast<char*>(&record_len_be), 4);

            uint32_t record_len = swap_endian(record_len_be) * 2;  // length is in 16-bit words

            if (offset + 8 + record_len > static_cast<size_t>(file_size)) break;

            uint32_t shape_type_le;
            file.read(reinterpret_cast<char*>(&shape_type_le), 4);
            uint32_t shape_type = shape_type_le;

            // shape type 3 = PolyLine
            if (shape_type == 3) {
                parse_polyline(file, record_len);
            }

            offset += 8 + record_len;
        }

        file.close();
        return !polylines.empty();
    }

private:
    static uint32_t swap_endian(uint32_t val) {
        return ((val & 0x000000FFU) << 24) |
               ((val & 0x0000FF00U) << 8) |
               ((val & 0x00FF0000U) >> 8) |
               ((val & 0xFF000000U) >> 24);
    }

    static double swap_endian_double(double val) {
        uint64_t bits;
        std::memcpy(&bits, &val, sizeof(double));
        uint64_t swapped = swap_endian_double_bits(bits);
        double result;
        std::memcpy(&result, &swapped, sizeof(double));
        return result;
    }

    static uint64_t swap_endian_double_bits(uint64_t val) {
        return ((val & 0x00000000000000FFULL) << 56) |
               ((val & 0x000000000000FF00ULL) << 40) |
               ((val & 0x0000000000FF0000ULL) << 24) |
               ((val & 0x00000000FF000000ULL) << 8) |
               ((val & 0x000000FF00000000ULL) >> 8) |
               ((val & 0x0000FF0000000000ULL) >> 24) |
               ((val & 0x00FF000000000000ULL) >> 40) |
               ((val & 0xFF00000000000000ULL) >> 56);
    }

    // called with the file positioned right after the shape type field
    void parse_polyline(std::ifstream& file, uint32_t record_len) {
        double xmin, ymin, xmax, ymax;  // bounding box, unused
        file.read(reinterpret_cast<char*>(&xmin), 8);
        file.read(reinterpret_cast<char*>(&ymin), 8);
        file.read(reinterpret_cast<char*>(&xmax), 8);
        file.read(reinterpret_cast<char*>(&ymax), 8);

        uint32_t num_parts, num_points;
        file.read(reinterpret_cast<char*>(&num_parts), 4);
        file.read(reinterpret_cast<char*>(&num_points), 4);

        if (num_parts == 0 || num_points == 0) {
            return;
        }

        std::vector<uint32_t> part_indices(num_parts);
        file.read(reinterpret_cast<char*>(part_indices.data()), num_parts * 4);

        for (uint32_t part_idx = 0; part_idx < num_parts; ++part_idx) {
            uint32_t start_point = part_indices[part_idx];
            uint32_t end_point = (part_idx + 1 < num_parts) ? part_indices[part_idx + 1] : num_points;

            std::vector<std::pair<float, float>> polyline;

            for (uint32_t i = start_point; i < end_point; ++i) {
                double lon, lat;
                file.read(reinterpret_cast<char*>(&lon), 8);
                file.read(reinterpret_cast<char*>(&lat), 8);
                polyline.push_back({static_cast<float>(lat), static_cast<float>(lon)});
            }

            if (!polyline.empty()) {
                polylines.push_back(polyline);
            }
        }
    }
};
`,yn=`#pragma once

#include <GLFW/glfw3.h>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

//included here so everyone that incldues this header can use ImGui and ImPlot
#include "imgui.h"
#include "implot.h"

#include "cler.hpp"

namespace cler {

namespace gui::detail {

template <typename FG>
void render_blocks(FG& fg) {
    fg.for_each_block([](auto& block) {
        if constexpr (cler::block_declares_is_gui_v<decltype(block)>) {
            block.render();
        }
    });
}

}

class GuiManager {
public:
    GuiManager(int width = 800, int height = 400, std::string_view title = "DSP Blocks");

    GuiManager(const GuiManager&) = delete; // Copy constructor is deleted
    GuiManager& operator=(const GuiManager&) = delete; // Copy assignment operator is deleted

    ~GuiManager();

    void begin_frame();
    void end_frame();
    bool should_close() const;

    void request_close();

    void set_frame_sleep_ms(int ms) { _frame_sleep_ms = ms; }
    void frame_sleep() const;

    template <typename FG>
    void render(FG& fg) {
        begin_frame();
        gui::detail::render_blocks(fg);
        end_frame();
        frame_sleep();
    }

    // Request a one-shot screenshot of the next completed frame. The capture
    // happens inside end_frame() after the UI is drawn but before the buffer
    // swap. A \`.png\` path writes an 8-bit RGB PNG (needs zlib at build time,
    // else it falls back to the same base name as .bmp); any other extension
    // writes a 24-bit uncompressed BMP. Failures are reported on stderr.
    // GUI-thread only (same thread as end_frame()).
    void request_screenshot(const std::string& path);

private:
    GLFWwindow* window = nullptr;

    std::string _screenshot_path;
    bool        _screenshot_pending = false;
    int         _frame_sleep_ms = 15;
};

} // namespace cler
`,kn=`#pragma once

#include "desktop_blocks/gui/coastline_loader.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>

// Shared lat/lon canvas for the ADS-B and AIS maps: equirectangular
// projection around a centre, grid, coastlines, wheel zoom and drag pan.
// Call begin() inside an ImGui window, draw markers with to_screen(), then
// interact().
struct MapCanvas {
    static constexpr float DEFAULT_LAT_SPAN = 2.0f;
    static constexpr float MIN_ZOOM = 0.01f, MAX_ZOOM = 50.0f, ZOOM_SENSITIVITY = 0.1f;
    static constexpr float MIN_CANVAS_SIZE = 200.0f, CANVAS_BOUNDS_MARGIN = 100.0f;

    float center_lat, center_lon, zoom;
    CoastlineData coastlines;
    bool coastlines_loaded = false;
    ImVec2 pos{0, 0}, size{0, 0};

    MapCanvas(float lat, float lon, const char* coastline_shp, float initial_zoom = 0.1f)
        : center_lat(lat), center_lon(lon), zoom(initial_zoom) {
        coastlines_loaded = coastlines.load_from_shapefile(coastline_shp);
    }

    // background, grid and coastlines over the remaining window area
    void begin() {
        pos = ImGui::GetCursorScreenPos();
        size = ImGui::GetContentRegionAvail();
        size.x = std::max(size.x, MIN_CANVAS_SIZE);
        size.y = std::max(size.y, MIN_CANVAS_SIZE);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p1(pos.x + size.x, pos.y + size.y);
        dl->AddRectFilled(pos, p1, IM_COL32(30, 40, 50, 255));
        dl->AddRect(pos, p1, IM_COL32(200, 200, 200, 255));
        draw_grid(dl);
        draw_coastlines(dl);
    }

    float lat_span() const { return DEFAULT_LAT_SPAN / zoom; }
    float lon_span() const { return lat_span() * (size.x / size.y); }

    ImVec2 to_screen(float lat, float lon) const {
        const float lat_min = center_lat - lat_span() / 2.0f;
        const float lon_min = center_lon - lon_span() / 2.0f;
        float x = std::clamp((lon - lon_min) / lon_span(), 0.0f, 1.0f);
        float y = std::clamp((lat - lat_min) / lat_span(), 0.0f, 1.0f);
        return ImVec2(pos.x + x * size.x, pos.y + (1.0f - y) * size.y);
    }

    bool on_screen(ImVec2 p) const {
        return p.x >= pos.x - CANVAS_BOUNDS_MARGIN && p.x < pos.x + size.x + CANVAS_BOUNDS_MARGIN &&
               p.y >= pos.y - CANVAS_BOUNDS_MARGIN && p.y < pos.y + size.y + CANVAS_BOUNDS_MARGIN;
    }

    bool mouse_over() const {
        const ImVec2 m = ImGui::GetIO().MousePos;
        return m.x >= pos.x && m.x < pos.x + size.x && m.y >= pos.y && m.y < pos.y + size.y;
    }

    void interact() {
        if (!mouse_over()) return;
        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0f) {
            zoom = std::clamp(zoom * (1.0f + io.MouseWheel * ZOOM_SENSITIVITY), MIN_ZOOM, MAX_ZOOM);
        }
        for (ImGuiMouseButton b : {ImGuiMouseButton_Left, ImGuiMouseButton_Right}) {
            if (!ImGui::IsMouseDragging(b, 0.0f)) continue;
            const ImVec2 d = ImGui::GetMouseDragDelta(b);
            center_lon -= (d.x / size.x) * lon_span();
            center_lat += (d.y / size.y) * lat_span();
            ImGui::ResetMouseDragDelta(b);
            break;
        }
    }

    // heading-pointing triangle (heading in degrees, 0 = north)
    void marker(ImDrawList* dl, ImVec2 p, float heading_deg, float s, ImU32 fill) const {
        const float a = heading_deg * 3.14159265f / 180.0f - 3.14159265f / 2.0f;
        const float c = std::cos(a), sn = std::sin(a);
        const ImVec2 v0(p.x + s * 1.2f * c, p.y + s * 1.2f * sn);
        const ImVec2 v1(p.x - s * 0.8f * c - s * 0.5f * sn, p.y - s * 0.8f * sn + s * 0.5f * c);
        const ImVec2 v2(p.x - s * 0.8f * c + s * 0.5f * sn, p.y - s * 0.8f * sn - s * 0.5f * c);
        dl->AddTriangleFilled(v0, v1, v2, fill);
        dl->AddTriangle(v0, v1, v2, IM_COL32(255, 255, 255, 200), 1.0f);
    }

private:
    void draw_grid(ImDrawList* dl) const {
        const float lat_min = center_lat - lat_span() / 2.0f, lon_min = center_lon - lon_span() / 2.0f;
        const float step = lat_span() > 1.0f ? 0.5f : 0.1f;
        for (float lat = std::floor(lat_min / step) * step; lat < lat_min + lat_span(); lat += step)
            dl->AddLine(to_screen(lat, lon_min), to_screen(lat, lon_min + lon_span()), IM_COL32(100, 100, 120, 100), 0.5f);
        for (float lon = std::floor(lon_min / step) * step; lon < lon_min + lon_span(); lon += step)
            dl->AddLine(to_screen(lat_min, lon), to_screen(lat_min + lat_span(), lon), IM_COL32(100, 100, 120, 100), 0.5f);
    }

    void draw_coastlines(ImDrawList* dl) const {
        if (!coastlines_loaded) return;
        for (const auto& line : coastlines.polylines) {
            for (size_t i = 0; i + 1 < line.size(); ++i) {
                const ImVec2 a = to_screen(line[i].first, line[i].second), b = to_screen(line[i + 1].first, line[i + 1].second);
                if (on_screen(a) || on_screen(b)) dl->AddLine(a, b, IM_COL32(100, 200, 100, 180), 1.5f);
            }
        }
    }
};
`,vn=`#pragma once

#include <complex>
#include <random>
#include <type_traits>

template<typename T>
struct GainKernel {
    T gain;
    T operator()(T x) const { return x * gain; }
};

template<typename T>
struct AWGNKernel {
    using scalar_type = typename std::conditional<
        std::is_same_v<T, std::complex<float>>, float,
        typename std::conditional<std::is_same_v<T, std::complex<double>>, double, T>::type>::type;

    // seed 0 draws one from random_device; anything else is reproducible, which
    // is what a test asserting on the statistics needs
    explicit AWGNKernel(scalar_type noise_stddev, uint32_t seed = 0)
        : _normal_dist(0.0, noise_stddev) {
        if (seed == 0) {
            std::random_device rd;
            _rng.seed(rd());
        } else {
            _rng.seed(seed);
        }
    }

    void set_stddev(scalar_type stddev) {
        _normal_dist = std::normal_distribution<scalar_type>(0.0, stddev);
    }

    T operator()(T x) {
        if constexpr (std::is_same_v<T, std::complex<float>> || std::is_same_v<T, std::complex<double>>) {
            auto n_re = _normal_dist(_rng);
            auto n_im = _normal_dist(_rng);
            return x + T{n_re, n_im};
        } else {
            return x + _normal_dist(_rng);
        }
    }

private:
    std::mt19937 _rng;
    std::normal_distribution<scalar_type> _normal_dist;
};
`,wn=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "liquid.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <utility>
#include <vector>

// Counts bit errors against a known symbol sequence that repeats with period
// reference.size(). The receiver delay is unknown and decision-directed carrier
// recovery locks to any constellation symmetry, so the block first searches
// (delay, constellation rotation) once by brute force over one period, then
// counts errors while tracking its position in the reference.
//
// Rotation candidates are the M rotations by 2*pi*k/M; the ones that are not a
// bijection of the constellation onto itself are dropped, which leaves exactly
// the ambiguities the carrier loop can actually settle on (180 deg for BPSK,
// 90 deg multiples for QPSK/QAM, 45 deg multiples for 8-PSK).
//
// Alignment is held, not assumed: a decision-directed carrier loop cycle-slips
// at moderate SNR, and every symbol after the slip is counted against the wrong
// rotation (BER pinned at ~0.5 for the rest of the run). The symbol error rate
// over the last HOLD_SYMBOLS is therefore checked against a threshold looser
// than acquisition's; failing it drops back to searching and restarts the count,
// since the corrupted stretch has already polluted it.
struct BERCounterBlock : public cler::BlockBase {
    cler::Channel<uint8_t> in;

    BERCounterBlock(const char* name,
                    modulation_scheme scheme,
                    std::vector<uint8_t> reference,
                    size_t skip_symbols = 2000,
                    size_t search_symbols = 512,
                    size_t buffer_size = 4096)
        : cler::BlockBase(name), in(buffer_size), _ref(std::move(reference)),
          _skip_symbols(skip_symbols), _search_symbols(search_symbols) {
        if (_ref.empty()) {
            cler::panic("BERCounterBlock requires a non-empty reference sequence");
        }
        modemcf mod = modemcf_create(scheme);
        if (!mod) {
            cler::panic("BERCounterBlock: unsupported modulation scheme");
        }
        _bps = modemcf_get_bps(mod);
        const unsigned int M = 1u << _bps;
        std::vector<uint8_t> perm(M);
        std::vector<bool> seen(M);
        for (unsigned int k = 0; k < M; ++k) {
            const std::complex<float> rot = std::polar(1.0f, 2.0f * static_cast<float>(M_PI) * k / M);
            std::fill(seen.begin(), seen.end(), false);
            bool bijection = true;
            for (unsigned int s = 0; s < M; ++s) {
                std::complex<float> x;
                modemcf_modulate(mod, s, &x);
                unsigned int d = 0;
                modemcf_demodulate(mod, x * rot, &d);
                if (seen[d]) { bijection = false; break; }
                seen[d] = true;
                perm[s] = static_cast<uint8_t>(d);
            }
            if (bijection) _perms.push_back(perm);
        }
        modemcf_destroy(mod);
        _window.reserve(_search_symbols);
        _scratch.resize(4096);
    }

    // Thread-safe readouts for the GUI thread.
    bool aligned() const { return _aligned.load(std::memory_order_relaxed); }
    uint64_t bits() const { return _bits.load(std::memory_order_relaxed); }
    uint64_t bit_errors() const { return _errors.load(std::memory_order_relaxed); }
    double ber() const {
        const uint64_t b = bits();
        return b ? static_cast<double>(bit_errors()) / static_cast<double>(b) : 0.0;
    }

    // Callable from the GUI thread; takes effect at the top of the next procedure().
    void reset() { _reset_request.store(true, std::memory_order_relaxed); }

    cler::Result<cler::Empty, cler::Error> procedure() {
        if (_reset_request.exchange(false, std::memory_order_relaxed)) {
            _window.clear();
            _skipped = 0;
            _hold_count = _hold_errors = 0;
            _aligned.store(false, std::memory_order_relaxed);
            _bits.store(0, std::memory_order_relaxed);
            _errors.store(0, std::memory_order_relaxed);
        }

        const size_t n = std::min(in.size(), _scratch.size());
        if (n == 0) {
            return cler::Error::NotEnoughSamples;
        }
        in.readN(_scratch.data(), n);

        for (size_t i = 0; i < n; ++i) {
            const uint8_t rx = _scratch[i];
            if (_skipped < _skip_symbols) {
                ++_skipped;
                continue;
            }
            if (!_aligned.load(std::memory_order_relaxed)) {
                _window.push_back(rx);
                if (_window.size() == _search_symbols) align();
                continue;
            }
            count(rx);
        }
        return cler::Empty{};
    }

private:
    static constexpr size_t HOLD_SYMBOLS = 1024;

    void count(uint8_t rx) {
        const uint8_t expect = _perms[_perm_index][_ref[_pos]];
        const unsigned int diff = static_cast<unsigned int>(rx ^ expect) & ((1u << _bps) - 1u);
        _errors.fetch_add(static_cast<uint64_t>(__builtin_popcount(diff)), std::memory_order_relaxed);
        _bits.fetch_add(_bps, std::memory_order_relaxed);
        if (++_pos == _ref.size()) _pos = 0;

        if (rx != expect) ++_hold_errors;
        if (++_hold_count == HOLD_SYMBOLS) {
            // Acquisition needs a symbol error rate below 1/5; holding tolerates
            // up to 2/5, so a marginal link does not chatter in and out.
            const bool lost = _hold_errors * 5 > HOLD_SYMBOLS * 2;
            _hold_count = _hold_errors = 0;
            if (lost) {
                _aligned.store(false, std::memory_order_relaxed);
                _window.clear();
                _bits.store(0, std::memory_order_relaxed);
                _errors.store(0, std::memory_order_relaxed);
            }
        }
    }

    void align() {
        const size_t P = _ref.size();
        const size_t W = _window.size();
        size_t best_errors = W + 1, best_delay = 0, best_perm = 0;
        for (size_t p = 0; p < _perms.size(); ++p) {
            const uint8_t* perm = _perms[p].data();
            for (size_t d = 0; d < P; ++d) {
                size_t errors = 0;
                for (size_t i = 0; i < W && errors < best_errors; ++i) {
                    if (_window[i] != perm[_ref[(d + i) % P]]) ++errors;
                }
                if (errors < best_errors) {
                    best_errors = errors;
                    best_delay = d;
                    best_perm = p;
                }
            }
        }
        // A wrong hypothesis leaves ~(1 - 1/M) of the window in error; only a
        // clearly better match is an alignment.
        if (best_errors * 5 < W) {
            _perm_index = best_perm;
            _pos = (best_delay + W) % P;
            _hold_count = _hold_errors = 0;
            _aligned.store(true, std::memory_order_relaxed);
        }
        _window.clear();
    }

    std::vector<uint8_t> _ref;
    std::vector<std::vector<uint8_t>> _perms;
    std::vector<uint8_t> _window;
    std::vector<uint8_t> _scratch;
    unsigned int _bps = 1;
    size_t _skip_symbols;
    size_t _search_symbols;
    size_t _skipped = 0;
    size_t _pos = 0;
    size_t _perm_index = 0;
    size_t _hold_count = 0;
    size_t _hold_errors = 0;

    std::atomic<bool> _aligned{false};
    std::atomic<bool> _reset_request{false};
    std::atomic<uint64_t> _bits{0};
    std::atomic<uint64_t> _errors{0};
};
`,xn=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "liquid.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

// Linear demodulator: complex baseband in, hard symbol decisions out on the
// first output and the recovered (carrier- and timing-corrected, unit-energy)
// constellation points on the second, one point per symbol.
//
// Chain: AGC -> RRC matched filter with symbol timing recovery (symsync) ->
// symbol-rate power normalisation -> decision-directed carrier recovery (the
// modem's phase error drives an NCO PLL).
//
// The power normalisation is what makes EVM meaningful: liquid's constellations
// have unit average symbol energy, so scaling the recovered symbols to unit mean
// power puts them on the same scale as the ideal points.
struct LinearDemodulatorBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    LinearDemodulatorBlock(const char* name,
                     modulation_scheme scheme,
                     unsigned int sps,
                     float beta,
                     unsigned int filter_delay_symbols = 5,
                     float pll_bandwidth = 0.002f,
                     float lock_evm = 0.5f,
                     size_t buffer_size = 8192)
        : cler::BlockBase(name), in(buffer_size), _lock_evm(lock_evm) {
        if (sps < 2) {
            cler::panic("LinearDemodulatorBlock requires samples/symbol >= 2");
        }
        _mod = modemcf_create(scheme);
        if (!_mod) {
            cler::panic("LinearDemodulatorBlock: unsupported modulation scheme");
        }
        _agc = agc_crcf_create();
        agc_crcf_set_bandwidth(_agc, 1e-3f);
        _sync = symsync_crcf_create_rnyquist(LIQUID_FIRFILT_RRC, sps, filter_delay_symbols, beta, 32);
        symsync_crcf_set_output_rate(_sync, 1);
        symsync_crcf_set_lf_bw(_sync, 0.02f);
        _nco = nco_crcf_create(LIQUID_VCO);
        nco_crcf_pll_set_bandwidth(_nco, pll_bandwidth);

        _scratch = 4096;
        _in_buf.resize(_scratch);
        _agc_buf.resize(_scratch);
        _sym_buf.resize(_scratch);
        _out_syms.resize(_scratch);
        _out_pts.resize(_scratch);
        _last_rate_time = std::chrono::steady_clock::now();
    }

    ~LinearDemodulatorBlock() {
        nco_crcf_destroy(_nco);
        symsync_crcf_destroy(_sync);
        agc_crcf_destroy(_agc);
        modemcf_destroy(_mod);
    }

    unsigned int bits_per_symbol() const { return modemcf_get_bps(_mod); }

    // Thread-safe: readable from the GUI thread while procedure() runs.
    float evm_percent() const { return 100.0f * std::sqrt(_err_acc.load(std::memory_order_relaxed)); }
    float snr_db() const {
        const float e = _err_acc.load(std::memory_order_relaxed);
        return e > 0.0f ? -10.0f * std::log10(e) : 99.0f;
    }
    bool locked() const { return _locked.load(std::memory_order_relaxed); }
    float symbol_rate() const { return _sym_rate.load(std::memory_order_relaxed); }
    float carrier_offset() const { return _freq.load(std::memory_order_relaxed); }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<uint8_t>* out_symbols,
                                                     cler::ChannelBase<std::complex<float>>* out_constellation) {
        // symsync consumes every sample handed to it, so the input is bounded by
        // the smaller output space: it can never emit more symbols than samples in.
        const size_t n = std::min({in.size(), out_symbols->space(), out_constellation->space(), _scratch});
        if (n == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }

        in.readN(_in_buf.data(), n);
        agc_crcf_execute_block(_agc, _in_buf.data(), static_cast<unsigned int>(n), _agc_buf.data());

        unsigned int ny = 0;
        symsync_crcf_execute(_sync, _agc_buf.data(), static_cast<unsigned int>(n), _sym_buf.data(), &ny);

        constexpr float alpha = 0.01f;
        for (unsigned int i = 0; i < ny; ++i) {
            std::complex<float> v;
            nco_crcf_mix_down(_nco, _sym_buf[i], &v);

            _pwr = (1.0f - alpha) * _pwr + alpha * std::norm(v);
            if (_pwr < 1e-12f) _pwr = 1e-12f;
            v /= std::sqrt(_pwr);

            unsigned int sym = 0;
            modemcf_demodulate(_mod, v, &sym);
            std::complex<float> ideal;
            modemcf_get_demodulator_sample(_mod, &ideal);
            _err = (1.0f - alpha) * _err + alpha * std::norm(v - ideal);

            nco_crcf_pll_step(_nco, modemcf_get_demodulator_phase_error(_mod));
            nco_crcf_step(_nco);

            _out_syms[i] = static_cast<uint8_t>(sym);
            _out_pts[i] = v;
        }

        if (ny > 0) {
            out_symbols->writeN(_out_syms.data(), ny);
            out_constellation->writeN(_out_pts.data(), ny);
            _err_acc.store(_err, std::memory_order_relaxed);
            _locked.store(std::sqrt(_err) < _lock_evm, std::memory_order_relaxed);
            _freq.store(nco_crcf_get_frequency(_nco), std::memory_order_relaxed);
            update_rate(ny);
        }
        return cler::Empty{};
    }

private:
    void update_rate(unsigned int ny) {
        _sym_count += ny;
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - _last_rate_time).count();
        if (dt >= 0.5f) {
            _sym_rate.store(_sym_count / dt, std::memory_order_relaxed);
            _sym_count = 0;
            _last_rate_time = now;
        }
    }

    modemcf _mod = nullptr;
    agc_crcf _agc = nullptr;
    symsync_crcf _sync = nullptr;
    nco_crcf _nco = nullptr;

    size_t _scratch = 0;
    std::vector<std::complex<float>> _in_buf, _agc_buf, _sym_buf, _out_pts;
    std::vector<uint8_t> _out_syms;

    float _pwr = 1.0f;
    float _err = 1.0f;
    float _lock_evm;
    size_t _sym_count = 0;
    std::chrono::steady_clock::time_point _last_rate_time;

    std::atomic<float> _err_acc{1.0f};
    std::atomic<float> _sym_rate{0.0f};
    std::atomic<float> _freq{0.0f};
    std::atomic<bool> _locked{false};
};
`,zn=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "liquid.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

// Linear modulator: symbol indices 0..M-1 in, RRC-shaped complex baseband out at
// \`sps\` samples per symbol. The pulse taps are normalised to unit energy, so the
// mean output sample power is Es/sps with Es = 1 (liquid normalises its
// constellations to unit average symbol energy). That fixes the SNR convention
// the AWGN level and the EVM estimate are quoted against.
inline unsigned int scheme_bits_per_symbol(modulation_scheme scheme) {
    modemcf m = modemcf_create(scheme);
    if (!m) cler::panic("unsupported modulation scheme");
    const unsigned int bps = modemcf_get_bps(m);
    modemcf_destroy(m);
    return bps;
}

// Per-component stddev of complex AWGN for a target Es/N0, given the unit-energy
// pulse normalisation above: the complex noise variance is 10^(-esn0/10), which
// after the unit-energy matched filter is exactly N0 against Es = 1.
inline float awgn_stddev_for_esn0_db(float esn0_db) {
    return std::sqrt(0.5f * std::pow(10.0f, -esn0_db / 10.0f));
}

struct LinearModulatorBlock : public cler::BlockBase {
    cler::Channel<uint8_t> in;

    LinearModulatorBlock(const char* name,
                   modulation_scheme scheme,
                   unsigned int sps,
                   float beta,
                   unsigned int filter_delay_symbols = 5,
                   size_t buffer_size = 4096)
        : cler::BlockBase(name), in(buffer_size), _sps(sps) {
        if (sps < 2) {
            cler::panic("LinearModulatorBlock requires samples/symbol >= 2");
        }
        _mod = modemcf_create(scheme);
        if (!_mod) {
            cler::panic("LinearModulatorBlock: unsupported modulation scheme");
        }

        const unsigned int h_len = 2 * sps * filter_delay_symbols + 1;
        std::vector<float> h(h_len);
        liquid_firdes_prototype(LIQUID_FIRFILT_RRC, sps, filter_delay_symbols, beta, 0.0f, h.data());
        float energy = 0.0f;
        for (float t : h) energy += t * t;
        const float g = 1.0f / std::sqrt(energy);
        for (float& t : h) t *= g;
        _interp = firinterp_crcf_create(sps, h.data(), h_len);

        _sym_scratch.resize(1024);
        _samp_scratch.resize(_sym_scratch.size() * sps);
    }

    ~LinearModulatorBlock() {
        firinterp_crcf_destroy(_interp);
        modemcf_destroy(_mod);
    }

    unsigned int bits_per_symbol() const { return modemcf_get_bps(_mod); }
    unsigned int samples_per_symbol() const { return _sps; }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        const size_t n = std::min({in.size(), out->space() / _sps, _sym_scratch.size()});
        if (n == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }

        in.readN(_sym_scratch.data(), n);
        for (size_t i = 0; i < n; ++i) {
            std::complex<float> sym;
            modemcf_modulate(_mod, _sym_scratch[i], &sym);
            firinterp_crcf_execute(_interp, sym, _samp_scratch.data() + i * _sps);
        }
        out->writeN(_samp_scratch.data(), n * _sps);
        return cler::Empty{};
    }

private:
    unsigned int _sps;
    modemcf _mod = nullptr;
    firinterp_crcf _interp = nullptr;
    std::vector<uint8_t> _sym_scratch;
    std::vector<std::complex<float>> _samp_scratch;
};
`,Sn=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <atomic>
#include <complex>
#include <cstdio>
#include <mutex>
#include <vector>

// Scatter plot of the last N recovered constellation points. Same shape as the
// other plot blocks: procedure() rings the points into a Channel, render()
// snapshots it into preallocated x/y arrays under a try_lock and draws; nothing
// is allocated per frame and the point count is bounded by the ring.
struct PlotConstellationBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;

    cler::Channel<std::complex<float>> in;

    PlotConstellationBlock(const char* name, size_t num_points = 2048, size_t buffer_size = 4096)
        : cler::BlockBase(name), in(buffer_size), _capacity(num_points), _ring(num_points) {
        if (num_points < 64) {
            cler::panic("PlotConstellationBlock needs at least 64 points");
        }
        _tmp.resize(std::min<size_t>(4096, num_points));
        _snapshot_x.resize(num_points);
        _snapshot_y.resize(num_points);
    }

    void set_initial_window(float x, float y, float w, float h) {
        _initial_window_position = ImVec2(x, y);
        _initial_window_size = ImVec2(w, h);
    }

    // GUI-THREAD-ONLY: overlay text drawn on the plot.
    void set_metrics(float evm_percent, float snr_db, bool locked) {
        _evm = evm_percent;
        _snr = snr_db;
        _locked = locked;
        _has_metrics = true;
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        const size_t work = std::min(in.size(), _tmp.size());
        if (work == 0) {
            return cler::Error::NotEnoughSamples;
        }
        const size_t drop = (_ring.size() + work > _capacity) ? (_ring.size() + work - _capacity) : 0;
        in.readN(_tmp.data(), work);
        _ring.commit_read(drop);
        _ring.writeN(_tmp.data(), work);
        return cler::Empty{};
    }

    void render() {
        // Skip the snapshot while paused so the display freezes even though
        // procedure() keeps draining input underneath.
        if (!_gui_pause.load(std::memory_order_acquire) && _snapshot_mutex.try_lock()) {
            const std::complex<float>* ptr1; const std::complex<float>* ptr2;
            size_t size1, size2;
            const size_t available = _ring.peek_read(ptr1, size1, ptr2, size2);
            if (available > 0) {
                for (size_t i = 0; i < size1; ++i) {
                    _snapshot_x[i] = ptr1[i].real();
                    _snapshot_y[i] = ptr1[i].imag();
                }
                for (size_t i = 0; i < size2; ++i) {
                    _snapshot_x[size1 + i] = ptr2[i].real();
                    _snapshot_y[size1 + i] = ptr2[i].imag();
                }
                _snapshot_ready_size = available;
            }
            _snapshot_mutex.unlock();
        }

        if (_snapshot_ready_size == 0) {
            return;
        }

        ImGui::SetNextWindowSize(_initial_window_size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(_initial_window_position, ImGuiCond_FirstUseEver);
        ImGui::Begin(name());

        if (ImGui::Button(_gui_pause.load() ? "Resume" : "Pause")) {
            _gui_pause.store(!_gui_pause.load(), std::memory_order_release);
        }
        if (_has_metrics) {
            ImGui::SameLine(0, 16);
            ImGui::Text("EVM %.1f %%   SNR %.1f dB   %s", _evm, _snr, _locked ? "LOCK" : "no lock");
        }

        if (ImPlot::BeginPlot(name(), ImVec2(-1, -1), ImPlotFlags_Equal | ImPlotFlags_NoLegend)) {
            ImPlot::SetupAxes("I", "Q");
            ImPlot::SetupAxesLimits(-1.5, 1.5, -1.5, 1.5, ImPlotCond_Always);
            ImPlot::PlotScatter("points", _snapshot_x.data(), _snapshot_y.data(),
                                static_cast<int>(_snapshot_ready_size),
                                ImPlotSpec(ImPlotProp_Marker, ImPlotMarker_Circle,
                                           ImPlotProp_MarkerSize, 1.6f));
            ImPlot::EndPlot();
        }
        ImGui::End();
    }

private:
    size_t _capacity;
    cler::Channel<std::complex<float>> _ring;
    std::vector<std::complex<float>> _tmp;
    std::vector<float> _snapshot_x, _snapshot_y;
    size_t _snapshot_ready_size = 0;
    std::mutex _snapshot_mutex;
    std::atomic<bool> _gui_pause = false;

    ImVec2 _initial_window_position{0.0f, 0.0f};
    ImVec2 _initial_window_size{600.0f, 600.0f};

    float _evm = 0.0f, _snr = 0.0f;
    bool _locked = false, _has_metrics = false;
};
`,En=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "liquid.h"
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

// \`count\` symbols of a maximal-length sequence, bps bits each. The caller keeps
// the vector and hands the same one to SymbolSourceBlock and BERCounterBlock, so
// the reference sequence is shared data rather than a re-derived generator.
inline std::vector<uint8_t> prbs_symbols(unsigned int bps, size_t count) {
    msequence ms = msequence_create_default(10);
    std::vector<uint8_t> syms(count);
    for (size_t i = 0; i < count; ++i) {
        syms[i] = static_cast<uint8_t>(msequence_generate_symbol(ms, bps));
    }
    msequence_destroy(ms);
    return syms;
}

// Cycles a fixed symbol vector forever.
struct SymbolSourceBlock : public cler::BlockBase {
    SymbolSourceBlock(const char* name, std::vector<uint8_t> symbols)
        : cler::BlockBase(name), _symbols(std::move(symbols)) {
        if (_symbols.empty()) {
            cler::panic("SymbolSourceBlock requires a non-empty symbol vector");
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<uint8_t>* out) {
        size_t n = std::min(out->space(), _scratch.size());
        if (n == 0) {
            return cler::Error::NotEnoughSpace;
        }
        for (size_t i = 0; i < n; ++i) {
            _scratch[i] = _symbols[_pos];
            if (++_pos == _symbols.size()) _pos = 0;
        }
        out->writeN(_scratch.data(), n);
        return cler::Empty{};
    }

private:
    std::vector<uint8_t> _symbols;
    std::vector<uint8_t> _scratch = std::vector<uint8_t>(4096);
    size_t _pos = 0;
};
`,Bn=`#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <type_traits>

template <typename T, size_t NumInputs>
struct AddBlock : public cler::BlockBase {
    static_assert(NumInputs >= 2, "AddBlock requires at least two input channels");

    cler::Channel<T>* in = nullptr;

    AddBlock(const char* name, const size_t buffer_size = 0)
        : cler::BlockBase(name) {

        size_t actual_buffer_size = (buffer_size == 0) ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size;

        if (buffer_size > 0 && buffer_size * sizeof(T) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }

        in = reinterpret_cast<cler::Channel<T>*>(_in_storage);
        for (size_t i = 0; i < NumInputs; ++i) {
            new (&in[i]) cler::Channel<T>(actual_buffer_size);
        }
    }
    ~AddBlock() {
        using TChannel = cler::Channel<T>;
        for (size_t i = 0; i < NumInputs; ++i) {
            in[i].~TChannel();
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        auto [write_ptr, write_size] = out->write_dbf();
        if (!write_ptr || write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t min_available = write_size;
        for (size_t i = 0; i < NumInputs; ++i) {
            auto [read_ptr, read_size] = in[i].read_dbf();
            min_available = std::min(min_available, read_size);
        }

        if (min_available == 0) {
            return cler::Error::NotEnoughSamples;
        }

        std::fill_n(write_ptr, min_available, T{});

        for (size_t i = 0; i < NumInputs; ++i) {
            auto [read_ptr, read_size] = in[i].read_dbf();
            for (size_t j = 0; j < min_available; ++j) {
                write_ptr[j] += read_ptr[j];
            }
            in[i].commit_read(min_available);
        }

        out->commit_write(min_available);
        return cler::Empty{};
    }

    private:
        std::aligned_storage_t<sizeof(cler::Channel<T>), alignof(cler::Channel<T>)> _in_storage[NumInputs];
};
`,Rn=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"

struct ComplexToMagPhaseBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    enum Mode {
        MagPhase = 0,
        RealImag = 1
    };

    ComplexToMagPhaseBlock(const char* name, const Mode block_mode, const size_t buffer_size = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size), _block_mode(block_mode)
    {
        if (buffer_size > 0 && buffer_size * sizeof(std::complex<float>) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        if (block_mode != Mode::MagPhase && block_mode != Mode::RealImag) {
            cler::panic("Invalid block mode. Use MagPhase or RealImag.");
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(
        cler::ChannelBase<float>* a_out,
        cler::ChannelBase<float>* b_out)
    {
        auto [read_ptr, read_size] = in.read_dbf();
        auto [a_ptr, a_space] = a_out->write_dbf();
        auto [b_ptr, b_space] = b_out->write_dbf();

        size_t transferable = std::min({read_size, a_space, b_space});
        if (transferable == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }

        for (size_t i = 0; i < transferable; ++i) {
            switch (_block_mode) {
                case Mode::MagPhase:
                    a_ptr[i] = std::abs(read_ptr[i]);
                    b_ptr[i] = std::arg(read_ptr[i]);
                    break;
                case Mode::RealImag:
                    a_ptr[i] = read_ptr[i].real();
                    b_ptr[i] = read_ptr[i].imag();
                    break;
            }
        }

        in.commit_read(transferable);
        a_out->commit_write(transferable);
        b_out->commit_write(transferable);

        return cler::Empty{};
    }

private:
    Mode _block_mode;
};
`,In=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <atomic>
#include <new>

struct FrequencyShiftBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    FrequencyShiftBlock(const char* name, const double frequency_shift_hz, const double sample_rate_hz,
        const size_t buffer_size = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size), _frequency_shift(frequency_shift_hz), _sample_rate(sample_rate_hz) {

        _buffer_size = buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size;
        _buffer = new (std::nothrow) std::complex<float>[_buffer_size];
        if (!_buffer) {
            cler::panic("Failed to allocate temporary buffer");
        }

        _dshift = std::exp(std::complex<float>(0.0, 2.0 * M_PI * _frequency_shift / _sample_rate));
        _pending_shift.store(frequency_shift_hz, std::memory_order_relaxed);
    }

    ~FrequencyShiftBlock() {
        delete[] _buffer;
    }

    // Thread-safe: applied at the top of the next procedure().
    void set_frequency_shift(double frequency_shift_hz) {
        _pending_shift.store(frequency_shift_hz, std::memory_order_relaxed);
        _shift_dirty.store(true, std::memory_order_release);
    }

    // Only while the graph is stopped.
    void set_sample_rate(double sample_rate_hz) {
        _sample_rate = sample_rate_hz;
        _shift_dirty.store(true, std::memory_order_release);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        if (_shift_dirty.exchange(false, std::memory_order_acquire)) {
            _frequency_shift = _pending_shift.load(std::memory_order_relaxed);
            _dshift = std::exp(std::complex<float>(0.0, 2.0 * M_PI * _frequency_shift / _sample_rate));
        }
        // in's buffer_size isn't validated to be >=4KB (custom sizes allowed), so dbf isn't
        // guaranteed available here; readN/writeN into a temp buffer stays correct for any size.
        size_t transferable = std::min({in.size(), out->space(), _buffer_size});
        if (transferable == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }
        
        in.readN(_buffer, transferable);

        for (size_t i = 0; i < transferable; ++i) {
            _buffer[i] = _buffer[i] * _shifter;
            _shifter *= _dshift;
            // renormalize each sample: repeated multiplication drifts the phasor's magnitude from 1 via fp error
            _shifter /= std::abs(_shifter);
        }
        
        out->writeN(_buffer, transferable);
        return cler::Empty{};
    }

    private:
        double _frequency_shift;
        double _sample_rate;
        std::complex<float>* _buffer;
        size_t _buffer_size;
        std::complex<float> _shifter{1.0 ,0.0};
        std::complex<float> _dshift;
        std::atomic<double> _pending_shift{0.0};
        std::atomic<bool> _shift_dirty{false};
};`,An=`#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/kernels/kernels.hpp"
#include <new>

template <typename T>
struct GainBlock : public cler::BlockBase {
    cler::Channel<T> in;

    GainBlock(const char* name, const T gain_value, const size_t buffer_size = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size), _kernel{gain_value} {

        _buffer_size = buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size;
        _buffer = new (std::nothrow) T[_buffer_size];
        if (!_buffer) {
            cler::panic("Failed to allocate temporary buffer");
        }
    }

    ~GainBlock() {
        delete[] _buffer;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        size_t transferable = std::min({in.size(), out->space(), _buffer_size});
        if (transferable == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }

        in.readN(_buffer, transferable);

        for (size_t i = 0; i < transferable; ++i) {
            _buffer[i] = _kernel(_buffer[i]);
        }

        out->writeN(_buffer, transferable);
        return cler::Empty{};
    }

    T processOne(T x) { return _kernel(x); }

    private:
        GainKernel<T> _kernel;
        T* _buffer;
        size_t _buffer_size;
};`,Cn=`#pragma once
#include <string>

struct UHDConfig {
    double center_freq_Hz = 915e6;
    double sample_rate_Hz = 2e6;
    double gain = 40.0;
    double bandwidth_Hz = 4e6;
};

template<typename T>
inline std::string get_uhd_format() {
    if constexpr (std::is_same_v<T, std::complex<float>>) {
        return "fc32";
    } else if constexpr (std::is_same_v<T, std::complex<int16_t>>) {
        return "sc16";
    } else if constexpr (std::is_same_v<T, std::complex<int8_t>>) {
        return "sc8";
    } else {
        static_assert(!std::is_same_v<T, T>, "UHD blocks only support complex types");
    }
}
`,Dn=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/kernels/kernels.hpp"
#include <atomic>
#include <random>
#include <type_traits>
#include <new>

template <typename T>
struct NoiseAWGNBlock : public cler::BlockBase {
    cler::Channel<T> in;

    using scalar_type = typename AWGNKernel<T>::scalar_type;

    NoiseAWGNBlock(const char* name, scalar_type noise_stddev, const size_t buffer_size = 0, uint32_t seed = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size), _kernel(noise_stddev, seed) {

        _buffer_size = buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size;
        _buffer = new (std::nothrow) T[_buffer_size];
        if (!_buffer) {
            cler::panic("Failed to allocate temporary buffer");
        }
    }

    ~NoiseAWGNBlock() {
        delete[] _buffer;
    }

    // Thread-safe: applied at the top of the next procedure().
    void set_noise_stddev(scalar_type stddev) {
        _pending_stddev.store(stddev, std::memory_order_relaxed);
        _stddev_dirty.store(true, std::memory_order_release);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        if (_stddev_dirty.exchange(false, std::memory_order_acquire)) {
            _kernel.set_stddev(_pending_stddev.load(std::memory_order_relaxed));
        }
        size_t transferable = std::min({in.size(), out->space(), _buffer_size});
        if (transferable == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }

        in.readN(_buffer, transferable);

        for (size_t i = 0; i < transferable; ++i) {
            _buffer[i] = _kernel(_buffer[i]);
        }

        out->writeN(_buffer, transferable);

        return cler::Empty{};
    }

    T processOne(T x) { return _kernel(x); }

private:
    AWGNKernel<T> _kernel;
    std::atomic<scalar_type> _pending_stddev{0};
    std::atomic<bool> _stddev_dirty{false};

    T* _buffer;
    size_t _buffer_size;
};
`,Tn=`#pragma once

#include "cler.hpp"
#include "liquid.h"
#include "imgui.h"
#include "implot.h"
#include "spectral_windows.hpp"
#include <mutex>
#include <vector>
#include <type_traits>

struct PlotCSpectrogramBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;
    const size_t BUFFER_SIZE_MULTIPLIER = 3;

    // Input is always fully drained (upstream fanout never stalls); only the
    // most recent MAX_FFTS_PER_CALL frames become rows, bounding CPU/lock time.
    static constexpr size_t MAX_FFTS_PER_CALL = 32;
    static constexpr size_t MAX_INPUT_CHANNEL_SLOTS = 16;

    cler::Channel<std::complex<float>>* in;

    PlotCSpectrogramBlock(const char* name,
                          const std::vector<std::string> signal_labels,
                          size_t sps,
                          size_t n_fft_samples,
                          size_t tall,
                          SpectralWindow window_type = SpectralWindow::BlackmanHarris);

    ~PlotCSpectrogramBlock();

    cler::Result<cler::Empty, cler::Error> procedure();
    void render();
    void set_initial_window(float x, float y, float w, float h);

    // One-shot: next render() applies this rect (ImGuiCond_Always) then clears
    // the request, so the user can still move/resize afterward.
    void apply_window_rect(float x, float y, float w, float h) {
        _pending_rect_pos  = ImVec2(x, y);
        _pending_rect_size = ImVec2(w, h);
        _pending_rect      = true;
    }

    // While inactive, input is still fully drained (no upstream stall) but no
    // FFT/row work happens.
    void set_active(bool active) {
        _external_pause.store(!active, std::memory_order_release);
    }

    void set_visible(bool visible) { _visible = visible; }

    // GUI-THREAD-ONLY. Existing ring rows were recorded at the old rate and
    // would be mislabeled on the new axis, so this clears the ring (under
    // _spectrogram_mutex) and the waterfall restarts empty.
    void set_sample_rate(size_t sps);

    // How many FFT frames are collapsed (peak-held) into one waterfall row.
    // Larger => each row spans more time => longer total history on screen.
    void   set_frames_per_row(size_t n) { _frames_per_row.store(n < 1 ? 1 : n); }
    size_t frames_per_row() const { return _frames_per_row.load(); }

    // Measurement-grid overlay (GUI thread only). Spacing: time between
    // horizontal lines in ms, frequency between vertical lines in MHz.
    void  set_show_grid(bool on)       { _show_grid = on; }
    bool  show_grid() const            { return _show_grid; }
    void  set_grid_time_ms(float ms)   { _grid_time_ms = ms; }
    float grid_time_ms() const         { return _grid_time_ms; }
    void  set_grid_freq_mhz(float mhz) { _grid_freq_mhz = mhz; }
    float grid_freq_mhz() const        { return _grid_freq_mhz; }

    // GUI-THREAD-ONLY. \`data\` gets rows*cols dB floats, row-major, NEWEST row
    // first (unfilled rows padded with DB_FLOOR); one row spans
    // frames_per_row_out * cols / sps_out seconds. False if no row recorded yet
    // or \`channel\` out of range.
    bool export_display(size_t channel, std::vector<float>& data,
                        size_t& rows, size_t& cols,
                        size_t& frames_per_row_out, size_t& sps_out) const;

private:
    std::aligned_storage_t<sizeof(cler::Channel<std::complex<float>>), alignof(cler::Channel<std::complex<float>>)> _in_storage[MAX_INPUT_CHANNEL_SLOTS];

    size_t _num_inputs;
    std::vector<std::string> _signal_labels;

    size_t _sps;
    size_t _n_fft_samples;
    size_t _tall;
    SpectralWindow _window_type;

    std::complex<float>* _liquid_inout;
    std::complex<float>* _tmp_y_buffer;
    float* _tmp_mag_buffer;

    // Row-major ring of waterfall rows: [num_inputs][tall * n_fft_samples].
    // Nothing reorders it on the frame path; export_display() reorders on
    // demand for snapshots.
    float** _spectrograms;
    size_t  _ring_write_pos = 0;   // next row to overwrite, mod _tall
    size_t  _ring_count = 0;       // valid rows so far (saturates at _tall)

    // Bumped once per new ring row, inside the row-write's _spectrogram_mutex
    // section; render() diffs against its last-seen value to skip texture work
    // when no new rows landed.
    size_t  _row_gen = 0;                             // guarded by _spectrogram_mutex
    size_t  _row_gen_seen = static_cast<size_t>(-1);  // GUI thread only

    // GL waterfall texture (GUI thread only, incl. all GL): one RGBA8 texture
    // per input, rows at their ring positions. render() copies new rows out
    // under _spectrogram_mutex, then colorizes/uploads after unlocking (never
    // holds the DSP-facing lock across GL calls). Color scale is baked into
    // the texture with a dB margin and hysteresis (see .cpp) rather than
    // rescaled every frame; per-ring-row min/max make that decision O(tall).
    static constexpr float DB_FLOOR = -147.0f;  // empty-row fill value (dB)
    unsigned int* _tex = nullptr;  // [num_inputs] GL texture names (0 until created)
    bool          _lut_built = false;
    ImU32         _lut[256];       // Plasma colormap LUT
    float**       _stage;          // [num_inputs][tall * n_fft] rows copied out under the mutex
    ImU32*        _pixels;         // [tall * n_fft] RGBA8 upload staging (shared across inputs)
    float**       _row_min;        // [num_inputs][tall] per-ring-row dB min (DB_FLOOR if unwritten)
    float**       _row_max;        // [num_inputs][tall] per-ring-row dB max
    float*        _scale_min;      // [num_inputs] dB->color scale baked into the texture
    float*        _scale_max;      //   (min == max means "flat": solid colormap color 0)
    bool*         _needs_full;     // [num_inputs] per-frame scratch: full recolor this frame?
    bool          _tex_full_dirty = true;  // force full recolor+upload on next render()
    size_t        _tex_ring_pos   = 0;     // ring snapshot the texture contents reflect;
    size_t        _tex_ring_count = 0;     //   also used to place the two seam quads

    // Peak-hold accumulator: FFT frames are max-combined here, flushed to one
    // ring row every _frames_per_row frames.
    float** _accum;              // [num_inputs][n_fft_samples]
    size_t  _accum_count = 0;    // frames folded into the current row so far
    std::atomic<size_t> _frames_per_row{1};

    std::atomic<bool> _external_pause{false};

    float* _freq_bins;

    fftplan _fftplan;

    // mutable: export_display() is const but must lock against the DSP thread.
    mutable std::mutex _spectrogram_mutex;

    ImVec2 _initial_window_position = ImVec2(200, 200);
    ImVec2 _initial_window_size = ImVec2(600, 400);

    // One-shot rect request (GUI thread only; see apply_window_rect()).
    bool   _pending_rect = false;
    ImVec2 _pending_rect_pos  = ImVec2(0, 0);
    ImVec2 _pending_rect_size = ImVec2(0, 0);

    // One-shot X-axis re-fit after set_sample_rate() (GUI thread only).
    bool   _axis_refit = false;

    // Paused-view zoom (GUI thread only): image keeps the row_dt captured at
    // pause time while the Y axis tracks the live History value, so dragging
    // History zooms the viewport around the frozen data instead of scaling both.
    bool   _was_paused    = false;
    double _paused_row_dt = 0.0;

    // Measurement-grid overlay state (GUI thread only; see the setters above).
    bool   _show_grid     = false;   // "Grid" checkbox in this window
    float  _grid_time_ms  = 100.0f;  // horizontal (time) line spacing, ms
    float  _grid_freq_mhz = 1.0f;    // vertical (frequency) line spacing, MHz

    std::atomic<bool> _gui_pause = false;

    bool _visible = true;
};
`,Pn=`#pragma once

#include "cler.hpp"
#include "liquid.h"
#include "spectral_windows.hpp"
#include "imgui.h"
#include <vector>
#include <mutex>
#include <vector>
#include <type_traits>

struct PlotCSpectrumBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;
    const size_t BUFFER_SIZE_MULTIPLIER = 3;
    static constexpr size_t MAX_INPUT_CHANNEL_SLOTS = 16;

    cler::Channel<std::complex<float>>* in;

    PlotCSpectrumBlock(const char* name,
                       const std::vector<std::string>& signal_labels,
                       size_t sps,
                       size_t n_fft_samples,
                       SpectralWindow window_type = SpectralWindow::BlackmanHarris);

    ~PlotCSpectrumBlock();

    cler::Result<cler::Empty, cler::Error> procedure();
    void render();
    void set_initial_window(float x, float y, float w, float h);

    // One-shot: next render() applies this rect (ImGuiCond_Always) then clears
    // the request, so the user can still move/resize afterward.
    void apply_window_rect(float x, float y, float w, float h);

    // While INACTIVE, procedure() still drains input each call (no upstream
    // stall) but DROPS the samples instead of ringing them. Distinct from the
    // Pause button (_gui_pause), which freezes the display while data still
    // flows through.
    void set_active(bool active) {
        _external_pause.store(!active, std::memory_order_release);
    }

    void set_visible(bool visible) { _visible = visible; }

    // GUI-THREAD-ONLY: retunes the frequency axis and requests a one-shot
    // X-axis re-fit on the next render(). procedure() never reads _sps.
    void set_sample_rate(size_t sps);

    // GUI-THREAD-ONLY export of the currently displayed (averaged) spectrum:
    // freq_hz is baseband Hz (size n_fft), mag_db the averaged magnitudes.
    // False if render() hasn't produced a spectrum yet or channel is invalid.
    bool export_spectrum(size_t channel, std::vector<float>& freq_hz,
                         std::vector<float>& mag_db) const;

    // GUI-THREAD-ONLY: a double-click on the plot leaves its baseband
    // frequency here until taken (tune-by-click for receiver panels).
    bool take_click(double& baseband_hz) {
        if (!_click_pending) return false;
        _click_pending = false;
        baseband_hz = _click_hz;
        return true;
    }

private:
    void next_window_geometry();   // SetNextWindowPos/Size before Begin()

    size_t _samples_counter = 0;

    size_t _num_inputs;
    std::vector<std::string> _signal_labels;
    size_t _sps;
    size_t _n_fft_samples;
    size_t _buffer_size;
    SpectralWindow _window_type;

    std::aligned_storage_t<sizeof(cler::Channel<std::complex<float>>), alignof(cler::Channel<std::complex<float>>)> _in_storage[MAX_INPUT_CHANNEL_SLOTS];

    std::complex<float>** _snapshot_buffers = nullptr;
    std::complex<float>* _tmp_buffer = nullptr;

    std::complex<float>* _liquid_inout = nullptr;
    float* _freq_bins = nullptr;
    float* _tmp_mag_buffer = nullptr;
    float** _spectrum_avg = nullptr;  // Averaged spectrum for each input
    float _avg_alpha = 0.7f;          // Exponential averaging factor (0=frozen, 1=no averaging)
    bool _first_spectrum = true;

    fftplan _fftplan;

    ImVec2 _initial_window_position {0.0f, 0.0f};
    ImVec2 _initial_window_size {600.0f, 300.0f};

    // One-shot rect request (GUI thread only; see apply_window_rect()).
    bool   _pending_rect = false;
    ImVec2 _pending_rect_pos {0.0f, 0.0f};
    ImVec2 _pending_rect_size {0.0f, 0.0f};

    // One-shot X-axis re-fit after set_sample_rate() (GUI thread only).
    bool   _axis_refit = false;
    bool   _click_pending = false;
    double _click_hz = 0.0;

    std::mutex _snapshot_mutex;
    size_t _snapshot_ready_size = 0;

    std::atomic<bool> _gui_pause = false;

    std::atomic<bool> _external_pause{false};

    bool _visible = true;
};
`,Mn=`#pragma once

#include "cler.hpp"
#include <vector>
#include <mutex>
#include <type_traits>
#include "imgui.h"

struct PlotTimeSeriesBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;
    static constexpr size_t MAX_INPUT_CHANNEL_SLOTS = 16;

    cler::Channel<float>* in;

    PlotTimeSeriesBlock(const char* name,
        const std::vector<std::string> signal_labels,
        const size_t sps,
        const float duration_s);
    ~PlotTimeSeriesBlock();

    cler::Result<cler::Empty, cler::Error> procedure();
    void render();
    void set_initial_window(float x, float y, float w, float h);

private:
    size_t _samples_counter = 0;

    size_t _num_inputs;
    std::vector<std::string> _signal_labels;
    size_t _sps;
    size_t _buffer_size;

    cler::Channel<float>* _y_channels;   // ring buffers for each signal
    cler::Channel<float>* _x_channel;    // ring buffer for timestamps

    std::aligned_storage_t<sizeof(cler::Channel<float>), alignof(cler::Channel<float>)> _in_storage[MAX_INPUT_CHANNEL_SLOTS];

    float* _snapshot_x_buffer = nullptr; // holds last good snapshot
    float** _snapshot_y_buffers = nullptr;

    float* _tmp_y_buffer = nullptr;
    float* _tmp_x_buffer = nullptr;

    size_t _snapshot_ready_size = 0;  // size of last good snapshot
    std::mutex _snapshot_mutex;       // protect snapshot

    std::atomic<bool> _gui_pause = false;

    ImVec2 _initial_window_position {0.0f, 0.0f};
    ImVec2 _initial_window_size {600.0f, 300.0f};
};
`,Fn=`#pragma once
#include <cmath>

enum class SpectralWindow {
    BlackmanHarris,
    Hamming,
    Hann,
    Rectangular,
    Kaiser,
    FlatTop,
};

// Simple constexpr approximation of I₀(x)
inline constexpr float bessel_i0(float x) {
    // Abramowitz & Stegun approximation
    float sum = 1.0f;
    float y = x * x / 4.0f;
    float t = y;
    int k = 1;
    while (t > 1e-8f) {
        sum += t;
        ++k;
        t *= y / (k * k);
    }
    return sum;
}

// Kaiser window needs beta parameter
inline constexpr float kaiser_window(float x, float beta) {
    float t = 2.0f * x - 1.0f; // scale x to [-1, 1]
    return bessel_i0(beta * std::sqrt(1.0f - t * t)) / bessel_i0(beta);
}

// Flat Top window: flat in freq, poor res
inline constexpr float flattop_window(float x) {
    return 1.0f
        - 1.93f * std::cos(2 * cler::PI * x)
        + 1.29f * std::cos(4 * cler::PI * x)
        - 0.388f * std::cos(6 * cler::PI * x)
        + 0.0322f * std::cos(8 * cler::PI * x);
}

inline constexpr float spectral_window_function(SpectralWindow type, float x, float beta = 8.6f) {
    switch (type) {
        case SpectralWindow::BlackmanHarris:
            return 0.35875f - 0.48829f * std::cos(2 * cler::PI * x)
                           + 0.14128f * std::cos(4 * cler::PI * x)
                           - 0.01168f * std::cos(6 * cler::PI * x);
        case SpectralWindow::Hamming:
            return 0.54f - 0.46f * std::cos(2 * cler::PI * x);
        case SpectralWindow::Hann:
            return 0.5f * (1.0f - std::cos(2 * cler::PI * x));
        case SpectralWindow::Rectangular:
            return 1.0f;
        case SpectralWindow::Kaiser:
            return kaiser_window(x, beta);
        case SpectralWindow::FlatTop:
            return flattop_window(x);
    }
    return 0.0f;
}
`,qn=`#pragma once

#include "liquid.h"
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <type_traits>
#include <new>

template <typename T>
struct MultiStageResamplerBlock : public cler::BlockBase {
    cler::Channel<T> in;

    MultiStageResamplerBlock(const char* name, const float ratio, const float attenuation,
        const size_t buffer_size = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size),
          _ratio(ratio), _attenuation(attenuation)
    {
        if (buffer_size > 0 && buffer_size * sizeof(T) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        if (ratio <= 0.0f) {
            cler::panic("Ratio must be greater than zero.");
        }
        if (attenuation < 0.0f) {
            cler::panic("Attenuation must be non-negative.");
        }

        static_assert(std::is_same_v<T, float> || std::is_same_v<T, std::complex<float>>,
                      "MultiStageResamplerBlock only supports float or std::complex<float>");

        _buffer_size = buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size;
        _input_buffer = new (std::nothrow) T[_buffer_size];
        if (!_input_buffer) {
            cler::panic("Failed to allocate input buffer");
        }
        create(ratio);
    }

    // Only while the graph is stopped: the input channel keeps its samples, the
    // filter state restarts from zero.
    void set_ratio(const float ratio) {
        if (ratio <= 0.0f) {
            cler::panic("Ratio must be greater than zero.");
        }
        destroy();
        create(ratio);
    }

    float ratio() const { return _ratio; }

    ~MultiStageResamplerBlock() {
        delete[] _input_buffer;
        destroy();
    }

    // liquid-dsp's msresamp needs contiguous in+out arrays and the output count is
    // data-dependent (not exactly input*ratio), so it can't write straight into the
    // channel's dbf window without risking an overrun; readN/writeN + temp buffers stays.
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out)
    {
        size_t available_input = in.size();
        size_t available_output = out->space();

        if (available_input == 0) {
            return cler::Error::NotEnoughSamples;
        }

        if (available_output == 0) {
            return cler::Error::NotEnoughSpace;
        }

        // Cap input by output space / ratio: downsampling (ratio<1) needs more input
        // than output space, upsampling the reverse.
        size_t max_input_by_output = static_cast<size_t>(available_output / _ratio);
        size_t max_input = std::min({available_input, max_input_by_output, _buffer_size});

        if (max_input == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }

        in.readN(_input_buffer, max_input);

        unsigned int n_resampled = 0;

        if constexpr (std::is_same_v<T, float>) {
            msresamp_rrrf_execute(
                _msresamp_r,
                _input_buffer,
                max_input,
                _output_buffer,
                &n_resampled
            );
        } else if constexpr (std::is_same_v<T, std::complex<float>>) {
            msresamp_crcf_execute(
                _msresamp_c,
                reinterpret_cast<liquid_float_complex*>(_input_buffer),
                max_input,
                reinterpret_cast<liquid_float_complex*>(_output_buffer),
                &n_resampled
            );
        }

        out->writeN(_output_buffer, n_resampled);

        return cler::Empty{};
    }

private:
    void create(const float ratio) {
        _ratio = ratio;
        if constexpr (std::is_same_v<T, float>) {
            _msresamp_r = msresamp_rrrf_create(ratio, _attenuation);
            if (!_msresamp_r) {
                cler::panic("Failed to create multi-stage resampler for float");
            }
        } else {
            _msresamp_c = msresamp_crcf_create(ratio, _attenuation);
            if (!_msresamp_c) {
                cler::panic("Failed to create multi-stage resampler for complex float");
            }
        }
        // msresamp can emit slightly more than buffer_size * ratio samples per call
        // (interpolator/decimator state carried across calls); +100 is a safety margin.
        _output_buffer_size = static_cast<size_t>(_buffer_size * _ratio + 100);
        _output_buffer = new (std::nothrow) T[_output_buffer_size];
        if (!_output_buffer) {
            cler::panic("Failed to allocate output buffer");
        }
    }

    void destroy() {
        delete[] _output_buffer;
        _output_buffer = nullptr;
        if constexpr (std::is_same_v<T, float>) {
            if (_msresamp_r) msresamp_rrrf_destroy(_msresamp_r);
            _msresamp_r = nullptr;
        } else {
            if (_msresamp_c) msresamp_crcf_destroy(_msresamp_c);
            _msresamp_c = nullptr;
        }
    }

    float _ratio;
    float _attenuation;
    size_t _buffer_size;
    size_t _output_buffer_size;

    T* _input_buffer = nullptr;
    T* _output_buffer = nullptr;

    msresamp_rrrf _msresamp_r = nullptr;
    msresamp_crcf _msresamp_c = nullptr;
};
`,On=`#pragma once

#include "liquid.h"
#include "cler.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstring>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

template <size_t INTERP, size_t DECIM, size_t TAPS_PER_PHASE>
class RationalResampler {
    static_assert(INTERP > 0, "Interpolation factor must be positive");
    static_assert(DECIM > 0, "Decimation factor must be positive");
    static_assert(TAPS_PER_PHASE > 1, "Need at least two taps per phase");

public:
    static constexpr size_t interp = INTERP;
    static constexpr size_t decim = DECIM;
    static constexpr size_t taps_per_phase = TAPS_PER_PHASE;
    static constexpr size_t history = TAPS_PER_PHASE - 1;

    explicit RationalResampler(float stopband_attenuation_db)
    {
        design_phase_taps(stopband_attenuation_db);
    }

    static constexpr size_t max_outputs(size_t num_input)
    {
        return (num_input * INTERP) / DECIM + 1;
    }

    size_t outputs_for(size_t num_input) const
    {
        size_t phase = _phase;
        size_t needed = _input_until_next_output;
        size_t count = 0;
        while (needed <= num_input) {
            ++count;
            const size_t step = (phase + DECIM) / INTERP;
            phase = (phase + DECIM) % INTERP;
            needed += step;
        }
        return count;
    }

    size_t process(const std::complex<float>* in, size_t num_input, std::complex<float>* out)
    {
        constexpr size_t sample_bytes = sizeof(std::complex<float>);
        const size_t from_carry = std::min(history, num_input);
        std::memcpy(_carry.data() + history, in, from_carry * sample_bytes);

        size_t produced = 0;
        size_t cursor = _input_until_next_output;

        while (cursor <= num_input) {
            const size_t newest = cursor - 1;
            const std::complex<float>* window =
                newest < history ? _carry.data() + newest
                                 : in + (newest - history);
            out[produced++] = filter(window, _phase);

            const size_t step = (_phase + DECIM) / INTERP;
            _phase = (_phase + DECIM) % INTERP;
            cursor += step;
        }
        _input_until_next_output = cursor - num_input;

        if (num_input >= history) {
            std::memcpy(_carry.data(), in + (num_input - history), history * sample_bytes);
        } else {
            std::memmove(_carry.data(), _carry.data() + num_input, (history - num_input) * sample_bytes);
            std::memcpy(_carry.data() + (history - num_input), in, num_input * sample_bytes);
        }
        return produced;
    }

private:
    static constexpr size_t prototype_len = INTERP * TAPS_PER_PHASE;

    void design_phase_taps(float stopband_attenuation_db)
    {
        std::array<float, prototype_len> prototype{};
        const float cutoff = 0.5f / static_cast<float>(std::max(INTERP, DECIM));
        liquid_firdes_kaiser(static_cast<unsigned int>(prototype_len), cutoff,
                             std::fabs(stopband_attenuation_db), 0.0f, prototype.data());

        float dc = 0.0f;
        for (float t : prototype) dc += t;
        const float gain = static_cast<float>(INTERP) / dc;

        for (size_t phase = 0; phase < INTERP; ++phase) {
            for (size_t j = 0; j < TAPS_PER_PHASE; ++j) {
                const size_t tap = (TAPS_PER_PHASE - 1 - j) * INTERP + phase;
                _taps[phase * TAPS_PER_PHASE + j] = gain * prototype[tap];
            }
        }
    }

#if defined(__ARM_NEON)

    inline std::complex<float> filter(const std::complex<float>* window, size_t phase) const
    {
        const float* tap = _taps.data() + phase * TAPS_PER_PHASE;
        const float* w = reinterpret_cast<const float*>(window);
        constexpr size_t vec = TAPS_PER_PHASE / 4 * 4;

        float32x4_t acc_r4 = vdupq_n_f32(0.0f);
        float32x4_t acc_i4 = vdupq_n_f32(0.0f);
        for (size_t j = 0; j < vec; j += 4) {
            float32x4x2_t s = vld2q_f32(w + 2 * j);
            float32x4_t t4 = vld1q_f32(tap + j);
            acc_r4 = vmlaq_f32(acc_r4, s.val[0], t4);
            acc_i4 = vmlaq_f32(acc_i4, s.val[1], t4);
        }

        float32x2_t r2 = vadd_f32(vget_low_f32(acc_r4), vget_high_f32(acc_r4));
        float32x2_t i2 = vadd_f32(vget_low_f32(acc_i4), vget_high_f32(acc_i4));
        float acc_r = vget_lane_f32(vpadd_f32(r2, r2), 0);
        float acc_i = vget_lane_f32(vpadd_f32(i2, i2), 0);

        for (size_t j = vec; j < TAPS_PER_PHASE; ++j) {
            acc_r += window[j].real() * tap[j];
            acc_i += window[j].imag() * tap[j];
        }
        return std::complex<float>(acc_r, acc_i);
    }

#else

    inline std::complex<float> filter(const std::complex<float>* window, size_t phase) const
    {
        const float* tap = _taps.data() + phase * TAPS_PER_PHASE;
        float acc_r = 0.0f;
        float acc_i = 0.0f;
        for (size_t j = 0; j < TAPS_PER_PHASE; ++j) {
            acc_r += window[j].real() * tap[j];
            acc_i += window[j].imag() * tap[j];
        }
        return std::complex<float>(acc_r, acc_i);
    }

#endif

    std::array<float, INTERP * TAPS_PER_PHASE> _taps{};
    std::array<std::complex<float>, 2 * history> _carry{};
    size_t _phase = 0;
    size_t _input_until_next_output = 1;
};

template <size_t INTERP, size_t DECIM, size_t TAPS_PER_PHASE>
struct RationalResamplerBlock : public cler::BlockBase {
    using Sample = std::complex<float>;
    cler::Channel<Sample> in;

    RationalResamplerBlock(const char* name, const float attenuation,
        const size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(Sample) : buffer_size),
          _resampler(attenuation)
    {
        if (buffer_size > 0 && buffer_size * sizeof(Sample) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        if (attenuation < 0.0f) {
            cler::panic("Attenuation must be non-negative.");
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<Sample>* out)
    {
        auto [read_ptr, read_size]   = in.read_dbf();
        auto [write_ptr, write_size] = out->write_dbf();

        if (read_size == 0)  return cler::Error::NotEnoughSamples;
        if (write_size < 2)  return cler::Error::NotEnoughSpace;

        const size_t inputs_that_fit = ((write_size - 1) * DECIM) / INTERP;
        const size_t num_input = std::min(read_size, inputs_that_fit);
        if (num_input == 0) return cler::Error::NotEnoughSpaceOrSamples;

        const size_t produced = _resampler.process(read_ptr, num_input, write_ptr);
        in.commit_read(num_input);
        out->commit_write(produced);
        return cler::Empty{};
    }

private:
    RationalResampler<INTERP, DECIM, TAPS_PER_PHASE> _resampler;
};
`,Nn=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/sigmf/sigmf.hpp"

#include <atomic>
#include <complex>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

// Record-on-demand SigMF sink: always drains its input, writes ci16_le +
// meta only between start() and stop() (GUI-thread calls). procedure() never
// allocates; the conversion buffer is preallocated and files are opened in
// start().
struct SigMFRecorderBlock : public cler::BlockBase {
    static constexpr bool may_block = true;
    cler::Channel<std::complex<float>> in;

    SigMFRecorderBlock(const char* name, double sample_rate, size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size),
          _rate(sample_rate)
    {
        if (buffer_size > 0 && buffer_size * sizeof(std::complex<float>) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        _conv.resize(2 * (1 << 16));
    }

    ~SigMFRecorderBlock() { stop(); }

    cler::Result<cler::Empty, cler::Error> procedure() {
        auto [rptr, rsize] = in.read_dbf();
        if (rsize == 0) return cler::Error::NotEnoughSamples;
        if (_recording.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_fp) {
                size_t done = 0;
                while (done < rsize) {
                    const size_t n = std::min(rsize - done, _conv.size() / 2);
                    for (size_t i = 0; i < n; ++i) {
                        const auto& s = rptr[done + i];
                        _conv[2 * i] = static_cast<int16_t>(std::max(-32767.0f, std::min(32767.0f, s.real() * 32767.0f)));
                        _conv[2 * i + 1] = static_cast<int16_t>(std::max(-32767.0f, std::min(32767.0f, s.imag() * 32767.0f)));
                    }
                    if (std::fwrite(_conv.data(), sizeof(int16_t), 2 * n, _fp) != 2 * n) {
                        std::fclose(_fp);
                        _fp = nullptr;
                        _recording.store(false, std::memory_order_release);
                        _failed.store(true, std::memory_order_release);
                        break;
                    }
                    done += n;
                    _samples.fetch_add(n, std::memory_order_relaxed);
                }
            }
        }
        in.commit_read(rsize);
        return cler::Empty{};
    }

    // GUI thread. Opens <prefix>_YYYYmmdd_HHMMSS.sigmf-{meta,data}; the meta
    // carries the given centre frequency.
    bool start(const std::string& prefix, double center_frequency_hz) {
        char stamp[32];
        const std::time_t now = std::time(nullptr);
        std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::gmtime(&now));
        return start_at(prefix + "_" + stamp, center_frequency_hz);
    }

    // exact base path, no stamp appended
    bool start_at(const std::string& base, double center_frequency_hz) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_fp) return false;
        _base = base;
        _fp = std::fopen((base + ".sigmf-data").c_str(), "wb");
        if (!_fp) return false;
        sigmf::Meta meta;
        meta.datatype = sigmf::Datatype::ci16_le;
        meta.sample_rate = _rate;
        sigmf::Capture cap;
        cap.sample_start = 0;
        cap.frequency = center_frequency_hz;
        cap.has_frequency = true;
        cap.datetime = sigmf::utc_now();
        meta.captures.push_back(cap);
        if (!sigmf::write_meta(_base + ".sigmf-meta", meta)) {
            std::fclose(_fp);
            _fp = nullptr;
            std::remove((_base + ".sigmf-data").c_str());
            return false;
        }
        _samples.store(0, std::memory_order_relaxed);
        _failed.store(false, std::memory_order_release);
        _recording.store(true, std::memory_order_release);
        return true;
    }

    void stop() {
        std::lock_guard<std::mutex> lock(_mutex);
        _recording.store(false, std::memory_order_release);
        if (_fp) {
            std::fclose(_fp);
            _fp = nullptr;
        }
    }

    bool recording() const { return _recording.load(std::memory_order_acquire); }
    // one-shot: true after a write failure stopped the recording, cleared by the next start
    bool take_failure() { return _failed.exchange(false, std::memory_order_acq_rel); }
    // graph stopped and not recording only; the rate lands in the next start()'s meta
    void set_rate(double rate) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_fp) return;
        _rate = rate;
    }
    uint64_t bytes() const { return _samples.load(std::memory_order_relaxed) * 2 * sizeof(int16_t); }
    uint64_t samples() const { return _samples.load(std::memory_order_relaxed); }
    double sample_rate() const { return _rate; }
    std::string base() const { std::lock_guard<std::mutex> lock(_mutex); return _base; }

private:
    double _rate;
    std::vector<int16_t> _conv;
    mutable std::mutex _mutex;
    FILE* _fp = nullptr;
    std::string _base;
    std::atomic<bool> _recording{false};
    std::atomic<bool> _failed{false};
    std::atomic<uint64_t> _samples{0};
};
`,Ln=`#pragma once

#include "cler_desktop_utils.hpp"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

// SigMF v1.x metadata: a <base>.sigmf-meta JSON file beside a <base>.sigmf-data
// raw sample file. Little-endian datatypes only.
namespace sigmf {

enum class Datatype { cf32_le, ci16_le, ci8, cu8, rf32_le, ri16_le };

inline const char* datatype_name(Datatype dt) {
    switch (dt) {
        case Datatype::cf32_le: return "cf32_le";
        case Datatype::ci16_le: return "ci16_le";
        case Datatype::ci8:     return "ci8";
        case Datatype::cu8:     return "cu8";
        case Datatype::rf32_le: return "rf32_le";
        case Datatype::ri16_le: return "ri16_le";
    }
    return "cf32_le";
}

inline bool datatype_is_complex(Datatype dt) {
    return dt == Datatype::cf32_le || dt == Datatype::ci16_le ||
           dt == Datatype::ci8 || dt == Datatype::cu8;
}

// bytes on disk per sample (a complex sample counts both components)
inline size_t datatype_size(Datatype dt) {
    switch (dt) {
        case Datatype::cf32_le: return 8;
        case Datatype::ci16_le: return 4;
        case Datatype::ci8:     return 2;
        case Datatype::cu8:     return 2;
        case Datatype::rf32_le: return 4;
        case Datatype::ri16_le: return 2;
    }
    return 8;
}

// SigMF core:datetime is ISO8601 in UTC, e.g. 2026-08-20T11:22:33.123Z
inline std::string utc_now() {
    auto now = std::chrono::system_clock::now();
    std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()).count() % 1000;
    std::tm tm_utc;
    gmtime_r(&seconds, &tm_utc);
    char buf[40];
    size_t n = std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_utc);
    std::snprintf(buf + n, sizeof(buf) - n, ".%03dZ", static_cast<int>(millis));
    return buf;
}

inline bool try_parse_datatype(const std::string& name, Datatype& out) {
    // 8-bit types carry no meaningful byte order; the spec still allows the suffix
    if (name == "cf32_le") out = Datatype::cf32_le;
    else if (name == "ci16_le") out = Datatype::ci16_le;
    else if (name == "ci8" || name == "ci8_le") out = Datatype::ci8;
    else if (name == "cu8" || name == "cu8_le") out = Datatype::cu8;
    else if (name == "rf32_le") out = Datatype::rf32_le;
    else if (name == "ri16_le") out = Datatype::ri16_le;
    else return false;
    return true;
}

inline Datatype parse_datatype(const std::string& name) {
    Datatype dt;
    if (!try_parse_datatype(name, dt)) {
        std::string msg = "SigMF core:datatype not supported (little-endian only): " + name;
        cler::panic(msg.c_str());
    }
    return dt;
}

// key -> raw JSON text, used to carry keys this reader does not model
using Fields = std::vector<std::pair<std::string, std::string>>;

struct Capture {
    uint64_t sample_start = 0;
    double frequency = 0.0;
    std::string datetime;
    bool has_frequency = false;
    Fields extra;
};

struct Meta {
    std::string version = "1.0.0";
    Datatype datatype = Datatype::cf32_le;
    double sample_rate = 0.0;
    std::string author;
    std::string description;
    std::string hw;
    Fields extra_global;
    std::vector<Capture> captures;
    std::vector<Fields> annotations;

    double center_frequency() const {
        for (const auto& c : captures) {
            if (c.has_frequency) return c.frequency;
        }
        return 0.0;
    }
};

namespace detail {

inline void skip_ws(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\\t' || s[i] == '\\n' || s[i] == '\\r')) i++;
}

inline void json_fail(const char* what) {
    std::string msg = std::string("Malformed SigMF metadata JSON: ") + what;
    cler::panic(msg.c_str());
}

// consumes one JSON value and returns its raw text
inline std::string skip_value(const std::string& s, size_t& i) {
    skip_ws(s, i);
    size_t start = i;
    if (i >= s.size()) json_fail("unexpected end of input");
    char c = s[i];
    if (c == '"') {
        i++;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\\\') i++;
            i++;
        }
        if (i >= s.size()) json_fail("unterminated string");
        i++;
    } else if (c == '{' || c == '[') {
        char close = (c == '{') ? '}' : ']';
        int depth = 0;
        while (i < s.size()) {
            if (s[i] == '"') {
                size_t j = i;
                skip_value(s, j);
                i = j;
                continue;
            }
            if (s[i] == c) depth++;
            else if (s[i] == close) {
                depth--;
                if (depth == 0) { i++; break; }
            }
            i++;
        }
        if (depth != 0) json_fail("unbalanced brackets");
    } else {
        while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' &&
               s[i] != ' ' && s[i] != '\\t' && s[i] != '\\n' && s[i] != '\\r') i++;
        if (i == start) json_fail("unexpected character");
    }
    return s.substr(start, i - start);
}

inline std::string unescape(const std::string& raw) {
    if (raw.size() < 2 || raw.front() != '"') return raw;
    std::string out;
    for (size_t i = 1; i + 1 < raw.size(); ++i) {
        if (raw[i] == '\\\\' && i + 2 < raw.size()) {
            char e = raw[++i];
            if (e == 'n') out += '\\n';
            else if (e == 't') out += '\\t';
            else if (e == 'r') out += '\\r';
            else out += e;
        } else {
            out += raw[i];
        }
    }
    return out;
}

inline std::string escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '"' || c == '\\\\') { out += '\\\\'; out += c; }
        else if (c == '\\n') out += "\\\\n";
        else if (c == '\\t') out += "\\\\t";
        else if (c == '\\r') out += "\\\\r";
        else out += c;
    }
    return out;
}

inline double as_number(const std::string& raw) { return std::strtod(raw.c_str(), nullptr); }

// parses "{...}" into its key/raw-value pairs, in file order
inline Fields parse_object(const std::string& raw) {
    Fields fields;
    size_t i = 0;
    skip_ws(raw, i);
    if (i >= raw.size() || raw[i] != '{') json_fail("expected an object");
    i++;
    skip_ws(raw, i);
    if (i < raw.size() && raw[i] == '}') return fields;
    while (i < raw.size()) {
        skip_ws(raw, i);
        std::string key = unescape(skip_value(raw, i));
        skip_ws(raw, i);
        if (i >= raw.size() || raw[i] != ':') json_fail("expected ':' after a key");
        i++;
        fields.emplace_back(key, skip_value(raw, i));
        skip_ws(raw, i);
        if (i < raw.size() && raw[i] == ',') { i++; continue; }
        break;
    }
    return fields;
}

inline std::vector<std::string> parse_array(const std::string& raw) {
    std::vector<std::string> items;
    size_t i = 0;
    skip_ws(raw, i);
    if (i >= raw.size() || raw[i] != '[') json_fail("expected an array");
    i++;
    skip_ws(raw, i);
    if (i < raw.size() && raw[i] == ']') return items;
    while (i < raw.size()) {
        items.push_back(skip_value(raw, i));
        skip_ws(raw, i);
        if (i < raw.size() && raw[i] == ',') { i++; continue; }
        break;
    }
    return items;
}

inline const std::string* find(const Fields& fields, const char* key) {
    for (const auto& kv : fields) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

inline void write_fields(std::string& out, const Fields& fields, const char* indent) {
    for (const auto& kv : fields) {
        out += ",\\n";
        out += indent;
        out += "\\"" + escape(kv.first) + "\\": " + kv.second;
    }
}

inline std::string number_text(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    return buf;
}

} // namespace detail

// derives the <base> from a base path or from either sidecar file's path
inline std::string base_path(const std::string& path) {
    const char* suffixes[] = {".sigmf-meta", ".sigmf-data"};
    for (const char* suffix : suffixes) {
        size_t n = std::string(suffix).size();
        if (path.size() > n && path.compare(path.size() - n, n, suffix) == 0) {
            return path.substr(0, path.size() - n);
        }
    }
    return path;
}

inline std::string meta_path(const std::string& path) { return base_path(path) + ".sigmf-meta"; }
inline std::string data_path(const std::string& path) { return base_path(path) + ".sigmf-data"; }

// why, when given, says what was wrong with the file — the datatype by name if
// that is what this reader cannot handle.
inline bool try_read_meta(const std::string& path, Meta& meta, std::string* why = nullptr) {
    std::string file = meta_path(path);
    FILE* fp = std::fopen(file.c_str(), "rb");
    if (!fp) {
        if (why) *why = "cannot read the metadata";
        return false;
    }
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0) text.append(buf, n);
    std::fclose(fp);

    for (const auto& kv : detail::parse_object(text)) {
        if (kv.first == "global") {
            for (const auto& g : detail::parse_object(kv.second)) {
                if (g.first == "core:datatype") {
                    const std::string dt = detail::unescape(g.second);
                    if (!try_parse_datatype(dt, meta.datatype)) {
                        if (why) *why = "is " + dt + ", which this build cannot read";
                        return false;
                    }
                }
                else if (g.first == "core:sample_rate") meta.sample_rate = detail::as_number(g.second);
                else if (g.first == "core:version") meta.version = detail::unescape(g.second);
                else if (g.first == "core:author") meta.author = detail::unescape(g.second);
                else if (g.first == "core:description") meta.description = detail::unescape(g.second);
                else if (g.first == "core:hw") meta.hw = detail::unescape(g.second);
                else meta.extra_global.push_back(g);
            }
        } else if (kv.first == "captures") {
            for (const auto& item : detail::parse_array(kv.second)) {
                Capture capture;
                for (const auto& c : detail::parse_object(item)) {
                    if (c.first == "core:sample_start") {
                        capture.sample_start = static_cast<uint64_t>(detail::as_number(c.second));
                    } else if (c.first == "core:frequency") {
                        capture.frequency = detail::as_number(c.second);
                        capture.has_frequency = true;
                    } else if (c.first == "core:datetime") {
                        capture.datetime = detail::unescape(c.second);
                    } else {
                        capture.extra.push_back(c);
                    }
                }
                meta.captures.push_back(std::move(capture));
            }
        } else if (kv.first == "annotations") {
            for (const auto& item : detail::parse_array(kv.second)) {
                meta.annotations.push_back(detail::parse_object(item));
            }
        }
    }
    if (meta.captures.empty()) meta.captures.push_back(Capture{});
    return true;
}

inline Meta read_meta(const std::string& path) {
    Meta meta;
    if (!try_read_meta(path, meta)) {
        std::string msg = "Failed to read SigMF metadata: " + meta_path(path);
        cler::panic(msg.c_str());
    }
    return meta;
}

inline std::string to_json(const Meta& meta) {
    std::string out = "{\\n  \\"global\\": {\\n";
    out += "    \\"core:datatype\\": \\"" + std::string(datatype_name(meta.datatype)) + "\\"";
    out += ",\\n    \\"core:sample_rate\\": " + detail::number_text(meta.sample_rate);
    out += ",\\n    \\"core:version\\": \\"" + detail::escape(meta.version) + "\\"";
    if (!meta.author.empty()) out += ",\\n    \\"core:author\\": \\"" + detail::escape(meta.author) + "\\"";
    if (!meta.description.empty()) out += ",\\n    \\"core:description\\": \\"" + detail::escape(meta.description) + "\\"";
    if (!meta.hw.empty()) out += ",\\n    \\"core:hw\\": \\"" + detail::escape(meta.hw) + "\\"";
    detail::write_fields(out, meta.extra_global, "    ");
    out += "\\n  },\\n  \\"captures\\": [";

    for (size_t i = 0; i < meta.captures.size(); ++i) {
        const Capture& capture = meta.captures[i];
        out += (i == 0) ? "\\n" : ",\\n";
        out += "    {\\n      \\"core:sample_start\\": " + std::to_string(capture.sample_start);
        if (capture.has_frequency) out += ",\\n      \\"core:frequency\\": " + detail::number_text(capture.frequency);
        if (!capture.datetime.empty()) out += ",\\n      \\"core:datetime\\": \\"" + detail::escape(capture.datetime) + "\\"";
        detail::write_fields(out, capture.extra, "      ");
        out += "\\n    }";
    }
    out += meta.captures.empty() ? "],\\n  \\"annotations\\": [" : "\\n  ],\\n  \\"annotations\\": [";

    for (size_t i = 0; i < meta.annotations.size(); ++i) {
        out += (i == 0) ? "\\n" : ",\\n";
        out += "    {";
        const Fields& annotation = meta.annotations[i];
        for (size_t k = 0; k < annotation.size(); ++k) {
            out += (k == 0) ? "\\n      " : ",\\n      ";
            out += "\\"" + detail::escape(annotation[k].first) + "\\": " + annotation[k].second;
        }
        out += "\\n    }";
    }
    out += meta.annotations.empty() ? "]\\n}\\n" : "\\n  ]\\n}\\n";
    return out;
}

inline bool write_meta(const std::string& path, const Meta& meta) {
    std::string file = meta_path(path);
    FILE* fp = std::fopen(file.c_str(), "wb");
    if (!fp) return false;
    std::string text = to_json(meta);
    bool ok = std::fwrite(text.data(), 1, text.size(), fp) == text.size();
    std::fclose(fp);
    return ok;
}

// {"core:sample_start": .., "core:sample_count": .., "core:label": ".."}
inline Fields make_annotation(uint64_t sample_start, uint64_t sample_count, const std::string& label) {
    Fields annotation;
    annotation.emplace_back("core:sample_start", std::to_string(sample_start));
    annotation.emplace_back("core:sample_count", std::to_string(sample_count));
    if (!label.empty()) annotation.emplace_back("core:label", "\\"" + detail::escape(label) + "\\"");
    return annotation;
}

} // namespace sigmf
`,Hn=`#pragma once

#include "cler.hpp"
#include "desktop_blocks/sigmf/sigmf.hpp"
#include <complex>
#include <cstring>
#include <cmath>
#include <mutex>
#include <type_traits>

template <typename T>
struct SinkSigMFBlock : public cler::BlockBase {
    static_assert(std::is_same<T, float>::value || std::is_same<T, std::complex<float>>::value,
                  "SinkSigMFBlock supports float and std::complex<float>");
    static constexpr bool may_block = true;

    cler::Channel<T> in;

    SinkSigMFBlock(const char* name,
                   const char* path,
                   double sample_rate,
                   double center_frequency,
                   sigmf::Datatype datatype = sigmf::Datatype::cf32_le,
                   size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size),
          _base(sigmf::base_path(path))
    {
        constexpr bool complex_source = std::is_same<T, std::complex<float>>::value;
        if (sigmf::datatype_is_complex(datatype) != complex_source) {
            std::string msg = "SigMF datatype " + std::string(sigmf::datatype_name(datatype)) +
                              " does not match the block sample type";
            cler::panic(msg.c_str());
        }

        _meta.datatype = datatype;
        _meta.sample_rate = sample_rate;
        sigmf::Capture capture;
        capture.frequency = center_frequency;
        capture.has_frequency = true;
        capture.datetime = sigmf::utc_now();
        _meta.captures.push_back(capture);
        if (!sigmf::write_meta(_base, _meta)) {
            std::string msg = "Failed to write SigMF metadata: " + sigmf::meta_path(_base);
            cler::panic(msg.c_str());
        }

        std::string file = sigmf::data_path(_base);
        _fp = std::fopen(file.c_str(), "wb");
        if (!_fp) {
            std::string msg = "Failed to open SigMF data file for writing: " + file;
            cler::panic(msg.c_str());
        }
        _raw.resize(_chunk_samples * sigmf::datatype_size(datatype));
    }

    ~SinkSigMFBlock() {
        if (_fp) {
            std::fflush(_fp);
            std::fclose(_fp);
            _fp = nullptr;
        }
        std::lock_guard<std::mutex> lock(_annotations_mutex);
        sigmf::write_meta(_base, _meta);
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        if (!_fp) {
            return cler::Error::TERM_IOError;
        }

        auto [read_ptr, read_size] = in.read_dbf();
        if (read_ptr == nullptr || read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        const size_t sample_bytes = sigmf::datatype_size(_meta.datatype);
        size_t samples = std::min(read_size, _chunk_samples);
        convert(read_ptr, _raw.data(), samples);
        if (std::fwrite(_raw.data(), sample_bytes, samples, _fp) != samples) {
            return cler::Error::TERM_IOError;
        }
        in.commit_read(samples);
        _samples_written += samples;
        return cler::Empty{};
    }

    void add_annotation(uint64_t sample_start, uint64_t sample_count, const char* label) {
        std::lock_guard<std::mutex> lock(_annotations_mutex);
        _meta.annotations.push_back(sigmf::make_annotation(sample_start, sample_count, label ? label : ""));
    }

    size_t samples_written() const { return _samples_written; }
    const sigmf::Meta& meta() const { return _meta; }

private:
    void convert(const T* samples_in, uint8_t* raw, size_t samples) const {
        const float* src = reinterpret_cast<const float*>(samples_in);
        const size_t components = std::is_same<T, std::complex<float>>::value ? 2 * samples : samples;
        switch (_meta.datatype) {
            case sigmf::Datatype::cf32_le:
            case sigmf::Datatype::rf32_le:
                std::memcpy(raw, src, components * sizeof(float));
                break;
            case sigmf::Datatype::ci16_le:
            case sigmf::Datatype::ri16_le:
                for (size_t i = 0; i < components; ++i) {
                    float scaled = src[i] * 32768.0f;
                    if (scaled > 32767.0f) scaled = 32767.0f;
                    if (scaled < -32768.0f) scaled = -32768.0f;
                    int16_t value = static_cast<int16_t>(std::lrintf(scaled));
                    std::memcpy(raw + i * sizeof(int16_t), &value, sizeof(int16_t));
                }
                break;
            case sigmf::Datatype::ci8:
                for (size_t i = 0; i < components; ++i) {
                    float scaled = src[i] * 128.0f;
                    if (scaled > 127.0f) scaled = 127.0f;
                    if (scaled < -128.0f) scaled = -128.0f;
                    raw[i] = static_cast<uint8_t>(static_cast<int8_t>(std::lrintf(scaled)));
                }
                break;
            case sigmf::Datatype::cu8:
                for (size_t i = 0; i < components; ++i) {
                    float scaled = src[i] * 127.5f + 127.5f;
                    if (scaled > 255.0f) scaled = 255.0f;
                    if (scaled < 0.0f) scaled = 0.0f;
                    raw[i] = static_cast<uint8_t>(std::lrintf(scaled));
                }
                break;
        }
    }

    static constexpr size_t _chunk_samples = 8192;
    std::string _base;
    sigmf::Meta _meta;
    std::vector<uint8_t> _raw;
    FILE* _fp = nullptr;
    size_t _samples_written = 0;
    std::mutex _annotations_mutex;
};
`,Un=`#pragma once

#include "cler.hpp"
#include "desktop_blocks/sigmf/sigmf.hpp"
#include <atomic>
#include <chrono>
#include <complex>
#include <cstring>
#include <thread>
#include <type_traits>

template <typename T>
struct SourceSigMFBlock : public cler::BlockBase {
    static_assert(std::is_same<T, float>::value || std::is_same<T, std::complex<float>>::value,
                  "SourceSigMFBlock supports float and std::complex<float>");
    static constexpr bool may_block = true;

    // transport = real-time pacing at the file rate plus seek/pause/loop/ended,
    // for interactive playback; EOF then parks (file stays open) instead of
    // terminating
    SourceSigMFBlock(const char* name, const char* path, bool repeat = false, size_t chunk_samples = 8192,
                     bool transport = false)
        : cler::BlockBase(name),
          _meta(sigmf::read_meta(path)),
          _repeat(repeat),
          _chunk_samples(chunk_samples == 0 ? 8192 : chunk_samples),
          _transport(transport),
          _loop(repeat)
    {
        constexpr bool complex_sink = std::is_same<T, std::complex<float>>::value;
        if (sigmf::datatype_is_complex(_meta.datatype) != complex_sink) {
            std::string msg = "SigMF datatype " + std::string(sigmf::datatype_name(_meta.datatype)) +
                              " does not match the block sample type";
            cler::panic(msg.c_str());
        }

        std::string file = sigmf::data_path(path);
        _fp = std::fopen(file.c_str(), "rb");
        if (!_fp) {
            std::string msg = "Failed to open SigMF data file: " + file;
            cler::panic(msg.c_str());
        }
        _raw.resize(_chunk_samples * sigmf::datatype_size(_meta.datatype));
        std::fseek(_fp, 0, SEEK_END);
        _total = static_cast<uint64_t>(std::ftell(_fp)) / sigmf::datatype_size(_meta.datatype);
        std::fseek(_fp, 0, SEEK_SET);
    }

    ~SourceSigMFBlock() {
        if (_fp) std::fclose(_fp);
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        if (!_fp) {
            return cler::Error::TERM_EOFReached;
        }
        const size_t sample_bytes = sigmf::datatype_size(_meta.datatype);
        if (_transport) {
            const int64_t want_seek = _pending_seek.exchange(-1, std::memory_order_acq_rel);
            if (want_seek >= 0) {
                const uint64_t sample = std::min<uint64_t>(static_cast<uint64_t>(want_seek), _total);
                std::clearerr(_fp);
                std::fseek(_fp, static_cast<long>(sample * sample_bytes), SEEK_SET);
                _pos.store(sample, std::memory_order_relaxed);
                _ended.store(false, std::memory_order_relaxed);
                _started = false;
            }
            if (_ended.load(std::memory_order_relaxed) && _loop.load(std::memory_order_relaxed)) {
                std::clearerr(_fp);
                std::fseek(_fp, 0, SEEK_SET);
                _pos.store(0, std::memory_order_relaxed);
                _ended.store(false, std::memory_order_relaxed);
                _started = false;
            }
            if (_pause.load(std::memory_order_relaxed) || _ended.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                _started = false;
                return cler::Error::NotEnoughSamples;
            }
        }

        auto [write_ptr, write_size] = out->write_dbf();
        if (write_ptr == nullptr || write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t want = std::min(write_size, _chunk_samples);
        if (_transport) {
            const double rate = _meta.sample_rate > 0 ? _meta.sample_rate : 1e6;
            if (!_started) { _epoch = clock::now() - to_duration(_emitted, rate); _started = true; }
            size_t due = samples_due(rate);
            if (due <= _emitted) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                due = samples_due(rate);
                if (due <= _emitted) return cler::Error::NotEnoughSamples;
            }
            want = std::min(want, due - _emitted);
            if (due - _emitted > rate / 10.0) _epoch = clock::now() - to_duration(_emitted, rate);
        }
        size_t got = std::fread(_raw.data(), 1, want * sample_bytes, _fp);
        size_t samples = got / sample_bytes;

        if (samples == 0) {
            if (_repeat || (_transport && _loop.load(std::memory_order_relaxed))) {
                std::clearerr(_fp);
                std::fseek(_fp, 0, SEEK_SET);
                _pos.store(0, std::memory_order_relaxed);
                return cler::Error::NotEnoughSamples;
            }
            if (_transport) {
                _ended.store(true, std::memory_order_relaxed);
                return cler::Error::NotEnoughSamples;
            }
            std::fclose(_fp);
            _fp = nullptr;
            return cler::Error::NotEnoughSamples;
        }

        convert(_raw.data(), write_ptr, samples);
        out->commit_write(samples);
        _emitted += samples;
        _pos.fetch_add(samples, std::memory_order_relaxed);
        return cler::Empty{};
    }

    void seek(double seconds) {
        const double rate = _meta.sample_rate > 0 ? _meta.sample_rate : 1e6;
        _pending_seek.store(static_cast<int64_t>(std::max(0.0, seconds) * rate), std::memory_order_release);
    }
    void pause(bool p) { _pause.store(p, std::memory_order_relaxed); }
    bool paused() const { return _pause.load(std::memory_order_relaxed); }
    void set_loop(bool l) { _loop.store(l, std::memory_order_relaxed); }
    bool looping() const { return _loop.load(std::memory_order_relaxed); }
    bool ended() const { return _ended.load(std::memory_order_relaxed); }
    double pos_seconds() const {
        const double rate = _meta.sample_rate > 0 ? _meta.sample_rate : 1e6;
        return static_cast<double>(_pos.load(std::memory_order_relaxed)) / rate;
    }
    double duration_seconds() const {
        const double rate = _meta.sample_rate > 0 ? _meta.sample_rate : 1e6;
        return static_cast<double>(_total) / rate;
    }

    double sample_rate() const { return _meta.sample_rate; }
    double center_frequency() const { return _meta.center_frequency(); }
    sigmf::Datatype datatype() const { return _meta.datatype; }
    const sigmf::Meta& meta() const { return _meta; }

private:
    // one stored sample -> one T; complex types write two floats per sample
    void convert(const uint8_t* raw, T* out, size_t samples) const {
        float* dst = reinterpret_cast<float*>(out);
        const size_t components = std::is_same<T, std::complex<float>>::value ? 2 * samples : samples;
        switch (_meta.datatype) {
            case sigmf::Datatype::cf32_le:
            case sigmf::Datatype::rf32_le:
                std::memcpy(dst, raw, components * sizeof(float));
                break;
            case sigmf::Datatype::ci16_le:
            case sigmf::Datatype::ri16_le: {
                int16_t value;
                for (size_t i = 0; i < components; ++i) {
                    std::memcpy(&value, raw + i * sizeof(int16_t), sizeof(int16_t));
                    dst[i] = static_cast<float>(value) / 32768.0f;
                }
                break;
            }
            case sigmf::Datatype::ci8:
                for (size_t i = 0; i < components; ++i) {
                    dst[i] = static_cast<float>(static_cast<int8_t>(raw[i])) / 128.0f;
                }
                break;
            case sigmf::Datatype::cu8:
                for (size_t i = 0; i < components; ++i) {
                    dst[i] = (static_cast<float>(raw[i]) - 127.5f) / 127.5f;
                }
                break;
        }
    }

    using clock = std::chrono::steady_clock;
    size_t samples_due(double rate) const {
        return static_cast<size_t>(std::chrono::duration<double>(clock::now() - _epoch).count() * rate);
    }
    static clock::duration to_duration(size_t samples, double rate) {
        return std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(samples / rate));
    }

    sigmf::Meta _meta;
    bool _repeat;
    size_t _chunk_samples;
    bool _transport;
    std::vector<uint8_t> _raw;
    FILE* _fp = nullptr;
    uint64_t _total = 0;
    std::atomic<bool> _pause{false}, _loop{false}, _ended{false};
    std::atomic<int64_t> _pending_seek{-1};
    std::atomic<uint64_t> _pos{0};
    clock::time_point _epoch;
    size_t _emitted = 0;
    bool _started = false;
};
`,Gn=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <string>
#include <cstring>
#include <cstdio>

#ifdef __has_include
    #if __has_include(<portaudio.h>)
        #include <portaudio.h>
    #else
        #error "PortAudio header not found. Please install portaudio19-dev package."
    #endif
#else
    #include <portaudio.h>
#endif

inline void pa_check(PaError err) {
    if (err != paNoError) {
        std::string msg = "PortAudio error: ";
        msg += Pa_GetErrorText(err);
        cler::panic(msg.c_str());
    }
}

struct SinkAudioBlock : public cler::BlockBase {
    static constexpr bool may_block = true;
    cler::Channel<float> in;

    // channels > 1 expects interleaved frames on \`in\` (L,R,L,R,... for stereo).
    // latency_s sizes the device buffer (0 = device default, ~35 ms); a bursty
    // upstream (SDR USB transfers) needs a few hundred ms or every gap
    // underflows and the restart penalty drags the sink below real time.
    SinkAudioBlock(const char* name,
                   double sample_rate = 48000.0,
                   int device_index = paNoDevice,
                   size_t buffer_size = 0,
                   int channels = 1,
                   double latency_s = 0.0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float) : buffer_size),
          _sample_rate(sample_rate),
          _device_index(device_index),
          _channels(channels),
          _latency_s(latency_s),
          _stream(nullptr)
    {
        if (sample_rate <= 0.0 || sample_rate > 1e6) {
            cler::panic("Invalid sample rate: must be > 0 and <= 1MHz");
        }
        if (channels < 1 || channels > 8) {
            cler::panic("Invalid channel count: must be 1..8");
        }

        if (buffer_size > 0 && buffer_size * sizeof(float) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }

        PaError err = Pa_Initialize();
        pa_check(err);

        if (device_index != paNoDevice) {
            int num_devices = Pa_GetDeviceCount();
            if (num_devices < 0 || device_index >= num_devices) {
                cler::panic("Invalid device index");
            }
        }

        _open_stream();
    }

    ~SinkAudioBlock() {
        _close_stream();
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        if (!_stream) {
            return cler::Error::TERM_IOError;
        }

        auto [read_ptr, read_size] = in.read_dbf();
        read_size -= read_size % static_cast<size_t>(_channels);
        if (read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        PaError err = Pa_WriteStream(_stream, read_ptr, read_size / static_cast<size_t>(_channels));

        if (err == paOutputUnderflowed) {
            in.commit_read(read_size);
            return cler::Empty{};
        } else if (err != paNoError) {
            return cler::Error::TERM_IOError;
        }

        in.commit_read(read_size);

        return cler::Empty{};
    }

    static void print_devices() {
        PaError err = Pa_Initialize();
        if (err != paNoError) {
            cler::panic("PortAudio init failed");
        }

        int num_devices = Pa_GetDeviceCount();
        if (num_devices < 0) {
            cler::panic("Pa_GetDeviceCount() failed");
        }

        printf("PortAudio Output Devices:\\n");
        for (int i = 0; i < num_devices; ++i) {
            const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
            if (!info) continue;

            if (info->maxOutputChannels > 0) {
                printf("  [%d] %s (outputs: %d, default latency: %.1f ms)\\n",
                       i, info->name, info->maxOutputChannels,
                       info->defaultHighOutputLatency * 1000.0);
            }
        }
    }

private:
    double _sample_rate;
    int _device_index;
    int _channels;
    double _latency_s;
    PaStream* _stream;

    void _open_stream() {
        PaStreamParameters output_params;
        std::memset(&output_params, 0, sizeof(output_params));

        output_params.device = (_device_index == paNoDevice) ? Pa_GetDefaultOutputDevice() : _device_index;
        if (output_params.device < 0) {
            cler::panic("No default output device found");
        }

        const PaDeviceInfo* device_info = Pa_GetDeviceInfo(output_params.device);
        if (!device_info) {
            cler::panic("Pa_GetDeviceInfo() failed for output device");
        }

        output_params.channelCount = _channels;
        output_params.sampleFormat = paFloat32;
        output_params.suggestedLatency = _latency_s > 0.0 ? _latency_s : device_info->defaultHighOutputLatency;
        output_params.hostApiSpecificStreamInfo = nullptr;

        PaError err = Pa_OpenStream(
            &_stream,
            nullptr,
            &output_params,
            _sample_rate,
            paFramesPerBufferUnspecified,
            paClipOff,
            nullptr,
            nullptr
        );
        pa_check(err);

        err = Pa_StartStream(_stream);
        if (err != paNoError) {
            Pa_CloseStream(_stream);
            _stream = nullptr;
            pa_check(err);
        }
    }

    void _close_stream() {
        if (_stream) {
            PaError err = Pa_StopStream(_stream);
            if (err != paNoError) {
                // Log but don't throw in destructor
                fprintf(stderr, "Warning: Pa_StopStream failed: %s\\n", Pa_GetErrorText(err));
            }
            err = Pa_CloseStream(_stream);
            if (err != paNoError) {
                fprintf(stderr, "Warning: Pa_CloseStream failed: %s\\n", Pa_GetErrorText(err));
            }
            _stream = nullptr;
        }
    }
};
`,Yn=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <cstdio>

template <typename T>
struct SinkFileBlock : public cler::BlockBase {
    static constexpr bool may_block = true;
    cler::Channel<T> in;

    SinkFileBlock(const char* name, const char* filename, size_t buffer_size = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size) {

        if (buffer_size > 0 && buffer_size * sizeof(T) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
        if (!filename || filename[0] == '\\0') {
            cler::panic("Filename must not be empty");
        }

        _fp = std::fopen(filename, "wb");
        if (!_fp) {
            cler::panic("Failed to open file for writing");
        }

        size_t actual_buffer_size = (buffer_size == 0) ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size;

        if (std::setvbuf(_fp, nullptr, _IOFBF, actual_buffer_size * sizeof(T)) != 0) {
            std::fclose(_fp);
            _fp = nullptr;
            cler::panic("Failed to setvbuf() on file stream");
        }
    }

    ~SinkFileBlock() {
        if (_fp) {
            std::fflush(_fp);
            std::fclose(_fp);
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure()
    {
        if (!_fp) {
            return cler::Error::TERM_IOError;
        }

        auto [span_ptr, span_size] = in.read_dbf();
        if (span_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        size_t written = std::fwrite(span_ptr, sizeof(T), span_size, _fp);
        if (written != span_size) return cler::Error::TERM_IOError;
        in.commit_read(written);
        return cler::Empty{};
    }

private:
    FILE* _fp = nullptr;
};
`,Kn=`#pragma once
#include "cler.hpp"
#include "cler_desktop_utils.hpp"

#ifdef __has_include
    #if __has_include(<libhackrf/hackrf.h>)
        #include <libhackrf/hackrf.h>
    #elif __has_include(<hackrf.h>)
        #include <hackrf.h>
    #else
        #error "HackRF header not found. Please install libhackrf or hackrf-dev package."
    #endif
#endif

#include <complex>
#include <atomic>
#include <cstring>

inline void hackrf_pack_iq(const std::complex<float>* src, uint8_t* dst, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        const float i_val = std::max(-1.0f, std::min(1.0f, src[i].real()));
        const float q_val = std::max(-1.0f, std::min(1.0f, src[i].imag()));
        dst[2 * i]     = static_cast<uint8_t>(static_cast<int8_t>(i_val * 127.0f));
        dst[2 * i + 1] = static_cast<uint8_t>(static_cast<int8_t>(q_val * 127.0f));
    }
}

struct SinkHackRFBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    SinkHackRFBlock(const char* name,
                    uint64_t freq_hz,
                    uint32_t samp_rate_hz,
                    int txvga_gain_db = 0,  // 0-47 dB
                    bool amp_enable = false, // Enable TX amplifier (adds ~10dB but increases harmonics)
                    size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size),
          _freq_hz(freq_hz),
          _samp_rate_hz(samp_rate_hz),
          _txvga_gain_db(txvga_gain_db),
          _amp_enable(amp_enable),
          _iq(2 * (buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>) : buffer_size))
    {
        if (buffer_size > 0 && buffer_size * sizeof(std::complex<float>) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }

        if (hackrf_init() != HACKRF_SUCCESS) {
            cler::panic("Failed to initialize HackRF library");
        }

        if (hackrf_open(&_dev) != HACKRF_SUCCESS) {
            cler::panic("Failed to open HackRF device");
        }

        if (hackrf_set_freq(_dev, freq_hz) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            cler::panic("Failed to set TX frequency");
        }

        if (hackrf_set_sample_rate(_dev, samp_rate_hz) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            cler::panic("Failed to set TX sample rate");
        }

        if (hackrf_set_txvga_gain(_dev, txvga_gain_db) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            cler::panic("Failed to set TXVGA gain");
        }

        if (hackrf_set_amp_enable(_dev, amp_enable ? 1 : 0) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            cler::panic("Failed to set amp enable");
        }

        if (hackrf_start_tx(_dev, tx_callback, this) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            cler::panic("Failed to start TX streaming");
        }

        std::cout << "SinkHackRFBlock: Initialized" << std::endl;
        std::cout << "  Frequency: " << freq_hz / 1e6 << " MHz" << std::endl;
        std::cout << "  Sample rate: " << samp_rate_hz / 1e6 << " MSPS" << std::endl;
        std::cout << "  TXVGA gain: " << txvga_gain_db << " dB" << std::endl;
        std::cout << "  Amp enabled: " << (amp_enable ? "Yes" : "No") << std::endl;
    }

    ~SinkHackRFBlock() {
        if (_dev) {
            hackrf_stop_tx(_dev);
            hackrf_close(_dev);
        }
        hackrf_exit();
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        auto [read_ptr, read_size] = in.read_dbf();
        if (read_ptr == nullptr || read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        auto [write_ptr, write_size] = _iq.write_dbf();
        if (write_ptr == nullptr || write_size < 2) {
            return cler::Error::NotEnoughSpace;
        }

        const size_t n = std::min(read_size, write_size / 2);
        hackrf_pack_iq(read_ptr, write_ptr, n);

        in.commit_read(n);
        _iq.commit_write(2 * n);
        return cler::Empty{};
    }

    size_t get_underrun_count() const { return _underrun_count.load(); }
    void reset_underrun_count() { _underrun_count.store(0); }

private:
    hackrf_device* _dev = nullptr;
    uint64_t _freq_hz;
    uint32_t _samp_rate_hz;
    int _txvga_gain_db;
    bool _amp_enable;

    cler::Channel<uint8_t> _iq;
    std::atomic<size_t> _underrun_count{0};

    static int tx_callback(hackrf_transfer* transfer) {
        SinkHackRFBlock* self = static_cast<SinkHackRFBlock*>(transfer->tx_ctx);
        uint8_t* buf = transfer->buffer;
        const size_t bytes_needed = static_cast<size_t>(transfer->buffer_length);

        auto [read_ptr, read_size] = self->_iq.read_dbf();
        const size_t bytes_to_send = (read_ptr == nullptr) ? 0 : std::min(read_size, bytes_needed);

        if (bytes_to_send > 0) {
            std::memcpy(buf, read_ptr, bytes_to_send);
            self->_iq.commit_read(bytes_to_send);
        }

        if (bytes_to_send < bytes_needed) {
            std::memset(&buf[bytes_to_send], 0, bytes_needed - bytes_to_send);
            self->_underrun_count++;
        }

        return 0;
    }
};`,Xn=`#pragma once

#include "cler.hpp"
template <typename T>
struct SinkNullBlock : public cler::BlockBase {
    
    typedef size_t (*OnReceiveCallback)(cler::Channel<T>*, void* context);

    cler::Channel<T> in;

    SinkNullBlock(const char* name,
                      OnReceiveCallback callback = nullptr,
                      [[maybe_unused]] void* callback_context = nullptr,
                      size_t buffer_size = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size),
         _callback(callback), _callback_context(callback_context) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        const size_t reads_before = in.consumer_thread_cumulative_read_count();

        size_t to_commit;
        if (_callback) {
            to_commit = _callback(&in, _callback_context);
        } else {
            to_commit = in.size();
        }
        in.commit_read(to_commit);

        if (in.consumer_thread_cumulative_read_count() == reads_before) {
            return cler::Error::NotEnoughSamples;
        }
        return cler::Empty{};
    }

    private:
        OnReceiveCallback _callback = nullptr;
        void* _callback_context = nullptr;
};`,Vn=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Formats.hpp>
#include <SoapySDR/Errors.hpp>
#include <complex>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

// Helper to map C++ types to SoapySDR format strings
template<typename T>
inline std::string get_soapy_format() {
    if constexpr (std::is_same_v<T, std::complex<float>>) {
        return SOAPY_SDR_CF32;
    } else if constexpr (std::is_same_v<T, std::complex<int16_t>>) {
        return SOAPY_SDR_CS16;
    } else if constexpr (std::is_same_v<T, std::complex<int8_t>>) {
        return SOAPY_SDR_CS8;
    } else if constexpr (std::is_same_v<T, std::complex<uint8_t>>) {
        return SOAPY_SDR_CU8;
    } else if constexpr (std::is_same_v<T, int32_t>) {
        return SOAPY_SDR_S32;
    } else if constexpr (std::is_same_v<T, int16_t>) {
        return SOAPY_SDR_S16;
    } else if constexpr (std::is_same_v<T, uint8_t>) {
        return SOAPY_SDR_U8;
    } else if constexpr (std::is_same_v<T, float>) {
        return SOAPY_SDR_F32;
    } else {
        static_assert(!std::is_same_v<T, T>, "Unsupported type for SoapySDR");
    }
}

template<typename T>
struct SinkSoapySDRBlock : public cler::BlockBase {
    static constexpr bool may_block = true;

    cler::Channel<T> in;

    SinkSoapySDRBlock(const char* name,
                      const std::string& args,
                      double freq,
                      double rate,
                      double gain = 0.0,
                      size_t channel = 0,
                      size_t channel_size = 0)
        : BlockBase(name),
          in(channel_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : channel_size),
          device_args(args),
          center_freq(freq),
          sample_rate(rate),
          gain_db(gain),
          channel_idx(channel),
          device(nullptr),
          stream(nullptr) {
        
        if (channel_size > 0 && channel_size * sizeof(T) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Channel size too small for doubly-mapped buffers");
        }

        device = SoapySDR::Device::make(device_args);
        if (!device) {
            cler::panic(("SinkSoapySDRBlock: Failed to create SoapySDR device with args: " + device_args).c_str());
        }

        auto sample_rates = device->getSampleRateRange(SOAPY_SDR_TX, channel_idx);
        bool rate_valid = false;
        for (const auto& range : sample_rates) {
            if (sample_rate >= range.minimum() && sample_rate <= range.maximum()) {
                rate_valid = true;
                break;
            }
        }
        if (!rate_valid) {
            std::stringstream ss;
            ss << "Sample rate " << sample_rate/1e6 << " MSPS not supported. Supported rates: ";
            for (const auto& range : sample_rates) {
                ss << range.minimum()/1e6 << "-" << range.maximum()/1e6 << " MSPS ";
            }
            SoapySDR::Device::unmake(device);
            cler::panic(ss.str().c_str());
        }
        device->setSampleRate(SOAPY_SDR_TX, channel_idx, sample_rate);

        auto freq_ranges = device->getFrequencyRange(SOAPY_SDR_TX, channel_idx);
        bool freq_valid = false;
        for (const auto& range : freq_ranges) {
            if (center_freq >= range.minimum() && center_freq <= range.maximum()) {
                freq_valid = true;
                break;
            }
        }
        if (!freq_valid) {
            std::stringstream ss;
            ss << "Frequency " << center_freq/1e6 << " MHz not supported. Supported ranges: ";
            for (const auto& range : freq_ranges) {
                ss << range.minimum()/1e6 << "-" << range.maximum()/1e6 << " MHz ";
            }
            SoapySDR::Device::unmake(device);
            cler::panic(ss.str().c_str());
        }
        device->setFrequency(SOAPY_SDR_TX, channel_idx, center_freq);

        auto gain_range = device->getGainRange(SOAPY_SDR_TX, channel_idx);
        if (gain_db < gain_range.minimum() || gain_db > gain_range.maximum()) {
            std::stringstream ss;
            ss << "Gain " << gain_db << " dB not supported. Supported range: "
               << gain_range.minimum() << "-" << gain_range.maximum() << " dB";
            SoapySDR::Device::unmake(device);
            cler::panic(ss.str().c_str());
        }
        if (device->hasGainMode(SOAPY_SDR_TX, channel_idx)) {
            device->setGainMode(SOAPY_SDR_TX, channel_idx, false);
        }
        device->setGain(SOAPY_SDR_TX, channel_idx, gain_db);

        if (device->getBandwidthRange(SOAPY_SDR_TX, channel_idx).size() > 0) {
            device->setBandwidth(SOAPY_SDR_TX, channel_idx, sample_rate);
        }

        std::vector<size_t> channels = {channel_idx};
        std::string format = get_soapy_format<T>();

        stream = device->setupStream(SOAPY_SDR_TX, format, channels);
        if (!stream) {
            SoapySDR::Device::unmake(device);
            cler::panic("SinkSoapySDRBlock: Failed to setup TX stream");
        }

        mtu = device->getStreamMTU(stream);
        buffer.resize(mtu);

        int ret = device->activateStream(stream);
        if (ret != 0) {
            device->closeStream(stream);
            SoapySDR::Device::unmake(device);
            cler::panic(("SinkSoapySDRBlock: Failed to activate stream: " + std::string(SoapySDR::errToStr(ret))).c_str());
        }

        std::cout << "SinkSoapySDRBlock: Initialized " << device->getDriverKey()
                  << " (" << device->getHardwareKey() << ")"
                  << " at " << center_freq/1e6 << " MHz"
                  << ", " << sample_rate/1e6 << " MSPS"
                  << ", " << gain_db << " dB gain"
                  << ", MTU: " << mtu << " samples" << std::endl;

        auto antennas = device->listAntennas(SOAPY_SDR_TX, channel_idx);
        if (!antennas.empty()) {
            std::cout << "  Available TX antennas: ";
            for (const auto& ant : antennas) {
                std::cout << ant << " ";
            }
            std::cout << std::endl;
        }
    }
    
    ~SinkSoapySDRBlock() {
        if (stream && device) {
            device->deactivateStream(stream);
            device->closeStream(stream);
        }
        if (device) {
            SoapySDR::Device::unmake(device);
        }
    }
    
    cler::Result<cler::Empty, cler::Error> procedure() {
        auto [read_ptr, read_size] = in.read_dbf();
        if (read_ptr == nullptr || read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        size_t samples_sent = 0;
        while (samples_sent < read_size) {
            size_t to_send = std::min(mtu, read_size - samples_sent);

            // SoapySDR writeStream needs a void* array, so a copy is unavoidable here
            std::memcpy(buffer.data(), read_ptr + samples_sent, to_send * sizeof(T));

            void* buffs[] = {buffer.data()};
            int flags = 0;
            const long long time_ns = 0;
            const long timeout_us = 100000; // 100ms

            int ret = device->writeStream(stream, buffs, to_send, flags, time_ns, timeout_us);

            if (ret == SOAPY_SDR_TIMEOUT) {
                break;
            } else if (ret == SOAPY_SDR_UNDERFLOW) {
                underflow_count++;
                if (underflow_count % 100 == 0) {
                    std::cerr << "SinkSoapySDRBlock: Underflow count: " << underflow_count << std::endl;
                }
                samples_sent += to_send;
            } else if (ret < 0) {
                std::cerr << "SinkSoapySDRBlock: writeStream error: " << SoapySDR::errToStr(ret) << std::endl;
                in.commit_read(samples_sent);
                return cler::Error::TERM_ProcedureError;
            } else if (ret == 0) {
                break;
            } else {
                samples_sent += ret;
            }
        }

        if (samples_sent == 0) {
            return cler::Error::NotEnoughSpace;
        }
        in.commit_read(samples_sent);
        return cler::Empty{};
    }
    
    void set_frequency(double freq) {
        try {
            device->setFrequency(SOAPY_SDR_TX, channel_idx, freq);
            center_freq = freq;
        } catch (const std::exception& e) {
            std::cerr << "SinkSoapySDRBlock: set_frequency failed: " << e.what() << std::endl;
        }
    }

    void set_gain(double gain) {
        try {
            device->setGain(SOAPY_SDR_TX, channel_idx, gain);
            gain_db = gain;
        } catch (const std::exception& e) {
            std::cerr << "SinkSoapySDRBlock: set_gain failed: " << e.what() << std::endl;
        }
    }

    void set_sample_rate(double rate) {
        try {
            device->setSampleRate(SOAPY_SDR_TX, channel_idx, rate);
            sample_rate = rate;
            if (device->getBandwidthRange(SOAPY_SDR_TX, channel_idx).size() > 0) {
                device->setBandwidth(SOAPY_SDR_TX, channel_idx, rate);
            }
        } catch (const std::exception& e) {
            std::cerr << "SinkSoapySDRBlock: set_sample_rate failed: " << e.what() << std::endl;
        }
    }

    void set_bandwidth(double bw) {
        try {
            device->setBandwidth(SOAPY_SDR_TX, channel_idx, bw);
        } catch (const std::exception& e) {
            std::cerr << "SinkSoapySDRBlock: set_bandwidth failed: " << e.what() << std::endl;
        }
    }

    void set_antenna(const std::string& antenna) {
        try {
            device->setAntenna(SOAPY_SDR_TX, channel_idx, antenna);
        } catch (const std::exception& e) {
            std::cerr << "SinkSoapySDRBlock: set_antenna failed: " << e.what() << std::endl;
        }
    }
    
    void set_dc_offset(const std::complex<double>& offset) {
        if (device->hasDCOffset(SOAPY_SDR_TX, channel_idx)) {
            device->setDCOffset(SOAPY_SDR_TX, channel_idx, offset);
        }
    }
    
    void set_iq_balance(const std::complex<double>& balance) {
        if (device->hasIQBalance(SOAPY_SDR_TX, channel_idx)) {
            device->setIQBalance(SOAPY_SDR_TX, channel_idx, balance);
        }
    }
    
    double get_frequency() const { return center_freq; }
    double get_gain() const { return gain_db; }
    double get_sample_rate() const { return sample_rate; }
    
    double get_bandwidth() const {
        return device->getBandwidth(SOAPY_SDR_TX, channel_idx);
    }
    
    std::string get_antenna() const {
        return device->getAntenna(SOAPY_SDR_TX, channel_idx);
    }
    
    std::vector<std::string> list_antennas() const {
        return device->listAntennas(SOAPY_SDR_TX, channel_idx);
    }
    
    SoapySDR::RangeList get_frequency_range() const {
        return device->getFrequencyRange(SOAPY_SDR_TX, channel_idx);
    }
    
    SoapySDR::Range get_gain_range() const {
        return device->getGainRange(SOAPY_SDR_TX, channel_idx);
    }
    
    std::vector<std::string> list_gains() const {
        return device->listGains(SOAPY_SDR_TX, channel_idx);
    }
    
    SoapySDR::Range get_gain_range(const std::string& name) const {
        return device->getGainRange(SOAPY_SDR_TX, channel_idx, name);
    }
    
    SoapySDR::RangeList get_sample_rate_range() const {
        return device->getSampleRateRange(SOAPY_SDR_TX, channel_idx);
    }
    
private:
    std::string device_args;
    double center_freq;
    double sample_rate;
    double gain_db;
    size_t channel_idx;

    SoapySDR::Device* device;
    SoapySDR::Stream* stream;

    std::vector<T> buffer;
    size_t mtu;

    size_t underflow_count = 0;
};

using SinkSoapySDRBlockCF32 = SinkSoapySDRBlock<std::complex<float>>;
using SinkSoapySDRBlockCS16 = SinkSoapySDRBlock<std::complex<int16_t>>;
using SinkSoapySDRBlockCS8 = SinkSoapySDRBlock<std::complex<int8_t>>;
using SinkSoapySDRBlockCU8 = SinkSoapySDRBlock<std::complex<uint8_t>>;
using SinkSoapySDRBlockS32 = SinkSoapySDRBlock<int32_t>;
using SinkSoapySDRBlockS16 = SinkSoapySDRBlock<int16_t>;
using SinkSoapySDRBlockU8 = SinkSoapySDRBlock<uint8_t>;
using SinkSoapySDRBlockF32 = SinkSoapySDRBlock<float>;`,jn=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/misc/uhd_common.hpp"

#ifdef __has_include
    #if __has_include(<uhd/usrp/multi_usrp.hpp>)
        #include <uhd/usrp/multi_usrp.hpp>
        #include <uhd/types/tune_request.hpp>
        #include <uhd/types/metadata.hpp>
        #include <uhd/utils/thread.hpp>
    #else
        #error "UHD headers not found. Please install libuhd-dev package."
    #endif
#endif
#include <complex>

#include <vector>
#include <string>
#include <sstream>
#include <numeric>
#include <type_traits>

struct AsyncTxEvent {
    bool event_occurred = false;
    uhd::async_metadata_t::event_code_t event_code = uhd::async_metadata_t::EVENT_CODE_BURST_ACK;
    double time_seconds = 0.0;
    double time_frac_seconds = 0.0;
};

template<typename T>
struct SinkUHDBlock : public cler::BlockBase {
    static constexpr bool may_block = true;
    static constexpr size_t MAX_INPUT_CHANNEL_SLOTS = 16;

    cler::Channel<T>* in = nullptr;

    SinkUHDBlock(const char* name,
                 const std::string& dvc_adrs = "",
                 size_t num_channels = 1,
                 size_t channel_size = 0,
                 const std::string& otw_format = "sc16",
                 const UHDConfig* initial_config = nullptr)
        : BlockBase(name),
          _device_address(dvc_adrs),
          _num_channels(num_channels),
          _wire_format(otw_format),
          _configuring(false) {

        if (_num_channels == 0) {
            cler::panic("SinkUHDBlock: num_channels must be at least 1");
        }
        if (_num_channels > MAX_INPUT_CHANNEL_SLOTS) {
            cler::panic("SinkUHDBlock: too many input channels for MAX_INPUT_CHANNEL_SLOTS");
        }

        size_t actual_buffer_size = (channel_size == 0) ?
                cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : channel_size;

        if (channel_size > 0 && channel_size * sizeof(T) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Channel size too small for doubly-mapped buffers");
        }

        in = reinterpret_cast<cler::Channel<T>*>(_in_storage);
        for (size_t i = 0; i < _num_channels; ++i) {
            new (&in[i]) cler::Channel<T>(actual_buffer_size);
        }

        _usrp = uhd::usrp::multi_usrp::make(_device_address);
        if (!_usrp) {
            cler::panic(("SinkUHDBlock: Failed to create USRP device with args: " + _device_address).c_str());
        }

        uhd::set_thread_priority_safe(0.5, true);

        // CPU format: what the host sees (fc32, sc16, sc8); OTW format: what goes over the wire
        uhd::stream_args_t stream_args(get_uhd_format<T>(), _wire_format);
        stream_args.channels.resize(_num_channels);
        std::iota(stream_args.channels.begin(), stream_args.channels.end(), 0);

        _tx_stream = _usrp->get_tx_stream(stream_args);
        if (!_tx_stream) {
            cler::panic("SinkUHDBlock: Failed to setup TX stream");
        }

        _uhd_buffs.resize(_num_channels);
        _read_ptrs.resize(_num_channels);
        _read_sizes.resize(_num_channels);

        UHDConfig config_to_use;
        if (initial_config) {
            config_to_use = *initial_config;
            std::cout << "  Using provided initial configuration" << std::endl;
        } else {
            std::cout << "  Using default configuration:" << std::endl;
        }
        for (size_t ch = 0; ch < _num_channels; ++ch) {
            if (!configure(config_to_use, ch)) {
                cler::panic(("Failed to configure channel " + std::to_string(ch)).c_str());
            }
        }
        std::cout << "SinkUHDBlock: Initialized "
                  << _usrp->get_mboard_name() << " / " << _usrp->get_pp_string()
                  << std::endl;
        std::cout << "  Channels: " << _num_channels << std::endl;
        std::cout << "  Frequency: " << config_to_use.center_freq_Hz/1e6 << " MHz (all channels)" << std::endl;
        std::cout << "  Sample rate: " << config_to_use.sample_rate_Hz/1e6 << " MSPS (all channels)" << std::endl;
        std::cout << "  Gain: " << config_to_use.gain << " dB (all channels)" << std::endl;
        std::cout << "  Format: CPU=" << get_uhd_format<T>() << ", OTW=" << _wire_format << std::endl;
    }

    ~SinkUHDBlock() {
        using TChannel = cler::Channel<T>;
        for (size_t i = 0; i < _num_channels; ++i) {
            in[i].~TChannel();
        }
        if (underflow_count > 0) {
            std::cout << "SinkUHDBlock: Total underflows: " << underflow_count << std::endl;
        }
    }

    bool configure(const UHDConfig& config, size_t channel = 0) {
    _configuring = true;
        try {
            _usrp->set_tx_rate(config.sample_rate_Hz, channel);
            double actual_rate = _usrp->get_tx_rate(channel);
            if (std::abs(actual_rate - config.sample_rate_Hz) > 1.0) {
                std::cout << "Warning: Requested " << config.sample_rate_Hz/1e6
                          << " MSPS, got " << actual_rate/1e6 << " MSPS" << std::endl;
            }

            auto freq_range = _usrp->get_tx_freq_range(channel);
            if (config.center_freq_Hz < freq_range.start() ||
                config.center_freq_Hz > freq_range.stop()) {
                std::cerr << "Frequency " << config.center_freq_Hz/1e6
                          << " MHz out of range" << std::endl;
            }
            _usrp->set_tx_freq(uhd::tune_request_t(config.center_freq_Hz), channel);

            auto gain_range = _usrp->get_tx_gain_range(channel);
            if (config.gain < gain_range.start() || config.gain > gain_range.stop()) {
                std::cerr << "Gain " << config.gain << " dB out of range" << std::endl;
            }
            _usrp->set_tx_gain(config.gain, channel);

            if (config.bandwidth_Hz > 0) {
                _usrp->set_tx_bandwidth(config.bandwidth_Hz, channel);
            }
            _configuring = false;
            return true;

        } catch (const std::exception& e) {
            std::cerr << "Configuration failed: " << e.what() << std::endl;
            _configuring = false;
            return false;
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        if (_configuring.load(std::memory_order_acquire)) {
            return cler::Error::NotEnoughSamples;
        }
        const size_t min_samples = (cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T)) / 4;
        for (size_t i = 0; i < _num_channels; ++i) {
            auto [ptr, size] = in[i].read_dbf();
            _read_ptrs[i] = ptr;
            _read_sizes[i] = size;

            if (_read_sizes[i] < min_samples) {
                return cler::Error::NotEnoughSamples;
            }
        }

        size_t aligned = _read_sizes[0];
        for (size_t i = 1; i < _num_channels; ++i) {
            aligned = std::min(aligned, _read_sizes[i]);
        }

        size_t sent_min = aligned;
        for (size_t i = 0; i < _num_channels; ++i) {
            uhd::tx_metadata_t md;
            md.start_of_burst = false;
            md.end_of_burst = false;
            md.has_time_spec = false;

            size_t sent = 0;
            try {
                sent = _tx_stream->send(_read_ptrs[i], aligned, md, 0.1);
            } catch (const std::exception& e) {
                std::cerr << "SinkUHDBlock: send failed: " << e.what() << std::endl;
                return cler::Error::TERM_ProcedureError;
            }
            sent_min = std::min(sent_min, sent);
        }

        if (sent_min == 0) {
            return cler::Error::NotEnoughSpace;
        }
        for (size_t i = 0; i < _num_channels; ++i) {
            in[i].commit_read(sent_min);
        }

        handle_async_events();

        return cler::Empty{};
    }

    bool poll_async_event(AsyncTxEvent& event, double timeout = 0.0) {
        uhd::async_metadata_t async_md;
        if (_tx_stream->recv_async_msg(async_md, timeout)) {
            event.event_occurred = true;
            event.event_code = async_md.event_code;
            event.time_seconds = async_md.time_spec.get_full_secs();
            event.time_frac_seconds = async_md.time_spec.get_frac_secs();
            return true;
        }
        event.event_occurred = false;
        return false;
    }

    size_t get_underflow_count() const { return underflow_count; }
    void reset_underflow_count() { underflow_count = 0; }

    void sync_all_devices() {
        std::cout << "Synchronizing USRP devices..." << std::endl;
        auto last_pps = _usrp->get_time_last_pps();
        while (last_pps == _usrp->get_time_last_pps()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        _usrp->set_time_next_pps(uhd::time_spec_t(0.0));
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "USRP devices synchronized at t=0" << std::endl;
    }

private:
    void handle_async_events() {
        uhd::async_metadata_t async_md;
        while (_tx_stream->recv_async_msg(async_md, 0.0)) {
            switch(async_md.event_code) {
                case uhd::async_metadata_t::EVENT_CODE_UNDERFLOW:
                case uhd::async_metadata_t::EVENT_CODE_UNDERFLOW_IN_PACKET:
                    underflow_count++;
                    if (underflow_count % 100 == 0) {
                        std::cerr << "SinkUHDBlock: Underflow count: " << underflow_count << std::endl;
                    }
                    break;

                case uhd::async_metadata_t::EVENT_CODE_TIME_ERROR:
                    std::cerr << "SinkUHDBlock: Time error - tried to send in the past" << std::endl;
                    break;

                case uhd::async_metadata_t::EVENT_CODE_SEQ_ERROR:
                case uhd::async_metadata_t::EVENT_CODE_SEQ_ERROR_IN_BURST:
                    std::cerr << "SinkUHDBlock: Sequence error" << std::endl;
                    break;

                case uhd::async_metadata_t::EVENT_CODE_BURST_ACK:
                    break;

                default:
                    break;
            }
        }
    }

    std::aligned_storage_t<sizeof(cler::Channel<T>), alignof(cler::Channel<T>)> _in_storage[MAX_INPUT_CHANNEL_SLOTS];

    uhd::usrp::multi_usrp::sptr _usrp;
    uhd::tx_streamer::sptr _tx_stream;

    UHDConfig _current_config;
    std::string _device_address;
    size_t _num_channels;
    std::string _wire_format;
    std::atomic<bool> _configuring;

    std::vector<void*> _uhd_buffs;      // Buffer pointers for UHD multi-channel send()
    std::vector<const T*> _read_ptrs;   // Temp storage for read_dbf pointers
    std::vector<size_t> _read_sizes;    // Temp storage for read_dbf sizes

    size_t underflow_count = 0;
};

// UHD operates on I/Q pairs - scalar types are not supported
using SinkUHDBlockCF32 = SinkUHDBlock<std::complex<float>>;
using SinkUHDBlockSC16 = SinkUHDBlock<std::complex<int16_t>>;
using SinkUHDBlockSC8 = SinkUHDBlock<std::complex<int8_t>>;
`,Wn=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <string>
#include <cstring>

#ifdef __has_include
    #if __has_include(<libavformat/avformat.h>)
        extern "C" {
            #include <libavformat/avformat.h>
            #include <libavcodec/avcodec.h>
            #include <libswresample/swresample.h>
            #include <libavutil/error.h>
        }
    #else
        #error "FFmpeg headers not found. Please install libavformat-dev, libavcodec-dev, libswresample-dev packages."
    #endif
#else
    extern "C" {
        #include <libavformat/avformat.h>
        #include <libavcodec/avcodec.h>
        #include <libswresample/swresample.h>
        #include <libavutil/error.h>
    }
#endif

inline void ffmpeg_check(int err, const char* context) {
    if (err < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(err, errbuf, sizeof(errbuf));
        char msg[AV_ERROR_MAX_STRING_SIZE + 128];
        std::snprintf(msg, sizeof(msg), "%s: %s", context, errbuf);
        cler::panic(msg);
    }
}

template <typename T = float>
struct SourceAudioFileBlock : public cler::BlockBase {
    static constexpr bool may_block = true;
    typedef void (*on_eof)(const char* filename);

    SourceAudioFileBlock(const char* name,
                        const char* filename,
                        uint32_t output_sample_rate = 48000,
                        bool repeat = true,
                        on_eof callback = nullptr)
        : cler::BlockBase(name),
          _filename(filename),
          _output_sample_rate(output_sample_rate),
          _repeat(repeat),
          _callback(callback),
          _format_ctx(nullptr),
          _codec_ctx(nullptr),
          _resampler(nullptr),
          _frame(nullptr),
          _audio_stream_idx(-1),
          _eof_reached(false)
    {
        _open_audio_file();
    }

    ~SourceAudioFileBlock() {
        _close_audio_file();
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        if (_format_ctx == nullptr || _codec_ctx == nullptr) {
            return cler::Error::TERM_IOError;
        }

        auto [write_ptr, write_size] = out->write_dbf();
        if (write_ptr == nullptr || write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t samples_written = 0;
        int ret = 0;

        while (samples_written < write_size) {
            ret = av_read_frame(_format_ctx, &_packet);

            if (ret == AVERROR_EOF) {
                if (_repeat) {
                    av_seek_frame(_format_ctx, _audio_stream_idx, 0, AVSEEK_FLAG_BACKWARD);
                    avcodec_flush_buffers(_codec_ctx);
                    continue;
                } else {
                    _eof_reached = true;
                    if (_callback) {
                        _callback(_filename.c_str());
                    }
                    break;
                }
            } else if (ret < 0) {
                return cler::Error::TERM_IOError;
            }

            if (_packet.stream_index != _audio_stream_idx) {
                av_packet_unref(&_packet);
                continue;
            }

            ret = avcodec_send_packet(_codec_ctx, &_packet);
            av_packet_unref(&_packet);

            if (ret < 0) {
                return cler::Error::TERM_IOError;
            }

            while (avcodec_receive_frame(_codec_ctx, _frame) == 0) {
                uint8_t* out_buf = reinterpret_cast<uint8_t*>(write_ptr) + samples_written * sizeof(T);
                uint8_t* out_bufs[] = {out_buf};

                int frame_count = swr_convert(
                    _resampler,
                    out_bufs,
                    write_size - samples_written,
                    (const uint8_t**)_frame->data,
                    _frame->nb_samples
                );

                if (frame_count < 0) {
                    return cler::Error::TERM_IOError;
                }

                samples_written += frame_count;

                if (samples_written >= write_size) {
                    break;
                }
            }
        }

        if (samples_written == 0) {
            return cler::Error::NotEnoughSamples;
        }
        out->commit_write(samples_written);

        return cler::Empty{};
    }

private:
    std::string _filename;
    uint32_t _output_sample_rate;
    bool _repeat;
    on_eof _callback;
    AVFormatContext* _format_ctx;
    AVCodecContext* _codec_ctx;
    SwrContext* _resampler;
    AVFrame* _frame;
    AVPacket _packet = {};  // Zero-init (no av_init_packet needed)
    int _audio_stream_idx;
    bool _eof_reached;

    void _open_audio_file() {
        int ret = avformat_open_input(&_format_ctx, _filename.c_str(), nullptr, nullptr);
        ffmpeg_check(ret, "Failed to open audio file");

        ret = avformat_find_stream_info(_format_ctx, nullptr);
        ffmpeg_check(ret, "Failed to find stream info");

        _audio_stream_idx = av_find_best_stream(_format_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (_audio_stream_idx < 0) {
            cler::panic("No audio stream found in file");
        }

        AVStream* stream = _format_ctx->streams[_audio_stream_idx];
        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) {
            cler::panic("Unsupported audio codec");
        }

        _codec_ctx = avcodec_alloc_context3(codec);
        if (!_codec_ctx) {
            cler::panic("Failed to allocate codec context");
        }

        avcodec_parameters_to_context(_codec_ctx, stream->codecpar);
        ret = avcodec_open2(_codec_ctx, codec, nullptr);
        ffmpeg_check(ret, "Failed to open codec");

        // FFmpeg 5.0+ (libavcodec 59+) uses AVChannelLayout; 4.x uses channel_layout/channels
        #if LIBAVCODEC_VERSION_MAJOR >= 59
            int64_t input_ch_layout = _codec_ctx->ch_layout.nb_channels > 1
                ? AV_CH_LAYOUT_STEREO
                : AV_CH_LAYOUT_MONO;
        #else
            int64_t input_ch_layout = _codec_ctx->channel_layout;
            if (input_ch_layout == 0) {
                input_ch_layout = _codec_ctx->channels > 1
                    ? AV_CH_LAYOUT_STEREO
                    : AV_CH_LAYOUT_MONO;
            }
        #endif

        _resampler = swr_alloc_set_opts(
            nullptr,
            AV_CH_LAYOUT_MONO,
            AV_SAMPLE_FMT_FLT,
            _output_sample_rate,
            input_ch_layout,
            _codec_ctx->sample_fmt,
            _codec_ctx->sample_rate,
            0,
            nullptr
        );

        if (!_resampler) {
            cler::panic("Failed to allocate resampler");
        }

        ret = swr_init(_resampler);
        ffmpeg_check(ret, "Failed to initialize resampler");

        _frame = av_frame_alloc();
        if (!_frame) {
            cler::panic("Failed to allocate frame");
        }
    }

    void _close_audio_file() {
        if (_frame) {
            av_frame_free(&_frame);
            _frame = nullptr;
        }

        if (_resampler) {
            swr_free(&_resampler);
            _resampler = nullptr;
        }

        if (_codec_ctx) {
            avcodec_free_context(&_codec_ctx);
            _codec_ctx = nullptr;
        }

        if (_format_ctx) {
            avformat_close_input(&_format_ctx);
            _format_ctx = nullptr;
        }
    }
};
`,Zn=`#pragma once
#include <CaribouLite.hpp>
#include "cler.hpp"
#include "cler_desktop_utils.hpp"

#include <unistd.h>

inline bool detect_cariboulite_board()
{
    CaribouLite::SysVersion ver;
    std::string name;
    std::string guid;

    if (CaribouLite::DetectBoard(&ver, name, guid))
    {
        std::cout << "Detected Version: " << CaribouLite::GetSystemVersionStr(ver) 
                                          << ", Name: " << name 
                                          << ", GUID: " << guid 
                                          << std::endl;
        return true;
    }
    return false;
}

template <typename T>
struct SourceCaribouliteBlock : public cler::BlockBase {
    static_assert(std::is_same_v<T, std::complex<short>> || std::is_same_v<T, std::complex<float>>,
            "SourceCaribouliteBlock only supports std::complex<short> or std::complex<float>");
    static constexpr bool may_block = true;

    SourceCaribouliteBlock(const char* name,
        CaribouLiteRadio::RadioType radio_type,
        float freq_hz,
        float samp_rate_hz,
        bool agc,
        float rx_gain_db = 0.0f,
        float bw_hz      = 0.0f
        ) : cler::BlockBase(name) {
            bool freq_valid = false;

            if (!detect_cariboulite_board()) {
                cler::panic("CaribouLite board not detected!");
            }

            CaribouLite& cl = CaribouLite::GetInstance(false);
            _radio = cl.GetRadioChannel(radio_type);
            if (!_radio) {
                cler::panic("Failed to get radio channel for selected radio type");
            }

            std::vector<CaribouLiteFreqRange> ranges = _radio->GetFrequencyRange();
            for (const auto& range : ranges) {
                if (freq_hz > range.fmin() && freq_hz < range.fmax()) {
                    freq_valid = true;
                }
            }
            if (!freq_valid) {
                cler::panic("Frequency is out of range for the selected radio type.");
            }

            if (samp_rate_hz > _radio->GetRxSampleRateMax() || samp_rate_hz < _radio->GetRxSampleRateMin()) {
                char msg[160];
                std::snprintf(msg, sizeof(msg),
                    "samp_rate_hz must be between %f and %f Hz, but got %f",
                    _radio->GetRxSampleRateMin(), _radio->GetRxSampleRateMax(), samp_rate_hz);
                cler::panic(msg);
            }

            _max_samples_to_read = _radio->GetNativeMtuSample();

            if (bw_hz > 0.0f &&
                (bw_hz > _radio->GetRxBandwidthMax() || bw_hz < _radio->GetRxBandwidthMin())) {
                char msg[160];
                std::snprintf(msg, sizeof(msg),
                    "bw_hz must be between %f and %f Hz, but got %f",
                    _radio->GetRxBandwidthMin(), _radio->GetRxBandwidthMax(), bw_hz);
                cler::panic(msg);
            }

            _radio->SetFrequency(freq_hz);
            _radio->SetRxSampleRate(samp_rate_hz);
            if (bw_hz > 0.0f) {
                _radio->SetRxBandwidth(bw_hz);
            }
            _radio->SetAgc(agc);
            if (!agc) {_radio->SetRxGain(rx_gain_db);}

            _radio->StartReceiving();
        }

        ~SourceCaribouliteBlock() {
            if (_radio) {
                _radio->StopReceiving();
            }            
        }

        static bool can_open() {
            // libcariboulite exits the process when it cannot mmap the GPIO,
            // which a caller probing for a device cannot survive, so check the
            // access it needs before handing control over.
            if (::access("/dev/gpiomem", R_OK | W_OK) != 0) return false;
            CaribouLite::SysVersion ver;
            std::string name, guid;
            return CaribouLite::DetectBoard(&ver, name, guid);
        }

        bool in_range(float hz) {
            for (const auto& r : _radio->GetFrequencyRange()) if (hz > r.fmin() && hz < r.fmax()) return true;
            return false;
        }
        void set_frequency(float hz) { if (in_range(hz)) _radio->SetFrequency(hz); }
        void set_rx_gain(float db) { _radio->SetRxGain(db); }
        void set_agc(bool on) { _radio->SetAgc(on); }
        void set_bandwidth(float hz) { _radio->SetRxBandwidth(hz); }
        float get_frequency() { return _radio->GetFrequency(); }
        float get_sample_rate() { return _radio->GetRxSampleRate(); }
        float get_rx_gain() { return _radio->GetRxGain(); }
        bool get_agc() { return _radio->GetAgc(); }
        float get_bandwidth() { return _radio->GetRxBandwidth(); }
        CaribouLiteRadio& radio() { return *_radio; }
        bool lost() const { return _lost; }

        cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
            auto [ptr, space] = out->write_dbf();
            if (ptr == nullptr || space == 0) {
                return cler::Error::NotEnoughSpace;
            }

            size_t to_read = std::min(space, _max_samples_to_read);
            int ret = _radio->ReadSamples(ptr, to_read);
            if (ret < 0) {
                _lost = true;
                return cler::Error::ProcedureError;
            }
            if (ret == 0) {
                return cler::Error::NotEnoughSamples;
            }
            out->commit_write(ret);
            return cler::Empty{};
        }

        private:    
            CaribouLiteRadio* _radio = nullptr;
            size_t _max_samples_to_read;
            bool _lost = false;
};
`,$n=`#pragma once
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <cmath>
#include <type_traits>
#include <complex>

template <typename T>
struct SourceChirpBlock : public cler::BlockBase {
    static_assert(std::is_same_v<T, float> || std::is_same_v<T, std::complex<float>>,
                  "SourceChirpBlock only supports float or std::complex<float>");

    SourceChirpBlock(const char* name,
                    float amplitude,
                    float f0_hz,
                    float f1_hz,
                    size_t sps,
                    float chirp_duration_s)
        : cler::BlockBase(name),
          _amplitude(amplitude),
          _f0_hz(f0_hz),
          _f1_hz(f1_hz),
          _sps(sps),
          _chirp_duration_s(chirp_duration_s)
    {
        if (_sps == 0) cler::panic("Sample rate must be greater than zero.");
        if (_chirp_duration_s <= 0) cler::panic("Chirp duration must be positive.");

        _n_samples_before_reset = static_cast<size_t>(_chirp_duration_s * _sps);
        _k = (_f1_hz - _f0_hz) / _chirp_duration_s; // Hz/s

        const double dt = 1.0 / static_cast<double>(_sps);
        const double w0 = 2.0 * cler::PI * static_cast<double>(_f0_hz) * dt;

        _psi = std::polar(1.0, w0);
        _psi_inc = std::polar(1.0, 2.0 * cler::PI * static_cast<double>(_k) * dt * dt); // second-difference recursion for phase acceleration

        _phasor = std::complex<double>(1.0, 0.0);
    }

    ~SourceChirpBlock() = default;

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        auto [write_ptr, write_size] = out->write_dbf();
        if (write_ptr == nullptr || write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }

        for (size_t i = 0; i < write_size; ++i) {
            std::complex<float> chirp(static_cast<float>(_phasor.real()),
                                      static_cast<float>(_phasor.imag()));

            if constexpr (std::is_same_v<T, std::complex<float>>) {
                write_ptr[i] = _amplitude * chirp;
            } else {
                write_ptr[i] = _amplitude * chirp.real();
            }

            _phasor *= _psi;
            _phasor /= std::abs(_phasor); // renormalize to unit circle each step, required for numerical stability
            _psi *= _psi_inc;

            ++_samples_counter;
            if (_samples_counter >= _n_samples_before_reset) {
                reset();
            }
        }

        out->commit_write(write_size);
        return cler::Empty{};
    }

private:
    void reset() {
        _samples_counter = 0;
        _phasor = std::complex<double>(1.0, 0.0);
        const double dt = 1.0 / static_cast<double>(_sps);
        const double w0 = 2.0 * cler::PI * static_cast<double>(_f0_hz) * dt;
        _psi = std::polar(1.0, w0);
        _psi_inc = std::polar(1.0, 2.0 * cler::PI * static_cast<double>(_k) * dt * dt);
    }

    float _amplitude;
    float _f0_hz;
    float _f1_hz;
    size_t _sps;
    float _chirp_duration_s;

    size_t _n_samples_before_reset;
    float _k;              // sweep rate, Hz/s
    size_t _samples_counter = 0;

    std::complex<double> _phasor;
    std::complex<double> _psi;
    std::complex<double> _psi_inc;
};
`,Qn=`#pragma once
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <cmath>
#include <type_traits>
#include <complex>

template <typename T>
struct SourceCWBlock : public cler::BlockBase {
    static_assert(std::is_same_v<T, float> || std::is_same_v<T, std::complex<float>>,
                  "SourceCWBlock only supports float or std::complex<float>");

    SourceCWBlock(const char* name,
                  float amplitude,
                  float frequency_hz,
                  size_t sps)
        : cler::BlockBase(name),
          _amplitude(amplitude),
          _frequency_hz(frequency_hz),
          _sps(sps)
    {
        if (_sps == 0) {
            cler::panic("Sample rate must be greater than zero.");
        }

        double phase_increment =
            2.0 * cler::PI * static_cast<double>(_frequency_hz) / static_cast<double>(_sps);

        _phasor = std::complex<double>(1.0, 0.0);
        _phasor_inc = std::polar(1.0, phase_increment);

        _buffer_size = cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T);
        _buffer = new T[_buffer_size];
    }

    ~SourceCWBlock() {
        delete[] _buffer;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        size_t to_generate = std::min(out->space(), _buffer_size);
        if (to_generate == 0) {
            return cler::Error::NotEnoughSpace;
        }

        for (size_t i = 0; i < to_generate; ++i) {
            std::complex<float> cw(static_cast<float>(_phasor.real()),
                                   static_cast<float>(_phasor.imag()));

            if constexpr (std::is_same_v<T, std::complex<float>>) {
                _buffer[i] = _amplitude * cw;
            } else {
                _buffer[i] = _amplitude * cw.real();
            }

            _phasor *= _phasor_inc;
            _phasor /= std::abs(_phasor); // renormalize to unit circle each step, required for numerical stability
        }

        out->writeN(_buffer, to_generate);
        return cler::Empty{};
    }

private:
    float _amplitude;
    float _frequency_hz;
    size_t _sps;

    std::complex<double> _phasor = {1.0, 0.0};
    std::complex<double> _phasor_inc = {1.0, 0.0};

    T* _buffer;
    size_t _buffer_size;
};
`,Jn=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <fstream>
#include <string>

template <typename T>
struct SourceFileBlock : public cler::BlockBase {
    static constexpr bool may_block = true;
    typedef void (*on_eof)(const char* filename);

    SourceFileBlock(const char* name, const char* filename, const bool repeat = true, on_eof callback = nullptr)
        : cler::BlockBase(name),
          _filename(filename),
          _repeat(repeat),
          _callback(callback)
    {
        _file.open(_filename, std::ios::binary);
        if (!_file.is_open()) {
            std::string msg = "Failed to open file: " + std::string(filename);
            cler::panic(msg.c_str());
        }
    }

    ~SourceFileBlock() {
        if (_file.is_open()) {
            _file.close();
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out)
    {
        if (!_file.is_open()) {
            return cler::Error::TERM_IOError;
        }

        auto [write_ptr, write_size] = out->write_dbf();
        if (write_ptr == nullptr || write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }

        _file.read(reinterpret_cast<char*>(write_ptr), write_size * sizeof(T));
        size_t samples_read = _file.gcount() / sizeof(T);
        
        if (samples_read == 0) {
            if (_file.eof() && _repeat) {
                _file.clear();
                _file.seekg(0, std::ios::beg);
                return cler::Error::NotEnoughSamples;
            } else {
                if (_callback) {
                    _callback(_filename.c_str());
                }
                if (_file.is_open()) {_file.close();}
                return cler::Error::NotEnoughSamples;
            }
        }
        
        out->commit_write(samples_read);
        return cler::Empty{};
    }

private:
    std::string _filename;
    bool _repeat;
    on_eof _callback;
    std::ifstream _file;
};
`,et=`#pragma once
#include "cler.hpp"
#include "cler_desktop_utils.hpp"

#ifdef __has_include
    #if __has_include(<libhackrf/hackrf.h>)
        #include <libhackrf/hackrf.h>
    #elif __has_include(<hackrf.h>)
        #include <hackrf.h>
    #else
        #error "HackRF header not found. Please install libhackrf or hackrf-dev package."
    #endif
#endif

#include <atomic>
#include <cstring>

struct SourceHackRFBlock : public cler::BlockBase {
    static constexpr size_t USB_TRANSFER_SAMPLES = 131072;
    static constexpr size_t DEFAULT_RING_SAMPLES = 4 * USB_TRANSFER_SAMPLES;

    SourceHackRFBlock(const char* name,
                      uint64_t freq_hz,
                      uint32_t samp_rate_hz,
                      int lna_gain_db = 40,  // 0-40 dB, multiple of 8
                      int vga_gain_db = 16,  // 0-62 dB, multiple of 2
                      bool amp_enable = false, // Enable RX amp (adds ~14dB)
                      size_t buffer_size = 0,
                      const char* serial = nullptr)  // nullptr = first device
        : cler::BlockBase(name),
          _iq(buffer_size == 0 ? DEFAULT_RING_SAMPLES : buffer_size),
          _freq_hz(freq_hz),
          _samp_rate_hz(samp_rate_hz),
          _lna_gain_db(lna_gain_db),
          _vga_gain_db(vga_gain_db),
          _amp_enable(amp_enable)
    {
        if (buffer_size > 0 && buffer_size * sizeof(std::complex<float>) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            char msg[160];
            std::snprintf(msg, sizeof(msg),
                "Buffer size too small for doubly-mapped buffers. Need at least %zu complex<float> elements",
                cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>));
            cler::panic(msg);
        }

        if (hackrf_init() != HACKRF_SUCCESS) {
            cler::panic("Failed to initialize HackRF library.");
        }

        const int opened = serial && *serial ? hackrf_open_by_serial(serial, &_dev) : hackrf_open(&_dev);
        if (opened != HACKRF_SUCCESS) {
            cler::panic("Failed to open HackRF device.");
        }

        if (hackrf_set_freq(_dev, freq_hz) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            cler::panic("Failed to set frequency.");
        }

        if (hackrf_set_sample_rate(_dev, samp_rate_hz) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            cler::panic("Failed to set sample rate.");
        }

        if (hackrf_set_lna_gain(_dev, lna_gain_db) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            cler::panic("Failed to set LNA gain.");
        }

        if (hackrf_set_vga_gain(_dev, vga_gain_db) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            cler::panic("Failed to set VGA gain.");
        }

        if (hackrf_set_amp_enable(_dev, amp_enable ? 1 : 0) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            cler::panic("Failed to set amp enable.");
        }

        if (hackrf_start_rx(_dev, rx_callback, this) != HACKRF_SUCCESS) {
            hackrf_close(_dev);
            _dev = nullptr;
            cler::panic("Failed to start RX streaming.");
        }

        std::cout << "SourceHackRFBlock: Initialized" << std::endl;
        std::cout << "  Frequency: " << freq_hz / 1e6 << " MHz" << std::endl;
        std::cout << "  Sample rate: " << samp_rate_hz / 1e6 << " MSPS" << std::endl;
        std::cout << "  LNA gain: " << lna_gain_db << " dB" << std::endl;
        std::cout << "  VGA gain: " << vga_gain_db << " dB" << std::endl;
        std::cout << "  Amp enabled: " << (amp_enable ? "Yes" : "No") << std::endl;
    }

    ~SourceHackRFBlock() {
        if (_dev) {
            hackrf_stop_rx(_dev);
            hackrf_close(_dev);
        }
        hackrf_exit();

        if (_overflow_count > 0) {
            std::cout << "SourceHackRFBlock: Total overflows: " << _overflow_count << std::endl;
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        auto [read_ptr, read_size] = _iq.read_dbf();
        if (read_ptr == nullptr || read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        auto [write_ptr, write_size] = out->write_dbf();
        if (write_ptr == nullptr || write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }
        
        size_t to_copy = std::min(read_size, write_size);
        std::memcpy(write_ptr, read_ptr, to_copy * sizeof(std::complex<float>));
        _iq.commit_read(to_copy);
        out->commit_write(to_copy);
        return cler::Empty{};
    }

    // Opens and closes the device without streaming, so a caller that cannot
    // afford the constructor's panic (busy, unplugged) can ask first.
    static int open_status(const char* serial = nullptr) {
        if (hackrf_init() != HACKRF_SUCCESS) return HACKRF_ERROR_LIBUSB;
        hackrf_device* dev = nullptr;
        const int r = serial && *serial ? hackrf_open_by_serial(serial, &dev) : hackrf_open(&dev);
        if (r == HACKRF_SUCCESS) hackrf_close(dev);
        hackrf_exit();
        return r;
    }

    static bool can_open(const char* serial = nullptr) { return open_status(serial) == HACKRF_SUCCESS; }

    uint64_t get_frequency() const { return _freq_hz; }
    uint32_t get_sample_rate() const { return _samp_rate_hz; }
    int get_lna_gain() const { return _lna_gain_db; }
    int get_vga_gain() const { return _vga_gain_db; }
    bool get_amp_enable() const { return _amp_enable; }
    size_t get_overflow_count() const { return _overflow_count.load(); }
    void reset_overflow_count() { _overflow_count.store(0); }
    bool lost() const { return _dev == nullptr || hackrf_is_streaming(_dev) != HACKRF_TRUE; }

    // Setters (be careful - changing these while streaming may cause issues)
    void set_frequency(uint64_t freq_hz) {
        if (_dev && hackrf_set_freq(_dev, freq_hz) == HACKRF_SUCCESS) {
            _freq_hz = freq_hz;
        }
    }

    // Live sample-rate change. hackrf_set_sample_rate reconfigures the sample
    // clock, which is not safe while the RX callback is delivering buffers at
    // the old rate, so we stop RX, apply the new rate, then restart streaming.
    // A few in-flight samples straddling the switch may be garbled once (same
    // caveat as the UHD path). The baseband filter is left at libhackrf's
    // default (as in the constructor), not re-derived here.
    void set_sample_rate(uint32_t samp_rate_hz) {
        if (!_dev || samp_rate_hz == 0 || samp_rate_hz == _samp_rate_hz) {
            return;
        }
        hackrf_stop_rx(_dev);
        if (hackrf_set_sample_rate(_dev, samp_rate_hz) == HACKRF_SUCCESS) {
            _samp_rate_hz = samp_rate_hz;
        } else {
            std::cerr << "SourceHackRFBlock: Failed to set sample rate" << std::endl;
        }
        if (hackrf_start_rx(_dev, rx_callback, this) != HACKRF_SUCCESS) {
            std::cerr << "SourceHackRFBlock: Failed to restart RX after rate change"
                      << std::endl;
        }
    }

    void set_lna_gain(int gain_db) {
        if (_dev && hackrf_set_lna_gain(_dev, gain_db) == HACKRF_SUCCESS) {
            _lna_gain_db = gain_db;
        }
    }

    void set_vga_gain(int gain_db) {
        if (_dev && hackrf_set_vga_gain(_dev, gain_db) == HACKRF_SUCCESS) {
            _vga_gain_db = gain_db;
        }
    }

    void set_amp_enable(bool enable) {
        if (_dev && hackrf_set_amp_enable(_dev, enable ? 1 : 0) == HACKRF_SUCCESS) {
            _amp_enable = enable;
        }
    }

private:
    hackrf_device* _dev = nullptr;
    cler::Channel<std::complex<float>> _iq;
    
    uint64_t _freq_hz;
    uint32_t _samp_rate_hz;
    int _lna_gain_db;
    int _vga_gain_db;
    bool _amp_enable;

    std::atomic<size_t> _overflow_count{0};

    static int rx_callback(hackrf_transfer* transfer) {
        SourceHackRFBlock* self = static_cast<SourceHackRFBlock*>(transfer->rx_ctx);
        const uint8_t* buf = transfer->buffer;

        // HackRF wire format: interleaved signed int8_t IQ, range [-128, 127]
        for (int i = 0; i < transfer->valid_length; i += 2) {
            float i_sample = static_cast<int8_t>(buf[i]) / 128.0f;
            float q_sample = static_cast<int8_t>(buf[i + 1]) / 128.0f;

            std::complex<float> sample(i_sample, q_sample);

            if (!self->_iq.try_push(sample)) {
                self->_overflow_count++;
            }
        }

        return 0;
    }
};`,nt=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"

#include <algorithm>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Signed 8-bit interleaved IQ capture (hackrf_transfer -r) replayed in a loop.
struct SourceIQFileBlock : public cler::BlockBase {
    static constexpr bool may_block = true;

    SourceIQFileBlock(const char* name, const std::string& path, size_t chunk = 1 << 16)
        : cler::BlockBase(name), _raw(2 * chunk) {
        _f = std::fopen(path.c_str(), "rb");
        if (!_f) cler::panic("SourceIQFileBlock: cannot open file");
    }
    ~SourceIQFileBlock() { if (_f) std::fclose(_f); }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        auto [wptr, wsize] = out->write_dbf();
        size_t n = std::min(wsize, _raw.size() / 2);
        if (n == 0) return cler::Error::NotEnoughSpace;
        size_t got = std::fread(_raw.data(), 2, n, _f);
        if (got == 0) { std::rewind(_f); got = std::fread(_raw.data(), 2, n, _f); }
        if (got == 0) return cler::Error::TERM_IOError;
        for (size_t i = 0; i < got; ++i) wptr[i] = {_raw[2 * i] / 128.0f, _raw[2 * i + 1] / 128.0f};
        out->commit_write(got);
        return cler::Empty{};
    }

private:
    FILE* _f = nullptr;
    std::vector<int8_t> _raw;
};
`,tt=`#pragma once
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/sources/source_sim.hpp"
#include "desktop_blocks/sigmf/source_sigmf.hpp"
#ifdef CLER_HAS_HACKRF
#include "desktop_blocks/sources/source_hackrf.hpp"
#endif
#ifdef CLER_HAS_CARIBOULITE
#include "desktop_blocks/sources/source_cariboulite.hpp"
#endif
#ifdef CLER_HAS_LIBIIO
#include "desktop_blocks/sources/source_pluto.hpp"
#endif
#ifdef CLER_HAS_UHD
#include "desktop_blocks/sources/source_uhd.hpp"
#endif
#ifdef CLER_HAS_SOAPYSDR
#include "desktop_blocks/sources/source_soapysdr.hpp"
#endif
#include <unistd.h>

#include <algorithm>

#include <complex>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

// One source block over every SDR backend compiled in, plus a simulator, so an
// app can list what is plugged in and switch between them while its graph is
// stopped. select() and the describe/set calls are control-path (allocate
// freely); procedure() only forwards to the active backend.
struct SourceMux : public cler::BlockBase {
    static constexpr bool may_block = true;

    enum class Kind { None, HackRF, Pluto, UHD, Cariboulite, Soapy, SigMF, Sim };

    struct DeviceInfo {
        Kind kind;
        std::string id;     // serial / address / file; what select() takes back
        std::string label;  // what the user sees
    };

    struct Control {
        std::string id, label, type, unit;   // type: range | enum | bool
        double min = 0, max = 0, step = 0, value = 0;
        std::vector<std::string> options;
        bool ro = false;
    };

    explicit SourceMux(const char* name) : cler::BlockBase(name) {}

    void set_sigmf_dir(const std::string& dir) { _sigmf_dirs = {dir}; }
    void set_sigmf_dirs(const std::vector<std::string>& dirs) { _sigmf_dirs = dirs; }
    // first directory that actually holds the capture, so a recording written to
    // any of them plays back; falls back to the first so a miss reads sensibly
    std::string sigmf_path(const std::string& name) const {
        std::error_code ec;
        for (const auto& d : _sigmf_dirs) {
            const std::string p = d + "/" + name + ".sigmf-meta";
            if (std::filesystem::is_regular_file(p, ec)) return p;
        }
        return (_sigmf_dirs.empty() ? std::string() : _sigmf_dirs.front()) + "/" + name + ".sigmf-meta";
    }
    static bool bare_name(const std::string& n) {
        return !n.empty() && n.find('/') == std::string::npos && n.find("..") == std::string::npos;
    }

    static const char* kind_name(Kind k) {
        switch (k) {
            case Kind::HackRF: return "hackrf";
            case Kind::Pluto: return "pluto";
            case Kind::UHD: return "uhd";
            case Kind::Cariboulite: return "cariboulite";
            case Kind::Soapy: return "soapy";
            case Kind::SigMF: return "sigmf";
            case Kind::Sim: return "sim";
            default: return "none";
        }
    }

    // "hackrf", "pluto:ip:1.2.3.4", "sigmf:capture" -> kind + id, and back.
    static bool parse_id(const std::string& s, Kind& kind, std::string& id) {
        const size_t colon = s.find(':');
        const std::string head = s.substr(0, colon);
        id = colon == std::string::npos ? "" : s.substr(colon + 1);
        for (auto k : {Kind::HackRF, Kind::Pluto, Kind::UHD, Kind::Cariboulite, Kind::Soapy, Kind::SigMF, Kind::Sim}) {
            if (head == kind_name(k)) { kind = k; return true; }
        }
        return false;
    }

    static std::string format_id(Kind kind, const std::string& id) {
        std::string s = kind_name(kind);
        if (!id.empty()) s += ":" + id;
        return s;
    }

    std::vector<DeviceInfo> enumerate() const {
        std::vector<DeviceInfo> out;
#ifdef CLER_HAS_HACKRF
        if (std::holds_alternative<SourceHackRFBlock>(_v)) {
            out.push_back({Kind::HackRF, _id, "HackRF " + short_serial(_id)});
        } else if (hackrf_init() == HACKRF_SUCCESS) {
            if (hackrf_device_list_t* list = hackrf_device_list()) {
                for (int i = 0; i < list->devicecount; ++i) {
                    const std::string serial = list->serial_numbers[i] ? list->serial_numbers[i] : "";
                    out.push_back({Kind::HackRF, serial, "HackRF " + short_serial(serial)});
                }
                hackrf_device_list_free(list);
            }
            hackrf_exit();
        }
#endif
#ifdef CLER_HAS_CARIBOULITE
        if (std::holds_alternative<CBL>(_v) || CBL::can_open()) {
            out.push_back({Kind::Cariboulite, "s1g", "CaribouLite S1G"});
            out.push_back({Kind::Cariboulite, "hif", "CaribouLite HiF"});
        }
#endif
#ifdef CLER_HAS_LIBIIO
        if (std::holds_alternative<Pluto>(_v)) {
            out.push_back({Kind::Pluto, _id, "PlutoSDR " + _id});
        } else if (iio_scan_context* sc = iio_create_scan_context("usb", 0)) {
            iio_context_info** info = nullptr;
            const ssize_t n = iio_scan_context_get_info_list(sc, &info);
            for (ssize_t i = 0; i < n; ++i) {
                const std::string uri = iio_context_info_get_uri(info[i]);
                out.push_back({Kind::Pluto, uri, "PlutoSDR " + uri});
            }
            if (n >= 0) iio_context_info_list_free(info);
            iio_scan_context_destroy(sc);
        }
#endif
#ifdef CLER_HAS_UHD
        if (std::holds_alternative<UHD>(_v)) {
            out.push_back({Kind::UHD, _id, "USRP " + _id});
        } else {
            for (const auto& addr : uhd::device::find(uhd::device_addr_t())) {
                const std::string id = addr.has_key("serial") ? "serial=" + addr["serial"] : addr.to_string();
                out.push_back({Kind::UHD, id, "USRP " + (addr.has_key("product") ? addr["product"] + " " : "") + id});
            }
        }
#endif
#ifdef CLER_HAS_SOAPYSDR
        if (std::holds_alternative<Soapy>(_v)) {
            out.push_back({Kind::Soapy, _id, "Soapy " + _id});
        } else {
            for (const auto& dev : enumerate_devices()) {
                if (native_driver(dev.driver)) continue;
                out.push_back({Kind::Soapy, dev.get_args_string(),
                               dev.label.empty() ? "Soapy " + dev.driver : dev.label});
            }
        }
#endif
        for (const auto& sigmf_dir : _sigmf_dirs) {
            std::error_code ec;
            for (const auto& e : std::filesystem::directory_iterator(sigmf_dir, ec)) {
                if (!e.is_regular_file() || e.path().extension() != ".sigmf-meta") continue;
                const std::string name = e.path().stem().string();
                bool listed = false;   // the first directory holding a name wins, as sigmf_path resolves it
                for (const auto& d : out) listed = listed || (d.kind == Kind::SigMF && d.id == name);
                if (listed) continue;
                std::string label = name;
                std::error_code ec2;
                sigmf::Meta meta;
                if (!sigmf::try_read_meta(e.path().string(), meta)) continue;
                const auto bytes = std::filesystem::file_size(sigmf::data_path(e.path().string()), ec2);
                if (!ec2 && meta.sample_rate > 0) {
                    const double secs = static_cast<double>(bytes) /
                        (sigmf::datatype_size(meta.datatype) * meta.sample_rate);
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), " (%.1f s @ %.3g MS/s)", secs, meta.sample_rate / 1e6);
                    label += buf;
                }
                out.push_back({Kind::SigMF, name, label});
            }
        }
        out.push_back({Kind::Sim, "", "Simulator"});
        return out;
    }

    // The CaribouLite library mmaps these and exits the process when it cannot,
    // so the reason has to come from an access check of our own.
    static std::string cariboulite_access_reason(const char* gpiomem = "/dev/gpiomem",
                                                 const char* spidev = "/dev/spidev1.0") {
        for (const char* p : {gpiomem, spidev}) {
            if (::access(p, R_OK | W_OK) != 0) {
                return "no access to " + std::string(p) + " — add the user running this to the gpio, spi and i2c groups";
            }
        }
        return {};
    }

    // Why this device cannot be opened right now; empty means it can. Never
    // touches the open source. Phrased for whoever has to fix it.
    std::string open_reason(Kind kind, const std::string& id) const {
        switch (kind) {
#ifdef CLER_HAS_HACKRF
            case Kind::HackRF: {
                // a device another process holds comes back as ERROR_LIBUSB, whose
                // name is "Resource busy", so the library's own word is the honest one
                const int r = SourceHackRFBlock::open_status(id.empty() ? nullptr : id.c_str());
                if (r == HACKRF_SUCCESS) return {};
                if (r == HACKRF_ERROR_NOT_FOUND) {
                    return id.empty() ? "no hackrf found" : "no hackrf with serial " + short_serial(id);
                }
                return "hackrf: " + std::string(hackrf_error_name(static_cast<hackrf_error>(r))) +
                       " — another program may have it open, or the udev rules are missing";
            }
#endif
#ifdef CLER_HAS_CARIBOULITE
            case Kind::Cariboulite: {
                if (id != "s1g" && id != "hif") return "unknown cariboulite channel " + id + " — use s1g or hif";
                const std::string access = cariboulite_access_reason();
                if (!access.empty()) return access;
                return CBL::can_open() ? std::string{} : "no cariboulite board detected — check the hat is seated";
            }
#endif
#ifdef CLER_HAS_LIBIIO
            case Kind::Pluto: {
                const std::string uri = id.empty() ? first_pluto_uri() : id;
                if (uri.empty()) return "no pluto on usb — give an address like pluto:ip:192.168.2.1";
                const auto pr = SourcePlutoBlock::probe(uri.c_str());
                if (pr.ok) return {};
                if (!pr.reached) return "cannot reach pluto at " + uri;
                return "pluto at " + uri + " has no receiver — its firmware exposes no cf-ad9361-lpc";
            }
#endif
#ifdef CLER_HAS_UHD
            case Kind::UHD:
                if (uhd::device::find(uhd::device_addr_t(id)).empty()) {
                    return id.empty() ? "no usrp found" : "no usrp matching " + id;
                }
                return UHD::can_open(id) ? std::string{} : "usrp is busy or failed to open";
#endif
#ifdef CLER_HAS_SOAPYSDR
            case Kind::Soapy: {
                double f = 100e6, r = 2e6, g = 30.0;
                std::string why;
                return soapy_probe_clamp(id, f, r, g, &why) ? std::string{} : why;
            }
#endif
            case Kind::SigMF: {
                if (!bare_name(id)) return "bad recording name";
                const std::string meta_path = sigmf_path(id);
                std::error_code ec;
                if (!std::filesystem::is_regular_file(meta_path, ec)) return "no recording named " + id;
                if (!std::filesystem::is_regular_file(sigmf::data_path(meta_path), ec)) {
                    return id + " has no .sigmf-data next to its metadata";
                }
                sigmf::Meta meta;
                std::string meta_why;
                if (!sigmf::try_read_meta(meta_path, meta, &meta_why)) return id + " " + meta_why;
                if (!sigmf::datatype_is_complex(meta.datatype)) {
                    return id + " is " + sigmf::datatype_name(meta.datatype) + ", not a complex capture";
                }
                if (meta.sample_rate <= 0) return id + " has no sample rate in its metadata";
                return {};
            }
            case Kind::Sim:
                return {};
            default:
                return std::string(kind_name(kind)) + " is not compiled in";
        }
    }

    // Availability check that never touches the open source.
    bool probe(Kind kind, const std::string& id, std::string* why = nullptr) const {
        const std::string reason = open_reason(kind, id);
        if (why) *why = reason;
        return reason.empty();
    }

    // Graph must be stopped. Closes the current device, opens the new one;
    // false (and no source) if the device is gone, busy or not compiled in,
    // with the reason in *why.
    bool select(Kind kind, const std::string& id, double freq_hz, double rate_hz, std::string* why = nullptr) {
        _v.emplace<std::monostate>();
        _id = id;
        // The old device is closed above, so this asks about the new one alone.
        const std::string reason = open_reason(kind, id);
        if (why) *why = reason;
        if (!reason.empty()) return false;
        switch (kind) {
#ifdef CLER_HAS_HACKRF
            case Kind::HackRF:
                rate_hz = std::clamp(rate_hz, 2e6, 20e6);
                // ~870 ms of slack at 2.4 MS/s: hackrf_start_rx runs here, long before
                // an app's plots and panels finish initialising and start consuming.
                _v.emplace<SourceHackRFBlock>("hackrf", static_cast<uint64_t>(freq_hz + 0.5),
                                              static_cast<uint32_t>(rate_hz + 0.5), 40, 16, false, size_t{1} << 21,
                                              id.empty() ? nullptr : id.c_str());
                return true;
#endif
#ifdef CLER_HAS_CARIBOULITE
            case Kind::Cariboulite:
                {
                    const auto type = id == "hif" ? CaribouLiteRadio::HiF : CaribouLiteRadio::S1G;
                    CaribouLiteRadio* r = CaribouLite::GetInstance(false).GetRadioChannel(type);
                    if (!r) {
                        if (why) *why = "cariboulite has no " + id + " radio channel";
                        return false;
                    }
                    const auto ranges = r->GetFrequencyRange();
                    bool ok = false;
                    for (const auto& fr : ranges) ok = ok || (freq_hz > fr.fmin() && freq_hz < fr.fmax());
                    if (!ok && !ranges.empty()) freq_hz = 0.5 * (ranges.front().fmin() + ranges.front().fmax());
                    rate_hz = std::min<double>(std::max<double>(rate_hz, r->GetRxSampleRateMin()), r->GetRxSampleRateMax());
                    _v.emplace<CBL>("cariboulite", type, static_cast<float>(freq_hz),
                                    static_cast<float>(rate_hz), false, 40.0f);
                }
                return true;
#endif
#ifdef CLER_HAS_LIBIIO
            case Kind::Pluto: {
                const std::string uri = id.empty() ? first_pluto_uri() : id;
                const auto pr = SourcePlutoBlock::probe(uri.c_str());
                _id = uri;
                _pluto_fmin = pr.fmin; _pluto_fmax = pr.fmax;
                _pluto_rmin = pr.rmin; _pluto_rmax = pr.rmax;
                const long long f = std::clamp<long long>(static_cast<long long>(freq_hz + 0.5), pr.fmin, pr.fmax);
                const long long r = std::clamp<long long>(static_cast<long long>(rate_hz + 0.5), pr.rmin, pr.rmax);
                _v.emplace<Pluto>("pluto", uri.c_str(), f, r, 50.0, 0LL, size_t{1} << 16);
                return true;
            }
#endif
#ifdef CLER_HAS_UHD
            case Kind::UHD: {
                _v.emplace<UHD>("uhd", freq_hz, rate_hz, id, 30.0, 1, "sc16", true);
                _uhd_freq = freq_hz;
                _uhd_gain = 30.0;
                return true;
            }
#endif
#ifdef CLER_HAS_SOAPYSDR
            case Kind::Soapy: {
                double f = freq_hz, r = rate_hz, g = 30.0;
                if (!soapy_probe_clamp(id, f, r, g, why)) return false;
                _v.emplace<Soapy>("soapy", id, f, r, g);
                return true;
            }
#endif
            case Kind::SigMF:
                _v.emplace<SigMFSrc>("sigmf", sigmf_path(id).c_str(), false, size_t(8192), true);
                return true;
            case Kind::Sim:
                _v.emplace<SimSourceBlock>("sim", rate_hz, freq_hz, 400e3);
                return true;
            default:
                return false;
        }
    }

    void close() { _v.emplace<std::monostate>(); }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        return std::visit([&](auto& src) -> cler::Result<cler::Empty, cler::Error> {
            if constexpr (std::is_same_v<std::decay_t<decltype(src)>, std::monostate>) {
                return cler::Error::NotEnoughSamples;
            } else {
                return src.procedure(out);
            }
        }, _v);
    }

    Kind kind() const {
        if (std::holds_alternative<SimSourceBlock>(_v)) return Kind::Sim;
        if (std::holds_alternative<SigMFSrc>(_v)) return Kind::SigMF;
#ifdef CLER_HAS_HACKRF
        if (std::holds_alternative<SourceHackRFBlock>(_v)) return Kind::HackRF;
#endif
#ifdef CLER_HAS_CARIBOULITE
        if (std::holds_alternative<CBL>(_v)) return Kind::Cariboulite;
#endif
#ifdef CLER_HAS_LIBIIO
        if (std::holds_alternative<Pluto>(_v)) return Kind::Pluto;
#endif
#ifdef CLER_HAS_UHD
        if (std::holds_alternative<UHD>(_v)) return Kind::UHD;
#endif
#ifdef CLER_HAS_SOAPYSDR
        if (std::holds_alternative<Soapy>(_v)) return Kind::Soapy;
#endif
        return Kind::None;
    }
    const std::string& id() const { return _id; }

    double rate() const {
        if (auto* s = std::get_if<SimSourceBlock>(&_v)) return s->rate();
        if (auto* f = std::get_if<SigMFSrc>(&_v)) return f->sample_rate();
#ifdef CLER_HAS_HACKRF
        if (auto* h = std::get_if<SourceHackRFBlock>(&_v)) return h->get_sample_rate();
#endif
#ifdef CLER_HAS_CARIBOULITE
        if (auto* c = std::get_if<CBL>(&_v)) return const_cast<CBL*>(c)->get_sample_rate();
#endif
#ifdef CLER_HAS_LIBIIO
        if (auto* pl = std::get_if<Pluto>(&_v)) return static_cast<double>(pl->get_sample_rate());
#endif
#ifdef CLER_HAS_UHD
        if (auto* u = std::get_if<UHD>(&_v)) return u->actual_sample_rate();
#endif
#ifdef CLER_HAS_SOAPYSDR
        if (auto* so = std::get_if<Soapy>(&_v)) return so->get_sample_rate();
#endif
        return 0.0;
    }

    double center() const {
        if (auto* s = std::get_if<SimSourceBlock>(&_v)) return s->center();
        if (auto* f = std::get_if<SigMFSrc>(&_v)) return f->center_frequency();
#ifdef CLER_HAS_HACKRF
        if (auto* h = std::get_if<SourceHackRFBlock>(&_v)) return static_cast<double>(h->get_frequency());
#endif
#ifdef CLER_HAS_CARIBOULITE
        if (auto* c = std::get_if<CBL>(&_v)) return const_cast<CBL*>(c)->get_frequency();
#endif
#ifdef CLER_HAS_LIBIIO
        if (auto* pl = std::get_if<Pluto>(&_v)) return static_cast<double>(pl->get_frequency());
#endif
#ifdef CLER_HAS_UHD
        if (std::holds_alternative<UHD>(_v)) return _uhd_freq;
#endif
#ifdef CLER_HAS_SOAPYSDR
        if (auto* so = std::get_if<Soapy>(&_v)) return so->get_frequency();
#endif
        return 0.0;
    }

    bool lost() const {
#ifdef CLER_HAS_HACKRF
        if (auto* h = std::get_if<SourceHackRFBlock>(&_v)) return h->lost();
#endif
#ifdef CLER_HAS_CARIBOULITE
        if (auto* c = std::get_if<CBL>(&_v)) return c->lost();
#endif
#ifdef CLER_HAS_LIBIIO
        if (auto* pl = std::get_if<Pluto>(&_v)) return pl->lost();
#endif
#ifdef CLER_HAS_UHD
        if (auto* u = std::get_if<UHD>(&_v)) return u->lost();
#endif
#ifdef CLER_HAS_SOAPYSDR
        if (auto* so = std::get_if<Soapy>(&_v)) return so->lost();
#endif
        return false;
    }

    size_t overflows() const {
#ifdef CLER_HAS_HACKRF
        if (auto* h = std::get_if<SourceHackRFBlock>(&_v)) return h->get_overflow_count();
#endif
#ifdef CLER_HAS_UHD
        if (auto* u = std::get_if<UHD>(&_v)) return u->get_overflow_count();
#endif
        return 0;
    }

    std::vector<Control> capabilities() const {
        std::vector<Control> c;
        if (auto* f = std::get_if<SigMFSrc>(&_v)) {
            c.push_back(range("freq", "Frequency", "Hz", 0, 6e9, 1, f->center_frequency(), true));
            c.push_back(range("rate", "Sample rate", "Hz", 0, 20e6, 1, f->sample_rate(), true));
            return c;
        }
        if (auto* s = std::get_if<SimSourceBlock>(&_v)) {
            c.push_back(range("freq", "Frequency", "Hz", 0, 6e9, 1, s->center()));
            c.push_back(range("rate", "Sample rate", "Hz", 48e3, 20e6, 1, s->rate(), true));
            c.push_back(range("tone_hz", "Tone offset", "Hz", -10e6, 10e6, 1, s->tone_hz()));
            c.push_back(range("snr_db", "SNR", "dB", -20, 80, 1, s->snr_db()));
        }
#ifdef CLER_HAS_HACKRF
        if (auto* h = std::get_if<SourceHackRFBlock>(&_v)) {
            c.push_back(range("freq", "Frequency", "Hz", 1e6, 6e9, 1, static_cast<double>(h->get_frequency())));
            Control r = range("rate", "Sample rate", "Hz", 2e6, 20e6, 0, h->get_sample_rate(), true);
            r.type = "enum";
            for (const char* o : {"2000000", "2400000", "4000000", "8000000", "10000000", "12500000", "16000000", "20000000"}) r.options.push_back(o);
            c.push_back(r);
            c.push_back(range("lna", "LNA gain", "dB", 0, 40, 8, h->get_lna_gain()));
            c.push_back(range("vga", "VGA gain", "dB", 0, 62, 2, h->get_vga_gain()));
            Control amp = range("amp", "RF amp", "", 0, 1, 1, h->get_amp_enable() ? 1 : 0);
            amp.type = "bool";
            c.push_back(amp);
        }
#endif
#ifdef CLER_HAS_CARIBOULITE
        if (auto* cc = std::get_if<CBL>(&_v)) {
            CaribouLiteRadio& r = const_cast<CBL*>(cc)->radio();
            double fmin = 1e12, fmax = 0;
            for (const auto& fr : r.GetFrequencyRange()) { fmin = std::min<double>(fmin, fr.fmin()); fmax = std::max<double>(fmax, fr.fmax()); }
            c.push_back(range("freq", "Frequency", "Hz", fmin, fmax, 1, r.GetFrequency()));
            c.push_back(range("rate", "Sample rate", "Hz", r.GetRxSampleRateMin(), r.GetRxSampleRateMax(), 1, r.GetRxSampleRate(), true));
            c.push_back(range("gain", "RX gain", "dB", r.GetRxGainMin(), r.GetRxGainMax(), r.GetRxGainSteps(), r.GetRxGain()));
            Control agc = range("agc", "AGC", "", 0, 1, 1, r.GetAgc() ? 1 : 0);
            agc.type = "bool";
            c.push_back(agc);
            c.push_back(range("bw", "RX bandwidth", "Hz", r.GetRxBandwidthMin(), r.GetRxBandwidthMax(), 1, r.GetRxBandwidth()));
        }
#endif
#ifdef CLER_HAS_LIBIIO
        if (auto* pl = std::get_if<Pluto>(&_v)) {
            c.push_back(range("freq", "Frequency", "Hz", static_cast<double>(_pluto_fmin), static_cast<double>(_pluto_fmax), 1,
                              static_cast<double>(const_cast<Pluto*>(pl)->get_frequency())));
            c.push_back(range("rate", "Sample rate", "Hz", static_cast<double>(_pluto_rmin), static_cast<double>(_pluto_rmax), 1,
                              static_cast<double>(pl->get_sample_rate()), true));
            c.push_back(range("gain", "RX gain", "dB", -3, 71, 1, pl->get_gain()));
            Control agc = range("agc", "AGC", "", 0, 1, 1, pl->get_agc() ? 1 : 0);
            agc.type = "bool";
            c.push_back(agc);
        }
#endif
#ifdef CLER_HAS_UHD
        if (auto* u = std::get_if<UHD>(&_v)) {
            const auto fr = u->rx_freq_range();
            const auto gr = u->rx_gain_range();
            const auto rr = u->rx_rate_range();
            c.push_back(range("freq", "Frequency", "Hz", fr.start(), fr.stop(), 1, u->get_frequency()));
            c.push_back(range("rate", "Sample rate", "Hz", rr.start(), rr.stop(), 1, u->actual_sample_rate(), true));
            c.push_back(range("gain", "RX gain", "dB", gr.start(), gr.stop(), gr.step() > 0 ? gr.step() : 1, _uhd_gain));
            const auto ants = u->rx_antennas();
            if (ants.size() > 1) {
                Control a;
                a.id = "antenna"; a.label = "Antenna"; a.type = "enum";
                a.options = ants;
                const auto cur = u->rx_antenna();
                a.value = static_cast<double>(std::find(ants.begin(), ants.end(), cur) - ants.begin());
                c.push_back(a);
            }
        }
#endif
#ifdef CLER_HAS_SOAPYSDR
        if (auto* so = std::get_if<Soapy>(&_v)) {
            double fmin = 1e12, fmax = 0;
            for (const auto& fr : so->get_frequency_range()) { fmin = std::min(fmin, fr.minimum()); fmax = std::max(fmax, fr.maximum()); }
            double rmin = 1e12, rmax = 0;
            for (const auto& rr : so->get_sample_rate_range()) { rmin = std::min(rmin, rr.minimum()); rmax = std::max(rmax, rr.maximum()); }
            const auto gr = so->get_gain_range();
            c.push_back(range("freq", "Frequency", "Hz", fmin, fmax, 1, so->get_frequency()));
            c.push_back(range("rate", "Sample rate", "Hz", rmin, rmax, 1, so->get_sample_rate(), true));
            for (const auto& name : so->list_gains()) {
                const auto g = so->get_gain_range(name);
                c.push_back(range(("gain_" + name).c_str(), (name + " gain").c_str(), "dB",
                                  g.minimum(), g.maximum(), g.step() > 0 ? g.step() : 1, so->get_gain(name)));
            }
            if (so->list_gains().empty()) {
                c.push_back(range("gain", "RX gain", "dB", gr.minimum(), gr.maximum(), gr.step() > 0 ? gr.step() : 1, so->get_gain()));
            }
        }
#endif
        return c;
    }

    bool is_file() const { return std::holds_alternative<SigMFSrc>(_v); }
    void seek(double seconds) { if (auto* f = std::get_if<SigMFSrc>(&_v)) f->seek(seconds); }
    void pause(bool p) { if (auto* f = std::get_if<SigMFSrc>(&_v)) f->pause(p); }
    bool paused() const { auto* f = std::get_if<SigMFSrc>(&_v); return f && f->paused(); }
    void set_loop(bool l) { if (auto* f = std::get_if<SigMFSrc>(&_v)) f->set_loop(l); }
    bool looping() const { auto* f = std::get_if<SigMFSrc>(&_v); return f && f->looping(); }
    bool ended() const { auto* f = std::get_if<SigMFSrc>(&_v); return f && f->ended(); }
    double pos_seconds() const { auto* f = std::get_if<SigMFSrc>(&_v); return f ? f->pos_seconds() : 0.0; }
    double duration_seconds() const { auto* f = std::get_if<SigMFSrc>(&_v); return f ? f->duration_seconds() : 0.0; }

    // Live controls only; rate changes go through select() with the graph stopped.
    void set(const std::string& id, double value) {
        if (auto* s = std::get_if<SimSourceBlock>(&_v)) {
            if (id == "freq") s->set_center(value);
            else if (id == "tone_hz") s->set_tone_hz(value);
            else if (id == "snr_db") s->set_snr_db(static_cast<float>(value));
            return;
        }
#ifdef CLER_HAS_HACKRF
        if (auto* h = std::get_if<SourceHackRFBlock>(&_v)) {
            if (id == "freq") h->set_frequency(static_cast<uint64_t>(value + 0.5));
            else if (id == "lna") h->set_lna_gain(static_cast<int>(value + 0.5));
            else if (id == "vga") h->set_vga_gain(static_cast<int>(value + 0.5));
            else if (id == "amp") h->set_amp_enable(value >= 0.5);
            return;
        }
#endif
#ifdef CLER_HAS_CARIBOULITE
        if (auto* c = std::get_if<CBL>(&_v)) {
            if (id == "freq") c->set_frequency(static_cast<float>(value));
            else if (id == "gain") c->set_rx_gain(static_cast<float>(value));
            else if (id == "agc") c->set_agc(value >= 0.5);
            else if (id == "bw") c->set_bandwidth(static_cast<float>(value));
            return;
        }
#endif
#ifdef CLER_HAS_LIBIIO
        if (auto* pl = std::get_if<Pluto>(&_v)) {
            if (id == "freq") pl->set_frequency(static_cast<long long>(value + 0.5));
            else if (id == "gain") pl->set_gain(value);
            else if (id == "agc") pl->set_agc(value >= 0.5);
            return;
        }
#endif
#ifdef CLER_HAS_UHD
        if (auto* u = std::get_if<UHD>(&_v)) {
            UHDConfig cfg{_uhd_freq, u->actual_sample_rate(), _uhd_gain, 0.0};
            if (id == "freq") { cfg.center_freq_Hz = value; _uhd_freq = value; }
            else if (id == "gain") { cfg.gain = value; _uhd_gain = value; }
            else if (id == "antenna") {
                const auto ants = u->rx_antennas();
                const size_t i = static_cast<size_t>(value + 0.5);
                if (i < ants.size()) u->set_rx_antenna(ants[i]);
                return;
            } else return;
            u->request_configure(cfg);
            return;
        }
#endif
#ifdef CLER_HAS_SOAPYSDR
        if (auto* so = std::get_if<Soapy>(&_v)) {
            if (id == "freq") so->set_frequency(value);
            else if (id == "gain") so->set_gain(value);
            else if (id.rfind("gain_", 0) == 0) so->set_gain(id.substr(5), value);
            return;
        }
#endif
    }

private:
    static Control range(const char* id, const char* label, const char* unit,
                         double min, double max, double step, double value, bool ro = false) {
        Control c;
        c.id = id; c.label = label; c.type = "range"; c.unit = unit;
        c.min = min; c.max = max; c.step = step; c.value = value; c.ro = ro;
        return c;
    }

    static std::string short_serial(const std::string& s) {
        return s.size() > 8 ? s.substr(s.size() - 8) : s;
    }

#ifdef CLER_HAS_LIBIIO
    static std::string first_pluto_uri() {
        std::string uri;
        if (iio_scan_context* sc = iio_create_scan_context("usb", 0)) {
            iio_context_info** info = nullptr;
            const ssize_t n = iio_scan_context_get_info_list(sc, &info);
            if (n > 0) uri = iio_context_info_get_uri(info[0]);
            if (n >= 0) iio_context_info_list_free(info);
            iio_scan_context_destroy(sc);
        }
        return uri;
    }
#endif

#ifdef CLER_HAS_SOAPYSDR
    static bool native_driver(const std::string& d) {
        (void)d;
#ifdef CLER_HAS_HACKRF
        if (d == "hackrf") return true;
#endif
#ifdef CLER_HAS_LIBIIO
        if (d == "plutosdr") return true;
#endif
#ifdef CLER_HAS_UHD
        if (d == "uhd") return true;
#endif
        return false;
    }

    static bool soapy_probe_clamp(const std::string& args, double& freq, double& rate, double& gain,
                                  std::string* why = nullptr) {
        SoapySDR::Device* dev = nullptr;
        try {
            dev = SoapySDR::Device::make(args);
        } catch (const std::exception& e) {
            if (why) *why = "soapy could not open " + args + ": " + e.what();
            return false;
        }
        if (!dev) {
            if (why) *why = "soapy found no device for " + args;
            return false;
        }
        const auto frs = dev->getFrequencyRange(SOAPY_SDR_RX, 0);
        bool ok = frs.empty();
        for (const auto& r : frs) ok = ok || (freq >= r.minimum() && freq <= r.maximum());
        if (!ok) freq = 0.5 * (frs.front().minimum() + frs.front().maximum());
        const auto rrs = dev->getSampleRateRange(SOAPY_SDR_RX, 0);
        ok = rrs.empty();
        for (const auto& r : rrs) ok = ok || (rate >= r.minimum() && rate <= r.maximum());
        if (!ok) rate = std::clamp(rate, rrs.front().minimum(), rrs.front().maximum());
        const auto gr = dev->getGainRange(SOAPY_SDR_RX, 0);
        gain = std::clamp(gain, gr.minimum(), gr.maximum());
        SoapySDR::Device::unmake(dev);
        return true;
    }
#endif

#ifdef CLER_HAS_CARIBOULITE
    using CBL = SourceCaribouliteBlock<std::complex<float>>;
#endif
#ifdef CLER_HAS_LIBIIO
    using Pluto = SourcePlutoBlock;
#endif
#ifdef CLER_HAS_UHD
    using UHD = SourceUHDBlock<std::complex<float>>;
#endif
#ifdef CLER_HAS_SOAPYSDR
    using Soapy = SourceSoapySDRBlock<std::complex<float>>;
#endif
public:
    using SigMFSrc = SourceSigMFBlock<std::complex<float>>;
private:
    std::variant<std::monostate,
#ifdef CLER_HAS_HACKRF
                 SourceHackRFBlock,
#endif
#ifdef CLER_HAS_CARIBOULITE
                 CBL,
#endif
#ifdef CLER_HAS_LIBIIO
                 Pluto,
#endif
#ifdef CLER_HAS_UHD
                 UHD,
#endif
#ifdef CLER_HAS_SOAPYSDR
                 Soapy,
#endif
                 SigMFSrc,
                 SimSourceBlock> _v;
    std::string _id;
    std::vector<std::string> _sigmf_dirs;
    long long _pluto_fmin = 0, _pluto_fmax = 0, _pluto_rmin = 0, _pluto_rmax = 0;
    double _uhd_freq = 0.0, _uhd_gain = 30.0;
};
`,st=`#pragma once
#include "cler.hpp"
#include "cler_desktop_utils.hpp"

#ifdef __has_include
    #if __has_include(<iio.h>)
        #include <iio.h>
    #else
        #error "libiio header not found. Please install libiio-dev package."
    #endif
#endif

#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

// PlutoSDR (and any AD936x/IIO device) RX source via libiio.
// Works with the same code over any libiio URI:
//   "ip:192.168.2.1"  - Pluto over USB-ethernet / network
//   "usb:"            - Pluto over raw USB
//   "local:"          - running ON the Pluto itself
enum class PlutoAgcMode { FastAttack, SlowAttack };

inline const char* to_iio_gain_control_mode(PlutoAgcMode mode) {
    switch (mode) {
        case PlutoAgcMode::FastAttack: return "fast_attack";
        case PlutoAgcMode::SlowAttack: return "slow_attack";
    }
    cler::panic("Pluto: unknown PlutoAgcMode");
}

struct SourcePlutoBlock : public cler::BlockBase {
    static constexpr bool may_block = true;

    struct Probe {
        bool ok = false;
        bool reached = false;   // the context opened; ok still false means no rx device on it
        long long fmin = 70000000, fmax = 6000000000;
        long long rmin = 2083333, rmax = 61440000;
    };

    // Non-panicking availability check; also reads what the driver will accept
    // so callers can clamp before the panicking constructor runs.
    static Probe probe(const char* uri) {
        Probe pr;
        iio_context* ctx = iio_create_context_from_uri(uri);
        if (!ctx) return pr;
        pr.reached = true;
        iio_device* phy = iio_context_find_device(ctx, "ad9361-phy");
        iio_device* rx_dev = iio_context_find_device(ctx, "cf-ad9361-lpc");
        if (phy && rx_dev) {
            pr.ok = true;
            char buf[128];
            iio_channel* lo = iio_device_find_channel(phy, "altvoltage0", true);
            if (lo && iio_channel_attr_read(lo, "frequency_available", buf, sizeof buf) > 0) {
                read_range(buf, pr.fmin, pr.fmax);
            }
            iio_channel* chn = iio_device_find_channel(phy, "voltage0", false);
            if (chn && iio_channel_attr_read(chn, "sampling_frequency_available", buf, sizeof buf) > 0) {
                read_range(buf, pr.rmin, pr.rmax);
                // the driver rejects exactly the advertised minimum (it rounds the
                // internal divider below the floor) but accepts min+1 and then
                // reports the minimum back, so a caller clamping to rmin fails
                ++pr.rmin;
            }
        }
        iio_context_destroy(ctx);
        return pr;
    }

    SourcePlutoBlock(const char* name,
                     const char* uri,          // e.g. "ip:192.168.2.1"
                     long long freq_hz,
                     long long samp_rate_hz,
                     double gain_db = -1.0,    // <0 => AGC (agc_mode), >=0 => manual gain
                     long long bandwidth_hz = 0, // 0 => same as sample rate
                     size_t buffer_size = 1 << 14,
                     PlutoAgcMode agc_mode = PlutoAgcMode::FastAttack)
        : cler::BlockBase(name),
          _freq_hz(freq_hz),
          _samp_rate_hz(samp_rate_hz),
          _buffer_size(buffer_size)
    {
        if (bandwidth_hz == 0) bandwidth_hz = samp_rate_hz;

        _ctx = iio_create_context_from_uri(uri);
        if (!_ctx) {
            std::string msg = std::string("Pluto: failed to create IIO context for uri: ") + uri;
            cler::panic(msg.c_str());
        }

        iio_device* phy = iio_context_find_device(_ctx, "ad9361-phy");
        iio_device* rx_dev = iio_context_find_device(_ctx, "cf-ad9361-lpc");
        if (!phy || !rx_dev) {
            iio_context_destroy(_ctx);
            cler::panic("Pluto: ad9361-phy / cf-ad9361-lpc not found (not a Pluto?)");
        }

        // RX LO frequency lives on output channel altvoltage0 of the phy
        iio_channel* lo = iio_device_find_channel(phy, "altvoltage0", true);
        // RX chain config lives on input channel voltage0 of the phy
        iio_channel* chn = iio_device_find_channel(phy, "voltage0", false);
        if (!lo || !chn) {
            iio_context_destroy(_ctx);
            cler::panic("Pluto: phy channels not found");
        }

        write_attr_or_panic(lo, "frequency", freq_hz);
        write_attr_or_panic(chn, "sampling_frequency", samp_rate_hz);
        write_attr_or_panic(chn, "rf_bandwidth", bandwidth_hz);

        long long actual_rate = 0;
        if (iio_channel_attr_read_longlong(chn, "sampling_frequency", &actual_rate) < 0 ||
            std::llabs(actual_rate - samp_rate_hz) > samp_rate_hz / 100) {
            char msg[160];
            std::snprintf(msg, sizeof(msg),
                          "Pluto: driver clamped sample rate to %lld Hz (requested %lld)",
                          actual_rate, samp_rate_hz);
            iio_context_destroy(_ctx);
            cler::panic(msg);
        }
        _lo = lo;
        _chn = chn;

        if (gain_db < 0.0) {
            iio_channel_attr_write(chn, "gain_control_mode", to_iio_gain_control_mode(agc_mode));
        } else {
            iio_channel_attr_write(chn, "gain_control_mode", "manual");
            iio_channel_attr_write_double(chn, "hardwaregain", gain_db);
        }

        _rx_i = iio_device_find_channel(rx_dev, "voltage0", false);
        _rx_q = iio_device_find_channel(rx_dev, "voltage1", false);
        if (!_rx_i || !_rx_q) {
            iio_context_destroy(_ctx);
            cler::panic("Pluto: RX streaming channels not found");
        }
        iio_channel_enable(_rx_i);
        iio_channel_enable(_rx_q);

        _buf = iio_device_create_buffer(rx_dev, buffer_size, false);
        if (!_buf) {
            iio_context_destroy(_ctx);
            cler::panic("Pluto: failed to create RX buffer");
        }

        std::cout << "SourcePlutoBlock: Initialized (" << uri << ")\\n"
                  << "  Frequency: " << freq_hz / 1e6 << " MHz\\n"
                  << "  Sample rate: " << samp_rate_hz / 1e6 << " MSPS\\n"
                  << "  Bandwidth: " << bandwidth_hz / 1e6 << " MHz\\n"
                  << "  Gain: " << (gain_db < 0.0 ? std::string("AGC (") + to_iio_gain_control_mode(agc_mode) + ")"
                                                  : std::to_string(gain_db) + " dB")
                  << std::endl;
    }

    ~SourcePlutoBlock() {
        if (_buf) iio_buffer_destroy(_buf);
        if (_ctx) iio_context_destroy(_ctx);
    }

    SourcePlutoBlock(const SourcePlutoBlock&) = delete;
    SourcePlutoBlock& operator=(const SourcePlutoBlock&) = delete;

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        // Refill from hardware when the previous buffer is fully consumed.
        // iio_buffer_refill blocks for ~one buffer duration - fine, this block
        // runs on its own flowgraph thread.
        if (_consumed >= _available) {
            ssize_t nbytes = iio_buffer_refill(_buf);
            if (nbytes < 0) {
                _lost = true;
                return cler::Error::ProcedureError;
            }
            _available = static_cast<size_t>(nbytes) / (2 * sizeof(int16_t));
            _consumed = 0;
        }

        auto [write_ptr, write_size] = out->write_dbf();
        if (write_ptr == nullptr || write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }

        const int16_t* samples = static_cast<const int16_t*>(iio_buffer_start(_buf));
        size_t n = std::min(_available - _consumed, write_size);
        convert_i16_to_cf32(samples + 2 * _consumed, write_ptr, n);
        _consumed += n;
        out->commit_write(n);
        return cler::Empty{};
    }

    long long get_frequency() const { return _freq_hz; }
    long long get_sample_rate() const { return _samp_rate_hz; }
    bool lost() const { return _lost; }

    double get_gain() const {
        double g = 0.0;
        if (_chn) iio_channel_attr_read_double(_chn, "hardwaregain", &g);
        return g;
    }

    bool get_agc() const {
        char buf[32];
        if (_chn && iio_channel_attr_read(_chn, "gain_control_mode", buf, sizeof buf) > 0) {
            return std::string(buf) != "manual";
        }
        return false;
    }

    void set_gain(double gain_db) {
        if (!_chn) return;
        iio_channel_attr_write(_chn, "gain_control_mode", "manual");
        iio_channel_attr_write_double(_chn, "hardwaregain", gain_db);
    }

    void set_agc(bool on, PlutoAgcMode mode = PlutoAgcMode::FastAttack) {
        if (!_chn) return;
        iio_channel_attr_write(_chn, "gain_control_mode", on ? to_iio_gain_control_mode(mode) : "manual");
    }

    // Retune while streaming (phy attrs are safe to write at runtime)
    void set_frequency(long long freq_hz) {
        if (_lo && iio_channel_attr_write_longlong(_lo, "frequency", freq_hz) >= 0) {
            _freq_hz = freq_hz;
        }
    }

private:
    static void read_range(const char* buf, long long& lo_out, long long& hi_out) {
        long long a = 0, b = 0, c = 0;
        if (std::sscanf(buf, "[%lld %lld %lld]", &a, &b, &c) == 3) { lo_out = a; hi_out = c; }
    }

    static void convert_i16_to_cf32(const int16_t* src, std::complex<float>* dst, size_t n) {
        constexpr float scale = 1.0f / 2048.0f;
        size_t i = 0;
#if defined(__ARM_NEON)
        float* out = reinterpret_cast<float*>(dst);
        const size_t n_floats = 2 * n;
        for (; i + 8 <= n_floats; i += 8) {
            int16x8_t v = vld1q_s16(src + i);
            float32x4_t lo = vcvtq_f32_s32(vmovl_s16(vget_low_s16(v)));
            float32x4_t hi = vcvtq_f32_s32(vmovl_s16(vget_high_s16(v)));
            vst1q_f32(out + i,     vmulq_n_f32(lo, scale));
            vst1q_f32(out + i + 4, vmulq_n_f32(hi, scale));
        }
        i /= 2;
#endif
        for (; i < n; ++i) {
            dst[i] = std::complex<float>(src[2 * i] * scale, src[2 * i + 1] * scale);
        }
    }

    void write_attr_or_panic(iio_channel* ch, const char* attr, long long val) {
        int ret = iio_channel_attr_write_longlong(ch, attr, val);
        if (ret < 0) {
            char err[64];
            iio_strerror(-ret, err, sizeof(err));
            char msg[160];
            std::snprintf(msg, sizeof(msg), "Pluto: failed to set %s to %lld: %s", attr, val, err);
            iio_context_destroy(_ctx);
            cler::panic(msg);
        }
    }

    iio_context* _ctx = nullptr;
    iio_buffer* _buf = nullptr;
    iio_channel* _rx_i = nullptr;
    iio_channel* _rx_q = nullptr;
    iio_channel* _lo = nullptr;
    iio_channel* _chn = nullptr;
    bool _lost = false;

    long long _freq_hz;
    long long _samp_rate_hz;
    size_t _buffer_size;

    size_t _available = 0; // samples in the current iio buffer
    size_t _consumed = 0;  // samples already pushed downstream
};
`,rt=`#pragma once
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <random>
#include <thread>

// A stand-in SDR: one complex tone at \`tone_hz\` above centre, in white noise,
// paced to real time so the rest of the graph behaves as with hardware.
// Centre and rate are what a radio would report; tone_hz is relative to centre.
struct SimSourceBlock : public cler::BlockBase {
    static constexpr bool may_block = true;

    SimSourceBlock(const char* name, double rate_hz, double center_hz = 100e6,
                   double tone_hz = 100e3, float snr_db = 30.0f)
        : cler::BlockBase(name), _rate(rate_hz), _center(center_hz),
          _tone(tone_hz), _snr_db(snr_db), _rng(12345)
    {
        if (rate_hz <= 0.0) cler::panic("SimSourceBlock: rate must be positive");
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        auto [wptr, space] = out->write_dbf();
        if (space == 0) return cler::Error::NotEnoughSpace;

        const double rate = _rate.load(std::memory_order_relaxed);
        if (!_started) { _epoch = clock::now(); _emitted = 0; _started = true; }
        size_t due = samples_due(rate);
        if (due <= _emitted) {
            std::this_thread::sleep_for(std::chrono::microseconds(1000));
            due = samples_due(rate);
            if (due <= _emitted) return cler::Error::NotEnoughSamples;
        }
        const size_t n = std::min(space, due - _emitted);

        const double inc = 2.0 * cler::PI * _tone.load(std::memory_order_relaxed) / rate;
        const std::complex<double> rot = std::polar(1.0, inc);
        const float sigma = std::pow(10.0f, -_snr_db.load(std::memory_order_relaxed) / 20.0f) / std::sqrt(2.0f);
        for (size_t i = 0; i < n; ++i) {
            wptr[i] = {static_cast<float>(_phasor.real()) + sigma * _gauss(_rng),
                       static_cast<float>(_phasor.imag()) + sigma * _gauss(_rng)};
            _phasor *= rot;
        }
        _phasor /= std::abs(_phasor);
        out->commit_write(n);
        _emitted += n;
        if (due - _emitted > rate / 10.0) _epoch = clock::now() - to_duration(_emitted, rate);
        return cler::Empty{};
    }

    double rate() const { return _rate.load(std::memory_order_relaxed); }
    double center() const { return _center.load(std::memory_order_relaxed); }
    double tone_hz() const { return _tone.load(std::memory_order_relaxed); }
    float snr_db() const { return _snr_db.load(std::memory_order_relaxed); }
    // Graph stopped only: restarts the pacing epoch.
    void set_rate(double hz) {
        if (hz <= 0.0) cler::panic("SimSourceBlock: rate must be positive");
        _rate.store(hz, std::memory_order_relaxed);
        _started = false;
    }
    void set_center(double hz) { _center.store(hz, std::memory_order_relaxed); }
    void set_tone_hz(double hz) { _tone.store(hz, std::memory_order_relaxed); }
    void set_snr_db(float db) { _snr_db.store(db, std::memory_order_relaxed); }

private:
    using clock = std::chrono::steady_clock;

    size_t samples_due(double rate) const {
        return static_cast<size_t>(std::chrono::duration<double>(clock::now() - _epoch).count() * rate);
    }
    static clock::duration to_duration(size_t samples, double rate) {
        return std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(samples / rate));
    }

    std::atomic<double> _rate, _center, _tone;
    std::atomic<float> _snr_db;
    std::complex<double> _phasor{1.0, 0.0};
    std::mt19937 _rng;
    std::normal_distribution<float> _gauss{0.0f, 1.0f};
    bool _started = false;
    size_t _emitted = 0;
    clock::time_point _epoch;
};
`,at=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Formats.hpp>
#include <SoapySDR/Errors.hpp>
#include <complex>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

// Helper to map C++ types to SoapySDR format strings
template<typename T>
inline std::string get_soapy_format() {
    if constexpr (std::is_same_v<T, std::complex<float>>) {
        return SOAPY_SDR_CF32;
    } else if constexpr (std::is_same_v<T, std::complex<int16_t>>) {
        return SOAPY_SDR_CS16;
    } else if constexpr (std::is_same_v<T, std::complex<int8_t>>) {
        return SOAPY_SDR_CS8;
    } else if constexpr (std::is_same_v<T, std::complex<uint8_t>>) {
        return SOAPY_SDR_CU8;
    } else if constexpr (std::is_same_v<T, int32_t>) {
        return SOAPY_SDR_S32;
    } else if constexpr (std::is_same_v<T, int16_t>) {
        return SOAPY_SDR_S16;
    } else if constexpr (std::is_same_v<T, uint8_t>) {
        return SOAPY_SDR_U8;
    } else if constexpr (std::is_same_v<T, float>) {
        return SOAPY_SDR_F32;
    } else {
        static_assert(!std::is_same_v<T, T>, "Unsupported type for SoapySDR");
    }
}

template<typename T>
struct SourceSoapySDRBlock : public cler::BlockBase {
    static constexpr bool may_block = true;

    SourceSoapySDRBlock(const char* name,
                        const std::string& args,
                        double freq,
                        double rate,
                        double gain = 20.0,
                        size_t channel = 0)
        : BlockBase(name),
          device_args(args),
          center_freq(freq),
          sample_rate(rate),
          gain_db(gain),
          channel_idx(channel),
          device(nullptr),
          stream(nullptr) {
        
        device = SoapySDR::Device::make(device_args);
        if (!device) {
            std::string msg = "SourceSoapySDRBlock: Failed to create SoapySDR device with args: " + device_args;
            cler::panic(msg.c_str());
        }

        auto sample_rates = device->getSampleRateRange(SOAPY_SDR_RX, channel_idx);
        bool rate_valid = false;
        for (const auto& range : sample_rates) {
            if (sample_rate >= range.minimum() && sample_rate <= range.maximum()) {
                rate_valid = true;
                break;
            }
        }
        if (!rate_valid) {
            std::stringstream ss;
            ss << "Sample rate " << sample_rate/1e6 << " MSPS not supported. Supported rates: ";
            for (const auto& range : sample_rates) {
                ss << range.minimum()/1e6 << "-" << range.maximum()/1e6 << " MSPS ";
            }
            SoapySDR::Device::unmake(device);
            cler::panic(ss.str().c_str());
        }
        device->setSampleRate(SOAPY_SDR_RX, channel_idx, sample_rate);

        auto freq_ranges = device->getFrequencyRange(SOAPY_SDR_RX, channel_idx);
        bool freq_valid = false;
        for (const auto& range : freq_ranges) {
            if (center_freq >= range.minimum() && center_freq <= range.maximum()) {
                freq_valid = true;
                break;
            }
        }
        if (!freq_valid) {
            std::stringstream ss;
            ss << "Frequency " << center_freq/1e6 << " MHz not supported. Supported ranges: ";
            for (const auto& range : freq_ranges) {
                ss << range.minimum()/1e6 << "-" << range.maximum()/1e6 << " MHz ";
            }
            SoapySDR::Device::unmake(device);
            cler::panic(ss.str().c_str());
        }
        device->setFrequency(SOAPY_SDR_RX, channel_idx, center_freq);

        auto gain_range = device->getGainRange(SOAPY_SDR_RX, channel_idx);
        if (gain_db < gain_range.minimum() || gain_db > gain_range.maximum()) {
            std::stringstream ss;
            ss << "Gain " << gain_db << " dB not supported. Supported range: "
               << gain_range.minimum() << "-" << gain_range.maximum() << " dB";
            SoapySDR::Device::unmake(device);
            cler::panic(ss.str().c_str());
        }
        if (device->hasGainMode(SOAPY_SDR_RX, channel_idx)) {
            device->setGainMode(SOAPY_SDR_RX, channel_idx, false);
        }
        device->setGain(SOAPY_SDR_RX, channel_idx, gain_db);

        if (device->getBandwidthRange(SOAPY_SDR_RX, channel_idx).size() > 0) {
            device->setBandwidth(SOAPY_SDR_RX, channel_idx, sample_rate);
        }

        std::vector<size_t> channels = {channel_idx};
        std::string format = get_soapy_format<T>();

        stream = device->setupStream(SOAPY_SDR_RX, format, channels);
        if (!stream) {
            SoapySDR::Device::unmake(device);
            cler::panic("SourceSoapySDRBlock: Failed to setup RX stream");
        }

        mtu = device->getStreamMTU(stream);

        int ret = device->activateStream(stream);
        if (ret != 0) {
            device->closeStream(stream);
            SoapySDR::Device::unmake(device);
            std::string msg = "SourceSoapySDRBlock: Failed to activate stream: " + std::string(SoapySDR::errToStr(ret));
            cler::panic(msg.c_str());
        }

        std::cout << "SourceSoapySDRBlock: Initialized " << device->getDriverKey()
                  << " (" << device->getHardwareKey() << ")"
                  << " at " << center_freq/1e6 << " MHz"
                  << ", " << sample_rate/1e6 << " MSPS"
                  << ", " << gain_db << " dB gain"
                  << ", MTU: " << mtu << " samples" << std::endl;

        auto antennas = device->listAntennas(SOAPY_SDR_RX, channel_idx);
        if (!antennas.empty()) {
            std::cout << "  Available RX antennas: ";
            for (const auto& ant : antennas) {
                std::cout << ant << " ";
            }
            std::cout << std::endl;
        }
    }
    
    ~SourceSoapySDRBlock() {
        if (stream && device) {
            device->deactivateStream(stream);
            device->closeStream(stream);
        }
        if (device) {
            SoapySDR::Device::unmake(device);
        }
    }
    
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        auto [write_ptr, write_size] = out->write_dbf();
        if (write_ptr == nullptr || write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t to_read = std::min(write_size, mtu);
        void* buffs[] = {write_ptr};
        int flags = 0;
        long long time_ns = 0;
        const long timeout_us = 100000;
        
        int ret = device->readStream(stream, buffs, to_read, flags, time_ns, timeout_us);
        
        if (ret > 0) {
            out->commit_write(ret);
            return cler::Empty{};
        } else if (ret == SOAPY_SDR_TIMEOUT) {
            return cler::Error::NotEnoughSamples;
        } else if (ret == SOAPY_SDR_OVERFLOW) {
            overflow_count++;
            if (overflow_count % 100 == 0) {
                std::cerr << "SourceSoapySDRBlock: Overflow count: " << overflow_count << std::endl;
            }
            return cler::Error::NotEnoughSamples;
        } else {
            std::cerr << "SourceSoapySDRBlock: readStream error: " << SoapySDR::errToStr(ret) << std::endl;
            _lost = true;
            return cler::Error::TERM_ProcedureError;
        }
    }
    
    void set_frequency(double freq) {
        try {
            device->setFrequency(SOAPY_SDR_RX, channel_idx, freq);
            center_freq = freq;
        } catch (const std::exception& e) {
            std::cerr << "SourceSoapySDRBlock: set_frequency failed: " << e.what() << std::endl;
        }
    }

    void set_gain(const std::string& name, double gain) {
        try {
            device->setGain(SOAPY_SDR_RX, channel_idx, name, gain);
        } catch (const std::exception& e) {
            std::cerr << "SourceSoapySDRBlock: set_gain(" << name << ") failed: " << e.what() << std::endl;
        }
    }

    double get_gain(const std::string& name) const {
        return device->getGain(SOAPY_SDR_RX, channel_idx, name);
    }

    bool lost() const { return _lost; }

    void set_gain(double gain) {
        try {
            device->setGain(SOAPY_SDR_RX, channel_idx, gain);
            gain_db = gain;
        } catch (const std::exception& e) {
            std::cerr << "SourceSoapySDRBlock: set_gain failed: " << e.what() << std::endl;
        }
    }

    void set_sample_rate(double rate) {
        try {
            device->setSampleRate(SOAPY_SDR_RX, channel_idx, rate);
            sample_rate = rate;
            if (device->getBandwidthRange(SOAPY_SDR_RX, channel_idx).size() > 0) {
                device->setBandwidth(SOAPY_SDR_RX, channel_idx, rate);
            }
        } catch (const std::exception& e) {
            std::cerr << "SourceSoapySDRBlock: set_sample_rate failed: " << e.what() << std::endl;
        }
    }

    void set_bandwidth(double bw) {
        try {
            device->setBandwidth(SOAPY_SDR_RX, channel_idx, bw);
        } catch (const std::exception& e) {
            std::cerr << "SourceSoapySDRBlock: set_bandwidth failed: " << e.what() << std::endl;
        }
    }

    bool set_antenna(const std::string& antenna) {
        auto antennas = device->listAntennas(SOAPY_SDR_RX, channel_idx);
        if (std::find(antennas.begin(), antennas.end(), antenna) == antennas.end()) {
            std::cerr << "Antenna '" << antenna << "' not supported. Available:";
            for (const auto& ant : antennas) {
                std::cerr << " " << ant;
            }
            std::cerr << std::endl;
            return false;
        }
        device->setAntenna(SOAPY_SDR_RX, channel_idx, antenna);
        return true;
    }
    
    void set_dc_offset_mode(bool automatic) {
        if (device->hasDCOffsetMode(SOAPY_SDR_RX, channel_idx)) {
            device->setDCOffsetMode(SOAPY_SDR_RX, channel_idx, automatic);
        }
    }
    
    void set_agc_mode(bool enable) {
        if (device->hasGainMode(SOAPY_SDR_RX, channel_idx)) {
            device->setGainMode(SOAPY_SDR_RX, channel_idx, enable);
        }
    }
    
    double get_frequency() const { return center_freq; }
    double get_gain() const { return gain_db; }
    double get_sample_rate() const { return sample_rate; }
    
    double get_bandwidth() const {
        return device->getBandwidth(SOAPY_SDR_RX, channel_idx);
    }
    
    std::string get_antenna() const {
        return device->getAntenna(SOAPY_SDR_RX, channel_idx);
    }
    
    std::vector<std::string> list_antennas() const {
        return device->listAntennas(SOAPY_SDR_RX, channel_idx);
    }
    
    SoapySDR::RangeList get_frequency_range() const {
        return device->getFrequencyRange(SOAPY_SDR_RX, channel_idx);
    }
    
    SoapySDR::Range get_gain_range() const {
        return device->getGainRange(SOAPY_SDR_RX, channel_idx);
    }
    
    std::vector<std::string> list_gains() const {
        return device->listGains(SOAPY_SDR_RX, channel_idx);
    }
    
    SoapySDR::Range get_gain_range(const std::string& name) const {
        return device->getGainRange(SOAPY_SDR_RX, channel_idx, name);
    }
    
    SoapySDR::RangeList get_sample_rate_range() const {
        return device->getSampleRateRange(SOAPY_SDR_RX, channel_idx);
    }
    
private:
    std::string device_args;
    double center_freq;
    double sample_rate;
    double gain_db;
    size_t channel_idx;

    SoapySDR::Device* device;
    SoapySDR::Stream* stream;

    size_t mtu;
    size_t overflow_count = 0;
    bool _lost = false;
};

using SourceSoapySDRBlockCF32 = SourceSoapySDRBlock<std::complex<float>>;
using SourceSoapySDRBlockCS16 = SourceSoapySDRBlock<std::complex<int16_t>>;
using SourceSoapySDRBlockCS8 = SourceSoapySDRBlock<std::complex<int8_t>>;
using SourceSoapySDRBlockCU8 = SourceSoapySDRBlock<std::complex<uint8_t>>;
using SourceSoapySDRBlockS32 = SourceSoapySDRBlock<int32_t>;
using SourceSoapySDRBlockS16 = SourceSoapySDRBlock<int16_t>;
using SourceSoapySDRBlockU8 = SourceSoapySDRBlock<uint8_t>;
using SourceSoapySDRBlockF32 = SourceSoapySDRBlock<float>;

// Helper function for device selection
struct SoapyDeviceInfo {
    std::string driver;
    std::string label;
    std::string serial;
    SoapySDR::Kwargs args;
    
    std::string get_args_string() const {
        return SoapySDR::KwargsToString(args);
    }
};

inline std::vector<SoapyDeviceInfo> enumerate_devices() {
    std::vector<SoapyDeviceInfo> devices;
    auto results = SoapySDR::Device::enumerate();
    
    for (const auto& result : results) {
        SoapyDeviceInfo info;
        info.args = result;

        if (result.count("driver")) info.driver = result.at("driver");
        if (result.count("label")) info.label = result.at("label");
        if (result.count("serial")) info.serial = result.at("serial");
        
        devices.push_back(info);
    }
    
    return devices;
}`,it=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/misc/uhd_common.hpp"

#ifdef __has_include
    #if __has_include(<uhd/usrp/multi_usrp.hpp>)
        #include <uhd/usrp/multi_usrp.hpp>
        #include <uhd/types/tune_request.hpp>
        #include <uhd/types/metadata.hpp>
        #include <uhd/utils/thread.hpp>
    #else
        #error "UHD headers not found. Please install libuhd-dev package."
    #endif
#endif

#include <vector>
#include <string>
#include <numeric>
#include <atomic>
#include <mutex>

template<typename T>
struct SourceUHDBlock : public cler::BlockBase {
    static constexpr bool may_block = true;

    SourceUHDBlock(const char* name,
                   double freq,
                   double rate,
                   const std::string& dvc_adrs = "",
                   double gain = 20.0,
                   size_t num_channels = 1,
                   const std::string& otw_format = "sc16",
                   bool quiet = false)
        : BlockBase(name),
          center_freq(freq),
          sample_rate(rate),
          device_address(dvc_adrs),
          gain_db(gain),
          _num_channels(num_channels),
          wire_format(otw_format),
          _quiet(quiet),
          _configuring(false) {

        try {
            usrp = uhd::usrp::multi_usrp::make(device_address);
        } catch (const uhd::exception& error) {
            std::string message = std::string("SourceUHDBlock: ") + error.what();
            cler::panic(message.c_str());
        }
        if (!usrp) {
            cler::panic("SourceUHDBlock: Failed to create USRP device");
        }
        if (num_channels > usrp->get_rx_num_channels()) {
            cler::panic("SourceUHDBlock: Not enough RX channels");
        }
        _write_ptrs.resize(num_channels);
        _write_sizes.resize(num_channels);
        _rx_counts.resize(num_channels);
        uhd::set_thread_priority_safe(0.5, true);
        for (size_t ch = 0; ch < num_channels; ++ch) {
            usrp->set_rx_rate(sample_rate, ch);
            double actual_rate = usrp->get_rx_rate(ch);
            if (ch == 0 && std::abs(actual_rate - sample_rate) > 1.0) {
                sample_rate = actual_rate;
            }
            usrp->set_rx_freq(uhd::tune_request_t(center_freq), ch);
            usrp->set_rx_gain(gain_db, ch);
        }
        // Hardware may snap the requested rate; sample_rate now holds the actual value.
        _actual_rate.store(sample_rate, std::memory_order_release);
        _last_applied_rate = sample_rate;
        uhd::stream_args_t stream_args(get_uhd_format<T>(), wire_format);
        stream_args.channels.resize(num_channels);
        std::iota(stream_args.channels.begin(), stream_args.channels.end(), 0);

        rx_stream = usrp->get_rx_stream(stream_args);
        if (!rx_stream) {
            cler::panic("SourceUHDBlock: Failed to setup RX stream");
        }
        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
        stream_cmd.stream_now = true;
        rx_stream->issue_stream_cmd(stream_cmd);
        // quiet=true for apps that reconfigure immediately after construction,
        // since these values would only describe the ctor args, not the final state.
        if (!_quiet) {
            std::cout << "SourceUHDBlock: Initialized " << usrp->get_mboard_name() << std::endl;
            std::cout << "  Channels: " << num_channels << std::endl;
            std::cout << "  Frequency: " << center_freq/1e6 << " MHz" << std::endl;
            std::cout << "  Sample rate: " << sample_rate/1e6 << " MSPS" << std::endl;
            std::cout << "  Gain: " << gain_db << " dB" << std::endl;
            std::cout << "  Format: CPU=" << get_uhd_format<T>() << ", OTW=" << wire_format << std::endl;
        }
    }

    ~SourceUHDBlock() {
        if (overflow_count > 0) {
            std::cout << "SourceUHDBlock: Total overflows: " << overflow_count << std::endl;
        }
    }

    bool configure(const UHDConfig& config, size_t channel = 0) {
        _configuring = true;

        // try/catch kept despite the style guide: the UHD API throws on errors.
        try {
            // Skip set_rx_rate when unchanged: callers re-send the same rate on
            // every freq/gain update, and a mid-stream set_rx_rate can glitch
            // some devices (e.g. B2xx).
            if (std::abs(config.sample_rate_Hz - _last_applied_rate) > RATE_EPSILON_HZ) {
                usrp->set_rx_rate(config.sample_rate_Hz, channel);
                double actual_rate = usrp->get_rx_rate(channel);
                if (std::abs(actual_rate - config.sample_rate_Hz) > 1.0) {
                    std::cout << "Warning: Requested " << config.sample_rate_Hz/1e6
                              << " MSPS, got " << actual_rate/1e6 << " MSPS" << std::endl;
                }
                _last_applied_rate = config.sample_rate_Hz;
                _actual_rate.store(actual_rate, std::memory_order_release);
            }

            auto freq_range = usrp->get_rx_freq_range(channel);
            if (config.center_freq_Hz < freq_range.start() || 
                config.center_freq_Hz > freq_range.stop()) {
                std::cerr << "Frequency " << config.center_freq_Hz/1e6 
                          << " MHz out of range" << std::endl;
            }
            usrp->set_rx_freq(uhd::tune_request_t(config.center_freq_Hz), channel);
            center_freq = config.center_freq_Hz;

            auto gain_range = usrp->get_rx_gain_range(channel);
            if (config.gain < gain_range.start() || config.gain > gain_range.stop()) {
                std::cerr << "Gain " << config.gain << " dB out of range" << std::endl;
            }
            usrp->set_rx_gain(config.gain, channel);
            gain_db = config.gain;

            // Same no-op skip as the rate above.
            if (config.bandwidth_Hz > 0 &&
                std::abs(config.bandwidth_Hz - _last_applied_bw) > RATE_EPSILON_HZ) {
                usrp->set_rx_bandwidth(config.bandwidth_Hz, channel);
                _last_applied_bw = config.bandwidth_Hz;
            }
            _configuring = false;
            return true;

        } catch (const std::exception& e) {
            std::cerr << "Configuration failed: " << e.what() << std::endl;
            _configuring = false;
            return false;
        }
    }

    // The driver may snap a requested rate to what its clocking supports;
    // this is the actual running rate, derived values must use it, not the
    // requested one. Written by the ctor and by configure() (streaming
    // thread); safe to read from any thread (e.g. a GUI polling for a change).
    double actual_sample_rate() const {
        return _actual_rate.load(std::memory_order_acquire);
    }

    // Callable from any thread; only stages the request here. It is applied by
    // the streaming thread at the top of procedure(), since multi_usrp is not
    // safe against a concurrent recv().
    void request_configure(const UHDConfig& cfg) {
        std::lock_guard<std::mutex> lk(_cfg_mutex);
        _pending_cfg = cfg;
        _pending_cfg_gen.fetch_add(1, std::memory_order_release);
    }

    template<typename... OChannels>
    cler::Result<cler::Empty, cler::Error> procedure(OChannels*... outs) {
        constexpr size_t num_outs = sizeof...(OChannels);
        if (num_outs != _num_channels) {
            cler::panic("SourceUHDBlock: number of procedure() output channels must match num_channels from constructor");
        }

        if (_pending_cfg_gen.load(std::memory_order_acquire) != _applied_cfg_gen) {
            UHDConfig cfg;
            {
                std::lock_guard<std::mutex> lk(_cfg_mutex);
                cfg = _pending_cfg;
                _applied_cfg_gen = _pending_cfg_gen.load(std::memory_order_acquire);
            }
            configure(cfg, 0);
        }

        if (_configuring.load(std::memory_order_acquire)) {
            return cler::Error::NotEnoughSpace;
        }

        size_t probe_ch = 0;
        bool all_have_space = true;
        ([&](OChannels* out) {
            auto [wp, ws] = out->write_dbf();
            _write_ptrs[probe_ch] = wp;
            _write_sizes[probe_ch] = ws;
            if (!wp || ws == 0) all_have_space = false;
            ++probe_ch;
        }(outs), ...);
        if (!all_have_space) {
            return cler::Error::NotEnoughSpace;
        }

        uhd::rx_metadata_t md;
        cler::Error result_error = cler::Error::OK;
        size_t recv_ch = 0;
        for (size_t i = 0; i < num_outs; ++i) _rx_counts[i] = 0;
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ([&](OChannels* out) {
                const size_t ch = recv_ch++;

                if (result_error != cler::Error::OK) {
                    return;
                }
                T* write_ptr = _write_ptrs[ch];
                const size_t write_size = _write_sizes[ch];
                size_t num_rx = 0;
                try {
                    num_rx = rx_stream->recv(write_ptr, write_size, md, 0.1);
                } catch (const std::exception& e) {
                    std::cerr << "SourceUHDBlock: recv failed: " << e.what() << std::endl;
                    _lost.store(true, std::memory_order_relaxed);
                    result_error = cler::Error::TERM_ProcedureError;
                    return;
                }
                if (num_rx == 0) {
                    return;
                }
                if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
                    overflow_count++;
                } else if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE &&
                            md.error_code != uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
                    std::cerr << "SourceUHDBlock: " << md.strerror() << std::endl;
                    _lost.store(true, std::memory_order_relaxed);
                    result_error = cler::Error::TERM_ProcedureError;
                    return;
                }

                _rx_counts[ch] = num_rx;
            }(outs), ...);
        }(std::make_index_sequence<num_outs>{});



        if (result_error != cler::Error::OK) {
            return result_error;
        }

        size_t aligned = _rx_counts[0];
        for (size_t i = 1; i < num_outs; ++i) {
            aligned = std::min(aligned, _rx_counts[i]);
        }
        if (aligned == 0) {
            return cler::Error::NotEnoughSamples;
        }

        ([&](OChannels* out) { out->commit_write(aligned); }(outs), ...);

        return cler::Empty{};


    }
    size_t get_overflow_count() const { return overflow_count; }

    static bool can_open(const std::string& addr) {
        try {
            return uhd::usrp::multi_usrp::make(addr) != nullptr;
        } catch (const std::exception&) {
            return false;
        }
    }

    bool lost() const { return _lost.load(std::memory_order_relaxed); }
    double get_frequency() const { return center_freq; }
    double get_gain() const { return gain_db; }
    uhd::freq_range_t rx_freq_range() const { return usrp->get_rx_freq_range(0); }
    uhd::gain_range_t rx_gain_range() const { return usrp->get_rx_gain_range(0); }
    uhd::meta_range_t rx_rate_range() const { return usrp->get_rx_rates(0); }
    std::vector<std::string> rx_antennas() const { return usrp->get_rx_antennas(0); }
    std::string rx_antenna() const { return usrp->get_rx_antenna(0); }
    void set_rx_antenna(const std::string& a) { usrp->set_rx_antenna(a, 0); }

protected:
    uhd::usrp::multi_usrp::sptr usrp;
    uhd::rx_streamer::sptr rx_stream;

private:
    double center_freq;
    double sample_rate;
    std::string device_address;
    double gain_db;
    size_t _num_channels;
    std::vector<T*> _write_ptrs;
    std::vector<size_t> _write_sizes;
    std::vector<size_t> _rx_counts;
    std::string wire_format;
    bool _quiet;
    std::atomic<bool> _configuring;
    std::atomic<bool> _lost{false};
    size_t overflow_count = 0;

    // Requested rates/bandwidths closer than this are treated as identical.
    static constexpr double RATE_EPSILON_HZ = 0.5;

    // Touched only by the ctor and configure() (streaming thread).
    std::atomic<double> _actual_rate{0.0};
    double _last_applied_rate = 0.0;
    double _last_applied_bw   = -1.0;   // <0: never set (ctor doesn't set it)

    // Live-reconfigure staging (written by any thread, applied by streaming thread)
    std::mutex _cfg_mutex;
    UHDConfig _pending_cfg;
    std::atomic<size_t> _pending_cfg_gen{0};
    size_t _applied_cfg_gen{0};
};

using SourceUHDBlockCF32 = SourceUHDBlock<std::complex<float>>;
using SourceUHDBlockSC16 = SourceUHDBlock<std::complex<int16_t>>;
using SourceUHDBlockSC8 = SourceUHDBlock<std::complex<int8_t>>;`,ot=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/plots/spectral_windows.hpp"
#include "desktop_blocks/spectrum/spectrum_frame.hpp"
#include "liquid.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

// Headless spectrum: FFT in procedure(), window + power average over up to
// \`avg\` consecutive frames, at most \`fps\` frames per second. A tap off a
// fanout: it drains its input every call and never backpressures the chain.
// Samples accumulate across calls until avg*n are held, so a scheduler handing
// out small spans still produces frames.
struct SpectrumBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    SpectrumBlock(const char* name, double rate_hz, size_t n_fft = 1024, float fps = 20.0f,
                  float db_min = -120.0f, float db_step = 0.5f, size_t avg = 4,
                  SpectralWindow window = SpectralWindow::Hann, size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? std::max(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(std::complex<float>), 4 * n_fft) : buffer_size),
          _n(n_fft), _avg(avg == 0 ? 1 : avg), _db_min(db_min), _db_step(db_step),
          _min_interval(std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / (fps > 0.0f ? fps : 20.0f)))),
          _rate(rate_hz), _center(0.0), _gen(0),
          _acc(n_fft * (avg == 0 ? 1 : avg)), _buf(n_fft), _window(n_fft), _power(n_fft)
    {
        if (n_fft < 16 || n_fft > SpectrumFrame::MAX_N || (n_fft & (n_fft - 1)) != 0) {
            cler::panic("SpectrumBlock: n_fft must be a power of two in [16, 4096]");
        }
        if (db_step <= 0.0f) cler::panic("SpectrumBlock: db_step must be positive");
        float gain = 0.0f;
        for (size_t i = 0; i < n_fft; ++i) {
            _window[i] = spectral_window_function(window, static_cast<float>(i) / static_cast<float>(n_fft - 1));
            gain += _window[i];
        }
        _scale2 = gain * gain;
        _plan = fft_create_plan(static_cast<unsigned int>(n_fft),
                                reinterpret_cast<liquid_float_complex*>(_buf.data()),
                                reinterpret_cast<liquid_float_complex*>(_buf.data()),
                                LIQUID_FFT_FORWARD, 0);
        if (!_plan) cler::panic("SpectrumBlock: fft_create_plan failed");
        _last = std::chrono::steady_clock::now() - _min_interval;
    }

    ~SpectrumBlock() { if (_plan) fft_destroy_plan(_plan); }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<SpectrumFrame>* out) {
        auto [rptr, rsize] = in.read_dbf();
        if (rsize == 0) return cler::Error::NotEnoughSamples;

        const size_t keep = std::min(rsize, _acc.size() - _held);
        std::copy_n(rptr + rsize - keep, keep, _acc.data() + _held);
        _held += keep;
        in.commit_read(rsize);

        if (_held < _acc.size()) return cler::Empty{};
        _held = 0;
        const auto now = std::chrono::steady_clock::now();
        if (now - _last >= _min_interval) {
            auto [wptr, wsize] = out->write_dbf();
            if (wsize > 0) {
                const size_t frames = _avg;
                std::fill(_power.begin(), _power.end(), 0.0f);
                for (size_t f = 0; f < frames; ++f) {
                    for (size_t i = 0; i < _n; ++i) _buf[i] = _acc[f * _n + i] * _window[i];
                    fft_execute(_plan);
                    for (size_t i = 0; i < _n; ++i) _power[i] += std::norm(_buf[i]);
                }
                const float norm = 1.0f / (_scale2 * static_cast<float>(frames));
                SpectrumFrame& fr = wptr[0];
                fr.gen = _gen.load(std::memory_order_relaxed);
                fr.center_hz = _center.load(std::memory_order_relaxed);
                fr.rate_hz = _rate.load(std::memory_order_relaxed);
                fr.n = static_cast<uint16_t>(_n);
                fr.db_min = _db_min;
                fr.db_step = _db_step;
                const size_t half = _n / 2;
                for (size_t i = 0; i < _n; ++i) {
                    const float db = 10.0f * std::log10(_power[(i + half) % _n] * norm + 1e-20f);
                    const float q = (db - _db_min) / _db_step;
                    fr.bins[i] = static_cast<uint8_t>(q <= 0.0f ? 0.0f : q >= 255.0f ? 255.0f : q + 0.5f);
                }
                out->commit_write(1);
                _last = now;
            }
        }
        return cler::Empty{};
    }

    void set_rate(double hz) { _rate.store(hz, std::memory_order_relaxed); }
    void set_center(double hz) { _center.store(hz, std::memory_order_relaxed); }
    void set_gen(uint32_t gen) { _gen.store(gen, std::memory_order_relaxed); }
    size_t n_fft() const { return _n; }

private:
    size_t _n, _avg;
    float _db_min, _db_step, _scale2 = 1.0f;
    std::chrono::steady_clock::duration _min_interval;
    std::chrono::steady_clock::time_point _last;
    std::atomic<double> _rate, _center;
    std::atomic<uint32_t> _gen;
    size_t _held = 0;
    std::vector<std::complex<float>> _acc, _buf;
    std::vector<float> _window, _power;
    fftplan _plan = nullptr;
};
`,_t=`#pragma once

#include <cstddef>
#include <cstdint>

// One averaged power spectrum, dB quantised to u8 on a fixed scale so a
// consumer can draw an axis without autoscale: dB = db_min + bins[i] * db_step.
// Fixed-size so it is a POD channel element; n <= MAX_N bins are valid.
struct SpectrumFrame {
    static constexpr size_t MAX_N = 4096;
    uint32_t gen;
    double center_hz;
    double rate_hz;
    uint16_t n;
    float db_min, db_step;
    uint8_t bins[MAX_N];
};
`,ct=`#pragma once

#include "cler.hpp"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <cstring>
#include <vector>

// Zero-span capture trigger for a real-valued stream, rendered as an
// oscilloscope: each trigger paints one fixed window pinned at t=0, replacing
// the previous frame. Sink block (no output channel): consumes continuously
// so it never backs up the upstream source. procedure() (block thread) never
// touches ImGui; it publishes to a mutex-guarded snapshot that render() (GUI
// thread) reads.
template <typename T = float>
struct TriggerBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;
    cler::Channel<T> in;

    enum class Edge   { Rising, Falling };
    enum class Mode   { Normal, Single, Auto };
    enum class State  { Idle, Armed, Capturing };

    TriggerBlock(const char* name,
                 size_t sample_rate,
                 float  threshold,          // same units as the input stream, e.g. dB
                 float  window_ms,
                 float  pretrigger_pct = 10.0f,
                 float  holdoff_ms     = 100.0f,
                 Edge   edge           = Edge::Rising,
                 Mode   mode           = Mode::Auto,
                 float  hysteresis     = 3.0f,
                 float  auto_ms        = 200.0f,
                 float  max_window_ms  = 0.0f,    // 0 => use window_ms as the max
                 size_t buffer_size    = 65536)
        : BlockBase(name),
          in(buffer_size),
          _ctor_rate(sample_rate)
    {
        float max_ms = (max_window_ms > 0.0f) ? std::max(max_window_ms, window_ms) : window_ms;
        _max_window  = ms_to_samples(max_ms);
        if (_max_window < 1) _max_window = 1;
        // Ceiling: ~16 bytes/sample across buffers, so 16 Msamples ~= 256 MB.
        if (_max_window > MAX_CAPTURE_SAMPLES) _max_window = MAX_CAPTURE_SAMPLES;

        _capture  = new T[_max_window];
        _ring     = new T[_max_window];
        _snap_buf = new float[_max_window];   // published frame; DSP thread writes it under _snap_mutex
        _render_y = new float[_max_window];
        _x_render = new float[MAX_PLOT_POINTS];
        _plot_x   = new float[MAX_PLOT_POINTS];
        _plot_y   = new float[MAX_PLOT_POINTS];

        _requested_rate.store(_ctor_rate, std::memory_order_release);
        _snap_rate = _ctor_rate;
        _render_rate = _ctor_rate;

        Config c;
        c.sample_rate        = _ctor_rate;
        c.threshold          = threshold;
        c.window_samples     = clamp_window(ms_to_samples(window_ms));
        c.pretrigger_samples = clamp_pre(static_cast<size_t>(c.window_samples * (pretrigger_pct / 100.0f)),
                                         c.window_samples);
        c.holdoff_samples    = ms_to_samples(holdoff_ms);
        c.auto_samples       = ms_to_samples(auto_ms);
        c.edge               = edge;
        c.mode               = mode;
        c.hysteresis         = std::max(0.0f, hysteresis);
        {
            std::lock_guard<std::mutex> lk(_cfg_mutex);
            _pending     = c;
            _pending_gen = 1;
        }
        _active = c;
        _state  = State::Armed;
    }

    ~TriggerBlock() {
        delete[] _capture;
        delete[] _ring;
        delete[] _snap_buf;
        delete[] _render_y;
        delete[] _x_render;
        delete[] _plot_x;
        delete[] _plot_y;
    }

    // GUI/render thread. sample_rate is part of Config so a rate change and its
    // derived sample counts land on the block thread as one generation. Capture
    // buffers are sized once at construction and never reallocate, so the
    // achievable window in milliseconds shrinks as the live rate rises.
    void set_config(float threshold, float window_ms, float pretrigger_pct,
                    float holdoff_ms, Edge edge, Mode mode,
                    float hysteresis, float auto_ms, size_t sample_rate) {
        if (sample_rate < 1) sample_rate = 1;
        Config c;
        c.sample_rate        = sample_rate;
        c.threshold          = threshold;
        c.window_samples     = clamp_window(ms_to_samples_at(window_ms, sample_rate));
        c.pretrigger_samples = clamp_pre(static_cast<size_t>(c.window_samples * (pretrigger_pct / 100.0f)),
                                         c.window_samples);
        c.holdoff_samples    = ms_to_samples_at(holdoff_ms, sample_rate);
        c.auto_samples       = ms_to_samples_at(auto_ms, sample_rate);
        c.edge               = edge;
        c.mode               = mode;
        c.hysteresis         = std::max(0.0f, hysteresis);
        _requested_rate.store(sample_rate, std::memory_order_release);
        std::lock_guard<std::mutex> lk(_cfg_mutex);
        _pending = c;
        ++_pending_gen;
    }

    void force_trigger() { _force.store(true, std::memory_order_release); }
    void rearm()         { _rearm.store(true, std::memory_order_release); }

    State  state()       const { return _state.load(std::memory_order_acquire); }
    // Published-frame counter, lock-free mirror of the snapshot's _frame_count
    // (same value render() shows as "Frames"). Lets a poller notice a new
    // capture without export_frame()'s copy or touching _snap_mutex.
    unsigned long frame_count() const {
        return _frames_published.load(std::memory_order_acquire);
    }
    // Rate of the most recently requested config (ctor rate until set_config
    // is first called). max_window_ms() is the fixed sample capacity expressed
    // at that rate, so it shrinks as the requested rate rises.
    size_t sample_rate() const { return _requested_rate.load(std::memory_order_acquire); }
    size_t max_window_samples() const { return _max_window; }
    float  max_window_ms() const {
        return 1000.0f * static_cast<float>(_max_window)
             / static_cast<float>(_requested_rate.load(std::memory_order_acquire));
    }

    void set_initial_window(float x, float y, float w, float h) {
        _win_pos  = ImVec2(x, y);
        _win_size = ImVec2(w, h);
    }

    // Copies the latest published frame (same one render() draws) into caller
    // storage; t_ms[i] = (i - trig_idx) * 1000 / sample_rate_hz, trigger at t=0.
    // sample_rate_hz is the rate the frame was captured at, not the live rate.
    // May allocate; call from the GUI thread only. False if nothing published yet.
    bool export_frame(std::vector<float>& samples, float& pre_ms, float& post_ms,
                      size_t& trig_idx, unsigned long& frame_count,
                      size_t& sample_rate_hz) {
        std::lock_guard<std::mutex> lk(_snap_mutex);
        if (_snap_len == 0) return false;
        samples.assign(_snap_buf, _snap_buf + _snap_len);
        pre_ms         = _snap_pre_ms;
        post_ms        = _snap_post_ms;
        trig_idx       = _snap_trig_idx;
        frame_count    = _frame_count;
        sample_rate_hz = _snap_rate;
        return true;
    }

    // One-shot: the next render() applies this rect then clears the request, so
    // the user can move/resize afterward. GUI thread only (procedure() never touches these).
    void apply_window_rect(float x, float y, float w, float h) {
        _pending_rect_pos  = ImVec2(x, y);
        _pending_rect_size = ImVec2(w, h);
        _pending_rect      = true;
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        State st = _state.load(std::memory_order_relaxed);
        // Abort an in-flight capture on reconfig/re-arm: it was sized in samples
        // under the OLD rate, so at a much lower new rate it could take tens of
        // seconds to fill and freeze the scope until then. Discard (never
        // published) and fall through so the new config applies immediately.
        if (st == State::Capturing &&
            (_pending_gen.load(std::memory_order_acquire) != _applied_gen ||
             _rearm.load(std::memory_order_acquire))) {
            _state.store(State::Armed, std::memory_order_release);
            st = State::Armed;
        }
        if (st == State::Armed || st == State::Idle) {
            maybe_apply_config();
            if (_rearm.exchange(false, std::memory_order_acq_rel)) { arm(); st = State::Armed; }
        }
        switch (st) {
            case State::Idle:      return drain_input_idle();
            case State::Armed:     return scan_for_trigger();
            case State::Capturing: return fill_capture();
        }
        return cler::Empty{};
    }

    void set_visible(bool visible) { _visible = visible; }

    void render() {
        if (!_visible) return;
        // Always is applied once, not every frame, or the window would snap
        // back and become unresizable.
        if (_pending_rect) {
            ImGui::SetNextWindowPos(_pending_rect_pos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(_pending_rect_size, ImGuiCond_Always);
            _pending_rect = false;
        } else {
            ImGui::SetNextWindowSize(_win_size, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(_win_pos, ImGuiCond_FirstUseEver);
        }
        ImGui::Begin(name());

        size_t len = 0, trig_idx = 0, rate = _ctor_rate;
        float  pre_ms = 0.0f, post_ms = 0.0f, level = 0.0f;
        unsigned long frame = 0;
        if (_snap_mutex.try_lock()) {
            len      = _snap_len;
            trig_idx = _snap_trig_idx;
            pre_ms   = _snap_pre_ms;
            post_ms  = _snap_post_ms;
            level    = _snap_level;
            frame    = _frame_count;
            rate     = _snap_rate;
            if (len > 0) std::memcpy(_render_y, _snap_buf, len * sizeof(float));
            _snap_mutex.unlock();
        } else {
            // Couldn't grab the lock this frame; reuse last render copy.
            len      = _render_len;
            trig_idx = _render_trig;
            pre_ms   = _render_pre_ms;
            post_ms  = _render_post_ms;
            level    = _render_level;
            frame    = _render_frame;
            rate     = _render_rate;
        }
        _render_len = len; _render_trig = trig_idx; _render_pre_ms = pre_ms;
        _render_post_ms = post_ms; _render_level = level; _render_frame = frame;
        _render_rate = rate;

        ImGui::Text("Frames: %lu", frame);
        ImGui::SameLine();
        ImGui::Text("| trigger @ t=0, window [%.1f, %.1f] ms", -pre_ms, post_ms);

        if (len == 0) {
            ImGui::TextUnformatted("Waiting for trigger...");
            ImGui::End();
            return;
        }

        // rate is the snapshot's captured rate, so a live rate change never
        // mislabels an already-published frame.
        const float dt_ms = 1000.0f / static_cast<float>(rate);
        const float* plot_x;
        const float* plot_y;
        int          plot_n;
        if (len <= MAX_PLOT_POINTS) {
            for (size_t i = 0; i < len; ++i)
                _x_render[i] = (static_cast<float>(i) - static_cast<float>(trig_idx)) * dt_ms;
            plot_x = _x_render; plot_y = _render_y; plot_n = static_cast<int>(len);
        } else {
            size_t nb = MAX_PLOT_POINTS / 2;             // 2 points (min,max) per bucket
            for (size_t b = 0; b < nb; ++b) {
                size_t s = (b * len) / nb;
                size_t e = ((b + 1) * len) / nb;
                if (e <= s) e = s + 1;
                if (e > len) e = len;
                float mn = _render_y[s], mx = _render_y[s];
                for (size_t i = s + 1; i < e; ++i) {
                    mn = std::min(mn, _render_y[i]);
                    mx = std::max(mx, _render_y[i]);
                }
                _plot_x[2 * b]     = (static_cast<float>(s)     - static_cast<float>(trig_idx)) * dt_ms;
                _plot_y[2 * b]     = mn;
                _plot_x[2 * b + 1] = (static_cast<float>(e - 1) - static_cast<float>(trig_idx)) * dt_ms;
                _plot_y[2 * b + 1] = mx;
            }
            plot_x = _plot_x; plot_y = _plot_y; plot_n = static_cast<int>(2 * nb);
        }

        if (ImPlot::BeginPlot("##scope", ImVec2(-1, -1))) {
            ImPlot::SetupAxes("Time [ms]", "Power [dB]");
            // Fixed timebase so the trigger point stays put across frames.
            ImPlot::SetupAxisLimits(ImAxis_X1, -pre_ms, post_ms, ImPlotCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, -120.0, 10.0, ImPlotCond_Once);

            ImPlot::PlotLine("Power", plot_x, plot_y, plot_n);

            double lvl = level;
            ImPlotSpec hspec;
            hspec.Flags = ImPlotInfLinesFlags_Horizontal;
            ImPlot::PlotInfLines("Level", &lvl, 1, hspec);
            double t0 = 0.0;
            ImPlot::PlotInfLines("Trig", &t0, 1);
            ImPlot::EndPlot();
        }
        ImGui::End();
    }

private:
    bool _visible = true;

    struct Config {
        size_t sample_rate        = 1;   // rate the sample counts below were derived at
        float  threshold          = 0.0f;
        size_t window_samples     = 1;
        size_t pretrigger_samples = 0;
        size_t holdoff_samples    = 0;
        size_t auto_samples       = 0;
        Edge   edge               = Edge::Rising;
        Mode   mode               = Mode::Auto;
        float  hysteresis         = 0.0f;
    };

    static size_t ms_to_samples_at(float ms, size_t rate) {
        long n = static_cast<long>((ms / 1000.0f) * static_cast<float>(rate));
        return n < 0 ? 0 : static_cast<size_t>(n);
    }
    size_t ms_to_samples(float ms) const { return ms_to_samples_at(ms, _ctor_rate); }
    size_t clamp_window(size_t w) const {
        if (w < 1) w = 1;
        return std::min(w, _max_window);
    }
    static size_t clamp_pre(size_t p, size_t window) {
        if (window == 0) return 0;
        return std::min(p, window - 1);   // leave >=1 post-trigger sample
    }

    void maybe_apply_config() {
        size_t gen = _pending_gen.load(std::memory_order_acquire);
        if (gen == _applied_gen) return;
        {
            std::lock_guard<std::mutex> lk(_cfg_mutex);
            _active      = _pending;
            _applied_gen = gen;
        }
        _active.window_samples     = clamp_window(_active.window_samples);
        _active.pretrigger_samples = clamp_pre(_active.pretrigger_samples, _active.window_samples);
        reset_ring();
        reset_edge_latch();
        _holdoff_counter = 0;
        _auto_counter    = 0;
    }

    void arm() {
        _state.store(State::Armed, std::memory_order_release);
        _auto_counter    = 0;
        _holdoff_counter = 0;
    }
    void reset_ring()       { _ring_w = 0; _ring_fill = 0; }
    void reset_edge_latch() { _armed_band = false; }

    inline void ring_push(T s) {
        size_t pre = _active.pretrigger_samples;
        if (pre == 0) return;
        _ring[_ring_w] = s;
        _ring_w = (_ring_w + 1) % pre;
        if (_ring_fill < pre) _ring_fill++;
    }

    // Latch persists across procedure() calls, so an edge is never missed or double-fired at a call seam.
    inline bool edge_fires(T s) {
        const float lvl = _active.threshold;
        const float h   = _active.hysteresis;
        if (_active.edge == Edge::Rising) {
            if (s < lvl - h) _armed_band = true;
            if (_armed_band && s >= lvl) { _armed_band = false; return true; }
        } else {
            if (s > lvl + h) _armed_band = true;
            if (_armed_band && s <= lvl) { _armed_band = false; return true; }
        }
        return false;
    }

    cler::Result<cler::Empty, cler::Error> drain_input_idle() {
        size_t avail = in.size();
        if (avail == 0) return cler::Error::NotEnoughSamples;
        const T* p1; const T* p2; size_t s1, s2;
        in.peek_read(p1, s1, p2, s2);
        size_t n = std::min(avail, s1 + s2);
        for (size_t i = 0; i < n; ++i) {
            T s = (i < s1) ? p1[i] : p2[i - s1];
            ring_push(s);
            (void)edge_fires(s);
        }
        in.commit_read(n);
        return cler::Empty{};
    }

    cler::Result<cler::Empty, cler::Error> scan_for_trigger() {
        size_t avail = in.size();
        if (avail == 0) return cler::Error::NotEnoughSamples;
        const bool forced = _force.exchange(false, std::memory_order_acq_rel);

        const T* p1; const T* p2; size_t s1, s2;
        in.peek_read(p1, s1, p2, s2);
        size_t n = std::min(avail, s1 + s2);

        size_t consumed = 0;
        bool   triggered = false;
        bool   force_now = forced;
        for (size_t i = 0; i < n; ++i) {
            T s = (i < s1) ? p1[i] : p2[i - s1];
            ring_push(s);

            bool can_trigger = (_holdoff_counter == 0);
            if (_holdoff_counter > 0) _holdoff_counter--;

            bool fire = false;
            if (can_trigger) {
                if (force_now) fire = true;
                else if (edge_fires(s)) fire = true;
                else if (_active.mode == Mode::Auto && _active.auto_samples > 0 &&
                         ++_auto_counter >= _active.auto_samples) fire = true;
            } else {
                (void)edge_fires(s);
            }
            force_now = false;
            consumed++;
            if (fire) { triggered = true; break; }
        }
        in.commit_read(consumed);
        if (triggered) begin_capture();
        return cler::Empty{};
    }

    void begin_capture() {
        size_t pre      = _active.pretrigger_samples;
        size_t pre_have = std::min(_ring_fill, pre);
        if (pre > 0 && pre_have > 0) {
            size_t start = (_ring_w + pre - pre_have) % pre;   // oldest sample
            for (size_t j = 0; j < pre_have; ++j)
                _capture[j] = _ring[(start + j) % pre];
        }
        size_t post   = _active.window_samples - _active.pretrigger_samples;
        _capture_fill = pre_have;
        _capture_len  = std::min(pre_have + post, _max_window);
        _capture_trig = pre_have;          // trigger sits right after the pre-trigger run
        _auto_counter = 0;
        if (_capture_fill >= _capture_len) publish_frame();
        else _state.store(State::Capturing, std::memory_order_release);
    }

    cler::Result<cler::Empty, cler::Error> fill_capture() {
        size_t avail = in.size();
        if (avail == 0) return cler::Error::NotEnoughSamples;
        size_t need = _capture_len - _capture_fill;
        size_t got  = in.readN(_capture + _capture_fill, std::min(avail, need));
        // Keeps the pre-trigger ring advancing during capture too, otherwise
        // consecutive frames' pre-trigger segments visibly shift at the seam.
        for (size_t i = 0; i < got; ++i)
            ring_push(_capture[_capture_fill + i]);
        _capture_fill += got;
        if (_capture_fill >= _capture_len) publish_frame();
        return cler::Empty{};
    }

    void publish_frame() {
        // Uses _active.sample_rate (the rate this frame was captured at), not
        // the ctor rate, since the rate can change live.
        const float dt_ms = 1000.0f / static_cast<float>(_active.sample_rate);
        {
            std::lock_guard<std::mutex> lk(_snap_mutex);
            std::memcpy(_snap_buf, _capture, _capture_len * sizeof(T));
            _snap_len      = _capture_len;
            _snap_trig_idx = _capture_trig;
            _snap_rate     = _active.sample_rate;
            // From the configured window, not captured length, so the trigger
            // line stays fixed at the pre-trigger fraction across frames.
            _snap_pre_ms   = static_cast<float>(_active.pretrigger_samples) * dt_ms;
            _snap_post_ms  = static_cast<float>(_active.window_samples
                                                - _active.pretrigger_samples) * dt_ms;
            _snap_level    = _active.threshold;
            ++_frame_count;
        }
        // Published after the lock so a poller that sees the bump can then read
        // a fully written snapshot.
        _frames_published.store(_frame_count, std::memory_order_release);
        _holdoff_counter = _active.holdoff_samples;
        reset_edge_latch();
        if (_active.mode == Mode::Single) _state.store(State::Idle, std::memory_order_release);
        else arm();
    }

    std::mutex            _cfg_mutex;
    Config                _pending;
    Config                _active;
    std::atomic<size_t>   _pending_gen{0};
    size_t                _applied_gen{0};
    std::atomic<bool>     _force{false};
    std::atomic<bool>     _rearm{false};
    std::atomic<State>    _state{State::Armed};

    // _ctor_rate sizes buffer capacity and seeds the initial config; the live
    // rate travels inside Config, _requested_rate mirrors it for accessors.
    size_t  _ctor_rate;
    std::atomic<size_t> _requested_rate{1};
    size_t  _max_window = 0;
    T*      _capture = nullptr;
    T*      _ring    = nullptr;

    // Snapshot shared with the GUI thread; guarded by _snap_mutex.
    std::mutex    _snap_mutex;
    float*        _snap_buf = nullptr;          // written by the block thread under lock
    size_t        _snap_len = 0;
    size_t        _snap_trig_idx = 0;
    float         _snap_pre_ms = 0.0f;
    float         _snap_post_ms = 0.0f;
    float         _snap_level = 0.0f;
    size_t        _snap_rate = 1;   // rate the published frame was captured at
    unsigned long _frame_count = 0;
    // Lock-free mirror of _frame_count for frame_count(); written by the block
    // thread just after publish_frame() releases _snap_mutex.
    std::atomic<unsigned long> _frames_published{0};

    static constexpr size_t MAX_PLOT_POINTS    = 8000;  // large windows decimate to a min/max envelope of this many points
    static constexpr size_t MAX_CAPTURE_SAMPLES = 16u * 1024 * 1024;  // ~256 MB across buffers

    // GUI-thread-only render copies.
    float*        _render_y = nullptr;
    float*        _x_render = nullptr;
    float*        _plot_x = nullptr;   // decimated envelope x (large windows)
    float*        _plot_y = nullptr;   // decimated envelope y
    size_t        _render_len = 0, _render_trig = 0;
    size_t        _render_rate = 1;
    float         _render_pre_ms = 0.0f, _render_post_ms = 0.0f, _render_level = 0.0f;
    unsigned long _render_frame = 0;

    // Block-thread-only runtime state.
    size_t  _ring_w = 0, _ring_fill = 0;
    size_t  _capture_fill = 0, _capture_len = 0, _capture_trig = 0;
    size_t  _holdoff_counter = 0, _auto_counter = 0;
    bool    _armed_band = false;

    ImVec2 _win_pos{380.0f, 10.0f};
    ImVec2 _win_size{1100.0f, 430.0f};

    // One-shot rect request (GUI thread only; see apply_window_rect()).
    bool   _pending_rect = false;
    ImVec2 _pending_rect_pos{0.0f, 0.0f};
    ImVec2 _pending_rect_size{0.0f, 0.0f};
};
`,lt=`#pragma once
#include "cler.hpp"
#include <cstring>
#include <cerrno>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <unistd.h>
#include <assert.h>
#include <memory>
#include <string>

namespace UDPBlock {

enum class SocketType {
    INET_UDP,     // IPv4 UDP
    INET6_UDP,    // IPv6 UDP
    UNIX_DGRAM    // UNIX datagram
};

struct ParsedAddress {
    std::string address;
    uint16_t port;
};

ParsedAddress parse_address_string(SocketType type, const std::string& addr_str);
struct GenericDatagramSocket {
    static GenericDatagramSocket make_receiver(SocketType type,
                                           const std::string& bind_addr_str);

    static GenericDatagramSocket make_sender(SocketType type,
                                            const std::string& dest_addr_str);

    ~GenericDatagramSocket();
    void bind(const std::string& bind_addr_or_path, uint16_t port = 0);
    ssize_t send(const uint8_t* data, size_t len) const;
    ssize_t recv(uint8_t* buffer, size_t max_len, int flags = 0) const;
    inline bool is_valid() const { return _sockfd >= 0; }
    void set_receive_timeout(std::chrono::milliseconds timeout);

private:
    GenericDatagramSocket(SocketType type,
                const std::string& host_or_path,
                uint16_t port = 0);

    SocketType _type;
    int _sockfd;

    struct sockaddr_in  _dest_inet {};
    struct sockaddr_in6 _dest_inet6 {};
    struct sockaddr_un  _dest_un {};

    std::string _bound_unix_path {}; // track UNIX socket file for cleanup
};

} // namespace UDPBlock
`,dt=`#pragma once
#include "shared.hpp"
#include "../blob.hpp"
#include "cler_desktop_utils.hpp"
#include <new>

template<typename T>
struct SinkUDPSocketBlock : public cler::BlockBase {
    static constexpr bool may_block = true;
    static constexpr bool IS_BLOB = std::is_same_v<T, Blob>;

    cler::Channel<T> in;
    typedef void (*OnSendCallback)(const T&, void* context);

    SinkUDPSocketBlock(const char* name,
                      const UDPBlock::SocketType type,
                      const std::string& dest_host_or_path,
                      OnSendCallback callback = nullptr,
                      void* callback_context = nullptr,
                      const size_t buffer_size = 512)
        : cler::BlockBase(name),
          in(buffer_size),
          _socket(UDPBlock::GenericDatagramSocket::make_sender(type, dest_host_or_path)),
          _callback(callback),
          _callback_context(callback_context),
          _buffer_size(buffer_size) {

        _buffer = new (std::nothrow) T[_buffer_size];
        if (!_buffer) {
            cler::panic("Failed to allocate temporary buffer");
        }
    }

    ~SinkUDPSocketBlock() {
        delete[] _buffer;
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        if (!_socket.is_valid()) {
            return cler::Error::TERM_IOError;
        }

        size_t available = std::min(in.size(), _buffer_size);
        if (available == 0) {
            return cler::Error::NotEnoughSamples;
        }

        T* buffer = _buffer;
        in.readN(buffer, available);

        for (size_t i = 0; i < available; ++i) {
            ssize_t bytes;

            if constexpr (IS_BLOB) {
                bytes = _socket.send(buffer[i].data, buffer[i].len);
                if (_callback) {
                    _callback(buffer[i], _callback_context);
                }
                buffer[i].release(); // return the slab slot now that it's sent
            } else {
                bytes = _socket.send(reinterpret_cast<const uint8_t*>(&buffer[i]), sizeof(T));
                if (_callback) {
                    _callback(buffer[i], _callback_context);
                }
            }

            if (bytes < 0) {
                return cler::Error::TERM_IOError;
            }
        }

        return cler::Empty{};
    }

private:
    UDPBlock::GenericDatagramSocket _socket;
    OnSendCallback _callback = nullptr;
    void* _callback_context = nullptr;
    size_t _buffer_size;
    T* _buffer = nullptr;
};
`,ut=`#pragma once
#include "shared.hpp"
#include "../blob.hpp"
#include "cler_desktop_utils.hpp"
#include <new>

template<typename T>
struct SourceUDPSocketBlock : public cler::BlockBase {
    static constexpr bool IS_BLOB = std::is_same_v<T, Blob>;
    static constexpr bool may_block = true;

    typedef bool (*ValidateCallback)(const T&, void* context);
    typedef void (*OnReceiveCallback)(const T&, void* context);

    // Single constructor works for both Blob and generic types
    // For Blob: pass max_blob_size and num_slab_slots (required for pooling)
    // For generic fixed-size types: omit slab parameters (defaults are sufficient)
    SourceUDPSocketBlock(const char* name,
                        UDPBlock::SocketType type,
                        const std::string& bind_addr_or_path,
                        ValidateCallback validate = nullptr,
                        OnReceiveCallback callback = nullptr,
                        void* callback_context = nullptr,
                        size_t max_blob_size = 256, /*only used if IS_BLOB */
                        size_t num_slab_slots = 100, /*only used if IS_BLOB */
                        size_t buffer_size = 512,
                        std::chrono::milliseconds recv_timeout = std::chrono::milliseconds(100))
        : cler::BlockBase(name),
          _socket(UDPBlock::GenericDatagramSocket::make_receiver(type, bind_addr_or_path)),
          _slab(IS_BLOB ? num_slab_slots : 1, IS_BLOB ? max_blob_size : 0), //if not Blob, slab is dummy
          _validate(validate),
          _validate_context(callback_context),
          _callback(callback),
          _callback_context(callback_context),
          _buffer_size(buffer_size) {

        _buffer = new (std::nothrow) T[_buffer_size];
        if (!_buffer) {
            cler::panic("Failed to allocate temporary buffer");
        }
        _socket.set_receive_timeout(recv_timeout);
    }

    ~SourceUDPSocketBlock() {
        delete[] _buffer;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        if (!_socket.is_valid()) {
            return cler::Error::TERM_IOError;
        }

        size_t available = std::min(out->space(), _buffer_size);
        if (available == 0) {
            return cler::Error::NotEnoughSpace;
        }

        T* buffer = _buffer;
        size_t count = 0;

        for (size_t i = 0; i < available; ++i) {
            const int recv_flags = (i == 0) ? 0 : MSG_DONTWAIT;

            if constexpr (IS_BLOB) {
                auto result = _slab.take_slot();
                if (result.is_err()) break;

                Blob blob = result.unwrap();
                ssize_t bytes = _socket.recv(blob.data, blob.len, recv_flags);

                if (bytes == 0) {
                    blob.release();
                    break;
                }

                if (bytes < 0) {
                    const int err = -bytes;
                    blob.release();
                    if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR || err == EMSGSIZE) {
                        break;
                    }
                    return cler::Error::TERM_IOError;
                }

                blob.len = static_cast<size_t>(bytes);

                if (_validate && !_validate(blob, _validate_context)) {
                    blob.release();
                    continue;
                }

                if (_callback) {
                    _callback(blob, _callback_context);
                }

                buffer[count++] = blob;
            } else {
                ssize_t bytes = _socket.recv(reinterpret_cast<uint8_t*>(&buffer[count]), sizeof(T), recv_flags);
                if (bytes <= 0) break;

                if (_validate && !_validate(buffer[count], _validate_context)) {
                    continue;
                }

                if (_callback) {
                    _callback(buffer[count], _callback_context);
                }

                count++;
            }
        }

        if (count == 0) {
            return cler::Error::NotEnoughSamples;
        }
        out->writeN(buffer, count);

        return cler::Empty{};
    }

private:
    UDPBlock::GenericDatagramSocket _socket;
    Slab _slab;  // Only used when IS_BLOB == true
    ValidateCallback _validate = nullptr;
    void* _validate_context = nullptr;
    OnReceiveCallback _callback = nullptr;
    void* _callback_context = nullptr;
    size_t _buffer_size;
    T* _buffer = nullptr;
};
`,pt=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <cassert>

// Routes input samples to multiple outputs (broadcast, not round-robin: every output gets every sample).
template <typename T>
struct FanoutBlock : public cler::BlockBase {
    cler::Channel<T> in;

    FanoutBlock(const char* name, const size_t num_outputs, const size_t buffer_size = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size), _num_outputs(num_outputs) {

        if (buffer_size > 0 && buffer_size * sizeof(T) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }

        assert(num_outputs > 0 && "Number of outputs must be greater than zero");
    }

    ~FanoutBlock() = default;

    template <typename... OChannels>
    cler::Result<cler::Empty, cler::Error> procedure(OChannels*... outs) {
        constexpr size_t num_outs = sizeof...(OChannels);
        assert(num_outs == _num_outputs && "Number of output channels defined in block constructor must match the number of channels");

        auto [read_ptr, read_size] = in.read_dbf();

        size_t min_write_size = read_size;
        auto check_write_space = [&min_write_size](auto* out) {
            auto [write_ptr, write_size] = out->write_dbf();
            min_write_size = std::min(min_write_size, write_size);
        };
        (check_write_space(outs), ...);

        if (min_write_size == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }

        // clang (C++17) cannot capture a structured binding in a lambda
        const T* src = read_ptr;
        auto copy_to_output = [src, min_write_size](auto* out) {
            auto [write_ptr, write_size] = out->write_dbf();
            std::memcpy(write_ptr, src, min_write_size * sizeof(T));
            out->commit_write(min_write_size);
        };
        (copy_to_output(outs), ...);

        in.commit_read(min_write_size);

        return cler::Empty{};
    }

    private:
        size_t _num_outputs;
};
`,ft=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <tuple>
#include <type_traits>

template<typename Ptr>
struct fused_member_fn_traits;

template<typename R, typename C, typename A>
struct fused_member_fn_traits<R (C::*)(A)> {
    using In = std::decay_t<A>;
    using Out = R;
};

template<typename R, typename C, typename A>
struct fused_member_fn_traits<R (C::*)(A) const> {
    using In = std::decay_t<A>;
    using Out = R;
};

template<typename Kernel, typename = void>
struct fused_kernel_traits {
    static_assert(!std::is_same_v<Kernel, Kernel>,
        "FusedBlock: Kernel must expose \`Out operator()(In)\`");
};

template<typename Kernel>
struct fused_kernel_traits<Kernel, std::void_t<decltype(&Kernel::operator())>>
    : fused_member_fn_traits<decltype(&Kernel::operator())> {};

template<typename... Kernels>
struct fused_chain;

template<typename Kernel>
struct fused_chain<Kernel> {
    using In = typename fused_kernel_traits<Kernel>::In;
    using Out = typename fused_kernel_traits<Kernel>::Out;
};

template<typename Kernel0, typename Kernel1, typename... Rest>
struct fused_chain<Kernel0, Kernel1, Rest...> {
    using Head = fused_kernel_traits<Kernel0>;
    using Next = fused_chain<Kernel1, Rest...>;
    static_assert(std::is_same_v<typename Head::Out, typename Next::In>,
        "FusedBlock: adjacent kernels in chain have mismatched operator() Out/In types");
    using In = typename Head::In;
    using Out = typename Next::Out;
};

template<typename... Kernels>
struct FusedBlock : public cler::BlockBase {
    static_assert(sizeof...(Kernels) >= 1, "FusedBlock requires at least one kernel");

    using FirstIn = typename fused_chain<Kernels...>::In;
    using LastOut = typename fused_chain<Kernels...>::Out;

    cler::Channel<FirstIn> in;

    FusedBlock(const char* name, Kernels... kernels, const size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(FirstIn) : buffer_size),
          _kernels(std::move(kernels)...)
    {
        if (buffer_size > 0 && buffer_size * sizeof(FirstIn) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<LastOut>* out) {
        auto [read_ptr, read_size] = in.read_dbf();
        if (!read_ptr || read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        auto [write_ptr, write_space] = out->write_dbf();
        if (!write_ptr || write_space == 0) {
            return cler::Error::NotEnoughSpace;
        }

        size_t transferable = std::min(read_size, write_space);

        for (size_t i = 0; i < transferable; ++i) {
            write_ptr[i] = apply_chain<0>(read_ptr[i]);
        }

        in.commit_read(transferable);
        out->commit_write(transferable);

        return cler::Empty{};
    }

private:
    std::tuple<Kernels...> _kernels;

    template<size_t I, typename Val>
    auto apply_chain(Val v) {
        if constexpr (I == sizeof...(Kernels)) {
            return v;
        } else {
            return apply_chain<I + 1>(std::get<I>(_kernels)(v));
        }
    }
};
`,mt=`#pragma once

#include "cler.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>

// A monitoring tap that can be switched off at runtime: forwards while open,
// discards while closed. This stays an app-level block on purpose: cler's contract
// is lossless, so a framework-level "disabled block" would either contradict it by
// eating input or deadlock the upstream fanout by refusing it. A gate is one branch
// opting into lossiness so the live path stays lossless.
// A closed gate consumes everything, because an upstream
// fanout advances by its slowest output and would otherwise stall the live path
// whenever this branch is idle.
// An open gate normally backpressures like any block. Only once its input is
// backing up towards full does it discard the excess and count it, so a slow or
// stalled decoder degrades itself instead of freezing audio.
template <typename T>
struct GateBlock : public cler::BlockBase {
    cler::Channel<T> in;

    GateBlock(const char* name, bool open = false, size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size),
          _open(open)
    {
        if (buffer_size > 0 && buffer_size * sizeof(T) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        auto [rptr, rsize] = in.read_dbf();
        if (rsize == 0) return cler::Error::NotEnoughSamples;
        if (!_open.load(std::memory_order_relaxed)) {
            in.commit_read(rsize);
            return cler::Empty{};
        }

        auto [wptr, wsize] = out->write_dbf();
        const size_t n = std::min(rsize, wsize);
        if (n > 0) {
            std::memcpy(wptr, rptr, n * sizeof(T));
            out->commit_write(n);
        }
        size_t consumed = n;
        const size_t capacity = in.size() + in.space();
        if (n < rsize && in.size() - n > capacity - capacity / 4) {
            consumed = rsize;
            _dropped.fetch_add(rsize - n, std::memory_order_relaxed);
        }
        in.commit_read(consumed);
        if (consumed == 0) return cler::Error::NotEnoughSpaceOrSamples;
        return cler::Empty{};
    }

    void set_open(bool open) { _open.store(open, std::memory_order_relaxed); }
    bool open() const { return _open.load(std::memory_order_relaxed); }
    uint64_t dropped() const { return _dropped.load(std::memory_order_relaxed); }
    void clear_dropped() { _dropped.store(0, std::memory_order_relaxed); }

private:
    std::atomic<bool> _open;
    std::atomic<uint64_t> _dropped{0};
};
`,ht=`#pragma once
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <algorithm>
#include <chrono>

template <typename T>
struct ThrottleBlock : public cler::BlockBase {
    static constexpr bool may_block = true;

    cler::Channel<T> in;

    ThrottleBlock(const char* name, const size_t sps, size_t const buffer_size = 1024)
        : cler::BlockBase(name),
          in(buffer_size),
          _sps(static_cast<double>(sps))
    {
        if (sps == 0) {
            cler::panic("Sample rate must be greater than zero.");
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        if (in.size() == 0) {
            return cler::Error::NotEnoughSamples;
        }
        if (out->space() == 0) {
            return cler::Error::NotEnoughSpace;
        }

        if (!_pacing_started) {
            start_pacing();
        }

        const clock::time_point now = wait_until_sample_is_due(_emitted);

        const size_t due = samples_due_by(now);
        const size_t owed = due > _emitted ? due - _emitted : 1;
        const size_t transferable = std::min(owed, std::min(in.size(), out->space()));

        for (size_t i = 0; i < transferable; ++i) {
            T sample;
            in.pop(sample);
            out->push(sample);
        }
        _emitted += transferable;

        if (transferable < owed) {
            drop_pacing_debt(now);
        }

        return cler::Empty{};
    }

private:
    using clock = std::chrono::high_resolution_clock;

    void start_pacing() {
        _epoch = clock::now();
        _emitted = 0;
        _pacing_started = true;
    }

    clock::time_point wait_until_sample_is_due(size_t sample_index) const {
        const clock::time_point due_at = _epoch + time_to_emit(sample_index);
        const clock::time_point now = clock::now();
        if (now >= due_at) {
            return now;
        }
        std::this_thread::sleep_for(due_at - now);
        return clock::now();
    }

    void drop_pacing_debt(clock::time_point now) {
        _epoch = now - time_to_emit(_emitted);
    }

    clock::duration time_to_emit(size_t samples) const {
        return std::chrono::duration_cast<clock::duration>(
            std::chrono::duration<double>(static_cast<double>(samples) / _sps));
    }

    size_t samples_due_by(clock::time_point now) const {
        return static_cast<size_t>(std::chrono::duration<double>(now - _epoch).count() * _sps);
    }

    double _sps;
    bool _pacing_started = false;
    size_t _emitted = 0;
    clock::time_point _epoch;
};
`,bt=`#pragma once
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <chrono>

template <typename T>
struct ThroughputBlock : public cler::BlockBase {
    cler::Channel<T> in;

    ThroughputBlock(std::string name, size_t buffer_size = 0)
        : cler::BlockBase(std::move(name)),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size),
          _start_time(std::chrono::high_resolution_clock::now())
    {
        if (buffer_size > 0 && buffer_size * sizeof(T) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Buffer size too small for doubly-mapped buffers");
        }
    }

    ~ThroughputBlock() = default;

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        auto [read_ptr, read_size] = in.read_dbf();
        auto [write_ptr, write_size] = out->write_dbf();
        
        size_t to_transfer = std::min(read_size, write_size);
        if (to_transfer == 0) {
            return cler::Error::NotEnoughSpaceOrSamples;
        }
        std::memcpy(write_ptr, read_ptr, to_transfer * sizeof(T));
        in.commit_read(to_transfer);
        out->commit_write(to_transfer);
        _samples_passed += to_transfer;
        return cler::Empty{};
    }

    void report() {
        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end_time - _start_time;

        double seconds = elapsed.count();
        double throughput = (_samples_passed) / seconds;

        std::cout << "[ThroughputBlock] \\"" << this->name() << "\\" statistics:\\n";
        std::cout << "  Total samples passed: " << _samples_passed << "\\n";
        std::cout << "  Elapsed time (s):     " << seconds << "\\n";
        std::cout << "  Throughput (samples/s): " << throughput << "\\n";
    }

    size_t samples_passed() const {
        return _samples_passed;
    }

private:
    size_t _samples_passed = 0;
    std::chrono::high_resolution_clock::time_point _start_time;
};
`,gt=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/web/proto.hpp"
#include "desktop_blocks/web/web_server.hpp"

#include <string>

namespace web {

// One JSON line per decoded item, straight to every browser. \`to_json(const T&,
// JsonWriter&)\` is found by ADL — JsonWriter makes \`web\` an associated namespace,
// so an app can add adapters for its own types without touching this header.
// ponytail: push_text copies into a std::string per item — packet rates are a
// few per second, not per sample, so the writer's reused buffer is the only
// thing worth preallocating here.
template <typename T>
struct JsonTextSinkBlock : public cler::BlockBase {
    cler::Channel<T> in;

    JsonTextSinkBlock(const char* name, WebServer& server, const char* stream, size_t buffer_size = 256)
        : cler::BlockBase(name), in(buffer_size), _server(server), _stream(stream ? stream : "") {
        // the client routes text frames by stream id; an empty one is dropped silently
        if (_stream.empty()) cler::panic("JsonTextSinkBlock: empty stream id");
        _w.out.reserve(1024);
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        auto [ptr, n] = in.read_dbf();
        if (n == 0) return cler::Error::NotEnoughSamples;
        for (size_t i = 0; i < n; ++i) {
            _w.out.clear();
            to_json(ptr[i], _w);
            _server.push_text(_stream, _w.out);
        }
        in.commit_read(n);
        return cler::Empty{};
    }

    size_t buffer_capacity() const { return _w.out.capacity(); }

private:
    WebServer& _server;
    std::string _stream;
    JsonWriter _w;
};

}
`,yt=`#pragma once

#include "desktop_blocks/spectrum/spectrum_frame.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "protocol v1 is little-endian on the wire");

namespace web {

constexpr uint8_t PROTO_VER = 1;
constexpr uint8_t T_SPECTRUM = 0x01;
constexpr uint8_t T_AUDIO = 0x02;
// codec 0 fixes both the wire rate and the frame length; a new rate or chunk is a
// new codec id, not a parameter
constexpr uint8_t CODEC_PCM16_48K = 0;
constexpr size_t HEADER_BYTES = 10;
constexpr size_t SPECTRUM_HEAD_BYTES = HEADER_BYTES + 8 + 8 + 2 + 4 + 4;
constexpr size_t AUDIO_HEAD_BYTES = HEADER_BYTES + 1;
constexpr size_t AUDIO_CHUNK = 960;

struct Header {
    uint8_t type = 0, ver = 0;
    uint32_t gen = 0, seq = 0;
};

namespace detail {
template <typename T> inline void put(uint8_t*& p, const T& v) { std::memcpy(p, &v, sizeof(T)); p += sizeof(T); }
template <typename T> inline void get(const uint8_t*& p, T& v) { std::memcpy(&v, p, sizeof(T)); p += sizeof(T); }
inline void put_header(uint8_t*& p, uint8_t type, uint32_t gen, uint32_t seq) {
    put(p, type); put(p, PROTO_VER); put(p, gen); put(p, seq);
}
}

inline size_t encode_spectrum(const SpectrumFrame& f, uint32_t seq, uint8_t* out, size_t cap) {
    const size_t need = SPECTRUM_HEAD_BYTES + f.n;
    if (cap < need || f.n > sizeof(f.bins)) return 0;
    uint8_t* p = out;
    detail::put_header(p, T_SPECTRUM, f.gen, seq);
    detail::put(p, f.center_hz);
    detail::put(p, f.rate_hz);
    detail::put(p, f.n);
    detail::put(p, f.db_min);
    detail::put(p, f.db_step);
    std::memcpy(p, f.bins, f.n);
    return need;
}

inline size_t encode_audio(uint32_t gen, uint32_t seq, const int16_t* pcm, size_t n, uint8_t* out, size_t cap) {
    const size_t need = AUDIO_HEAD_BYTES + 2 * n;
    if (cap < need) return 0;
    uint8_t* p = out;
    detail::put_header(p, T_AUDIO, gen, seq);
    detail::put(p, CODEC_PCM16_48K);
    std::memcpy(p, pcm, 2 * n);
    return need;
}

inline bool decode_header(const uint8_t* in, size_t len, Header& h) {
    if (len < HEADER_BYTES) return false;
    const uint8_t* p = in;
    detail::get(p, h.type); detail::get(p, h.ver); detail::get(p, h.gen); detail::get(p, h.seq);
    return h.ver == PROTO_VER;
}

inline bool decode_spectrum(const uint8_t* in, size_t len, Header& h, SpectrumFrame& f) {
    if (!decode_header(in, len, h) || h.type != T_SPECTRUM || len < SPECTRUM_HEAD_BYTES) return false;
    const uint8_t* p = in + HEADER_BYTES;
    detail::get(p, f.center_hz);
    detail::get(p, f.rate_hz);
    detail::get(p, f.n);
    detail::get(p, f.db_min);
    detail::get(p, f.db_step);
    if (f.n > sizeof(f.bins) || len != SPECTRUM_HEAD_BYTES + f.n) return false;
    f.gen = h.gen;
    std::memcpy(f.bins, p, f.n);
    return true;
}

inline bool decode_audio(const uint8_t* in, size_t len, Header& h, uint8_t& codec, int16_t* pcm, size_t cap, size_t& n) {
    if (!decode_header(in, len, h) || h.type != T_AUDIO || len < AUDIO_HEAD_BYTES) return false;
    codec = in[HEADER_BYTES];
    n = (len - AUDIO_HEAD_BYTES) / 2;
    if (n > cap) return false;
    std::memcpy(pcm, in + AUDIO_HEAD_BYTES, 2 * n);
    return true;
}

inline std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (unsigned char c : s) {
        if (c == '"' || c == '\\\\') { out += '\\\\'; out += static_cast<char>(c); }
        else if (c == '\\n') out += "\\\\n";
        else if (c == '\\t') out += "\\\\t";
        else if (c == '\\r') out += "\\\\r";
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\\\u%04x", c); out += b; }
        else out += static_cast<char>(c);
    }
    return out;
}

inline std::string json_number(double v) {
    if (!std::isfinite(v)) return "null";
    char b[32];
    std::snprintf(b, sizeof(b), "%.10g", v);
    return b;
}

struct JsonWriter {
    std::string out;
    JsonWriter& begin_obj() { return open('{', '}'); }
    JsonWriter& begin_arr() { return open('[', ']'); }
    JsonWriter& end() { out += closers.back(); closers.pop_back(); firsts.pop_back(); return *this; }
    JsonWriter& key(const std::string& k) { sep(); out += '"'; out += json_escape(k); out += "\\":"; pending_value = true; return *this; }
    JsonWriter& str(const std::string& v) { sep(); out += '"'; out += json_escape(v); out += '"'; return *this; }
    JsonWriter& num(double v) { sep(); out += json_number(v); return *this; }
    // %.10g would round a byte count or a drop counter past ~10 digits
    template <typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    JsonWriter& num(T v) { sep(); out += std::to_string(v); return *this; }
    JsonWriter& boolean(bool v) { sep(); out += v ? "true" : "false"; return *this; }
    JsonWriter& raw(const std::string& v) { sep(); out += v.empty() ? "null" : v; return *this; }
    // splice another object's members into the one being written; anything that
    // is not an object with at least one member contributes nothing, so no
    // separator is emitted and no trailing junk can ride along
    JsonWriter& fields_of(const std::string& obj) {
        const size_t a = obj.find('{');
        const size_t b = obj.find_last_not_of(" \\t\\n\\r");
        if (a == std::string::npos || b == std::string::npos || obj[b] != '}' || b <= a + 1) return *this;
        if (obj.find_first_not_of(" \\t\\n\\r", a + 1) >= b) return *this;
        sep();
        out.append(obj, a + 1, b - a - 1);
        return *this;
    }
private:
    std::vector<bool> firsts;
    std::vector<char> closers;
    bool pending_value = false;
    JsonWriter& open(char o, char c) { sep(); out += o; firsts.push_back(true); closers.push_back(c); return *this; }
    void sep() {
        if (pending_value) { pending_value = false; return; }
        if (firsts.empty()) return;
        if (!firsts.back()) out += ',';
        firsts.back() = false;
    }
};

using Fields = std::vector<std::pair<std::string, std::string>>;

namespace detail {
inline void skip_ws(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\\t' || s[i] == '\\n' || s[i] == '\\r')) i++;
}
inline bool skip_value(const std::string& s, size_t& i, std::string& raw) {
    skip_ws(s, i);
    const size_t start = i;
    if (i >= s.size()) return false;
    const char c = s[i];
    if (c == '"') {
        i++;
        while (i < s.size() && s[i] != '"') { if (s[i] == '\\\\') i++; i++; }
        if (i >= s.size()) return false;
        i++;
    } else if (c == '{' || c == '[') {
        int depth = 0;
        while (i < s.size()) {
            if (s[i] == '"') { std::string ignored; if (!skip_value(s, i, ignored)) return false; continue; }
            if (s[i] == '{' || s[i] == '[') depth++;
            else if (s[i] == '}' || s[i] == ']') { if (--depth == 0) { i++; break; } }
            i++;
        }
        if (depth != 0) return false;
    } else {
        while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && s[i] != ' ' && s[i] != '\\t' && s[i] != '\\n' && s[i] != '\\r') i++;
        if (i == start) return false;
    }
    raw = s.substr(start, i - start);
    return true;
}
}

inline std::string json_unescape(const std::string& raw) {
    if (raw.size() < 2 || raw.front() != '"') return raw;
    std::string out;
    for (size_t i = 1; i + 1 < raw.size(); ++i) {
        if (raw[i] == '\\\\' && i + 2 < raw.size()) {
            const char e = raw[++i];
            if (e == 'n') out += '\\n'; else if (e == 't') out += '\\t'; else if (e == 'r') out += '\\r'; else out += e;
        } else out += raw[i];
    }
    return out;
}

// the flat parser hands back a value's raw text, so an array of strings arrives
// here as \`["rds","ais"]\`; anything that is not one yields nothing
inline std::vector<std::string> json_str_array(const std::string& raw) {
    std::vector<std::string> out;
    size_t i = 0;
    detail::skip_ws(raw, i);
    if (i >= raw.size() || raw[i] != '[') return out;
    ++i;
    while (i < raw.size()) {
        detail::skip_ws(raw, i);
        if (i < raw.size() && raw[i] == ']') break;
        std::string v;
        if (!detail::skip_value(raw, i, v)) break;
        out.push_back(json_unescape(v));
        detail::skip_ws(raw, i);
        if (i < raw.size() && raw[i] == ',') ++i;
    }
    return out;
}

// flat object: keys unescaped, values raw (strings keep their quotes); false on malformed input
inline bool json_parse_object(const std::string& s, Fields& fields) {
    fields.clear();
    size_t i = 0;
    detail::skip_ws(s, i);
    if (i >= s.size() || s[i] != '{') return false;
    i++;
    detail::skip_ws(s, i);
    if (i < s.size() && s[i] == '}') return true;
    while (i < s.size()) {
        std::string key, value;
        if (!detail::skip_value(s, i, key) || key.empty() || key.front() != '"') return false;
        detail::skip_ws(s, i);
        if (i >= s.size() || s[i] != ':') return false;
        i++;
        if (!detail::skip_value(s, i, value)) return false;
        fields.emplace_back(json_unescape(key), value);
        detail::skip_ws(s, i);
        if (i < s.size() && s[i] == ',') { i++; continue; }
        if (i < s.size() && s[i] == '}') return true;
        return false;
    }
    return false;
}

inline const std::string* json_find(const Fields& f, const char* key) {
    for (const auto& kv : f) if (kv.first == key) return &kv.second;
    return nullptr;
}

inline std::string json_str(const Fields& f, const char* key, const std::string& dflt = "") {
    const std::string* v = json_find(f, key);
    return v ? json_unescape(*v) : dflt;
}

inline double json_num(const Fields& f, const char* key, double dflt = 0.0) {
    const std::string* v = json_find(f, key);
    return v ? std::strtod(v->c_str(), nullptr) : dflt;
}

}
`,kt=`#pragma once

#include "cler.hpp"
#include "desktop_blocks/spectrum/spectrum_frame.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ix { class HttpServer; class WebSocket; }

namespace web {

struct EmbeddedFile {
    const char* name;
    const char* data;
    size_t size;
};

struct ServerOptions {
    std::string bind = "127.0.0.1";
    int port = 8080;
    std::string token;
    std::string client_dir;
    const EmbeddedFile* files = nullptr;
    size_t file_count = 0;
    std::string version = "dev";
    double audio_rate = 48000.0;   // sizes the audio ring; codec 0 fixes the wire rate
};

struct ClientStats {
    uint64_t spectrum_dropped = 0, audio_dropped = 0;
};

struct HttpReply {
    int status = 404;
    std::string body = "not found";
    std::string content_type = "text/plain";
};

// Runs on an HTTP thread: whatever it touches must outlive the server or be guarded.
using HttpRoute = std::function<HttpReply(const std::string& path, const std::string& query)>;

// push_* are single-producer (one cler worker) into SPSC rings; call set_on_control before start().
class WebServer {
public:
    explicit WebServer(ServerOptions opts);
    ~WebServer();

    void start();
    void stop();
    int port() const { return _opts.port; }

    // Serves \`prefix\` and \`prefix/...\`; longest match wins, token-gated like any
    // other route. Register before start(). The library owns transport and access
    // control, the app owns what its own paths mean.
    void add_http_route(std::string prefix, HttpRoute handler);
    uint64_t uptime_seconds() const;
    // "" unless \`name\` is a single path component: no separators, no "..", no
    // leading dot. It says nothing about where the name resolves — a symlink in
    // the directory still escapes it, so a route serving files must also check
    // the resolved path (see recordings_route.hpp).
    static std::string safe_name(const std::string& name);

    bool push_spectrum(const SpectrumFrame& f);
    size_t push_audio(const int16_t* pcm, size_t n);
    void push_text(const std::string& stream, const std::string& json);

    void set_gen(uint32_t gen) { _gen.store(gen, std::memory_order_relaxed); }
    void set_state(const std::string& json_object);
    void set_hello_extra(const std::string& json_object);
    void resend_hello();
    void set_stats_extra(const std::string& json_object);
    // id names what the error is about, so a client can attach it to that row.
    void send_error(const std::string& code, const std::string& msg, const std::string& id = "");

    bool pop_control(std::string& json);
    void set_on_control(std::function<void(const std::string&)> fn) { _on_control = std::move(fn); }

    // Spectrum frames the tick thread has broadcast. A watchdog reads this to
    // tell "the receiver is serving" from "a block is still producing".
    uint64_t sent() const { return _sent.load(std::memory_order_relaxed); }
    size_t client_count() const;
    ClientStats total_dropped() const;

private:
    struct Client;
    struct Impl;
    ServerOptions _opts;
    std::unique_ptr<Impl> _impl;
    cler::Channel<SpectrumFrame> _spec;
    cler::Channel<int16_t> _audio;
    std::atomic<uint32_t> _gen{0};
    std::atomic<uint32_t> _seq_spec{0}, _seq_audio{0};
    // Audio is stamped when it is sent, so a retune would relabel whatever is
    // still queued as the new frequency. The producer notes where each gen
    // starts and the tick thread stamps every chunk with the gen it was made
    // under, which the client then flushes on its own terms.
    std::mutex _audio_gen_mutex;
    std::deque<std::pair<uint64_t, uint32_t>> _audio_gen_marks;
    uint64_t _audio_written = 0;   // producer thread only
    uint64_t _audio_read = 0;      // tick thread only
    uint32_t _audio_gen = 0;       // tick thread only
    std::atomic<uint64_t> _sent{0};
    mutable std::mutex _mutex;
    std::deque<std::string> _text;
    std::deque<std::string> _control;
    std::vector<std::pair<std::string, HttpRoute>> _routes;
    std::string _state, _hello_extra, _stats_extra;
    std::function<void(const std::string&)> _on_control;
    std::thread _tick;
    std::atomic<bool> _running{false};
    std::atomic<uint64_t> _spectrum_dropped{0}, _audio_dropped{0}, _text_dropped{0};

    const HttpRoute* match_route(const std::string& path) const;
    void tick_loop();
    void broadcast(const std::string& text);
    std::string hello_for(const Client& c);
};

}
`,vt=`#pragma once

#include "cler.hpp"
#include "desktop_blocks/web/web_server.hpp"

#include <algorithm>
#include <array>
#include <cmath>

struct WebSinkBlock : public cler::BlockBase {
    cler::Channel<SpectrumFrame> spectrum;
    cler::Channel<float> audio;

    WebSinkBlock(const char* name, web::WebServer& server, size_t audio_buffer = 1 << 16)
        : cler::BlockBase(name), spectrum(8), audio(audio_buffer), _server(server) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        bool moved = false;
        auto [sptr, ssize] = spectrum.read_dbf();
        for (size_t i = 0; i < ssize; ++i) _server.push_spectrum(sptr[i]);
        if (ssize) { spectrum.commit_read(ssize); moved = true; }

        auto [aptr, asize] = audio.read_dbf();
        size_t done = 0;
        while (done < asize) {
            const size_t n = std::min(asize - done, _pcm.size());
            for (size_t i = 0; i < n; ++i) {
                const float v = std::clamp(aptr[done + i], -1.0f, 1.0f);
                _pcm[i] = static_cast<int16_t>(std::lrint(v * 32767.0f));
            }
            _server.push_audio(_pcm.data(), n);
            done += n;
        }
        if (asize) { audio.commit_read(asize); moved = true; }
        if (!moved) return cler::Error::NotEnoughSamples;
        return cler::Empty{};
    }

private:
    web::WebServer& _server;
    std::array<int16_t, 4096> _pcm{};
};
`,wt=Object.assign({"../../../../../desktop_blocks/adsb/adsb_aggregate.hpp":Ye,"../../../../../desktop_blocks/adsb/adsb_coastline_loader.hpp":Ke,"../../../../../desktop_blocks/adsb/adsb_decoder.hpp":Xe,"../../../../../desktop_blocks/adsb/adsb_types.hpp":Ve,"../../../../../desktop_blocks/ais/ais.hpp":je,"../../../../../desktop_blocks/ais/ais_decoder.hpp":We,"../../../../../desktop_blocks/ais/ais_map.hpp":Ze,"../../../../../desktop_blocks/aprs/afsk_demod.hpp":$e,"../../../../../desktop_blocks/aprs/aprs.hpp":Qe,"../../../../../desktop_blocks/aprs/aprs_map.hpp":Je,"../../../../../desktop_blocks/blob.hpp":en,"../../../../../desktop_blocks/channelizers/polyphase_analyzer.hpp":nn,"../../../../../desktop_blocks/channelizers/polyphase_channelizer.hpp":tn,"../../../../../desktop_blocks/channelizers/polyphase_transform_5.hpp":sn,"../../../../../desktop_blocks/demod/analog_demod.hpp":rn,"../../../../../desktop_blocks/ezgmsk/ezgmsk_demod.hpp":an,"../../../../../desktop_blocks/ezgmsk/ezgmsk_mod.hpp":on,"../../../../../desktop_blocks/fec/deframer.hpp":_n,"../../../../../desktop_blocks/fec/fec.hpp":cn,"../../../../../desktop_blocks/fec/fec_decoder.hpp":ln,"../../../../../desktop_blocks/fec/fec_encoder.hpp":dn,"../../../../../desktop_blocks/fec/framer.hpp":un,"../../../../../desktop_blocks/filters/kaiser_lpf.hpp":pn,"../../../../../desktop_blocks/fm/fm_demod.hpp":fn,"../../../../../desktop_blocks/fm/fm_mpx_decoder.hpp":mn,"../../../../../desktop_blocks/fm/rds.hpp":hn,"../../../../../desktop_blocks/gui/cler_palette.hpp":bn,"../../../../../desktop_blocks/gui/coastline_loader.hpp":gn,"../../../../../desktop_blocks/gui/gui_manager.hpp":yn,"../../../../../desktop_blocks/gui/map_canvas.hpp":kn,"../../../../../desktop_blocks/kernels/kernels.hpp":vn,"../../../../../desktop_blocks/linear_modem/ber_counter.hpp":wn,"../../../../../desktop_blocks/linear_modem/demodulator.hpp":xn,"../../../../../desktop_blocks/linear_modem/modulator.hpp":zn,"../../../../../desktop_blocks/linear_modem/plot_constellation.hpp":Sn,"../../../../../desktop_blocks/linear_modem/symbol_source.hpp":En,"../../../../../desktop_blocks/math/add.hpp":Bn,"../../../../../desktop_blocks/math/complex_demux.hpp":Rn,"../../../../../desktop_blocks/math/frequency_shift.hpp":In,"../../../../../desktop_blocks/math/gain.hpp":An,"../../../../../desktop_blocks/misc/uhd_common.hpp":Cn,"../../../../../desktop_blocks/noise/awgn.hpp":Dn,"../../../../../desktop_blocks/plots/plot_cspectrogram.hpp":Tn,"../../../../../desktop_blocks/plots/plot_cspectrum.hpp":Pn,"../../../../../desktop_blocks/plots/plot_timeseries.hpp":Mn,"../../../../../desktop_blocks/plots/spectral_windows.hpp":Fn,"../../../../../desktop_blocks/resamplers/multistage_resampler.hpp":qn,"../../../../../desktop_blocks/resamplers/rational_resampler.hpp":On,"../../../../../desktop_blocks/sigmf/recorder_sigmf.hpp":Nn,"../../../../../desktop_blocks/sigmf/sigmf.hpp":Ln,"../../../../../desktop_blocks/sigmf/sink_sigmf.hpp":Hn,"../../../../../desktop_blocks/sigmf/source_sigmf.hpp":Un,"../../../../../desktop_blocks/sinks/sink_audio.hpp":Gn,"../../../../../desktop_blocks/sinks/sink_file.hpp":Yn,"../../../../../desktop_blocks/sinks/sink_hackrf.hpp":Kn,"../../../../../desktop_blocks/sinks/sink_null.hpp":Xn,"../../../../../desktop_blocks/sinks/sink_soapysdr.hpp":Vn,"../../../../../desktop_blocks/sinks/sink_uhd.hpp":jn,"../../../../../desktop_blocks/sources/source_audio_file.hpp":Wn,"../../../../../desktop_blocks/sources/source_cariboulite.hpp":Zn,"../../../../../desktop_blocks/sources/source_chirp.hpp":$n,"../../../../../desktop_blocks/sources/source_cw.hpp":Qn,"../../../../../desktop_blocks/sources/source_file.hpp":Jn,"../../../../../desktop_blocks/sources/source_hackrf.hpp":et,"../../../../../desktop_blocks/sources/source_iq_file.hpp":nt,"../../../../../desktop_blocks/sources/source_mux.hpp":tt,"../../../../../desktop_blocks/sources/source_pluto.hpp":st,"../../../../../desktop_blocks/sources/source_sim.hpp":rt,"../../../../../desktop_blocks/sources/source_soapysdr.hpp":at,"../../../../../desktop_blocks/sources/source_uhd.hpp":it,"../../../../../desktop_blocks/spectrum/spectrum.hpp":ot,"../../../../../desktop_blocks/spectrum/spectrum_frame.hpp":_t,"../../../../../desktop_blocks/triggers/trigger_block.hpp":ct,"../../../../../desktop_blocks/udp/shared.hpp":lt,"../../../../../desktop_blocks/udp/sink_udp.hpp":dt,"../../../../../desktop_blocks/udp/source_udp.hpp":ut,"../../../../../desktop_blocks/utils/fanout.hpp":pt,"../../../../../desktop_blocks/utils/fused.hpp":ft,"../../../../../desktop_blocks/utils/gate.hpp":mt,"../../../../../desktop_blocks/utils/throttle.hpp":ht,"../../../../../desktop_blocks/utils/throughput.hpp":bt,"../../../../../desktop_blocks/web/json_sink.hpp":gt,"../../../../../desktop_blocks/web/proto.hpp":yt,"../../../../../desktop_blocks/web/web_server.hpp":kt,"../../../../../desktop_blocks/web/web_sink.hpp":vt}),Y={};for(const[e,n]of Object.entries(Z)){const t=$[e]?.file;t&&(Y[t]=n)}for(const[e,n]of Object.entries(wt))Y[e.replace(/^(\.\.\/)+/,"")]=n;const xt=["hello_world","mass_spring_damper","plots","polyphase_channelizer"],zt=xt.flatMap(e=>{const n=$[e]?.file,t=Z[e];return n&&t!==void 0?[{name:e,path:n,source:t}]:[]});async function Et(){try{const n=new URLSearchParams({toolchain:ze,pins:JSON.stringify(Se)});if(await navigator.serviceWorker.register(`./cler-sw.js?${n}`,{scope:"./"}),await navigator.serviceWorker.ready,window.crossOriginIsolated)sessionStorage.removeItem("clerSwReload");else if(!sessionStorage.getItem("clerSwReload")){sessionStorage.setItem("clerSwReload","1"),location.reload();return}}catch(n){console.warn("cler service worker unavailable",n),Re("Build and Run need a service worker; this browser mode disables it")}await Ne(Y,zt)}export{Et as bootBrowser};
