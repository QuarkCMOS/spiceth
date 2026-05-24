#pragma once
/**
 * html_export.h  –  Generate a self-contained HTML plot from SimulationResult.
 *
 * No Python, no dependencies. Uses Chart.js via CDN (or embedded fallback).
 * Output: one .html file, double-click to open in any browser.
 *
 * Usage:
 *   circuit_engine my.cir --plot            → writes my.html, opens browser
 *   circuit_engine my.cir --plot out.html   → writes to named file
 */

#include "core/types.h"
#include "json_export.h"
#include <string>
#include <sstream>
#include <cmath>
#include <vector>
#include <algorithm>

namespace CircuitEngine {

inline std::string to_html(const SimulationResult& res, const std::string& source_name = "")
{
    // Embed JSON directly into the HTML so it's self-contained
    std::string json = to_json(res);

    // Escape for JS string embedding (json is already valid; just need backtick safety)
    // We'll use JSON.parse on a var, so wrap in single-quoted string with escapes
    std::string json_escaped;
    json_escaped.reserve(json.size() * 2);
    for (char c : json) {
        if (c == '\\') json_escaped += "\\\\";
        else if (c == '`') json_escaped += "\\`";
        else if (c == '$') json_escaped += "\\$";
        else json_escaped += c;
    }

    std::ostringstream h;
    h << R"(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>CircuitEngine — )" << (source_name.empty() ? "Simulation Result" : source_name) << R"(</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
<style>
  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    background: #0f1117;
    color: #e0e0e0;
    font-family: 'Segoe UI', system-ui, sans-serif;
    padding: 24px;
    min-height: 100vh;
  }
  h1 {
    font-size: 1.3rem;
    color: #90caf9;
    margin-bottom: 6px;
    letter-spacing: .04em;
  }
  .subtitle {
    font-size: .85rem;
    color: #666;
    margin-bottom: 28px;
  }
  .grid {
    display: grid;
    gap: 20px;
    grid-template-columns: repeat(auto-fill, minmax(560px, 1fr));
  }
  .card {
    background: #1a1d27;
    border: 1px solid #2a2d3a;
    border-radius: 10px;
    padding: 20px;
  }
  .card h2 {
    font-size: .95rem;
    color: #b0bec5;
    margin-bottom: 14px;
    text-transform: uppercase;
    letter-spacing: .06em;
  }
  canvas { width: 100% !important; }

  /* DC table */
  table { width: 100%; border-collapse: collapse; font-size: .9rem; }
  th { text-align: left; color: #78909c; font-weight: 500;
       border-bottom: 1px solid #2a2d3a; padding: 6px 10px; }
  td { padding: 7px 10px; border-bottom: 1px solid #1e2130; }
  td.name  { color: #90caf9; font-family: monospace; font-size: .93rem; }
  td.value { color: #a5d6a7; text-align: right; font-family: monospace; }
  td.type  { color: #666; font-size: .8rem; }
  tr:last-child td { border-bottom: none; }

  .error { color: #ef9a9a; background: #2d1a1a;
           border: 1px solid #7f1d1d; border-radius: 8px; padding: 16px; }
  .badge {
    display: inline-block;
    padding: 3px 10px;
    border-radius: 20px;
    font-size: .75rem;
    font-weight: 600;
    letter-spacing: .05em;
    text-transform: uppercase;
    margin-left: 12px;
    vertical-align: middle;
  }
  .badge-op   { background: #37474f; color: #b0bec5; }
  .badge-dc   { background: #1565c0; color: #90caf9; }
  .badge-ac   { background: #4a148c; color: #ce93d8; }
  .badge-tran { background: #1b5e20; color: #a5d6a7; }
</style>
</head>
<body>
<h1>CircuitEngine
  <span id="badge" class="badge"></span>
</h1>
<div class="subtitle" id="subtitle">)" << source_name << R"(</div>
<div class="grid" id="grid"></div>

<script>
const RAW = `)" << json_escaped << R"(`;
const R = JSON.parse(RAW);

// ── Helpers ──────────────────────────────────────────────────────
function eng(v, unit) {
  const a = Math.abs(v);
  if (a === 0) return `0 ${unit}`;
  if (a >= 1e9)  return `${(v/1e9).toPrecision(5)} G${unit}`;
  if (a >= 1e6)  return `${(v/1e6).toPrecision(5)} M${unit}`;
  if (a >= 1e3)  return `${(v/1e3).toPrecision(5)} k${unit}`;
  if (a >= 1)    return `${v.toPrecision(6)} ${unit}`;
  if (a >= 1e-3) return `${(v*1e3).toPrecision(5)} m${unit}`;
  if (a >= 1e-6) return `${(v*1e6).toPrecision(5)} µ${unit}`;
  if (a >= 1e-9) return `${(v*1e9).toPrecision(5)} n${unit}`;
  return `${(v*1e12).toPrecision(5)} p${unit}`;
}

const COLORS = ['#42a5f5','#ef5350','#66bb6a','#ffa726','#ab47bc',
                '#26c6da','#ffca28','#8d6e63','#78909c'];

function makeCard(title) {
  const card = document.createElement('div');
  card.className = 'card';
  const h2 = document.createElement('h2');
  h2.textContent = title;
  card.appendChild(h2);
  document.getElementById('grid').appendChild(card);
  return card;
}

function makeCanvas(card) {
  const wrap = document.createElement('div');
  wrap.style.position = 'relative';
  const cv = document.createElement('canvas');
  wrap.appendChild(cv);
  card.appendChild(wrap);
  return cv;
}

// ── Badge ────────────────────────────────────────────────────────
const badge = document.getElementById('badge');
badge.textContent = R.analysis_type;
badge.classList.add('badge-' + R.analysis_type);

if (!R.success) {
  const d = document.createElement('div');
  d.className = 'error';
  d.textContent = '✗ ' + R.error_msg;
  document.getElementById('grid').appendChild(d);
} else if (R.analysis_type === 'op') {

  // ── .OP: operating-point table + bar chart ─────────────────────
  const pt    = R.data[0];
  const volts = pt.values.filter(v => v.type === 'voltage');
  const amps  = pt.values.filter(v => v.type === 'current');

  // Table
  const card = makeCard('Operating Point  (.OP)');
  const tbl  = document.createElement('table');
  tbl.innerHTML = '<tr><th>Signal</th><th>Type</th><th style="text-align:right">Value</th></tr>';
  for (const v of volts)
    tbl.innerHTML += `<tr>
      <td class="name">V(${v.name})</td>
      <td class="type">voltage</td>
      <td class="value">${eng(v.real,'V')}</td></tr>`;
  for (const a of amps)
    tbl.innerHTML += `<tr>
      <td class="name">I(${a.name})</td>
      <td class="type">current</td>
      <td class="value" style="color:#ffa726">${eng(a.real,'A')}</td></tr>`;
  card.appendChild(tbl);

  // Bar chart — voltages only
  if (volts.length) {
    const c2 = makeCard('Node Voltages  (.OP)');
    new Chart(makeCanvas(c2), {
      type: 'bar',
      data: {
        labels: volts.map(v => v.name),
        datasets: [{ data: volts.map(v => v.real),
                     backgroundColor: COLORS.slice(0, volts.length),
                     borderRadius: 4 }]
      },
      options: {
        plugins: { legend: { display: false } },
        scales: {
          x: { ticks: { color: '#90caf9' }, grid: { color: '#2a2d3a' } },
          y: { ticks: { color: '#aaa', callback: v => eng(v,'V') },
               grid: { color: '#2a2d3a' } }
        }
      }
    });
  }

} else if (R.analysis_type === 'dc') {

  // ── .DC: sweep charts — one line per node/branch ───────────────
  const sweepVals = R.data.map(p => p.sweep_value);

  // Collect signal names (preserve order from first data point)
  const vnames = R.data[0].values
    .filter(v => v.type === 'voltage').map(v => v.name);
  const inames = R.data[0].values
    .filter(v => v.type === 'current').map(v => v.name);

  // x-axis label: the sweep source name (stored in subtitle)
  const srcLabel = R.data[0] ? 'Source Value' : 'V';

  const dcLineOpts = (ylabel, unitLabel) => ({
    animation: false,
    plugins: {
      legend: { labels: { color: '#aaa', boxWidth: 12 } },
      tooltip: {
        callbacks: {
          label: ctx => `${ctx.dataset.label}: ${eng(ctx.parsed.y, unitLabel)}`
        }
      }
    },
    scales: {
      x: {
        type: 'linear',
        ticks: { color: '#aaa', callback: v => eng(v, unitLabel === 'V' ? 'V' : 'A') },
        grid: { color: '#2a2d3a' },
        title: { display: true, text: 'Sweep value', color: '#78909c' }
      },
      y: {
        ticks: { color: '#aaa', callback: v => eng(v, unitLabel) },
        grid:  { color: '#2a2d3a' },
        title: { display: true, text: ylabel, color: '#78909c' }
      }
    },
    elements: { point: { radius: 0 } }
  });

  function buildDCDatasets(names, unitLabel) {
    return names.map((name, ci) => ({
      label: name,
      data: sweepVals.map((sv, i) => {
        const nv = R.data[i].values.find(v => v.name === name);
        return { x: sv, y: nv ? nv.real : null };
      }),
      borderColor: COLORS[ci % COLORS.length],
      borderWidth: 2,
      tension: 0.1,
      pointRadius: sweepVals.length < 50 ? 3 : 0
    }));
  }

  if (vnames.length) {
    const c = makeCard('Node Voltages vs Sweep  (.DC)');
    new Chart(makeCanvas(c), {
      type: 'line',
      data: { datasets: buildDCDatasets(vnames, 'V') },
      options: dcLineOpts('Voltage [V]', 'V')
    });
  }

  if (inames.length) {
    const c = makeCard('Branch Currents vs Sweep  (.DC)');
    new Chart(makeCanvas(c), {
      type: 'line',
      data: { datasets: buildDCDatasets(inames, 'A') },
      options: dcLineOpts('Current [A]', 'A')
    });
  }

} else if (R.analysis_type === 'ac') {

  // ── AC: Bode plots per voltage node ───────────────────────────
  const freqs = R.data.map(p => p.sweep_value);

  // Collect unique voltage signal names
  const names = [...new Set(
    R.data[0].values.filter(v=>v.type==='voltage').map(v=>v.name)
  )];

  names.forEach((name, ci) => {
    const mags   = [];
    const phases = [];
    for (const pt of R.data) {
      const nv = pt.values.find(v => v.name === name);
      if (!nv) { mags.push(null); phases.push(null); continue; }
      const mag = Math.sqrt(nv.real**2 + nv.imag**2);
      mags  .push(20 * Math.log10(Math.max(mag, 1e-30)));
      phases.push(Math.atan2(nv.imag, nv.real) * 180 / Math.PI);
    }

    const color = COLORS[ci % COLORS.length];
    const pts = freqs.map((f,i) => ({x: f, y: mags[i]}));
    const pts2= freqs.map((f,i) => ({x: f, y: phases[i]}));

    const common = (ylabel) => ({
      plugins: { legend: { display: false } },
      scales: {
        x: {
          type: 'logarithmic',
          ticks: { color: '#aaa',
            callback: v => v>=1e6 ? (v/1e6)+'M' : v>=1e3 ? (v/1e3)+'k' : v },
          grid: { color: '#2a2d3a' }
        },
        y: {
          ticks: { color: '#aaa' },
          grid:  { color: '#2a2d3a' },
          title: { display: true, text: ylabel, color: '#78909c' }
        }
      },
      elements: { point: { radius: 0 } }
    });

    const c1 = makeCard(`|V(${name})|  Magnitude`);
    new Chart(makeCanvas(c1), {
      type: 'line',
      data: { datasets: [{ data: pts, borderColor: color, borderWidth: 2, tension: 0.3 }] },
      options: common('dB')
    });

    const c2 = makeCard(`∠V(${name})  Phase`);
    new Chart(makeCanvas(c2), {
      type: 'line',
      data: { datasets: [{ data: pts2, borderColor: '#ffa726', borderWidth: 2, tension: 0.3 }] },
      options: common('degrees')
    });
  });

} else if (R.analysis_type === 'tran') {

  // ── TRAN: one chart per voltage, one for currents ──────────────
  const times = R.data.map(p => p.sweep_value * 1e3); // → ms

  const vnames = [...new Set(
    R.data[0].values.filter(v=>v.type==='voltage').map(v=>v.name)
  )];
  const inames = [...new Set(
    R.data[0].values.filter(v=>v.type==='current').map(v=>v.name)
  )];

  function buildDatasets(names, scale, unit) {
    return names.map((name, ci) => {
      const ydata = R.data.map(pt => {
        const nv = pt.values.find(v => v.name === name);
        return nv ? nv.real * scale : null;
      });
      return {
        label: name,
        data: times.map((t,i) => ({x:t, y:ydata[i]})),
        borderColor: COLORS[ci % COLORS.length],
        borderWidth: 1.8,
        tension: 0.1,
        pointRadius: 0
      };
    });
  }

  const tranOpts = (ylabel) => ({
    animation: false,
    plugins: { legend: { labels: { color: '#aaa', boxWidth: 12 } } },
    scales: {
      x: { type:'linear',
           ticks:{ color:'#aaa', callback: v => v+'ms' },
           grid: { color:'#2a2d3a' },
           title: { display:true, text:'Time [ms]', color:'#78909c' } },
      y: { ticks:{ color:'#aaa', callback: v => eng(v, ylabel==='V'?'V':'A') },
           grid: { color:'#2a2d3a' },
           title: { display:true, text: ylabel==='V'?'Voltage [V]':'Current [A]', color:'#78909c' }}
    },
    elements: { point: { radius: 0 } }
  });

  if (vnames.length) {
    const c = makeCard('Voltages vs Time');
    new Chart(makeCanvas(c), {
      type: 'line',
      data: { datasets: buildDatasets(vnames, 1, 'V') },
      options: tranOpts('V')
    });
  }
  if (inames.length) {
    const c = makeCard('Currents vs Time');
    new Chart(makeCanvas(c), {
      type: 'line',
      data: { datasets: buildDatasets(inames, 1e3, 'mA') },
      options: tranOpts('A')
    });
  }
}
</script>
</body>
</html>
)";
    return h.str();
}

} // namespace CircuitEngine