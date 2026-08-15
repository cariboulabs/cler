import{d as v,f as K,a as j}from"./progress-WxDHJIGZ.js";/**
 * @license
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: Apache-2.0
 */const $=Symbol("Comlink.proxy"),ue=Symbol("Comlink.endpoint"),pe=Symbol("Comlink.releaseProxy"),O=Symbol("Comlink.finalizer"),I=Symbol("Comlink.thrown"),Q=e=>typeof e=="object"&&e!==null||typeof e=="function",fe={canHandle:e=>Q(e)&&e[$],serialize(e){const{port1:n,port2:t}=new MessageChannel;return ee(e,n),[t,[t]]},deserialize(e){return e.start(),te(e)}},me={canHandle:e=>Q(e)&&I in e,serialize({value:e}){let n;return e instanceof Error?n={isError:!0,value:{message:e.message,name:e.name,stack:e.stack}}:n={isError:!1,value:e},[n,[]]},deserialize(e){throw e.isError?Object.assign(new Error(e.value.message),e.value):e.value}},J=new Map([["proxy",fe],["throw",me]]);function he(e,n){for(const t of e)if(n===t||t==="*"||t instanceof RegExp&&t.test(n))return!0;return!1}function ee(e,n=globalThis,t=["*"]){n.addEventListener("message",function r(a){if(!a||!a.data)return;if(!he(t,a.origin)){console.warn(`Invalid origin '${a.origin}' for comlink proxy`);return}const{id:i,type:o,path:c}=Object.assign({path:[]},a.data),d=(a.data.argumentList||[]).map(z);let _;try{const l=c.slice(0,-1).reduce((s,p)=>s[p],e),u=c.reduce((s,p)=>s[p],e);switch(o){case"GET":_=u;break;case"SET":l[c.slice(-1)[0]]=z(a.data.value),_=!0;break;case"APPLY":_=u.apply(l,d);break;case"CONSTRUCT":{const s=new u(...d);_=F(s)}break;case"ENDPOINT":{const{port1:s,port2:p}=new MessageChannel;ee(e,p),_=we(s,[s])}break;case"RELEASE":_=void 0;break;default:return}}catch(l){_={value:l,[I]:0}}Promise.resolve(_).catch(l=>({value:l,[I]:0})).then(l=>{const[u,s]=P(l);n.postMessage(Object.assign(Object.assign({},u),{id:i}),s),o==="RELEASE"&&(n.removeEventListener("message",r),ne(n),O in e&&typeof e[O]=="function"&&e[O]())}).catch(l=>{const[u,s]=P({value:new TypeError("Unserializable return value"),[I]:0});n.postMessage(Object.assign(Object.assign({},u),{id:i}),s)})}),n.start&&n.start()}function ge(e){return e.constructor.name==="MessagePort"}function ne(e){ge(e)&&e.close()}function te(e,n){const t=new Map;return e.addEventListener("message",function(a){const{data:i}=a;if(!i||!i.id)return;const o=t.get(i.id);if(o)try{o(i)}finally{t.delete(i.id)}}),q(e,t,[],n)}function B(e){if(e)throw new Error("Proxy has been released and is not useable")}function re(e){return y(e,new Map,{type:"RELEASE"}).then(()=>{ne(e)})}const A=new WeakMap,D="FinalizationRegistry"in globalThis&&new FinalizationRegistry(e=>{const n=(A.get(e)||0)-1;A.set(e,n),n===0&&re(e)});function be(e,n){const t=(A.get(n)||0)+1;A.set(n,t),D&&D.register(e,n,e)}function Se(e){D&&D.unregister(e)}function q(e,n,t=[],r=function(){}){let a=!1;const i=new Proxy(r,{get(o,c){if(B(a),c===pe)return()=>{Se(i),re(e),n.clear(),a=!0};if(c==="then"){if(t.length===0)return{then:()=>i};const d=y(e,n,{type:"GET",path:t.map(_=>_.toString())}).then(z);return d.then.bind(d)}return q(e,n,[...t,c])},set(o,c,d){B(a);const[_,l]=P(d);return y(e,n,{type:"SET",path:[...t,c].map(u=>u.toString()),value:_},l).then(z)},apply(o,c,d){B(a);const _=t[t.length-1];if(_===ue)return y(e,n,{type:"ENDPOINT"}).then(z);if(_==="bind")return q(e,n,t.slice(0,-1));const[l,u]=V(d);return y(e,n,{type:"APPLY",path:t.map(s=>s.toString()),argumentList:l},u).then(z)},construct(o,c){B(a);const[d,_]=V(c);return y(e,n,{type:"CONSTRUCT",path:t.map(l=>l.toString()),argumentList:d},_).then(z)}});return be(i,e),i}function ve(e){return Array.prototype.concat.apply([],e)}function V(e){const n=e.map(P);return[n.map(t=>t[0]),ve(n.map(t=>t[1]))]}const ae=new WeakMap;function we(e,n){return ae.set(e,n),e}function F(e){return Object.assign(e,{[$]:!0})}function P(e){for(const[n,t]of J)if(t.canHandle(e)){const[r,a]=t.serialize(e);return[{type:"HANDLER",name:n,value:r},a]}return[{type:"RAW",value:e},ae.get(e)||[]]}function z(e){switch(e.type){case"HANDLER":return J.get(e.name).deserialize(e.value);case"RAW":return e.value}}function y(e,n,t,r){return new Promise(a=>{const i=ze();n.set(i,a),e.start&&e.start(),e.postMessage(Object.assign({id:i},t),r)})}function ze(){return new Array(4).fill(0).map(()=>Math.floor(Math.random()*Number.MAX_SAFE_INTEGER).toString(16)).join("-")}const ke="https://jprendes.github.io/emception/",xe={"emception.worker.bundle.worker.js":"60b9f0fb7982f9395ef63872b5ed3b798377fab09a8666f28b67ccb5029c0107","f0283badd42fe745cbe4.wasm":"2c60c515eca756e80ddc752a6ac062e07f596eb70c7a1308321705f90e09b442","9d1e542b80004e27297f.wasm":"47a2b00defa938d4471ff6ffdbf4d424ee03599db7d8f56590c6223e96191631","cecdfcda360457a8f204.br":"9bd873132b4915a4da34a977a386a4ae68785df34b8cdb9c3d205fae26eeb772"},X=24992393,U="/working",se="draft.o",ye=["app.html","app.js","app.wasm","app.worker.js"],Ee=["-O1","-sMINIFY_HTML=0","--shell-file","shell.html"];let L=null;const ie=new TextEncoder,T=()=>new URL("./",location.href).href,Z=6e4;let R=null,w=()=>{},C=null;function Re(e){C=e}function Be(){return C}function W(e,n,t,r){return Te(e,r).then(async a=>(await H(a,n,ie.encode(t)),v({phase:"compile",detail:n}),oe(a,["em++",..._e().cxxflags,"-c",n,"-o",se],r)))}async function Ie(e){if(!R)throw new Error("the C++ toolchain is not running — compile first");const n=await R,t=["em++",se,"lib/libcler_web.a","lib/libliquid.a",..._e().ldflags,...Ee,"-o","app.html"];v({phase:"link"});let r=!1;const a=await oe(n,t,o=>{!r&&/wasm-opt/.test(o)&&(r=!0,v({phase:"optimize"})),e(o)}),i={};if(a===0)for(const o of ye)i[o]=new Uint8Array(await n.fileSystem.readFile(`${U}/${o}`));return{code:a,files:i}}function Te(e,n){return C?Promise.reject(new Error(C)):(w=n,R??=Ae(e).catch(t=>{throw R=null,t}),R)}async function Ae(e){let n=0,t=Date.now(),r=null;const a=_=>{const l=_.data;l?.toolchainError?(r=l.toolchainError,w(l.toolchainError)):l?.toolchain&&(n+=l.bytes??0,t=Date.now(),v(n>=X?{phase:"boot"}:{phase:"toolchain",bytes:n,total:X}),w(`downloading the C++ toolchain (first visit only)… ${(n/1e6).toFixed(1)} MB`))};navigator.serviceWorker.addEventListener("message",a),w("starting the in-browser C++ toolchain…"),v({phase:"boot"});const i=new Worker(`${T()}emception/emception.worker.bundle.worker.js`),o=te(i);o.onstdout=F(_=>w(_)),o.onstderr=F(_=>w(_));let c=0;const d=new Promise((_,l)=>{i.onerror=u=>l(new Error(r??`the C++ toolchain worker failed: ${u.message||"load error"}`)),c=self.setInterval(()=>{Date.now()-t<Z||l(new Error(r??`the C++ toolchain stalled for ${Z/1e3} s with no download progress`))},1e3)});try{await Promise.race([o.init(),d]),w("unpacking the cler headers and libraries…"),v({phase:"stage"}),await De(o,e)}catch(_){throw i.terminate(),_}finally{clearInterval(c),navigator.serviceWorker.removeEventListener("message",a)}return o}function _e(){if(!L)throw new Error("the build flags are not staged — the toolchain never booted");return L}async function De(e,n){L=await(await fetch(`${T()}payload/flags.json`)).json();const t=await(await fetch(`${T()}payload/headers.json`)).json();v({phase:"stage",detail:`${Object.keys(t).length} headers`});for(const[r,a]of Object.entries({...t,...n}))await H(e,r,ie.encode(a));for(const r of["libcler_web.a","libliquid.a"]){v({phase:"stage",detail:r});const a=await(await fetch(`${T()}payload/${r}`)).arrayBuffer();await H(e,`lib/${r}`,new Uint8Array(a))}}async function H(e,n,t){await e.fileSystem.mkdirTree(`${U}/${n}`.replace(/\/[^/]+$/,"")),await e.fileSystem.writeFile(`${U}/${n}`,t)}async function oe(e,n,t){return w=t,t(`$ ${n[0]} … ${n[n.length-2]} ${n[n.length-1]}`),(await e.run(...n)).returncode}const Pe=""+new URL("cler_web-DOA8Y4AK.wasm",import.meta.url).href,Ce=new TextEncoder,le=new TextDecoder;function Oe(e){const n=()=>new DataView(e().buffer),t=()=>new Uint8Array(e().buffer);return{random_get(r,a){return crypto.getRandomValues(t().subarray(r,r+a)),0},environ_get(){return 0},environ_sizes_get(r,a){return n().setUint32(r,0,!0),n().setUint32(a,0,!0),0},clock_time_get(r,a,i){return n().setBigUint64(i,BigInt(Math.round(performance.now()*1e6)),!0),0},fd_close(){return 0},fd_seek(){return 70},fd_write(r,a,i,o){let c=0,d="";for(let _=0;_<i;_++){const l=n().getUint32(a+_*8,!0),u=n().getUint32(a+_*8+4,!0);d+=le.decode(t().subarray(l,l+u)),c+=u}return n().setUint32(o,c,!0),(r===2?console.error:console.log)(d),0},proc_exit(r){throw new Error(`cler-web.wasm exited with ${r}`)}}}async function Ne(){return Me(e=>WebAssembly.instantiateStreaming(fetch(Pe),e))}async function Me(e){let n=null;const{instance:t}=await e({wasi_snapshot_preview1:Oe(()=>n.memory)});n=t.exports;const r=n;return(a,i)=>{const o=Ce.encode(JSON.stringify({cmd:a,args:i})),c=r.cler_alloc(o.length);new Uint8Array(r.memory.buffer).set(o,c);const d=r.cler_invoke(c,o.length);r.cler_free(c,o.length);const _=new Uint8Array(r.memory.buffer);let l=d;for(;_[l]!==0;)l++;const u=JSON.parse(le.decode(_.subarray(d,l)));if(r.cler_free(d,l-d+1),"loud"in u)throw new Error(u.loud);if("err"in u)throw u.err;return u.ok}}async function qe(e,n=[]){const t=await Ne();for(const[s,p]of Object.entries(e))t("put_file",{path:s,text:p});const r=new Map,a=new Map;let i=1;const o=(s,p)=>{for(const[f,g]of r)g.event===s&&a.get(g.handler)?.({event:s,id:f,payload:p})},c=new Map,d=new Map,_=s=>t("open_document",{path:s}).source,l=(s,p,f)=>{const g=i++,S={inputs:{},recipeSha256:""};s==="build"&&d.set(p,g);const E=h=>{for(const b of String(h).split(`
`))b.trim()&&o(`${s}-output`,{jobId:g,inputKey:S,path:p,line:b})},m=h=>{s==="build"&&d.delete(p),o(`${s}-finished`,{jobId:g,inputKey:S,path:p,code:h})};return f(E).then(m,h=>{E(h instanceof Error?h.message:String(h)),m(1)}),{jobId:g,inputKey:S}},u=window;u.__TAURI_INTERNALS__={invoke:async(s,p={})=>{if(s==="plugin:dialog|open"||s==="plugin:dialog|save")return null;if(s==="plugin:event|listen"){const m=i++;return r.set(m,{event:p.event,handler:p.handler}),m}if(s==="plugin:event|unlisten")return r.delete(p.eventId),null;const f=p.path,g=n.find(m=>m.path===f&&m.source===_(f)),S=Be();if(s==="find_target"){if(S&&!g)throw S;if(g)return{available:!0,reason:null,name:g.name,buildDir:null,binary:`run/${g.name}.html`,artifact:{state:"ready",artifactPath:`run/${g.name}.html`}};const m=d.get(f),h=await M(_(f)),b=`built/${h}/app.html`;return{available:!0,reason:null,name:f.split("/").pop()?.replace(/\.[^.]+$/,"")??"flowgraph",buildDir:null,binary:null,artifact:m!==void 0?{state:"building",jobId:m}:await N(h)?{state:"ready",artifactPath:b}:{state:"needs_build",reason:"compile this document in the browser first (Ctrl+B)"}}}if(s==="check_document"){if(S)throw S;return l("check",f,m=>W(e,f,_(f),m))}if(s==="build_target"){if(S)throw S;return l("build",f,async m=>{const h=_(f),b=await M(h);if(await N(b))return m(`built/${b}/app.html is already built — press Run`),0;const k=await W(e,f,h,m);if(k!==0)return k;const x=await Ie(m);return x.code!==0?x.code:(v({phase:"store"}),await Le(b,x.files),0)})}if(s==="run_target"){let m=`run/${g?.name}.html`;if(!g){const Y=await M(_(f));if(!await N(Y))throw"this edit is not built yet — press Build (Ctrl+B) first";m=`built/${Y}/app.html`}const h=i++;v({phase:"launch"});const b=window.open(m,"_blank","popup,width=1280,height=800");if(!b)throw"the browser blocked the run window — allow popups for this site";const k={inputs:{},recipeSha256:""},x=window.setInterval(()=>{b.closed&&(window.clearInterval(x),c.delete(f),o("run-finished",{jobId:h,inputKey:k,path:f,code:0}))},500);return c.set(f,{win:b,jobId:h,timer:x}),o("run-output",{jobId:h,inputKey:k,path:f,line:`running ${m} in a new window — close it or press Stop`}),{jobId:h,inputKey:k}}if(s==="stop_target")return c.get(f)?.win.close(),null;const E=t(s,p);return s==="save_document"&&He(f,E.source),E},transformCallback:s=>{const p=i++;return a.set(p,s),p},metadata:{}},u.__TAURI_EVENT_PLUGIN_INTERNALS__={unregisterListener:(s,p)=>r.delete(p)}}const Fe="cler-built",Ue=5,ce=()=>caches.open(Fe),de=e=>new URL(e,location.href).pathname;async function N(e){return!!await ce().then(n=>n.match(de(`built/${e}/app.html`)))}async function Le(e,n){const t=await ce();for(const[i,o]of Object.entries(n))await t.put(de(`built/${e}/${i}`),new Response(o));const r=await t.keys(),a=[...new Set(r.map(i=>new URL(i.url).pathname.split("/built/")[1]?.split("/")[0]))];for(const i of a.slice(0,Math.max(0,a.length-Ue)))for(const o of r)o.url.includes(`/built/${i}/`)&&await t.delete(o)}async function M(e){const n=await crypto.subtle.digest("SHA-256",new TextEncoder().encode(e));return Array.from(new Uint8Array(n),t=>t.toString(16).padStart(2,"0")).join("").slice(0,16)}function He(e,n){const t=e.split("/").pop()??"flowgraph.cpp",r=URL.createObjectURL(new Blob([n],{type:"text/plain"})),a=document.createElement("a");a.href=r,a.download=t,a.click(),URL.revokeObjectURL(r)}const Ge=`#pragma once

#include "cler.hpp"
#include "adsb_types.hpp"
#include "adsb_coastline_loader.hpp"
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
          _map_center_lat(initial_map_center_lat), _map_center_lon(initial_map_center_lon),
          _map_zoom(0.1f), _coastlines_loaded(false) {
        _coastlines_loaded = _coastline_data.load_from_shapefile(coastline_data_path);
    }

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

        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        ImVec2 canvas_size = ImGui::GetContentRegionAvail();
        if (canvas_size.x < MIN_CANVAS_SIZE) {canvas_size.x = MIN_CANVAS_SIZE;}
        if (canvas_size.y < MIN_CANVAS_SIZE) {canvas_size.y = MIN_CANVAS_SIZE;}

        ImVec2 canvas_p1 = ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        draw_list->AddRectFilled(canvas_pos, canvas_p1, IM_COL32(30, 40, 50, 255));
        draw_list->AddRect(canvas_pos, canvas_p1, IM_COL32(200, 200, 200, 255));

        draw_grid(draw_list, canvas_pos, canvas_size);
        draw_coastlines(draw_list, canvas_pos, canvas_size);
        draw_aircraft(draw_list, canvas_pos, canvas_size);

        ImGui::SetCursorScreenPos(ImVec2(canvas_pos.x + INFO_TEXT_OFFSET_X, canvas_p1.y - INFO_TEXT_OFFSET_Y));
        const char latitude_hemisphere = _map_center_lat >= 0.0f ? 'N' : 'S';
        const char longitude_hemisphere = _map_center_lon >= 0.0f ? 'E' : 'W';
        ImGui::Text("Aircraft: %zu | Center: %.2f°%c, %.2f°%c | Zoom: %.1fx",
                    _aircraft.size(), std::fabs(_map_center_lat), latitude_hemisphere,
                    std::fabs(_map_center_lon), longitude_hemisphere, _map_zoom);

        handle_map_interaction(canvas_pos, canvas_size);

        ImGui::End();
    }

    void set_initial_window(float x, float y, float w, float h) {
        _initial_window_position = ImVec2(x, y);
        _initial_window_size = ImVec2(w, h);
    }

private:
    static constexpr float CANVAS_BOUNDS_MARGIN = 100.0f;
    static constexpr float AIRCRAFT_SPREAD_RANGE = 200.0f;
    static constexpr float AIRCRAFT_SPREAD_OFFSET = 100.0f;
    static constexpr float TRIANGLE_SIZE = 8.0f;
    static constexpr float TRIANGLE_ANGLE_OFFSET = 0.5f;
    static constexpr float MAX_ALTITUDE_FOR_COLOR = 40000.0f;
    static constexpr float GRID_STEP_ZOOMED_OUT = 0.5f;
    static constexpr float GRID_STEP_ZOOMED_IN = 0.1f;
    static constexpr float GRID_ZOOM_THRESHOLD = 1.0f;
    static constexpr float COASTLINE_THICKNESS = 1.5f;
    static constexpr float MIN_CANVAS_SIZE = 200.0f;
    static constexpr float INFO_TEXT_OFFSET_X = 10.0f;
    static constexpr float INFO_TEXT_OFFSET_Y = 30.0f;
    static constexpr float LABEL_OFFSET_X = 10.0f;
    static constexpr float LABEL_OFFSET_Y_CALLSIGN = -8.0f;
    static constexpr float LABEL_OFFSET_Y_ALTITUDE = 4.0f;
    static constexpr float ZOOM_SENSITIVITY = 0.1f;
    static constexpr float MIN_ZOOM = 0.01f;
    static constexpr float MAX_ZOOM = 50.0f;
    static constexpr float DEFAULT_LAT_SPAN = 2.0f;
    static constexpr float INITIAL_WINDOW_SIZE_X = 1400.0f;
    static constexpr float INITIAL_WINDOW_SIZE_Y = 800.0f;

    std::unordered_map<uint32_t, ADSBState> _aircraft;
    OnAircraftUpdateCallback _callback;
    void* _callback_context;

    ImVec2 _initial_window_position{0.0f, 0.0f};
    ImVec2 _initial_window_size{INITIAL_WINDOW_SIZE_X, INITIAL_WINDOW_SIZE_Y};

    float _map_center_lat;
    float _map_center_lon;
    float _map_zoom;

    CoastlineData _coastline_data;
    bool _coastlines_loaded;

    ImVec2 lat_lon_to_screen(float lat, float lon, ImVec2 canvas_pos, ImVec2 canvas_size) {
        float lat_span = DEFAULT_LAT_SPAN / _map_zoom;
        float lon_span = lat_span * (canvas_size.x / canvas_size.y);

        float lat_min = _map_center_lat - lat_span / 2.0f;
        float lon_min = _map_center_lon - lon_span / 2.0f;

        float x_norm = (lon - lon_min) / lon_span;
        float y_norm = (lat - lat_min) / lat_span;

        x_norm = std::max(0.0f, std::min(1.0f, x_norm));
        y_norm = std::max(0.0f, std::min(1.0f, y_norm));

        // screen y is flipped: lat increases upward but screen y increases downward
        ImVec2 screen;
        screen.x = canvas_pos.x + x_norm * canvas_size.x;
        screen.y = canvas_pos.y + (1.0f - y_norm) * canvas_size.y;

        return screen;
    }

    void draw_grid(ImDrawList* draw_list, ImVec2 canvas_pos, ImVec2 canvas_size) {
        float lat_span = DEFAULT_LAT_SPAN / _map_zoom;
        float lon_span = lat_span * (canvas_size.x / canvas_size.y);

        float lat_min = _map_center_lat - lat_span / 2.0f;
        float lon_min = _map_center_lon - lon_span / 2.0f;

        float grid_step = (lat_span > GRID_ZOOM_THRESHOLD) ? GRID_STEP_ZOOMED_OUT : GRID_STEP_ZOOMED_IN;

        for (float lat = std::floor(lat_min / grid_step) * grid_step; lat < lat_min + lat_span; lat += grid_step) {
            ImVec2 p1 = lat_lon_to_screen(lat, lon_min, canvas_pos, canvas_size);
            ImVec2 p2 = lat_lon_to_screen(lat, lon_min + lon_span, canvas_pos, canvas_size);
            draw_list->AddLine(p1, p2, IM_COL32(100, 100, 120, 100), 0.5f);
        }

        for (float lon = std::floor(lon_min / grid_step) * grid_step; lon < lon_min + lon_span; lon += grid_step) {
            ImVec2 p1 = lat_lon_to_screen(lat_min, lon, canvas_pos, canvas_size);
            ImVec2 p2 = lat_lon_to_screen(lat_min + lat_span, lon, canvas_pos, canvas_size);
            draw_list->AddLine(p1, p2, IM_COL32(100, 100, 120, 100), 0.5f);
        }
    }

    void draw_coastlines(ImDrawList* draw_list, ImVec2 canvas_pos, ImVec2 canvas_size) {
        if (!_coastlines_loaded || _coastline_data.polylines.empty()) {
            return;
        }

        ImU32 coastline_color = IM_COL32(100, 200, 100, 180);

        for (const auto& polyline : _coastline_data.polylines) {
            if (polyline.size() < 2) continue;

            for (size_t i = 0; i < polyline.size() - 1; ++i) {
                ImVec2 p1 = lat_lon_to_screen(polyline[i].first, polyline[i].second, canvas_pos, canvas_size);
                ImVec2 p2 = lat_lon_to_screen(polyline[i + 1].first, polyline[i + 1].second, canvas_pos, canvas_size);

                // Only draw if both points are roughly on screen (avoid massive lines off-canvas)
                if ((p1.x >= canvas_pos.x - CANVAS_BOUNDS_MARGIN || p2.x >= canvas_pos.x - CANVAS_BOUNDS_MARGIN) &&
                    (p1.x < canvas_pos.x + canvas_size.x + CANVAS_BOUNDS_MARGIN || p2.x < canvas_pos.x + canvas_size.x + CANVAS_BOUNDS_MARGIN) &&
                    (p1.y >= canvas_pos.y - CANVAS_BOUNDS_MARGIN || p2.y >= canvas_pos.y - CANVAS_BOUNDS_MARGIN) &&
                    (p1.y < canvas_pos.y + canvas_size.y + CANVAS_BOUNDS_MARGIN || p2.y < canvas_pos.y + canvas_size.y + CANVAS_BOUNDS_MARGIN)) {
                    draw_list->AddLine(p1, p2, coastline_color, COASTLINE_THICKNESS);
                }
            }
        }
    }

    void draw_aircraft(ImDrawList* draw_list, ImVec2 canvas_pos, ImVec2 canvas_size) {
        for (const auto& pair : _aircraft) {
            const ADSBState& state = pair.second;

            // no fallback: unpositioned aircraft are not drawn
            if (!state.position_valid) {
                continue;
            }

            ImVec2 pos = lat_lon_to_screen(state.lat, state.lon, canvas_pos, canvas_size);

            float alt_norm = std::min(1.0f, state.altitude / MAX_ALTITUDE_FOR_COLOR);
            ImU32 color = ImGui::GetColorU32(ImVec4(alt_norm, 0.5f, 1.0f - alt_norm, 1.0f));

            // arrow triangle: v0 = tip (heading direction), v1/v2 = back corners
            // -90deg offset empirically needed to align triangle tip with track angle
            float heading_rad = state.track * cler::PI / 180.0f - cler::PI / 2.0f;
            float cos_h = std::cos(heading_rad);
            float sin_h = std::sin(heading_rad);

            ImVec2 v0(pos.x + TRIANGLE_SIZE * 1.2f * cos_h,
                      pos.y + TRIANGLE_SIZE * 1.2f * sin_h);
            ImVec2 v1(pos.x - TRIANGLE_SIZE * 0.8f * cos_h - TRIANGLE_SIZE * 0.5f * sin_h,
                      pos.y - TRIANGLE_SIZE * 0.8f * sin_h + TRIANGLE_SIZE * 0.5f * cos_h);
            ImVec2 v2(pos.x - TRIANGLE_SIZE * 0.8f * cos_h + TRIANGLE_SIZE * 0.5f * sin_h,
                      pos.y - TRIANGLE_SIZE * 0.8f * sin_h - TRIANGLE_SIZE * 0.5f * cos_h);

            draw_list->AddTriangleFilled(v0, v1, v2, color);
            draw_list->AddTriangle(v0, v1, v2, IM_COL32(255, 255, 255, 200), 1.0f);

            if (state.callsign[0] != '\\0') {
                draw_list->AddText(ImVec2(pos.x + LABEL_OFFSET_X, pos.y + LABEL_OFFSET_Y_CALLSIGN),
                                   IM_COL32(255, 255, 255, 255), state.callsign);
            }
        }
    }

    void handle_map_interaction(ImVec2 canvas_pos, ImVec2 canvas_size) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 mouse_pos = io.MousePos;

        bool mouse_over = mouse_pos.x >= canvas_pos.x && mouse_pos.x < canvas_pos.x + canvas_size.x &&
                          mouse_pos.y >= canvas_pos.y && mouse_pos.y < canvas_pos.y + canvas_size.y;

        if (mouse_over) {
            if (io.MouseWheel != 0.0f) {
                _map_zoom *= (1.0f + io.MouseWheel * ZOOM_SENSITIVITY);
                _map_zoom = std::max(MIN_ZOOM, std::min(MAX_ZOOM, _map_zoom));
            }

            bool panning_left = ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f);
            bool panning_right = ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f);

            if (panning_left || panning_right) {
                ImGuiMouseButton button = panning_left ? ImGuiMouseButton_Left : ImGuiMouseButton_Right;
                ImVec2 delta = ImGui::GetMouseDragDelta(button);
                float lat_span = DEFAULT_LAT_SPAN / _map_zoom;
                float lon_span = lat_span * (canvas_size.x / canvas_size.y);

                // inverted: drag right pushes the map right
                _map_center_lon -= (delta.x / canvas_size.x) * lon_span;
                _map_center_lat += (delta.y / canvas_size.y) * lat_span;

                ImGui::ResetMouseDragDelta(button);
            }
        }

    }
};
`,Ye=`#pragma once

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
`,Ve=`#pragma once

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
`,Xe=`#pragma once

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
`,Ze=`#pragma once
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
};`,We=`#pragma once

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
`,Ke=`#include "cler.hpp"
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
`,je=`#pragma once

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
`,$e=`#pragma once

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
`,Qe=`#pragma once

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
`,Je=`#pragma once

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
`,en=`#pragma once

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
`,nn=`#pragma once

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
`,tn=`#pragma once

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
`,rn=`#pragma once

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

    explicit AWGNKernel(scalar_type noise_stddev)
        : _normal_dist(0.0, noise_stddev) {
        std::random_device rd;
        _rng.seed(rd());
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
`,an=`#include "cler.hpp"
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
`,sn=`#pragma once

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
`,_n=`#include "cler.hpp"
#include "cler_desktop_utils.hpp"
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
    }

    ~FrequencyShiftBlock() {
        delete[] _buffer;
    }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
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
};`,on=`#include "cler.hpp"
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
};`,ln=`#pragma once
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
`,cn=`#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "desktop_blocks/kernels/kernels.hpp"
#include <random>
#include <type_traits>
#include <new>

template <typename T>
struct NoiseAWGNBlock : public cler::BlockBase {
    cler::Channel<T> in;

    using scalar_type = typename AWGNKernel<T>::scalar_type;

    NoiseAWGNBlock(const char* name, scalar_type noise_stddev, const size_t buffer_size = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size), _kernel(noise_stddev) {

        _buffer_size = buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size;
        _buffer = new (std::nothrow) T[_buffer_size];
        if (!_buffer) {
            cler::panic("Failed to allocate temporary buffer");
        }
    }

    ~NoiseAWGNBlock() {
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
    AWGNKernel<T> _kernel;

    T* _buffer;
    size_t _buffer_size;
};
`,dn=`#pragma once

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
`,un=`#pragma once

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

private:
    void next_window_geometry();   // SetNextWindowPos/Size before Begin()

    size_t _samples_counter = 0;

    size_t _num_inputs;
    std::vector<std::string> _signal_labels;
    size_t _sps;
    size_t _n_fft_samples;
    size_t _buffer_size;
    SpectralWindow _window_type;

    cler::Channel<std::complex<float>>* _signal_channels;

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

    std::mutex _snapshot_mutex;
    size_t _snapshot_ready_size = 0;

    std::atomic<bool> _gui_pause = false;

    std::atomic<bool> _external_pause{false};

    bool _visible = true;
};
`,pn=`#pragma once

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
`,fn=`#pragma once
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
`,mn=`#pragma once

#include "liquid.h"
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <type_traits>
#include <new>

template <typename>
inline constexpr bool dependent_false_v = false;

template <typename T>
struct MultiStageResamplerBlock : public cler::BlockBase {
    cler::Channel<T> in;

    MultiStageResamplerBlock(const char* name, const float ratio, const float attenuation,
        const size_t buffer_size = 0)
        : cler::BlockBase(name), in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size), _ratio(ratio)
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

        if constexpr (std::is_same_v<T, float>) {
            _msresamp_r = msresamp_rrrf_create(ratio, attenuation);
            if (!_msresamp_r) {
                cler::panic("Failed to create multi-stage resampler for float");
            }
        } else if constexpr (std::is_same_v<T, std::complex<float>>) {
            _msresamp_c = msresamp_crcf_create(ratio, attenuation);
            if (!_msresamp_c) {
                cler::panic("Failed to create multi-stage resampler for complex float");
            }
        } else {
            static_assert(dependent_false_v<T>, "MultiStageResamplerBlock only supports float or std::complex<float>");
        }

        _buffer_size = buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : buffer_size;

        _input_buffer = new (std::nothrow) T[_buffer_size];
        if (!_input_buffer) {
            cler::panic("Failed to allocate input buffer");
        }

        // msresamp can emit slightly more than buffer_size * ratio samples per call
        // (interpolator/decimator state carried across calls); +100 is a safety margin.
        size_t output_buffer_size = static_cast<size_t>(_buffer_size * _ratio + 100);
        _output_buffer = new (std::nothrow) T[output_buffer_size];
        if (!_output_buffer) {
            cler::panic("Failed to allocate output buffer");
        }
        _output_buffer_size = output_buffer_size;
    }

    ~MultiStageResamplerBlock() {
        delete[] _input_buffer;
        delete[] _output_buffer;
        if constexpr (std::is_same_v<T, float>) {
            if (_msresamp_r) {
                msresamp_rrrf_destroy(_msresamp_r);
            }
        } else if constexpr (std::is_same_v<T, std::complex<float>>) {
            if (_msresamp_c) {
                msresamp_crcf_destroy(_msresamp_c);
            }
        }
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
    float _ratio;
    size_t _buffer_size;
    size_t _output_buffer_size;

    T* _input_buffer = nullptr;
    T* _output_buffer = nullptr;

    msresamp_rrrf _msresamp_r = nullptr;
    msresamp_crcf _msresamp_c = nullptr;
};
`,hn=`#pragma once

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
`,gn=`#pragma once

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

    SinkAudioBlock(const char* name,
                   double sample_rate = 48000.0,
                   int device_index = paNoDevice,
                   size_t buffer_size = 0)
        : cler::BlockBase(name),
          in(buffer_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float) : buffer_size),
          _sample_rate(sample_rate),
          _device_index(device_index),
          _stream(nullptr)
    {
        if (sample_rate <= 0.0 || sample_rate > 1e6) {
            cler::panic("Invalid sample rate: must be > 0 and <= 1MHz");
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
        if (read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        PaError err = Pa_WriteStream(_stream, read_ptr, read_size);

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

        output_params.channelCount = 1;
        output_params.sampleFormat = paFloat32;
        output_params.suggestedLatency = device_info->defaultHighOutputLatency;
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
`,bn=`#pragma once

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
`,Sn=`#pragma once
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
};`,vn=`#pragma once

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
};`,wn=`#pragma once

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
using SinkSoapySDRBlockF32 = SinkSoapySDRBlock<float>;`,zn=`#pragma once

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
`,kn=`#pragma once

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
`,xn=`#pragma once
#include <CaribouLite.hpp>
#include "cler.hpp"
#include "cler_desktop_utils.hpp"

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

        cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
            auto [ptr, space] = out->write_dbf();
            if (ptr == nullptr || space == 0) {
                return cler::Error::NotEnoughSpace;
            }

            size_t to_read = std::min(space, _max_samples_to_read);
            int ret = _radio->ReadSamples(ptr, to_read);
            if (ret < 0) {
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
};
`,yn=`#pragma once
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
`,En=`#pragma once
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
`,Rn=`#pragma once

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
`,Bn=`#pragma once
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
                      size_t buffer_size = 0)
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

        if (hackrf_open(&_dev) != HACKRF_SUCCESS) {
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

    uint64_t get_frequency() const { return _freq_hz; }
    uint32_t get_sample_rate() const { return _samp_rate_hz; }
    int get_lna_gain() const { return _lna_gain_db; }
    int get_vga_gain() const { return _vga_gain_db; }
    bool get_amp_enable() const { return _amp_enable; }
    size_t get_overflow_count() const { return _overflow_count.load(); }
    void reset_overflow_count() { _overflow_count.store(0); }

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
};`,In=`#pragma once
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

    // Retune while streaming (phy attrs are safe to write at runtime)
    void set_frequency(long long freq_hz) {
        if (_lo && iio_channel_attr_write_longlong(_lo, "frequency", freq_hz) >= 0) {
            _freq_hz = freq_hz;
        }
    }

private:
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

    long long _freq_hz;
    long long _samp_rate_hz;
    size_t _buffer_size;

    size_t _available = 0; // samples in the current iio buffer
    size_t _consumed = 0;  // samples already pushed downstream
};
`,Tn=`#pragma once

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
}`,An=`#pragma once

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

            auto gain_range = usrp->get_rx_gain_range(channel);
            if (config.gain < gain_range.start() || config.gain > gain_range.stop()) {
                std::cerr << "Gain " << config.gain << " dB out of range" << std::endl;
            }
            usrp->set_rx_gain(config.gain, channel);

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
using SourceUHDBlockSC8 = SourceUHDBlock<std::complex<int8_t>>;`,Dn=`#pragma once

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
`,Pn=`#pragma once
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
`,Cn=`#pragma once
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
`,On=`#pragma once
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
`,Nn=`#pragma once

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

        auto copy_to_output = [read_ptr, min_write_size](auto* out) {
            auto [write_ptr, write_size] = out->write_dbf();
            std::memcpy(write_ptr, read_ptr, min_write_size * sizeof(T));
            out->commit_write(min_write_size);
        };
        (copy_to_output(outs), ...);

        in.commit_read(min_write_size);

        return cler::Empty{};
    }

    private:
        size_t _num_outputs;
};
`,Mn=`#pragma once

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
`,qn=`#pragma once
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
`,Fn=`#pragma once
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
`,Un=Object.assign({"../../../../../desktop_blocks/adsb/adsb_aggregate.hpp":Ge,"../../../../../desktop_blocks/adsb/adsb_coastline_loader.hpp":Ye,"../../../../../desktop_blocks/adsb/adsb_decoder.hpp":Ve,"../../../../../desktop_blocks/adsb/adsb_types.hpp":Xe,"../../../../../desktop_blocks/blob.hpp":Ze,"../../../../../desktop_blocks/channelizers/polyphase_analyzer.hpp":We,"../../../../../desktop_blocks/channelizers/polyphase_channelizer.hpp":Ke,"../../../../../desktop_blocks/channelizers/polyphase_transform_5.hpp":je,"../../../../../desktop_blocks/ezgmsk/ezgmsk_demod.hpp":$e,"../../../../../desktop_blocks/ezgmsk/ezgmsk_mod.hpp":Qe,"../../../../../desktop_blocks/filters/kaiser_lpf.hpp":Je,"../../../../../desktop_blocks/fm/fm_demod.hpp":en,"../../../../../desktop_blocks/gui/cler_palette.hpp":nn,"../../../../../desktop_blocks/gui/gui_manager.hpp":tn,"../../../../../desktop_blocks/kernels/kernels.hpp":rn,"../../../../../desktop_blocks/math/add.hpp":an,"../../../../../desktop_blocks/math/complex_demux.hpp":sn,"../../../../../desktop_blocks/math/frequency_shift.hpp":_n,"../../../../../desktop_blocks/math/gain.hpp":on,"../../../../../desktop_blocks/misc/uhd_common.hpp":ln,"../../../../../desktop_blocks/noise/awgn.hpp":cn,"../../../../../desktop_blocks/plots/plot_cspectrogram.hpp":dn,"../../../../../desktop_blocks/plots/plot_cspectrum.hpp":un,"../../../../../desktop_blocks/plots/plot_timeseries.hpp":pn,"../../../../../desktop_blocks/plots/spectral_windows.hpp":fn,"../../../../../desktop_blocks/resamplers/multistage_resampler.hpp":mn,"../../../../../desktop_blocks/resamplers/rational_resampler.hpp":hn,"../../../../../desktop_blocks/sinks/sink_audio.hpp":gn,"../../../../../desktop_blocks/sinks/sink_file.hpp":bn,"../../../../../desktop_blocks/sinks/sink_hackrf.hpp":Sn,"../../../../../desktop_blocks/sinks/sink_null.hpp":vn,"../../../../../desktop_blocks/sinks/sink_soapysdr.hpp":wn,"../../../../../desktop_blocks/sinks/sink_uhd.hpp":zn,"../../../../../desktop_blocks/sources/source_audio_file.hpp":kn,"../../../../../desktop_blocks/sources/source_cariboulite.hpp":xn,"../../../../../desktop_blocks/sources/source_chirp.hpp":yn,"../../../../../desktop_blocks/sources/source_cw.hpp":En,"../../../../../desktop_blocks/sources/source_file.hpp":Rn,"../../../../../desktop_blocks/sources/source_hackrf.hpp":Bn,"../../../../../desktop_blocks/sources/source_pluto.hpp":In,"../../../../../desktop_blocks/sources/source_soapysdr.hpp":Tn,"../../../../../desktop_blocks/sources/source_uhd.hpp":An,"../../../../../desktop_blocks/triggers/trigger_block.hpp":Dn,"../../../../../desktop_blocks/udp/shared.hpp":Pn,"../../../../../desktop_blocks/udp/sink_udp.hpp":Cn,"../../../../../desktop_blocks/udp/source_udp.hpp":On,"../../../../../desktop_blocks/utils/fanout.hpp":Nn,"../../../../../desktop_blocks/utils/fused.hpp":Mn,"../../../../../desktop_blocks/utils/throttle.hpp":qn,"../../../../../desktop_blocks/utils/throughput.hpp":Fn}),G={};for(const[e,n]of Object.entries(K)){const t=j[e]?.file;t&&(G[t]=n)}for(const[e,n]of Object.entries(Un))G[e.replace(/^(\.\.\/)+/,"")]=n;const Ln=["hello_world","mass_spring_damper","plots","polyphase_channelizer"],Hn=Ln.flatMap(e=>{const n=j[e]?.file,t=K[e];return n&&t!==void 0?[{name:e,path:n,source:t}]:[]});async function Yn(){try{const n=new URLSearchParams({toolchain:ke,pins:JSON.stringify(xe)});if(await navigator.serviceWorker.register(`./cler-sw.js?${n}`,{scope:"./"}),await navigator.serviceWorker.ready,window.crossOriginIsolated)sessionStorage.removeItem("clerSwReload");else if(!sessionStorage.getItem("clerSwReload")){sessionStorage.setItem("clerSwReload","1"),location.reload();return}}catch(n){console.warn("cler service worker unavailable",n),Re("Build and Run need a service worker; this browser mode disables it")}await qe(G,Hn)}export{Yn as bootBrowser};
