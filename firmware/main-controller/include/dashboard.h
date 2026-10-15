#pragma once
#include <Arduino.h>
static const char DASHBOARD_HTML[] PROGMEM=R"html(<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>RideShield controller</title><style>
body{font:16px system-ui;background:#101820;color:#eef4f5;max-width:900px;margin:24px auto;padding:0 18px}
h1{margin-bottom:4px}small,p{color:#afc1cd}.badge{padding:8px 12px;background:#20333f;border-radius:8px;display:inline-block}
section{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px;margin-top:20px}
article{background:#192731;border:1px solid #344550;border-radius:12px;padding:16px}h2{font-size:17px;margin:0 0 10px}
dl{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin:0}dt{color:#aebfc9}dd{margin:0;overflow-wrap:anywhere}a{color:#83d6ef}
</style><h1>RideShield</h1><small>Main controller · NodeMCU-32S</small>
<p id="link">Connecting to controller…</p><b class="badge" id="state">—</b> <b class="badge" id="warning">—</b>
<section>
<article><h2>Motion</h2><dl><dt>IMU</dt><dd id="imu">—</dd><dt>Acceleration</dt><dd id="accel_g">—</dd><dt>Rotation</dt><dd id="gyro_dps">—</dd><dt>Sample gaps</dt><dd id="gaps">—</dd><dt>Event index</dt><dd id="event_index">—</dd></dl></article>
<article><h2>Helmet</h2><dl><dt>Head</dt><dd id="head">—</dd><dt>Strap</dt><dd id="strap_closed">—</dd><dt>Wear state</dt><dd id="wear">—</dd><dt>Distance mm</dt><dd id="tof_mm">—</dd><dt>ToF valid</dt><dd id="tof_valid">—</dd><dt>Calibrated</dt><dd id="head_calibrated">—</dd></dl></article>
<article><h2>Rider inputs</h2><dl><dt>IR raw</dt><dd id="ir_raw">—</dd><dt>IR calibrated</dt><dd id="ir_calibrated">—</dd><dt>Prolonged signal</dt><dd id="eye_signal">—</dd><dt>MQ input mV</dt><dd id="mq_mv">—</dd><dt>Screening</dt><dd id="alcohol">—</dd><dt>Inputs ready</dt><dd id="pre_ride_ready">—</dd></dl></article>
<article><h2>Location</h2><dl><dt>Fresh GPS fix</dt><dd id="gps_fresh">—</dd><dt>Latitude</dt><dd id="latitude">—</dd><dt>Longitude</dt><dd id="longitude">—</dd><dt>Fix age ms</dt><dd id="gps_age_ms">—</dd><dt>Speed km/h</dt><dd id="speed_kmh">—</dd></dl></article>
<article><h2>Emergency</h2><dl><dt>Countdown ms</dt><dd id="remaining_ms">—</dd><dt>SMS state</dt><dd id="send">—</dd><dt>Live SMS</dt><dd id="sms_enabled">—</dd><dt>Modem online</dt><dd id="modem_online">—</dd><dt>Network registered</dt><dd id="modem_registered">—</dd><dt>SD ready</dt><dd id="sd_ready">—</dd></dl></article>
<article><h2>Connections</h2><dl><dt>Phone</dt><dd id="phone">—</dd><dt>Clock sync</dt><dd id="clock">—</dd><dt>Wi-Fi clients</dt><dd id="clients">—</dd><dt>Free heap</dt><dd id="free_heap">—</dd><dt>Loop max µs</dt><dd id="loop_max_us">—</dd></dl></article>
</section><p>Calibrate sensors through USB Serial. This page shows telemetry; use the physical Cancel and SOS buttons for rider actions.</p>
<p>IR is a calibrated reflectance signal. MQ is vapour screening. The experimental event index is not an injury score.</p>
<script>async function update(){try{const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw Error(r.status);const s=await r.json();for(const [k,v] of Object.entries(s)){const el=document.getElementById(k);if(el)el.textContent=v===null?'Unavailable':String(v)}document.getElementById('link').textContent='Connected · updated '+new Date().toLocaleTimeString()}catch(e){document.getElementById('link').textContent='Controller connection lost — displayed values are stale'}setTimeout(update,1000)}update();</script></html>)html";
