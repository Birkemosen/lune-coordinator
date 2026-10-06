/* Lune Touch binder — live data + save hooks for Design System 2 pages.
   Progressive enhancement only: UI works without this script except live values. */
(function () {
  'use strict';

  var BASE = '/api/lune-touch/v1';
  var POLL_MS = 20000;
  var i18n = {};
  try {
    var el = document.getElementById('i18n');
    if (el) i18n = JSON.parse(el.textContent || '{}');
  } catch (e) {}

  var dec = i18n._dec || '.';
  var state = {
    rooms: [], zones: [], nodes: [], overview: null, forecast: null, heat: null,
    settings: null, diagnostics: null, chartsLoaded: {}, selected: { m: null, z: null },
    strategy: null, scanFound: [],
    fwInstalled: '', fwLatest: null, fwAsset: null
  };

  var RELEASE_LATEST_API = 'https://api.github.com/repos/birkemosen/lune-coordinator/releases/latest';
  var OTA_UPLOAD_PATH = '/update';
  var BACKUP_TYPE = 'lune-touch-settings';
  var BACKUP_VERSION = 1;

  function esc(s) {
    return String(s == null ? '' : s).replace(/[&<>"']/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
    });
  }

  var PENCIL = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 20h4L18 10l-4-4L4 16v4z"/><path d="M13 7l4 4"/></svg>';

  function num(x, digits) {
    if (x == null || x !== x) return '—';
    var n = Number(x);
    if (!isFinite(n)) return '—';
    var d = digits == null ? 1 : digits;
    return n.toFixed(d).replace('.', dec);
  }

  function t(key, vars) {
    var s = i18n[key] || key;
    if (!vars) return s;
    return s.replace(/\{(\w+)\}/g, function (_, k) { return vars[k] != null ? vars[k] : ''; });
  }

  function qs(sel, root) { return (root || document).querySelector(sel); }
  function qsa(sel, root) { return Array.prototype.slice.call((root || document).querySelectorAll(sel)); }

  function setBind(key, html) {
    qsa('[data-bind="' + key + '"]').forEach(function (n) { n.innerHTML = html; });
  }

  function setText(key, text) {
    qsa('[data-bind="' + key + '"]').forEach(function (n) { n.textContent = text; });
  }

  function formObj(fd) {
    var o = {};
    fd.forEach(function (v, k) {
      if (o[k] !== undefined) {
        if (!Array.isArray(o[k])) o[k] = [o[k]];
        o[k].push(v);
      } else o[k] = v;
    });
    return o;
  }

  function queryUrl(path, body) {
    var q = Object.keys(body || {}).map(function (k) {
      return encodeURIComponent(k) + '=' + encodeURIComponent(body[k] == null ? '' : body[k]);
    }).join('&');
    return BASE + path + (q ? '?' + q : '');
  }

  async function get(path) {
    if (window.LUNE_TOUCH_MOCK) return mockGet(path);
    var res = await fetch(BASE + path, { headers: { Accept: 'application/json' } });
    var json = await res.json();
    if (!json || json.ok === false) throw new Error((json && json.error && json.error.message) || 'request failed');
    return json.data != null ? json.data : json;
  }

  async function post(path, body) {
    if (window.LUNE_TOUCH_MOCK) {
      if (path === '/heat-source/test-read' || path === '/heat-source/test-push') {
        await new Promise(function (r) { setTimeout(r, 450); });
        return mockProbe(path);
      }
      if (path === '/nodes/scan') return mockScan();
      if (path === '/nodes') {
        var raw = (body && (body.node_id || body.hostname || body.ip)) || 'v6-new';
        var id = String(raw).replace(/[^a-zA-Z0-9_-]/g, '-').slice(0, 24) || 'v6-new';
        mockNodeList.push({
          id: id, name: '', hostname: (body && body.hostname) || '', ip: (body && body.ip) || '',
          trust_label: 'paired', reachable: true, health: { mapped_zones: 0 }, runtime: { flow_c: null, return_c: null }
        });
        return { result: 'stored', node_id: id };
      }
      var mh = path.match(/^\/nodes\/([^/]+)\/host$/);
      if (mh && body && body.host) {
        var isIp = /^\d{1,3}(\.\d{1,3}){3}$/.test(body.host);
        mockNodeList.forEach(function (n) {
          if (n.id !== decodeURIComponent(mh[1])) return;
          if (isIp) { n.ip = body.host; n.hostname = ''; } else n.hostname = body.host;
        });
        return { result: 'stored' };
      }
      if (path === '/wifi' && body && body.ssid) {
        mockWifi = { ssid: mockWifi.ssid, connected: true, ap_active: false, 'switch': 'pending', target_ssid: body.ssid };
        setTimeout(function () { mockWifi = { ssid: body.ssid, connected: true, ap_active: false, 'switch': 'connected', target_ssid: body.ssid }; }, 1500);
        return mockWifi;
      }
      var prof = path.match(/^\/nodes\/([^/]+)\/profile$/);
      if (prof && body && body.name) {
        mockNodeList.forEach(function (n) { if (n.id === decodeURIComponent(prof[1])) n.name = body.name; });
        return { result: 'stored' };
      }
      var rm = path.match(/^\/nodes\/([^/]+)\/remove$/);
      if (rm) {
        var rid = decodeURIComponent(rm[1]);
        mockNodeList = mockNodeList.filter(function (n) { return n.id !== rid; });
        return { result: 'stored' };
      }
      if (path === '/prices/settings' && body) {
        var mp = mockPrices, wasPush = mp.enabled && mp.model === 'touch';
        var str = function (k) { return body[k] != null && body[k] !== '' ? String(body[k]) : null; };
        if (body.enabled != null) mp.enabled = body.enabled === '1' || body.enabled === true;
        if (str('model')) mp.model = str('model');
        if (str('zone')) { mp.zone = str('zone'); mp.dk = /^DK[12]$/.test(mp.zone); }
        if (str('spot_source')) mp.spot.source = str('spot_source');
        if (str('spot_fixed_eur')) mp.spot.fixed_eur = Number(body.spot_fixed_eur);
        if (str('currency')) mp.currency = str('currency');
        if (str('fx')) mp.fx = Number(body.fx);
        if (str('grid_source')) mp.grid.source = str('grid_source');
        if (str('grid_gln')) mp.grid.gln = str('grid_gln');
        if (str('grid_code')) mp.grid.code = str('grid_code');
        if (str('grid_schedule')) { try { mp.grid.schedule = JSON.parse(body.grid_schedule); } catch (e) {} }
        if (str('system_source')) mp.system.source = str('system_source');
        if (str('system_fixed')) mp.system.fixed = Number(body.system_fixed);
        ['energy_tax', 'markup', 'vat_pct'].forEach(function (k) { if (str(k)) mp[k] = Number(body[k]); });
        if (str('odin_mode')) mp.odin.mode = str('odin_mode');
        if (str('odin_source')) mp.odin.source = str('odin_source');
        if (str('odin_fixed_price')) mp.odin.fixed_price = Number(body.odin_fixed_price);
        if (str('entsoe_token')) mp.entsoe_token_set = true;
        var pushes = mp.enabled && mp.model === 'touch';
        if (mp.enabled && mp.model === 'odin') {
          mp.odin.current = { known: true, price_mode: mp.odin.mode, price_source: mp.odin.source, ec_bzn: mp.zone,
            fixed_price: mp.odin.fixed_price, token_set: mp.odin.current.token_set || !!str('entsoe_token'), age_s: 1 };
        } else if (pushes) mp.odin.current.price_source = 'api';
        else if (wasPush) mp.odin.current.price_source = 'energy_charts';
        mp.status.state = !mp.enabled ? 'disabled' : (pushes ? 'ok' : 'odin');
        mp.status.pushes = pushes;
        return { result: 'saved', enabled: mp.enabled, model: mp.model, push_queued: pushes };
      }
      if (path === '/prices/push') {
        if (!mockPrices.enabled) throw new Error('disabled');
        var st0 = mockPrices.status;
        st0.state = 'running';
        setTimeout(function () {
          st0.state = mockPrices.model === 'touch' ? 'ok' : 'odin';
          st0.reason = 'request';
          st0.last_attempt_epoch = Math.floor(Date.now() / 1000);
          if (mockPrices.model === 'touch') st0.last_push_epoch = st0.last_attempt_epoch;
          st0.odin_write_pending = false;
        }, 1800);
        return { result: 'queued' };
      }
      if (path === '/heat-source/settings' && body) {
        if (body.type) mockHeat.type = body.type === 'generic_http' ? 'generic_http' : 'asgard';
        if (body.host) mockHeat.host = body.host;
        if (body.port) mockHeat.port = Number(body.port);
        if (body.enabled != null) mockHeat.enabled = body.enabled === '1' || body.enabled === true || body.enabled === 1;
        if (body.target_sync_enabled != null) mockHeat.target_sync_enabled = body.target_sync_enabled === '1' || body.target_sync_enabled === true;
        if (body.odin_plan_enabled != null) mockHeat.odin_plan_enabled = body.odin_plan_enabled === '1' || body.odin_plan_enabled === true;
        if (body.climate_entity) mockHeat.climate_entity = body.climate_entity;
        if (body.weighted_temperature_variable) mockHeat.weighted_temperature_variable = body.weighted_temperature_variable;
        if (body.write_url_template != null) mockHeat.write_url_template = body.write_url_template;
        if (body.read_url_template != null) mockHeat.read_url_template = body.read_url_template;
        return { result: 'stored' };
      }
      return { ok: true, mock: true, path: path, body: body };
    }
    var payload = body || {};
    var res;
    try {
      res = await fetch(BASE + path, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: Object.keys(payload).map(function (k) {
          return encodeURIComponent(k) + '=' + encodeURIComponent(payload[k] == null ? '' : payload[k]);
        }).join('&')
      });
    } catch (e) {
      res = await fetch(queryUrl(path, payload), { method: 'POST', body: '' });
    }
    var json = await res.json();
    if (!json || json.ok === false) throw new Error((json && json.error && json.error.message) || 'save failed');
    return json.data != null ? json.data : json;
  }

  var mockHeat = {
    type: 'asgard',
    enabled: true,
    host: 'asgard.local',
    port: 80,
    weighted_temperature_variable: 'temperature_feedback_z1',
    push_interval_s: 60,
    write_url_template: '',
    read_url_template: '',
    climate_entity: 'virtual_thermostat',
    target_sync_enabled: true,
    odin_plan_enabled: true,
    physical_house_temperature_c: 20.3,
    house_comfort_target_c: 20.5,
    house_target: { available: true, value_c: 20.5 },
    send_preview: { available: true, value_c: 20.3, target_setpoint_c: 20.5, target_available: true, mode: 'active' },
    push: {
      has_result: true, status: 'confirmed', http_status: 200,
      requested_value_c: 20.3, confirmed_value_c: 20.3,
      write_age_s: 42, confirmation_age_s: 42, failure_streak: 0, last_error: ''
    },
    target_sync: { last_written_c: 20.5, last_confirmed_c: 20.5, failure_streak: 0, write_age_s: 42 },
    circulation: {
      host: 'pump.local', port: 80, flow_m3h: 1.2, head_m: 4.5, power_w: 42,
      flow_entity: 'pump_flow', head_entity: 'pump_head_pressure', power_entity: 'pump_power'
    }
  };

  var mockWifi = { ssid: 'Hjemme', connected: true, ap_active: false, 'switch': 'none', target_ssid: '' };

  // DK1 2026-10-06 from Energi Data Service (spot DKK/kWh, hourly means of 15-min prices).
  var mockPrices = (function () {
    var spot = [1.0687, 1.1007, 1.1199, 1.1162, 1.1288, 1.2298, 1.5392, 1.7849, 1.8631, 1.5656, 1.3057, 1.0951,
      1.0252, 0.9895, 1.0431, 1.1543, 1.4143, 1.8572, 2.5288, 2.85, 2.2129, 1.8561, 1.6844, 1.5644];
    var grid = [], sys = [], total = [];
    for (var h = 0; h < 24; h++) {
      grid.push(h < 6 ? 0.0769 : (h >= 17 && h < 21 ? 0.6922 : 0.2307));
      sys.push(0.115);
      total.push(Math.round((spot[h] + grid[h] + sys[h] + 0.008) * 1.25 * 10000) / 10000);
    }
    var d = new Date();
    var p2 = function (n) { return n < 10 ? '0' + n : String(n); };
    var now = Math.floor(Date.now() / 1000);
    return {
      available: true, enabled: true, model: 'touch', zone: 'DK1', dk: true,
      spot: { source: 'eds', fixed_eur: 0.1 }, currency: 'DKK', fx: 7.46,
      grid: { source: 'datahub', gln: '5790000610976', code: 'TNT1009',
        schedule: [{ h: 0, v: 0.077 }, { h: 6, v: 0.231 }, { h: 17, v: 0.692 }, { h: 21, v: 0.231 }] },
      system: { source: 'datahub', fixed: 0.115 },
      energy_tax: 0.008, markup: 0, vat_pct: 25, entsoe_token_set: false, odin_host_set: true,
      odin: { mode: 'dynamic', source: 'energy_charts', fixed_price: 0.25,
        current: { known: true, price_mode: 'dynamic', price_source: 'api', ec_bzn: 'DK1', fixed_price: 0.25, token_set: true, age_s: 60 } },
      status: { state: 'ok', pushes: true, reason: 'day_ahead', odin_source: 'api', odin_write_pending: false,
        last_fetch_age_s: 240, last_push_epoch: now - 240, last_attempt_epoch: now - 240, hours_pushed: 24, fx: 7.4736,
        spot_used: 'eds', spot_fallback: false, grid_from_cache: false, system_from_cache: false, last_error: '' },
      today: { date: d.getFullYear() + '-' + p2(d.getMonth() + 1) + '-' + p2(d.getDate()),
        spot: spot, grid: grid, system: sys, total: total },
      tomorrow: null
    };
  })();

  // Subset of energy_price.h's zone table for the preview's "Apply zone defaults".
  var mockZoneDefaults = {
    DK1: ['eds', 'DKK', 7.46, 0.008, 25, 'datahub', 'datahub', 0.115],
    DK2: ['eds', 'DKK', 7.46, 0.008, 25, 'datahub', 'datahub', 0.115],
    NL: ['energy_charts', 'EUR', 1, 0.109, 21, 'none', 'fixed', 0],
    'DE-LU': ['energy_charts', 'EUR', 1, 0.041, 19, 'none', 'fixed', 0],
    SE3: ['energy_charts', 'SEK', 11, 0, 25, 'none', 'fixed', 0],
    NO1: ['energy_charts', 'NOK', 11.7, 0, 25, 'none', 'fixed', 0]
  };

  function mockScan() {
    return {
      scan: 'lan', poll_pending: false, poll_generation: 2,
      found: [
        { source: 'known_node', node_id: 'v6-a', hostname: 'lune-v6-a.local', ip: '192.168.1.110', reachable: true },
        { source: 'lan_probe', node_id: 'v6-new', hostname: 'lune-v6-3.local', ip: '192.168.1.130', reachable: true }
      ]
    };
  }

  function mockProbe(path) {
    var http = document.getElementById('hs-http');
    var asgard = document.getElementById('hs-asgard');
    var type = asgard && asgard.checked ? 'asgard' : 'http';
    var hostEl = document.querySelector(type === 'asgard' ? '[name="asgard_host"]' : '[name="http_host"]');
    var portEl = document.querySelector(type === 'asgard' ? '[name="asgard_port"]' : '[name="http_port"]');
    var host = hostEl ? String(hostEl.value || '') : '';
    var port = portEl ? Number(portEl.value) : 80;
    var entity = type === 'asgard' ? 'temperature_feedback_z1' : 'house_temp';
    var base = { action: path.indexOf('push') !== -1 ? 'push' : 'read', host: host, port: port, entity: entity };
    if (/timeout/i.test(host)) {
      return Object.assign(base, { result: 'failed', error: 'probe_timeout', http_status: 0 });
    }
    if (port === 500 || /500/.test(host)) {
      return Object.assign(base, { result: 'failed', error: 'read http 500', http_status: 500 });
    }
    if (base.action === 'push') {
      return Object.assign(base, {
        result: 'ok', http_status: 200, requested_value_c: 20.3, confirmed_value_c: 20.3,
        write_url: 'http://' + (host || 'asgard.local') + ':' + port + '/number/' + entity + '/set?value=20.3'
      });
    }
    return Object.assign(base, { result: 'ok', http_status: 200, value_c: 20.3 });
  }

  function mockGet(path) {
    if (path === '/overview') {
      return {
        house_temp_c: 20.8, house_target_c: 21.0, coverage_ratio: 1, expected_manifolds: 2,
        contributing_manifolds: 2, calling_rooms: 3, fault_boards: 1, authority: 'Touch'
      };
    }
    if (path === '/nodes') return { nodes: mockNodeList };
    if (path === '/nodes/scan') return mockScan();
    if (path === '/wifi') return mockWifi;
    if (path === '/prices') return JSON.parse(JSON.stringify(mockPrices));
    var zd = path.match(/^\/prices\/zone-defaults\/(.+)$/);
    if (zd) {
      var zid = decodeURIComponent(zd[1]), r = mockZoneDefaults[zid];
      if (!r) return { zone: zid, known: false, spot_source: 'energy_charts', currency: 'EUR', fx: 1, energy_tax: 0, vat_pct: 21, grid_source: 'none', system_source: 'fixed', system_fixed: 0 };
      return { zone: zid, known: true, spot_source: r[0], currency: r[1], fx: r[2], energy_tax: r[3], vat_pct: r[4], grid_source: r[5], system_source: r[6], system_fixed: r[7] };
    }
    if (path === '/strategy') {
      return { weighting: { basis: 'area' }, physical_house_temperature_c: 20.3, house_comfort_target_c: 20.5 };
    }
    if (path === '/zones' || path === '/rooms') {
      return {
        rooms: [
          { room_id: 'room-01', name: 'Living', loop_count: 2, area_m2: 48, include_in_house_temperature: true },
          { room_id: 'room-02', name: 'Kitchen', loop_count: 1, area_m2: 14, include_in_house_temperature: true },
          { room_id: 'room-03', name: 'Bath', loop_count: 1, area_m2: 8, include_in_house_temperature: true },
          { room_id: 'room-04', name: 'Hall', loop_count: 1, area_m2: 10, include_in_house_temperature: true },
          { room_id: 'room-05', name: 'Office', loop_count: 1, area_m2: 12, include_in_house_temperature: true },
          { room_id: 'room-06', name: 'Bedroom', loop_count: 1, area_m2: 16, include_in_house_temperature: true },
          { room_id: 'room-07', name: 'Guest', loop_count: 1, area_m2: 11, include_in_house_temperature: true },
          { room_id: 'room-08', name: 'Workshop', loop_count: 1, area_m2: 22, include_in_house_temperature: true }
        ],
        zones: [
          { room_id: 'room-01', name: 'Living', node_index: 0, zone_index: 0, temperature_c: 21.3, setpoint_c: 21.5, status: 'calling', valve_pct: 45, fresh: true, is_group_secondary: false },
          { room_id: 'room-01', name: 'Living', node_index: 0, zone_index: 1, temperature_c: 21.3, setpoint_c: 21.5, status: 'calling', valve_pct: 30, fresh: true, is_group_secondary: true },
          { room_id: 'room-02', name: 'Kitchen', node_index: 0, zone_index: 2, temperature_c: 21.0, setpoint_c: 21.0, status: 'idle', valve_pct: 18, fresh: true },
          { room_id: 'room-03', name: 'Bath', node_index: 0, zone_index: 3, temperature_c: 22.4, setpoint_c: 22.5, status: 'calling', valve_pct: 28, fresh: true },
          { room_id: 'room-04', name: 'Hall', node_index: 0, zone_index: 4, temperature_c: 20.1, setpoint_c: 20.0, status: 'idle', valve_pct: 10, fresh: true },
          { room_id: 'room-05', name: 'Office', node_index: 0, zone_index: 5, temperature_c: 20.8, setpoint_c: 21.0, status: 'idle', valve_pct: 15, fresh: true },
          { room_id: 'room-06', name: 'Bedroom', node_index: 1, zone_index: 0, temperature_c: 19.4, setpoint_c: 19.5, status: 'calling', valve_pct: 35, fresh: true },
          { room_id: 'room-07', name: 'Guest', node_index: 1, zone_index: 1, temperature_c: 19.8, setpoint_c: 20.0, status: 'idle', valve_pct: 8, fresh: true },
          { room_id: 'room-08', name: 'Workshop', node_index: 1, zone_index: 2, temperature_c: 17.6, setpoint_c: 18.0, status: 'fault', valve_pct: 0, fresh: true },
          { room_id: 'room-09', name: '', node_index: 2, zone_index: 0, temperature_c: null, setpoint_c: null, status: 'unknown', valve_pct: null, fresh: false }
        ]
      };
    }
    if (path.indexOf('/comfort-chart') !== -1) {
      var hours = 24;
      var expected = [], scheduled = [], amin = [], amax = [], preload = [];
      for (var h = 0; h < hours; h++) {
        var sp = 21 + (h > 18 || h < 6 ? -1 : 0);
        expected.push(20.5 + Math.sin(h / 5) * 0.4);
        scheduled.push(sp);
        amin.push(sp - 0.5);
        amax.push(sp + 0.5);
        preload.push(h >= 18 && h <= 22);
      }
      return {
        room_id: 'room-01',
        comfort_chart: {
          hours: hours, offset_c: 0.4, expected_temp_c: expected,
          scheduled_setpoint_c: scheduled, comfort_min_c: amin, comfort_max_c: amax,
          preload_active: preload, actual_temp_c: 21.3
        }
      };
    }
    if (path === '/forecast') {
      var now = Math.floor(Date.now() / 1000);
      now -= now % 3600;
      return {
        status: 'ok',
        cache: { min_temp_c: 6.8, max_wind_ms: 11, hours: 72, decision_start_index: 14, fetch_epoch_s: now - 300, provider_timezone: 'Europe/Copenhagen' },
        hours: Array.from({ length: 72 }, function (_, h) {
          return {
            h: h, timestamp_s: now - 14 * 3600 + h * 3600,
            temp_c: 10 + Math.sin(h / 4) * 3, wind_ms: 5 + Math.sin(h / 3) * 2,
            wind_dir_deg: 250, solar_wm2: (h % 24 > 7 && h % 24 < 18) ? 200 : 0,
            precip_mm: (h >= 20 && h <= 28) ? 1.2 : 0, cloud_pct: 40
          };
        }),
        decisions: [
          { room_id: 'room-01', name: 'Living', offset_c: 0.4, active: true, preload_start_h: 6, preload_end_h: 17 },
          { room_id: 'room-07', name: 'Bedroom', node_index: 1, zone_index: 1, offset_c: 1.2, active: true,
            charge: { episode: true, now: true, insufficient: true, store_c: 1.5, deficit_kwh: 6.0 } }
        ],
        plan_vs_reality: Array.from({ length: 12 }, function (_, i) {
          return { h: i, planned_kw: 2.4 + i * 0.1, actual_kw: 2.1 + i * 0.08 };
        })
      };
    }
    if (path === '/heat-source') return mockHeat;
    if (path === '/plan') {
      var hk = Array.from({ length: 24 }, function (_, i) { return (i >= 3 && i <= 5) ? 4.8 : (i === 14 ? 2.1 : 0); });
      return {
        available: true, clock: true, start_hour: new Date().getHours(), hours: 24,
        house: { target_c: 22.0, temp_c: 21.8 },
        odin: { available: true, control: true, heat_kw: hk, energy_kwh: hk.map(function (v, i) { return i === 14 ? 1.5 : (i >= 18 && i <= 20 ? 0.8 : v / 4); }),
          mode: hk.map(function (v, i) { return i === 14 ? 1 : (i >= 18 && i <= 20 ? 6 : (v > 0 ? 2 : 0)); }),
          lift_applied: true, lift_c: Array.from({ length: 24 }, function (_, i) { return (i >= 2 && i <= 6) ? 0.5 : 0; }) },
        rooms: [
          { room_id: 'room-07', name: 'Bedroom', preload: null, charge: { from: 1, to: 8, store_c: 1.5, insufficient: true } },
          { room_id: 'room-01', name: 'Living', preload: { from: 0, to: 4, offset_c: 0.4 }, charge: null },
          { room_id: 'room-03', name: 'Office', preload: null, charge: { from: 2, to: 7, store_c: 0.8, insufficient: false } }
        ]
      };
    }
    if (path === '/heat-source/control') {
      return {
        demand: { route: 'odin_schedule', held_c: 0.6, target_uplift_c: 0 },
        odin: { enabled: true, max_lift_c: 1.5, status: 'lifted', reason: 'slab_charge', last_action: 'write_lift',
          user_schedule_known: true, yielded: false,
          wanted: { active: true, start_hour: 15, hours: 3, lift_c: 0.5, energy_kwh: 6.0 },
          applied: { active: true, start_hour: 15, hours: 3, lift_c: 0.5 },
          thermal_mass_kwh_per_k: 11.66, heat_loss_kw_per_k: 0.15 },
        link: { status: 'ok', alarm: false, odin_reachable: true, forwarder_known: true, forwarder_active: true, takeover: true,
          telemetry_age_s: 40, odin_room_c: 22.4 },
        generic: { applies: false, curve_gain: 2, curve_max_offset_c: 5, status: 'idle' }
      };
    }
    if (path === '/settings') {
      return {
        coordinator: { name: 'Lune Touch', site_label: 'House coordinator', install_id: 'demo', install_mode: 'commissioning' },
        display: { idle_timeout_s: 300 },
        weather: { max_boost_c: 1.5 },
        forecast: { latitude: 55.6761, longitude: 12.5683, max_boost_c: 1.5 }
      };
    }
    if (path === '/diagnostics') {
      return {
        heap: 'watching',
        polling: { last_error: '' },
        ota: { state: 'valid' },
        network: {
          ip: '192.168.1.186', mac: 'D8:3B:DA:AA:BB:CC', ssid: 'demo',
          version: 'v0.1.0-119', esphome: '2026.9.1', uptime_s: 540000
        }
      };
    }
    if (path === '/commands') {
      return { commands: [
        { source: 'forecast', reason: 'fetched', result: 'accepted', created_at_epoch_s: Math.floor(Date.now() / 1000) - 600 },
        { source: 'manual', reason: 'setpoint', result: 'accepted', created_at_epoch_s: Math.floor(Date.now() / 1000) - 1200, room_id: 'room-01' }
      ] };
    }
    return {};
  }

  function mapStatus(s) {
    if (s === 'calling' || s === 'call' || s === 'heat' || s === 'preheat') return 'calling';
    if (s === 'fault' || s === 'motor_fault') return 'fault';
    if (s === 'off' || s === 'unused') return 'off';
    return 'idle';
  }

  function statusRank(s) {
    s = mapStatus(s);
    if (s === 'fault') return 3;
    if (s === 'calling') return 2;
    if (s === 'off') return 0;
    return 1;
  }

  function compassDir(deg) {
    if (deg == null || deg !== deg) return '';
    var labels = ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE', 'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW'];
    var i = Math.round((((Number(deg) % 360) + 360) % 360) / 22.5) % 16;
    // Localise the compass letters (da: Ø/V) from the catalogue's _walls.
    var w = i18n._walls || {};
    var map = { N: w.n || 'N', E: w.e || 'E', S: w.s || 'S', W: w.w || 'W' };
    return labels[i].replace(/[NESW]/g, function (c) { return map[c]; });
  }

  function seriesPoints(values, width, height, yMin, yMax) {
    if (!values || !values.length) return '';
    var n = values.length;
    var span = Math.max(yMax - yMin, 0.1);
    return values.map(function (v, i) {
      var x = n === 1 ? 0 : (i * width / (n - 1));
      var y = height - ((Number(v) - yMin) / span) * height;
      if (!isFinite(y)) y = height / 2;
      return x.toFixed(1) + ',' + y.toFixed(1);
    }).join(' ');
  }

  function stepPoints(values, width, height, yMin, yMax) {
    if (!values || !values.length) return '';
    var n = values.length;
    var span = Math.max(yMax - yMin, 0.1);
    var pts = [];
    values.forEach(function (v, i) {
      var x = n === 1 ? 0 : (i * width / (n - 1));
      var y = height - ((Number(v) - yMin) / span) * height;
      if (!isFinite(y)) y = height / 2;
      if (i && Number(v) !== Number(values[i - 1])) {
        var prevY = height - ((Number(values[i - 1]) - yMin) / span) * height;
        pts.push(x.toFixed(1) + ',' + prevY.toFixed(1));
      }
      pts.push(x.toFixed(1) + ',' + y.toFixed(1));
    });
    return pts.join(' ');
  }

  function axisRange(seriesList, minSpan) {
    var lo = Infinity, hi = -Infinity;
    seriesList.forEach(function (arr) {
      (arr || []).forEach(function (v) {
        var n = Number(v);
        if (!isFinite(n)) return;
        if (n < lo) lo = n;
        if (n > hi) hi = n;
      });
    });
    if (!isFinite(lo) || !isFinite(hi)) return { lo: 18, hi: 22 };
    var mid = (lo + hi) / 2;
    var span = Math.max(hi - lo + 0.6, minSpan || 3);
    return { lo: mid - span / 2, hi: mid + span / 2 };
  }

  function updateSvgSeries(svg, tempSeries, targetSeries) {
    if (!svg) return;
    var vb = (svg.getAttribute('viewBox') || '0 0 240 40').split(/\s+/).map(Number);
    var W = vb[2] || 240, H = vb[3] || 40;
    var range = axisRange([tempSeries, targetSeries], 3);
    var tp = seriesPoints(tempSeries, W, H, range.lo, range.hi);
    var gp = stepPoints(targetSeries, W, H, range.lo, range.hi);
    var polyT = svg.querySelector('polyline.t, polyline.f');
    var polyG = svg.querySelector('polyline.g, polyline.r');
    var area = svg.querySelector('polygon.a, polygon.dt');
    if (polyT) polyT.setAttribute('points', tp);
    if (polyG) polyG.setAttribute('points', gp);
    if (area && tp && gp) {
      var gPts = gp.split(' ').reverse().join(' ');
      area.setAttribute('points', tp + ' ' + gPts);
    }
  }

  function liveSeries(room) {
    var temp = room && room.temperature_c != null && isFinite(Number(room.temperature_c))
      ? Number(room.temperature_c) : null;
    var target = room && room.setpoint_c != null && isFinite(Number(room.setpoint_c))
      ? Number(room.setpoint_c) : temp;
    if (temp == null && target == null) return { temp: [], target: [] };
    // No past ring-buffer on device yet: show a flat live sample so demo curves disappear.
    var tSeries = [];
    var gSeries = [];
    for (var i = 0; i < 48; i++) {
      tSeries.push(temp != null ? temp : target);
      gSeries.push(target != null ? target : temp);
    }
    return { temp: tSeries, target: gSeries };
  }

  function mergeRooms(payload) {
    var meta = (payload && payload.rooms) || [];
    var zones = (payload && payload.zones) || [];
    var by = {};

    zones.forEach(function (z) {
      if (!z || !z.room_id || z.is_group_secondary || z.unassigned) return;
      var cur = by[z.room_id];
      var sp = z.setpoint_c;
      if (sp == null && z.comfort && z.comfort.effective_setpoint_c != null)
        sp = z.comfort.effective_setpoint_c;
      var circuit = {
        node_index: z.node_index,
        zone_index: z.zone_index,
        valve_pct: z.valve_pct,
        name: z.name
      };
      if (!cur) {
        by[z.room_id] = {
          room_id: z.room_id,
          name: z.name,
          loop_count: 1,
          temperature_c: z.temperature_c,
          setpoint_c: sp,
          status: z.status,
          valve_pct: z.valve_pct,
          area_m2: z.room && z.room.total_area_m2,
          include_in_house_temperature: z.room ? z.room.include_in_house_temperature : true,
          circuits: [circuit]
        };
        return;
      }
      cur.loop_count += 1;
      cur.circuits.push(circuit);
      if (z.valve_pct != null && (cur.valve_pct == null || Number(z.valve_pct) > Number(cur.valve_pct)))
        cur.valve_pct = z.valve_pct;
      if (statusRank(z.status) > statusRank(cur.status)) cur.status = z.status;
      if (cur.temperature_c == null && z.temperature_c != null) cur.temperature_c = z.temperature_c;
      if (cur.setpoint_c == null && sp != null) cur.setpoint_c = sp;
      if (!cur.name && z.name) cur.name = z.name;
    });

    meta.forEach(function (r) {
      if (!r || !r.room_id) return;
      if (!by[r.room_id]) {
        by[r.room_id] = {
          room_id: r.room_id,
          name: r.name,
          loop_count: r.loop_count || 0,
          area_m2: r.area_m2,
          include_in_house_temperature: r.include_in_house_temperature,
          circuits: []
        };
        return;
      }
      if (r.name) by[r.room_id].name = r.name;
      if (r.area_m2 != null) by[r.room_id].area_m2 = r.area_m2;
      if (r.loop_count) by[r.room_id].loop_count = r.loop_count;
      if (r.include_in_house_temperature != null)
        by[r.room_id].include_in_house_temperature = r.include_in_house_temperature;
    });

    var out = [];
    var seen = {};
    meta.forEach(function (r) {
      if (r && r.room_id && by[r.room_id] && !seen[r.room_id]) {
        out.push(by[r.room_id]);
        seen[r.room_id] = true;
      }
    });
    Object.keys(by).forEach(function (k) {
      if (!seen[k]) out.push(by[k]);
    });
    return out;
  }


  // Live zone data: fresh, a real temperature and a known status. Without it
  // the zone shows "—" and unlit (seg-off) level bars — never a guessed level.
  function zoneHasData(z) {
    return !!z && z.fresh !== false && z.status !== 'unknown' &&
      z.temperature_c != null && isFinite(Number(z.temperature_c)) &&
      z.valve_pct != null && isFinite(Number(z.valve_pct));
  }

  // A poll must not overwrite a target the user is changing (autosave pending).
  function editingTarget(input) {
    return document.activeElement === input || !!(input.form && input.form.dataset.autoPending);
  }

  // House climate badge: the house against its target, not "some zone calls"
  // (that count is in the subtitle). Orange only while the house is below target.
  function paintHouseBadge(faults) {
    var ov = state.overview || {};
    var temp = ov.house_temp_c != null ? Number(ov.house_temp_c) : NaN;
    var target = ov.house_target_c != null ? Number(ov.house_target_c) : NaN;
    if (faults) { paintBadge('house.badge', 'fault'); return; }
    if (!isFinite(temp) || !isFinite(target)) { paintBadge('house.badge', 'idle', '—'); return; }
    var d = temp - target;
    if (d < -0.2) paintBadge('house.badge', 'calling', t('house.below'));
    else paintBadge('house.badge', 'idle', t(d > 0.2 ? 'house.above' : 'house.at'));
  }

  // Status badge: text and colour follow the state (orange only while calling).
  function paintBadge(bind, st, text) {
    qsa('[data-bind="' + bind + '"]').forEach(function (b) {
      b.textContent = text != null ? text : t('state.' + st);
      b.className = st === 'calling' ? 'badge hot' : (st === 'fault' ? 'badge bad' : 'badge');
    });
  }

  function deg(x) {
    var s = num(x);
    return s === '—' ? s : s + '°';
  }

  function zoneLevel(valve, st) {
    st = mapStatus(st);
    if (st === 'fault' || st === 'off') return 0;
    var v = Number(valve) || 0;
    if (v <= 0) return 0;   // closed valve: unlit, orange means heat
    return Math.max(1, Math.min(5, Math.ceil(v / 20)));
  }

  function mzOf(z) {
    // Firmware/API indexes are 0-based; UI tiles are Z1–Z6 / M1–M4.
    var m = (z.node_index != null ? Number(z.node_index) : 0) + 1;
    var zi = (z.zone_index != null ? Number(z.zone_index) : 0) + 1;
    return { m: m, z: zi };
  }

  function syncScope() {
    var r = qs('input[name="scope"]:checked');
    if (!r) return;
    var title = r.getAttribute('data-title') || '';
    var sub = r.getAttribute('data-sub') || '';
    qsa('[data-bind="scope.title"]').forEach(function (el) { el.textContent = title; });
    qsa('[data-bind="scope.sub"]').forEach(function (el) { el.textContent = sub; });
    state.selected.m = r.getAttribute('data-m') ? Number(r.getAttribute('data-m')) : null;
    state.selected.z = r.getAttribute('data-z') ? Number(r.getAttribute('data-z')) : null;
    if (r.getAttribute('data-kind') === 'zone') fillZoneViews(state.selected.m, state.selected.z);
    if (r.getAttribute('data-kind') === 'manifold') fillManifoldViews(state.selected.m);
  }

  function syncHsType(type) {
    type = type === 'generic_http' ? 'http' : (type || 'asgard');
    var radio = qs('#hs-' + type);
    if (radio) radio.checked = true;
    qsa('[data-hs-type]').forEach(function (el) { el.setAttribute('data-hs-type', type); });
    syncHsTypedFields();
  }

  function syncHsTypedFields() {
    var form = qs('form.panel[data-save="heat-source"]');
    if (!form) return;
    var checked = form.querySelector('input[name="hs_type"]:checked');
    var type = checked ? checked.value : 'asgard';
    form.querySelectorAll('fieldset.typed-fields, fieldset.hs-fields').forEach(function (fs) {
      var on = fs.getAttribute('data-type') === type;
      // Disable inactive fields so HTML5 validation and FormData ignore them.
      fs.querySelectorAll('input, select, textarea').forEach(function (el) {
        el.disabled = !on;
      });
    });
  }

  function currentZone() {
    var m = state.selected.m, z = state.selected.z;
    if (m == null || z == null) return null;
    return (state.zones || []).find(function (zz) {
      var mz = mzOf(zz);
      return mz.m === m && mz.z === z && !zz.is_group_secondary;
    }) || (state.zones || []).find(function (zz) {
      var mz = mzOf(zz);
      return mz.m === m && mz.z === z;
    }) || null;
  }

  function fillManifoldViews(m) {
    if (m == null) return;
    var node = state.nodes[m - 1];
    var nameEl = qs('#manifold_name');
    // Firmware names a new board after its node id; show that as "Unnamed" (same
    // as the strip) and leave the field empty so the user can name it here.
    var unnamed = !node || nodeUnnamed(node);
    var mName = nodeLabel(node) || t('common.unnamed');
    if (nameEl && document.activeElement !== nameEl && node) {
      nameEl.value = unnamed ? '' : node.name;
      nameEl.placeholder = (node.device_name || '').trim() || t('common.unnamed');
    }
    var zones = (state.zones || []).filter(function (z) { return mzOf(z).m === m; });
    var primaries = zones.filter(function (z) { return !z.is_group_secondary; });
    var calling = primaries.filter(function (z) { return mapStatus(z.status) === 'calling'; }).length;
    var faults = primaries.filter(function (z) { return mapStatus(z.status) === 'fault'; }).length;
    var st = faults ? 'fault' : (calling ? 'calling' : 'idle');
    var title = t('scope.title.manifold', { id: 'M' + m, name: mName });
    var sub = t('dash.manifold.sub', { zones: zones.length, calling: calling, state: t('state.' + st) });
    var rt = (node && node.runtime) || {};
    setBind('manifold.flow', rt.flow_c != null && isFinite(Number(rt.flow_c)) ? num(rt.flow_c) + ' <small>°C</small>' : '—');
    setBind('manifold.return', rt.return_c != null && isFinite(Number(rt.return_c)) ? num(rt.return_c) + ' <small>°C</small>' : '—');
    var radio = qs('#s-m' + m);
    if (radio) {
      radio.setAttribute('data-title', title);
      radio.setAttribute('data-sub', sub);
    }
    if (qs('input[name="scope"]:checked') === radio) {
      qsa('[data-bind="scope.title"]').forEach(function (el) { el.textContent = title; });
      qsa('[data-bind="scope.sub"]').forEach(function (el) { el.textContent = sub; });
    }
  }

  function fillZoneViews(m, z) {
    var zone = currentZone();
    if (!zone) return;
    var st = mapStatus(zone.status);
    var sp = zone.setpoint_c;
    if (sp == null && zone.comfort && zone.comfort.effective_setpoint_c != null) sp = zone.comfort.effective_setpoint_c;
    var title = t('scope.title.zone', { id: 'M' + m + ' Z' + z, name: zone.name || ('Z' + z) });
    var sub = t('dash.zone.sub', { state: t('state.' + st), target: num(sp) });
    var radio = qs('#s-m' + m + 'z' + z);
    if (radio) {
      radio.setAttribute('data-title', title);
      radio.setAttribute('data-sub', sub);
    }
    if (qs('input[name="scope"]:checked') === radio) {
      qsa('[data-bind="scope.title"]').forEach(function (el) { el.textContent = title; });
      qsa('[data-bind="scope.sub"]').forEach(function (el) { el.textContent = sub; });
    }
    paintBadge('zone.badge', st);
    qsa('[data-bind="zone.temp"]').forEach(function (n) {
      if (n.classList.contains('now')) n.innerHTML = num(zone.temperature_c) + '<small>°C</small>';
      else n.textContent = num(zone.temperature_c);
    });
    var tgt = qs('#zone_target');
    if (tgt && !editingTarget(tgt)) tgt.value = Number(sp != null ? sp : 21).toFixed(1);
    var nameInput = qs('#zone_name');
    if (nameInput && document.activeElement !== nameInput) nameInput.value = zone.name || '';
    var area = qs('#zone_area');
    if (area && document.activeElement !== area && zone.room && zone.room.total_area_m2 != null)
      area.value = Number(zone.room.total_area_m2).toFixed(0);
    var tbody = qs('[data-bind-circuits="zone"]');
    if (tbody) {
      tbody.innerHTML = '<tr><td>Z' + z + '</td><td class="num">' + num(zone.valve_pct, 0) + ' <small>%</small></td></tr>';
    }
  }

  function ensureComfortRow(m, z, zone) {
    var list = qs('[data-bind-comfort-list]');
    if (!list) return null;
    var key = 'm' + m + 'z' + z;
    var row = qs('[data-bind-comfort="' + key + '"]');
    if (row) return row;
    row = document.createElement('label');
    row.setAttribute('for', 's-m' + m + 'z' + z);
    row.setAttribute('data-bind-comfort', key);
    row.innerHTML =
      '<span class="id">M' + m + '·Z' + z + '</span>' +
      '<span class="name" data-bind="' + key + '.name"></span>' +
      '<span class="val"></span>' +
      '<svg class="spark" viewBox="0 0 240 40" preserveAspectRatio="none" aria-hidden="true" data-bind-spark="' + key + '">' +
      '<polygon class="a" points=""/><polyline class="g" points=""/><polyline class="t" points=""/></svg>';
    list.appendChild(row);
    return row;
  }

  function roomSlot(index, room) {
    var tile = qs('label.tile[data-room="' + index + '"]');
    var dash = qs('#v-dash-r' + index);
    var conf = qs('#v-conf-r' + index);
    if (!room) {
      if (tile) tile.hidden = true;
      if (dash) dash.setAttribute('data-empty', '1');
      if (conf) conf.setAttribute('data-empty', '1');
      var orphan = qs('[data-bind-comfort="r' + index + '"]');
      if (orphan) orphan.hidden = true;
      return;
    }
    var st = mapStatus(room.status);
    if (tile) {
      tile.hidden = false;
      tile.setAttribute('data-state', st);
      var lvl = st === 'fault' || st === 'off' ? 0 : Math.max(1, Math.ceil((room.valve_pct || 0) / 20));
      tile.setAttribute('data-level', String(lvl));
    }
    if (dash) dash.removeAttribute('data-empty');
    if (conf) conf.removeAttribute('data-empty');

    var name = room.name || ('R' + index);
    setText('r' + index + '.name', name);
    qsa('[data-bind="r' + index + '.temp"]').forEach(function (n) {
      if (n.classList.contains('now')) n.innerHTML = num(room.temperature_c) + '<small>°C</small>';
      else if (n.classList.contains('tile-val')) n.textContent = st === 'fault' ? t('tile.fault') : deg(room.temperature_c);
      else n.textContent = num(room.temperature_c);
    });
    setText('r' + index + '.sub', t('rdash.sub', { loops: room.loop_count || 1, state: t('state.' + st) }));
    setText('r' + index + '.badge', t('state.' + st));

    var target = qs('#r' + index + '_target');
    if (target && document.activeElement !== target) target.value = Number(room.setpoint_c != null ? room.setpoint_c : 20).toFixed(1);

    var nameInput = qs('#r' + index + '_name');
    if (nameInput && document.activeElement !== nameInput) nameInput.value = name;

    var series = liveSeries(room);
    updateSvgSeries(qs('[data-bind-spark="r' + index + '"]'), series.temp, series.target);
    updateSvgSeries(qs('[data-bind-trend="r' + index + '"]'), series.temp, series.target);

    var tbody = qs('[data-bind-circuits="r' + index + '"]');
    if (tbody && room.circuits && room.circuits.length) {
      tbody.innerHTML = room.circuits.map(function (c, j) {
        var label = c.name || ('Z' + ((c.zone_index != null ? c.zone_index : j) + 1));
        return '<tr><td>' + label + '</td><td class="num">' + num(c.valve_pct, 0) + ' <small>%</small></td></tr>';
      }).join('');
    }
  }

  function chargeFor(nodeIndex, zoneIndex) {
    var ds = (state.forecast && state.forecast.decisions) || [];
    for (var i = 0; i < ds.length; i++) {
      var d = ds[i];
      if (d && Number(d.node_index) === nodeIndex && Number(d.zone_index) === zoneIndex &&
          d.charge && d.charge.now) return d.charge;
    }
    return null;
  }

  // One row per V6 board: System tile (name, flow/return) + its zone tiles —
  // the same strip as on the V6 itself. Tiles link to the V6 (#s-zN deep link).
  function renderBoards(byM) {
    var host = qs('[data-bind-boards]');
    if (!host) return;
    state.boardsByM = byM;
    var ms = Object.keys(byM).map(Number).sort(function (a, b) { return a - b; });
    host.innerHTML = ms.map(function (m) {
      var node = state.nodes[m - 1];
      var name = nodeLabel(node) || (t('common.unnamed') + ' M' + m);
      var addr = (node && (node.hostname || node.ip)) || '';
      var base = addr ? (addr.indexOf('http') === 0 ? addr : 'http://' + addr + '/') : '';
      var tag = base ? 'a' : 'div';
      var href = function (hash) { return base ? ' href="' + esc(base + hash) + '"' : ''; };
      var rt = (node && node.runtime) || {};
      var temp = function (cls, lab, v) {
        var has = v != null && isFinite(Number(v));
        return '<span class="temp ' + cls + '"' + (has ? '' : ' data-empty') + '><span class="temp-lab">' +
          esc(t(lab)) + '</span><span class="tile-val">' + esc(deg(v)) + '</span></span>';
      };
      // Lease: every trusted V6 should hold Touch's lease; otherwise it runs locally.
      var lease = node && node.lease ? String(node.lease) : '';
      var leaseAttr = lease && lease !== 'granted' ? ' data-lease="' + esc(lease) + '"' : '';
      var sysTitle = addr + (leaseAttr ? ' · ' + t('strip.lease.' + lease) : '');
      var sys = '<' + tag + ' class="tile tile-sys"' + href('#s-sys') + leaseAttr + ' title="' + esc(sysTitle) + '">' +
        '<span class="tile-id">' + esc(name) + '</span><span class="temps">' +
        temp('flow', 'm.supplyShort', rt.flow_c) + temp('ret', 'm.returnShort', rt.return_c) + '</span></' + tag + '>';
      var zones = byM[m].slice().sort(function (a, b) { return mzOf(a).z - mzOf(b).z; });
      var tiles = zones.map(function (z) {
        var zi = mzOf(z).z;
        var st = mapStatus(z.status);
        var members = Array.isArray(z.group_members) ? z.group_members.map(Number).filter(isFinite) : [];
        var role = z.is_group_secondary ? 'member' : (members.length > 1 ? 'primary' : '');
        var id = 'Z' + zi;
        if (role === 'primary') {
          var ids = members.slice().sort(function (a, b) { return a - b; });
          var run = ids.every(function (v, i) { return i === 0 || v === ids[i - 1] + 1; });
          id = run ? 'Z' + ids[0] + '–' + ids[ids.length - 1] : 'Z' + ids.join('+');
        }
        var val = st === 'fault' ? t('tile.fault') : (st === 'off' ? t('tile.off') : deg(z.temperature_c));
        // Slab charge planned by Touch (forecast decision for this loop).
        var ch = chargeFor(m - 1, zi - 1);
        var chargeAttr = ch ? ' data-charge="' + (ch.insufficient ? 'insufficient' : 'now') + '" title="' +
          esc(t(ch.insufficient ? 'strip.charge.insufficient' : 'strip.charge.now', { c: num(ch.store_c) })) + '"' : '';
        return '<' + tag + ' class="tile"' + href('#s-z' + zi) + chargeAttr + ' data-state="' + st + '" data-level="' +
          (zoneHasData(z) ? zoneLevel(z.valve_pct, st) : 0) + '"' + (role ? ' data-group="' + role + '"' : '') + '>' +
          '<span class="lvl" aria-hidden="true"><i></i><i></i><i></i><i></i><i></i></span>' +
          '<span class="tile-id">' + esc(id) + '</span><span class="tile-name">' + esc(z.name || id) + '</span>' +
          '<span class="tile-val">' + esc(val) + '</span></' + tag + '>';
      }).join('');
      return '<nav class="strip" aria-label="' + esc(t('strip.sub', { name: name })) + '">' + sys + tiles + '</nav>';
    }).join('');
  }

  // Rooms (Konfiguration › Hus): the room settings Touch owns. Rendered from
  // /zones; left alone while the form has unsaved edits.
  function roomList() {
    var seen = {};
    return (state.zones || []).filter(function (z) {
      if (!z || !z.room_id || z.is_group_secondary || z.unassigned || seen[z.room_id]) return false;
      seen[z.room_id] = 1;
      return true;
    });
  }

  function renderRoomRows() {
    var body = qs('[data-bind-room-rows]');
    if (!body) return;
    var form = body.closest('form');
    if (form && (form.dataset.dirty != null || form.dataset.state === 'saving')) return;
    var rooms = roomList();
    if (!rooms.length) { body.innerHTML = '<tr><td colspan="5" class="muted">—</td></tr>'; return; }
    body.innerHTML = rooms.map(function (z) {
      var r = z.room || {};
      var f = z.forecast || {};
      var key = esc(z.room_id);
      var name = esc(z.name || z.room_id);
      var numIn = function (field, v, min, max, step, aria) {
        return '<input class="input w-xs" type="number" inputmode="decimal" name="room:' + key + ':' + field + '" value="' +
          esc(Number(v).toFixed(2)) + '" min="' + min + '" max="' + max + '" step="' + step + '" aria-label="' + esc(t(aria, { name: z.name || z.room_id })) + '">';
      };
      return '<tr><td>' + name + '</td>' +
        '<td><label class="switch"><input type="checkbox" role="switch" name="room:' + key + ':include"' +
        (r.include_in_house_temperature !== false ? ' checked' : '') + ' aria-label="' + esc(t('rooms.includeAria', { name: z.name || z.room_id })) + '"></label></td>' +
        '<td class="num">' + numIn('weight', r.physical_weight != null ? r.physical_weight : 1, 0, 5, 0.05, 'rooms.weightAria') + '</td>' +
        '<td class="num">' + numIn('wind', f.wind_exposure != null ? f.wind_exposure : 0.5, 0, 1, 0.05, 'rooms.windAria') + '</td>' +
        '<td class="num">' + numIn('solar', f.solar_gain != null ? f.solar_gain : 0.3, 0, 1, 0.05, 'rooms.solarAria') + '</td></tr>';
    }).join('');
    if (form && form.luneResnap) form.luneResnap();
  }

  // Full atomic room update (POST /zones/{room}/room) for each changed room.
  async function saveRooms(data) {
    var rooms = roomList();
    for (var i = 0; i < rooms.length; i++) {
      var z = rooms[i];
      var r = z.room || {};
      var f = z.forecast || {};
      var c = z.comfort || {};
      var sc = z.schedule || {};
      var k = 'room:' + z.room_id + ':';
      var include = !!data[k + 'include'];
      var weight = Number(data[k + 'weight']);
      var wind = Number(data[k + 'wind']);
      var solar = Number(data[k + 'solar']);
      if (include === (r.include_in_house_temperature !== false) &&
          weight === Number(r.physical_weight) && wind === Number(f.wind_exposure) && solar === Number(f.solar_gain)) continue;
      await post('/zones/' + encodeURIComponent(z.room_id) + '/room', {
        expected_revision: r.revision || 0,
        total_area_m2: r.total_area_m2 || 0,
        physical_weight: weight,
        include_in_house_temperature: include ? 1 : 0,
        comfort_setpoint_c: c.setpoint_c != null ? c.setpoint_c : (z.setpoint_c != null ? z.setpoint_c : 21),
        comfort_bias_c: c.bias_c || 0,
        priority: c.priority || 0,
        schedule_enabled: sc.enabled ? 1 : 0,
        schedule_day_mask: sc.day_mask || 0,
        schedule_start_min: sc.start_min || 0,
        schedule_end_min: sc.end_min || 0,
        schedule_setpoint_c: sc.setpoint_c != null ? sc.setpoint_c : 21,
        exterior_walls: f.exterior_walls || 0,
        wind_exposure: wind,
        solar_gain: solar,
        thermal_lead_h: f.thermal_lead_h || 4,
        max_offset_c: f.max_offset_c != null ? f.max_offset_c : 1.5
      });
    }
  }

  function applyHierarchy(payload, nodes) {
    state.zones = (payload && payload.zones) || [];
    state.rooms = mergeRooms(payload || {});
    state.nodes = nodes || state.nodes || [];

    // Reset all zone tiles hidden
    qsa('label.tile[data-z]').forEach(function (tile) {
      tile.hidden = true;
      tile.removeAttribute('data-group');
    });
    qsa('label.tile-manifold').forEach(function (tile) { tile.hidden = true; });

    // All physical zones (incl. unassigned / group secondary) belong on the strip.
    var byM = {};
    state.zones.forEach(function (z) {
      if (!z) return;
      var mz = mzOf(z);
      if (!(mz.m >= 1) || !(mz.z >= 1)) return;
      if (!byM[mz.m]) byM[mz.m] = [];
      byM[mz.m].push(z);
    });

    var flowSum = 0, flowN = 0, retSum = 0, retN = 0;

    Object.keys(byM).forEach(function (mk) {
      var m = Number(mk);
      var list = byM[m];
      var mTile = qs('label.tile-manifold[data-m="' + m + '"]');
      if (mTile) mTile.hidden = false;
      var node = state.nodes[m - 1];
      var label = nodeLabel(node);
      var unnamed = !label;
      var mName = label || t('common.unnamed');
      setText('m' + m + '.name', mName);
      if (mTile) {
        var addr = (node && (node.ip || node.hostname)) || '';
        var live = t('tile.manifold.live', { id: 'M' + m, name: unnamed ? (t('common.unnamed') + ' M' + m) : mName, addr: addr });
        mTile.setAttribute('aria-label', live);
        mTile.setAttribute('title', addr || live);
        var nameEl = mTile.querySelector('.tile-name');
        if (nameEl) nameEl.classList.toggle('muted', unnamed);
      }

      var rt = (node && node.runtime) || {};
      var flow = rt.flow_c != null ? Number(rt.flow_c) : NaN;
      var ret = rt.return_c != null ? Number(rt.return_c) : NaN;
      var tempsTxt = '—';
      if (isFinite(flow) && isFinite(ret)) {
        tempsTxt = num(flow) + '° / ' + num(ret) + '°';
        flowSum += flow; flowN++;
        retSum += ret; retN++;
      } else if (isFinite(flow)) {
        tempsTxt = num(flow) + '° / —';
        flowSum += flow; flowN++;
      } else if (isFinite(ret)) {
        tempsTxt = '— / ' + num(ret) + '°';
        retSum += ret; retN++;
      }
      setText('m' + m + '.temps', tempsTxt);

      var calling = 0, faults = 0;
      var mini = qs('[data-bind-mini="m' + m + '"]') || (mTile && mTile.querySelector('.mini'));
      if (mini) mini.innerHTML = '';
      list.forEach(function (z) {
        var mz = mzOf(z);
        var st = mapStatus(z.status);
        if (!z.is_group_secondary) {
          if (st === 'calling') calling++;
          if (st === 'fault') faults++;
        }
        var tile = qs('label.tile[data-m="' + mz.m + '"][data-z="' + mz.z + '"]');
        if (tile) {
          tile.hidden = false;
          tile.setAttribute('data-state', st);
          tile.setAttribute('data-level', String(zoneHasData(z) ? zoneLevel(z.valve_pct, st) : 0));
          if (z.is_group_secondary) tile.setAttribute('data-group', 'member');
          else if (z.group_members && z.group_members.length > 1) tile.setAttribute('data-group', 'primary');
          else tile.removeAttribute('data-group');
        }
        setText('m' + mz.m + 'z' + mz.z + '.name', z.name || ('Z' + mz.z));
        qsa('[data-bind="m' + mz.m + 'z' + mz.z + '.temp"]').forEach(function (n) {
          n.textContent = st === 'fault' ? t('tile.fault') : (st === 'off' ? t('tile.off') : deg(z.temperature_c));
        });
        if (mini) {
          var live = zoneHasData(z) || st === 'fault';
          var i = document.createElement('i');
          i.setAttribute('data-level', String(live ? zoneLevel(z.valve_pct, st) : 0));
          i.setAttribute('data-state', live ? st : 'off');
          if (z.is_group_secondary) i.setAttribute('data-group', 'member');
          mini.appendChild(i);
        }
        if (!z.is_group_secondary) {
          var row = ensureComfortRow(mz.m, mz.z, z);
          if (row) {
            row.hidden = false;
            row.setAttribute('data-state', st);
            var nameEl = qs('.name', row);
            if (nameEl) nameEl.textContent = z.name || ('Z' + mz.z);
            var val = qs('.val', row);
            var sp = z.setpoint_c;
            if (sp == null && z.comfort && z.comfort.effective_setpoint_c != null) sp = z.comfort.effective_setpoint_c;
            if (val) {
              if (st === 'fault') val.innerHTML = '<b class="bad">' + t('state.fault') + '</b>';
              else val.innerHTML = '<b>' + deg(z.temperature_c) + '</b> / ' + deg(sp);
            }
          }
        }
      });
      var mst = faults ? 'fault' : (calling ? 'calling' : 'idle');
      if (mTile) {
        if (mst === 'fault' || mst === 'calling') mTile.setAttribute('data-state', mst);
        else mTile.removeAttribute('data-state');
      }
      fillManifoldViews(m);
    });

    paintHouseTemp('house.flow', flowN ? flowSum / flowN : null);
    paintHouseTemp('house.return', retN ? retSum / retN : null);

    var manifolds = Object.keys(byM).length;
    var callingAll = state.zones.filter(function (z) { return !z.is_group_secondary && mapStatus(z.status) === 'calling'; }).length;
    var faultsAll = state.zones.filter(function (z) { return !z.is_group_secondary && mapStatus(z.status) === 'fault'; }).length;
    var houseSub = t(callingAll === 1 ? 'dash.house.sub.one' : 'dash.house.sub', { manifolds: manifolds, calling: callingAll, faults: faultsAll });
    paintHouseBadge(faultsAll);
    setText('scope.sub', houseSub);
    var houseRadio = qs('#s-house');
    if (houseRadio) houseRadio.setAttribute('data-sub', houseSub);
    renderBoards(byM);
    renderDist();
    renderRoomRows();
    syncScope();
  }

  function applyOverview(ov) {
    if (!ov) return;
    state.overview = ov;
    paintHouseBadge((state.zones || []).filter(function (z) { return z && !z.is_group_secondary && mapStatus(z.status) === 'fault'; }).length);
    var sum = ov.summary || {};
    var ht = ov.house_temp_c != null ? ov.house_temp_c
      : (ov.temperature_c != null ? ov.temperature_c
        : (ov.physical_house_temperature_c != null ? ov.physical_house_temperature_c : null));
    if (ht != null && isFinite(Number(ht))) {
      qsa('[data-bind="house.temp"]').forEach(function (n) {
        if (n.classList.contains('now') || n.classList.contains('big')) {
          if (n.classList.contains('now')) n.innerHTML = num(ht) + '<small>°C</small>';
          else n.textContent = num(ht) + '°';
        } else n.textContent = num(ht);
      });
    }
    var target = ov.house_target_c != null ? ov.house_target_c
      : (ov.house_comfort_target_c != null ? ov.house_comfort_target_c
        : (ov.house_target && ov.house_target.value_c != null ? ov.house_target.value_c : null));
    var tgt = qs('#house_target');
    if (tgt && !editingTarget(tgt) && target != null && isFinite(Number(target))) {
      tgt.value = Number(target).toFixed(1);
    }
    var contrib = ov.contributing_manifolds != null ? ov.contributing_manifolds
      : (sum.nodes != null ? sum.nodes : null);
    var expected = ov.expected_manifolds != null ? ov.expected_manifolds
      : (sum.nodes != null ? sum.nodes : null);
    if (contrib != null || expected != null) {
      setText('house.coverage', (contrib != null ? contrib : 0) + ' / ' + (expected != null ? expected : 0));
    }
    if (ov.authority) setText('house.authority', ov.authority);
    var calling = ov.calling_rooms != null ? ov.calling_rooms : sum.calling;
    setText('house.heatstate', t('house.heatstate', {
      state: t('state.' + (calling > 0 ? 'calling' : 'idle')),
      temp: num(target != null ? target : 21)
    }));
  }

  // Display name: the Touch-side name if set, else the label the V6 reports
  // (its Device identity name, or its location), else null ("Unnamed").
  function nodeLabel(n) {
    if (!n) return null;
    if (!nodeUnnamed(n)) return n.name;
    var dev = String(n.device_name || '').trim();
    return dev || null;
  }

  function nodeUnnamed(n) {
    var name = String((n && n.name) || '').trim();
    if (!name) return true;
    if (n.hostname && name === n.hostname) return true;
    if (n.id && name === n.id) return true;
    return false;
  }

  function nodeBadge(n) {
    if (n && n.reachable === false) return { cls: 'bad', key: 'status.unreachable' };
    var trust = (n && n.trust_label) || '';
    if (trust === 'trusted') return { cls: 'ok', key: 'status.trusted' };
    if (trust === 'paired' || trust === 'unpaired' || trust === '') return { cls: '', key: 'status.waiting' };
    return { cls: '', key: 'status.unknown' };
  }

  function statusLabel(raw) {
    var key = 'status.' + String(raw || '');
    return i18n[key] ? t(key) : t('status.unknown');
  }

  function relAge(age) {
    if (age == null || !isFinite(Number(age))) return '—';
    var n = Math.round(Number(age));
    if (n < 60) return t('rt.secondsAgo', { n: n });
    if (n < 3600) return t('rt.minutesAgo', { n: Math.round(n / 60) });
    return t('rt.minutesAgo', { n: Math.round(n / 60) });
  }

  function whenText(age, status) {
    var rel = relAge(age);
    var label = status ? statusLabel(status) : '';
    if (rel === '—' && !label) return '—';
    if (!label || label === t('status.unknown') && !i18n['status.' + status]) return rel === '—' ? t('status.unknown') : rel;
    if (rel === '—') return label;
    return rel + ' · ' + label;
  }

  function paintHouseTemp(bind, value) {
    var has = value != null && isFinite(Number(value));
    qsa('[data-bind="' + bind + '"]').forEach(function (n) {
      n.textContent = has ? num(value) + '°' : '—';
      var temp = n.closest('.temp');
      if (!temp) return;
      if (has) temp.removeAttribute('data-empty');
      else temp.setAttribute('data-empty', '');
    });
  }

  function zoneCountFor(n, index) {
    if (n && n.health && n.health.mapped_zones != null) return n.health.mapped_zones;
    if (n && n.health && n.health.imported_zones != null) return n.health.imported_zones;
    return (state.zones || []).filter(function (z) {
      return z && z.node_index === index && !z.is_group_secondary && !z.unassigned;
    }).length;
  }

  function renderScan(found) {
    var box = qs('[data-bind-scan]');
    if (!box) return;
    var rows = (found || []).filter(function (f) { return f && f.source === 'lan_probe'; });
    if (!rows.length) { box.innerHTML = ''; return; }
    box.innerHTML = rows.map(function (f) {
      var host = f.hostname || f.ip || '';
      return '<div class="field row"><span class="muted">' + esc(t('ctrl.found')) + '</span>' +
        '<span class="mono">' + esc(host) + '</span>' +
        '<button class="btn" type="button" data-action="add-found" data-host="' + esc(host) + '" data-fp="' + esc(f.pairing_fingerprint || '') + '">' + esc(t('ctrl.addRow')) + '</button></div>';
    }).join('');
  }

  // Device menu: this Touch and every registered V6 (link, IP, reachability).
  function renderDeviceMenu() {
    var nav = qs('[data-bind-devices]');
    if (!nav) return;
    var own = state.ownIp || '';
    var item = function (href, name, sub, current, offline) {
      return '<a href="' + esc(href) + '"' + (current ? ' aria-current="page"' : '') + (offline ? ' data-offline' : '') +
        '><i></i>' + esc(name) + '<small>' + esc(sub) + '</small></a>';
    };
    var html = item('/', 'Lune Touch', own ? own + ' · ' + t('device.this') : t('device.this'), true, false);
    (state.nodes || []).forEach(function (n) {
      var ip = n.ip || n.hostname || '';
      if (!ip) return;
      html += item('http://' + ip + '/', nodeLabel(n) || 'Lune V6', ip, false, n.reachable === false);
    });
    if (nav.innerHTML !== html) nav.innerHTML = html;
  }

  function applyNodes(nodes) {
    state.nodes = nodes || [];
    renderDeviceMenu();
    var bad = state.nodes.find(function (n) { return n.reachable === false; });
    var alert = qs('[data-bind-alert="board"]');
    if (alert) {
      alert.hidden = !bad;
      if (bad) setText('alert.boardTitle', t('alert.boardFault', { board: nodeLabel(bad) || t('common.unnamed') }));
    }
    applyDeviceList(state.nodes);
    var tbody = qs('[data-bind-nodes] tbody');
    if (!tbody) return;
    tbody.innerHTML = state.nodes.map(function (n, index) {
      var badge = nodeBadge(n);
      var unnamed = !nodeLabel(n);
      var label = nodeLabel(n) || t('common.unnamed');
      var addr = n.ip || n.hostname || '';
      var pop = 'confirm-rm-' + String(n.id).replace(/[^a-zA-Z0-9_-]/g, '');
      var nameCell = '<div class="name-edit">' +
        (unnamed
          ? '<span class="muted" data-name>' + esc(label) + '</span>' +
            '<button type="button" class="icon-btn" data-action="edit-node-name" data-id="' + esc(n.id) + '" aria-label="' + esc(t('ctrl.editName')) + '">' + PENCIL + '</button>'
          : '<span data-name>' + esc(label) + '</span>') +
        '<input class="input" hidden value="' + esc(nodeUnnamed(n) ? '' : n.name) + '" placeholder="' + esc(n.device_name || '') + '" data-rename="' + esc(n.id) + '" aria-label="' + esc(t('ctrl.editName')) + '">' +
        '</div>';
      var remove = '<button class="btn danger" type="button" popovertarget="' + pop + '">' + esc(t('ctrl.removeBtn')) + '</button>' +
        '<div class="confirm-pop" id="' + pop + '" popover>' +
        '<p class="confirm-title">' + esc(t('ctrl.removeAsk', { name: label })) + '</p>' +
        '<p>' + esc(t('ctrl.removeConfirm', { name: label })) + '</p>' +
        '<div class="confirm-actions">' +
        '<button class="btn" type="button" popovertarget="' + pop + '" popovertargetaction="hide">' + esc(t('common.cancel')) + '</button>' +
        '<button class="btn danger-solid" type="button" data-action="remove-node" data-id="' + esc(n.id) + '">' + esc(t('ctrl.removeDo')) + '</button>' +
        '</div></div>';
      // Address is editable: a V6 that got a new IP (DHCP, new router, WiFi
      // password) keeps its trust and zones when it is moved here.
      var addrCell = '<div class="name-edit">' +
        '<span class="mono" data-name>' + esc(addr || '—') + '</span>' +
        '<button type="button" class="icon-btn" data-action="edit-node-host" data-id="' + esc(n.id) + '" aria-label="' + esc(t('ctrl.editHost')) + '" title="' + esc(t('ctrl.editHost')) + '">' + PENCIL + '</button>' +
        '<input class="input mono" hidden value="' + esc(addr) + '" placeholder="192.168.1.50" data-rehost="' + esc(n.id) + '" aria-label="' + esc(t('ctrl.editHost')) + '" autocomplete="off" spellcheck="false">' +
        '</div>';
      return '<tr data-node="' + esc(n.id) + '"><td>' + nameCell + '</td><td>' + addrCell +
        '</td><td><span class="badge' + (badge.cls ? ' ' + badge.cls : '') + '">' + esc(t(badge.key)) + '</span></td><td class="num">' +
        esc(String(zoneCountFor(n, index))) + '</td><td>' + remove + '</td></tr>';
    }).join('') || '<tr><td colspan="5" class="muted">' + esc(t('ctrl.empty')) + '</td></tr>';
    renderScan(state.scanFound);
  }

  function heatSyncOn(hs) {
    var el = qs('input[name="target_sync_enabled"]');
    var wrap = el && el.closest('.switch');
    if (el && wrap && wrap.hasAttribute('data-dirty')) return !!el.checked;
    if (hs && hs.target_sync_enabled != null) return !!hs.target_sync_enabled;
    return !!(el && el.checked);
  }

  function fillTemplate(tpl, host, port, entity, value) {
    if (!tpl) return '';
    return String(tpl)
      .replace(/\{host\}/g, host || '')
      .replace(/\{port\}/g, port == null ? '' : String(port))
      .replace(/\{entity\}/g, entity || '')
      .replace(/\{value\}/g, value == null ? '' : String(value));
  }

  function sentUrl(hs, entity, value) {
    var host = hs.host || '';
    var port = hs.port != null ? hs.port : 80;
    var type = hs.type === 'generic_http' ? 'generic_http' : 'asgard';
    if (type === 'generic_http') {
      var tpl = hs.write_url_template || '';
      // TODO: generic_http has no default URL when the write template is empty.
      return fillTemplate(tpl, host, port, entity, value) || '—';
    }
    if (!host || !entity) return '—';
    return 'http://' + host + ':' + port + '/number/' + encodeURIComponent(entity) + '/set?value=' +
      (value == null || !isFinite(Number(value)) ? '' : Number(value).toFixed(1));
  }

  function paintHeatBadge(hs) {
    var typeName = (hs.type === 'generic_http') ? t('hs.typeHttp') : t('hs.typeAsgard');
    var cls = '';
    var label;
    if (hs.enabled === false) {
      label = typeName + ' · ' + t('heat.badge.off');
    } else {
      var st = (hs.push && hs.push.status) || '';
      if (!hs.push || hs.push.has_result === false || !st) {
        label = typeName + ' · ' + t('status.waiting');
      } else {
        label = typeName + ' · ' + statusLabel(st);
        if (st === 'confirmed' || st === 'sent') cls = 'ok';
        else if (st === 'unreachable') cls = 'bad';
        else if (st === 'mismatch' || st === 'blocked') cls = 'warn';
      }
    }
    qsa('[data-bind="heat.badge"]').forEach(function (n) {
      n.textContent = label;
      n.className = 'badge' + (cls ? ' ' + cls : '');
    });
  }

  function renderHeatDelivery(hs) {
    if (!hs) return;
    var push = hs.push || {};
    var preview = hs.send_preview || {};
    var weighted = hs.physical_house_temperature_c != null ? hs.physical_house_temperature_c
      : (preview.value_c != null ? preview.value_c : null);
    var target = hs.house_comfort_target_c != null ? hs.house_comfort_target_c
      : (hs.house_target && hs.house_target.value_c != null ? hs.house_target.value_c
        : (preview.target_setpoint_c != null ? preview.target_setpoint_c : null));
    var entity = hs.weighted_temperature_variable || '—';
    var climate = hs.climate_entity || '—';
    var age = push.confirmation_age_s != null ? Number(push.confirmation_age_s)
      : (push.write_age_s != null ? Number(push.write_age_s) : null);
    // No push yet and a V6 refuses Touch's lease → say why instead of "—".
    var leaseMissing = (state.nodes || []).some(function (n) {
      return n && n.trust_label === 'trusted' && n.lease && n.lease !== 'granted';
    });
    var tempWhen = push.has_result ? whenText(age, push.status) : (leaseMissing ? t('hs.notSentLease') : '—');
    var tempTxt = (weighted != null && isFinite(Number(weighted))) ? num(weighted) + ' °C' : '—';
    var setTxt = (target != null && isFinite(Number(target))) ? num(target) + ' °C' : '—';
    setText('hs.http.temp', tempTxt);
    setText('hs.asgard.temp', tempTxt);
    setText('hs.http.entity', entity);
    setText('hs.asgard.entity', entity);
    setText('hs.http.when', tempWhen);
    setText('hs.asgard.when', tempWhen);
    setText('hs.asgard.setpoint', setTxt);
    setText('hs.asgard.climate', climate);
    var valueToken = (weighted != null && isFinite(Number(weighted))) ? Number(weighted).toFixed(1) : '';
    setText('hs.sent.url', sentUrl(hs, entity === '—' ? '' : entity, valueToken));
    var sync = heatSyncOn(hs);
    qsa('[data-bind="hs.asgard.setpointWhen"]').forEach(function (n) {
      if (!sync) {
        n.textContent = t('hs.notSent');
        n.classList.add('muted');
      } else {
        var ts = hs.target_sync || {};
        var written = ts.last_written_c != null && isFinite(Number(ts.last_written_c));
        var tsAge = written && ts.write_age_s != null ? Number(ts.write_age_s) : null;
        var tsStatus = '';
        if (written) {
          if (ts.failure_streak > 0) tsStatus = 'unreachable';
          else if (ts.last_confirmed_c != null && isFinite(Number(ts.last_confirmed_c))) tsStatus = 'confirmed';
          else tsStatus = 'sent';
        }
        n.textContent = written ? whenText(tsAge, tsStatus) : '—';
        n.classList.remove('muted');
      }
    });
    qsa('[data-bind="heat.sentNote"]').forEach(function (n) {
      if (!sync && hs.type !== 'generic_http') {
        n.hidden = false;
        n.textContent = t('hs.notSent');
      } else {
        n.hidden = true;
        n.textContent = '';
      }
    });
  }

  function weightParts(z, basis) {
    var room = (z && z.room) || {};
    var listed = null;
    (state.rooms || []).some(function (r) {
      if (r && z && r.room_id === z.room_id) { listed = r; return true; }
      return false;
    });
    if (listed) {
      if (room.total_area_m2 == null && listed.area_m2 != null) room = Object.assign({ total_area_m2: listed.area_m2 }, room);
      if (room.physical_weight == null && listed.physical_weight != null) room.physical_weight = listed.physical_weight;
      if (room.ua_w_per_k == null && listed.ua_w_per_k != null) room.ua_w_per_k = listed.ua_w_per_k;
      if (room.include_in_house_temperature == null && listed.include_in_house_temperature != null)
        room.include_in_house_temperature = listed.include_in_house_temperature;
    }
    var mult = Number(room.physical_weight);
    if (!(mult > 0)) mult = 1;
    var area = Number(room.total_area_m2 != null ? room.total_area_m2 : room.area_m2);
    if (!(area > 0)) area = 1;
    var ua = Number(room.ua_w_per_k);
    if ((basis === 'ua' || basis === 'mixed') && ua > 0) return { w: ua * mult, kind: 'ua', area: area, ua: ua };
    return { w: area * mult, kind: 'area', area: area, ua: ua };
  }

  function renderWeightRows(hs) {
    // One table per adapter type (Asgard / generic HTTP) — fill all of them.
    var bodies = qsa('[data-bind-weight-rows]');
    if (!bodies.length) return;
    var body = { set innerHTML(v) { bodies.forEach(function (b) { b.innerHTML = v; }); } };
    var basis = (state.strategy && state.strategy.weighting && state.strategy.weighting.basis) || 'area';
    var seen = {};
    var rows = [];
    (state.zones || []).forEach(function (z) {
      if (!z || z.is_group_secondary || z.unassigned) return;
      if (!z.room_id || seen[z.room_id]) return;
      var incl = z.room && z.room.include_in_house_temperature;
      if (incl == null) {
        var listed = (state.rooms || []).filter(function (r) { return r.room_id === z.room_id; })[0];
        if (listed) incl = listed.include_in_house_temperature;
      }
      if (incl === false) return;
      if (z.fresh === false) return;
      if (z.temperature_c == null || !isFinite(Number(z.temperature_c))) return;
      seen[z.room_id] = 1;
      var part = weightParts(z, basis);
      rows.push({ name: z.name || z.room_id, temp: Number(z.temperature_c), part: part });
    });
    var sumW = rows.reduce(function (s, r) { return s + r.part.w; }, 0);
    var shown = hs && hs.physical_house_temperature_c != null ? Number(hs.physical_house_temperature_c)
      : (hs && hs.send_preview && hs.send_preview.value_c != null ? Number(hs.send_preview.value_c) : NaN);
    var computed = sumW > 0 ? rows.reduce(function (s, r) { return s + r.temp * r.part.w; }, 0) / sumW : NaN;
    // Commissioning preview is an equal average of fresh temperatures. Match that
    // shown number when a physical (area/UA) temperature is not available.
    if (!(hs && hs.physical_house_temperature_c != null) && isFinite(shown) && (!isFinite(computed) || Math.abs(computed - shown) > 0.15)) {
      rows.forEach(function (r) { r.part = { w: 1, kind: 'equal', area: r.part.area, ua: r.part.ua }; });
      sumW = rows.length;
    }
    if (!rows.length || !(sumW > 0)) {
      body.innerHTML = '<tr><td colspan="4" class="muted">—</td></tr>';
      return;
    }
    body.innerHTML = rows.map(function (r) {
      var pct = (100 * r.part.w / sumW);
      var weight = r.part.kind === 'ua'
        ? t('hs.calcWeightUa', { ua: num(r.part.ua, 1), pct: num(pct, 0) })
        : t('hs.calcWeightArea', { area: num(r.part.area, 0), pct: num(pct, 0) });
      var contrib = r.temp * r.part.w / sumW;
      return '<tr><td>' + esc(r.name) + '</td><td class="num">' + esc(num(r.temp)) + ' °C</td><td>' +
        esc(weight) + '</td><td class="num">' + esc(num(contrib)) + ' °C</td></tr>';
    }).join('');
  }

  function applyHeat(hs) {
    if (!hs) return;
    state.heat = hs;
    var type = hs.type || 'asgard';
    syncHsType(type);
    var uiType = type === 'generic_http' ? 'http' : 'asgard';
    var conn = hs.connection || {};
    var push = hs.push || {};
    var preview = hs.send_preview || {};
    var age = push.confirmation_age_s != null ? Number(push.confirmation_age_s)
      : (push.write_age_s != null ? Number(push.write_age_s)
        : (push.age_s != null ? Number(push.age_s) : null));
    // Status lives in the badge; the kv row is only "when".
    setText('heat.lastPush', relAge(push.has_result === false ? null : age));

    var weighted = hs.physical_house_temperature_c != null ? hs.physical_house_temperature_c
      : (preview.value_c != null ? preview.value_c
        : (hs.weighted_temperature && hs.weighted_temperature.value_c != null
          ? hs.weighted_temperature.value_c : null));
    var target = hs.house_comfort_target_c != null ? hs.house_comfort_target_c
      : (hs.house_target && hs.house_target.value_c != null ? hs.house_target.value_c
        : (preview.target_setpoint_c != null ? preview.target_setpoint_c : null));
    setBind('heat.weighted', num(weighted) + ' <small>°C</small>');
    setBind('heat.setpoint', num(target) + ' <small>°C</small>');

    paintHeatBadge(hs);
    renderHeatDelivery(hs);
    paintTargetRole();
    var hp = hs.heat_pump || {};
    setText('dash.hpTemps', hp.available && hp.feed_c != null && hp.return_c != null
      ? num(hp.feed_c) + ' → ' + num(hp.return_c) + ' °C · ' +
        (hp.compressor_on ? t('hp.compOn', { hz: num(hp.compressor_hz, 0) }) : t('hp.compOff'))
      : '—');
    renderWeightRows(hs);
    if (weighted != null && isFinite(Number(weighted))) {
      applyOverview({
        house_temp_c: weighted,
        house_target_c: target,
        calling_rooms: state.overview && state.overview.calling_rooms,
        contributing_manifolds: state.overview && state.overview.contributing_manifolds,
        expected_manifolds: state.overview && state.overview.expected_manifolds,
        authority: state.overview && state.overview.authority,
        summary: state.overview && state.overview.summary
      });
    }
    var prefix = uiType === 'http' ? 'http_' : 'asgard_';
    function setIf(id, val) {
      var el = qs('#' + id);
      if (el && document.activeElement !== el && val != null) el.value = val;
    }
    setIf(prefix + 'host', conn.host || hs.host || '');
    setIf(prefix + 'port', conn.port != null ? conn.port : (hs.port != null ? hs.port : 80));
    setIf(prefix + 'push_interval_s', hs.push_interval_s != null ? hs.push_interval_s : 60);
    setIf(prefix + 'weighted_temperature_variable', hs.weighted_temperature_variable || 'temperature_feedback_z1');
    setIf('write_url_template', hs.write_url_template || '');
    setIf('read_url_template', hs.read_url_template || '');
    setIf('climate_entity', hs.climate_entity || '');
    var en = qs('input[name="enabled"]');
    if (en && document.activeElement !== en) en.checked = !!(conn.enabled != null ? conn.enabled : hs.enabled);
    var syncEl = qs('input[name="target_sync_enabled"]');
    if (syncEl && document.activeElement !== syncEl) {
      syncEl.checked = !!(hs.target_sync_enabled || (hs.asgard && hs.asgard.sync_enabled));
    }
    var odin = qs('input[name="odin_plan_enabled"]');
    if (odin && document.activeElement !== odin) odin.checked = !!hs.odin_plan_enabled;
    setIf('odin_host', hs.odin_host || '');
    var circ = hs.circulation || {};
    var lpm = circ.flow_m3h != null && isFinite(Number(circ.flow_m3h)) ? Number(circ.flow_m3h) * 1000 / 60 : null;
    setBind('pump.flow', num(lpm, 0) + ' <small>l/min</small>');
    if (state.zones) renderDist();
    setText('pump.flowM3h', circ.flow_m3h != null && isFinite(Number(circ.flow_m3h)) ? num(circ.flow_m3h) + ' m³/h' : '—');
    setBind('pump.head', num(circ.head_m) + ' <small>m</small>');
    setBind('pump.power', num(circ.power_w, 0) + ' <small>W</small>');
    setText('pump.host', circ.host || '—');
    applyCirculation(circ);
    resnapForms(['heat-source', 'circulation']);
  }

  // Odin comfort control, Asgard→Odin link health and generic heat-source levers.
  function applyHeatControl(ctl) {
    if (!ctl) return;
    state.heatControl = ctl;
    var o = ctl.odin || {};
    var link = ctl.link || {};
    var d = ctl.demand || {};
    var g = ctl.generic || {};
    var ls = link.status || 'unknown';
    var linkText = t('hs.link.' + ls, { s: link.telemetry_age_s != null ? Math.round(link.telemetry_age_s / 60) : '—' });
    if (link.mqtt_telemetry_age_s != null) linkText += ' · MQTT';
    setText('odin.link', linkText);
    var st = o.status || 'disabled';
    var a = o.applied || {};
    var w = o.wanted || {};
    var stText = st === 'lifted'
      ? t('hs.odinStatus.lifted', { c: num(a.lift_c), h: a.start_hour, n: a.hours })
      : t('hs.odinStatus.' + st);
    if (st !== 'lifted' && w.active) stText += ' · ' + t('hs.odinWanted', { c: num(w.lift_c), h: w.start_hour, n: w.hours });
    setText('odin.state', stText);
    setText('dash.odinPlan', stText);
    setText('dash.odinLink', linkText);
    var hsNow = state.heat || {};
    setShow('dash.odin', !!(o.enabled || hsNow.odin_host));
    var tk = link.takeover;
    setText('dash.odinDriver', tk === true ? t('driver.odin') : (tk === false ? t('driver.asgard') : '—'));
    paintTargetRole();
    var route = d.route || 'none';
    var routeText = t('hs.route.' + route, { c: num(d.target_uplift_c) });
    setText('odin.route', routeText);
    setText('levers.state', g.status && g.status !== 'idle'
      ? t('hs.leverState', { t: num(g.last_target_c), r: g.last_heat_request ? '1' : '0', c: num(g.last_curve_offset_c) })
      : routeText);
    function setIf(id, val) {
      var el = qs('#' + id) || qs('[name="' + id + '"]');
      if (el && document.activeElement !== el && val != null) el.value = val;
    }
    var en = qs('input[name="odin_control_enabled"]');
    if (en && document.activeElement !== en) en.checked = !!o.enabled;
    setIf('odin_max_lift_c', o.max_lift_c != null ? o.max_lift_c : 1.5);
    setIf('target_url_template', g.target_url_template || '');
    setIf('heat_request_url_template', g.heat_request_url_template || '');
    setIf('curve_offset_url_template', g.curve_offset_url_template || '');
    setIf('curve_gain', g.curve_gain != null ? g.curve_gain : 2);
    setIf('curve_max_offset_c', g.curve_max_offset_c != null ? g.curve_max_offset_c : 5);
    resnapForms(['heat-source']);
  }

  function applyOdinMqtt(mq) {
    if (!mq) return;
    function setIf(id, val) {
      var el = qs('#' + id);
      if (el && document.activeElement !== el && val != null) el.value = val;
    }
    var en = qs('input[name="mqtt_enabled"]');
    if (en && document.activeElement !== en) en.checked = !!mq.enabled;
    setIf('mqtt_host', mq.host || '');
    setIf('mqtt_port', mq.port || 1883);
    setIf('mqtt_username', mq.username || '');
    setIf('mqtt_topic_prefix', mq.topic_prefix || '');
    setIf('mqtt_hp_id', mq.hp_id || '');
    var pw = qs('#mqtt_password');
    if (pw) pw.placeholder = mq.password_set ? t('hs.mqttPasswordSet') : '';
    // Saved is not connected: show what the client actually does.
    setText('hs.mqttStatus', !mq.enabled ? t('hs.mqttOff')
      : t('hs.mqttState', { s: mq.connected ? t('hs.mqttConnected')
        : t('hs.mqttDisconnected') + (mq.last_error ? ' (' + mq.last_error + ')' : '') }));
    resnapForms(['heat-source']);
  }

  /* ---- Elpris til Odin ---- */
  function priceForm() { return qs('form.panel[data-save="prices"]'); }

  function priceCur() {
    var sel = qs('#price_currency');
    return (sel && sel.value) || ((state.prices || {}).currency) || 'DKK';
  }

  // Units that follow the calculation currency; DK-only sources (Energi Data
  // Service, DataHub) only exist for DK1/DK2.
  function priceSyncForm() {
    var f = priceForm();
    if (!f) return;
    var unit = priceCur() + '/kWh';
    qsa('[data-price-unit]', f).forEach(function (u) { u.textContent = unit; });
    qsa('[data-price-cur]', f).forEach(function (u) { u.textContent = priceCur(); });
    var zone = (qs('#price_zone') || {}).value || 'DK1';
    var dk = /^DK[12]$/.test(zone);
    qsa('[data-dk-only]', f).forEach(function (lab) {
      lab.hidden = !dk;
      var r = document.getElementById(lab.getAttribute('for'));
      if (!dk && r && r.checked) {
        var alt = { 'ss-eds': 'ss-energy_charts', 'gt-datahub': 'gt-schedule', 'en-datahub': 'en-fixed' }[r.id];
        var a = alt && document.getElementById(alt);
        if (a) { a.checked = true; a.dispatchEvent(new Event('change', { bubbles: true })); }
      }
    });
    var dkNote = qs('[data-bind="price.dkNote"]');
    if (dkNote) dkNote.hidden = !dk;
  }

  function priceSchedRows(list) {
    var body = qs('[data-bind-price-sched]');
    if (!body) return;
    body.innerHTML = (list || []).map(function (b) {
      return '<tr><td><input class="input w-xs" type="number" inputmode="numeric" min="0" max="23" step="1" data-sched="h" value="' +
        esc(b.h) + '" aria-label="' + esc(t('price.schedHourAria')) + '"></td>' +
        '<td class="num"><input class="input" type="number" inputmode="decimal" min="-5" max="20" step="0.001" data-sched="v" value="' +
        esc(Number(b.v).toFixed(3)) + '" aria-label="' + esc(t('price.schedValueAria')) + '"></td>' +
        '<td><button class="btn" type="button" data-action="price-sched-remove">' + esc(t('price.schedRemove')) + '</button></td></tr>';
    }).join('');
  }

  function priceSchedRead() {
    var out = [];
    qsa('[data-bind-price-sched] tr').forEach(function (tr) {
      var h = Number((qs('[data-sched="h"]', tr) || {}).value);
      var v = Number((qs('[data-sched="v"]', tr) || {}).value);
      if (isFinite(h) && h >= 0 && h <= 23 && isFinite(v)) out.push({ h: Math.round(h), v: Math.round(v * 10000) / 10000 });
    });
    out.sort(function (a, b) { return a.h - b.h; });
    return out.filter(function (b, i) { return i === 0 || b.h !== out[i - 1].h; });
  }

  // The rows have no name; the hidden grid_schedule field carries them so the
  // normal clean/dirty + save path sees the change.
  function priceSchedSync() {
    var f = priceForm();
    var hidden = f && f.querySelector('input[name="grid_schedule"]');
    if (!hidden) return;
    var json = JSON.stringify(priceSchedRead());
    if (hidden.value === json) return;
    hidden.value = json;
    hidden.dispatchEvent(new Event('input', { bubbles: true }));
  }

  function priceErrText(err) {
    var e = String(err || '');
    if (!e) return '';
    if (e === 'spot_incomplete') return t('price.err.spot_incomplete');
    if (e === 'no_odin_host') return t('price.err.no_odin_host');
    if (e === 'clock_invalid') return t('price.err.clock');
    if (/datahub_dk_only|eds_dk_only/.test(e)) return t('price.err.datahub_dk_only');
    if (/no_token/.test(e)) return t('price.err.no_token');
    if (/^spot_/.test(e)) return t('price.err.spot');
    if (/^grid_/.test(e)) return t('price.err.grid');
    if (/^system_/.test(e)) return t('price.err.energinet');
    if (/^odin_|_http_/.test(e)) return t('price.err.odin');
    return t('price.err.other');
  }

  function priceWhen(epoch) {
    if (!epoch) return '—';
    var lang = (i18n._lang || document.documentElement.lang || 'en');
    return new Date(epoch * 1000).toLocaleString(lang, { weekday: 'short', hour: '2-digit', minute: '2-digit' });
  }

  function priceSourceText(src) {
    if (src === 'api' || src === 'energy_charts') return t('price.odinMode.' + src);
    if (src === 'entsoe' || src === 'fixed') return t('price.odinMode.' + src);
    return t('price.odinMode.unknown');
  }

  function applyPrices(p) {
    if (!p || p.available === false) return;
    state.prices = p;
    var st = (p.status || {});
    var stKey = st.state || (p.enabled ? 'waiting' : 'disabled');
    qsa('[data-bind="price.badge"]').forEach(function (b) {
      b.textContent = t('price.state.' + stKey);
      b.className = stKey === 'ok' || stKey === 'odin' ? 'badge ok' : (stKey === 'error' ? 'badge bad' : (stKey === 'disabled' ? 'badge' : 'badge warn'));
    });
    setText('price.lastPush', st.last_push_epoch ? t('price.lastPushValue', { time: priceWhen(st.last_push_epoch) }) : '—');
    setText('price.hours', st.hours_pushed ? t('price.hoursValue', { n: st.hours_pushed }) : '—');
    var used = st.spot_used ? t('price.spotUsed.' + st.spot_used) : '—';
    setText('price.spotUsed', st.spot_used && st.spot_fallback ? t('price.spotFallback', { src: used }) : used);
    setText('price.odinMode', st.odin_write_pending ? t('price.odinMode.pending') : priceSourceText(st.odin_source));
    setText('price.problem', stKey === 'error' ? priceErrText(st.last_error) : '—');
    var notes = [];
    if (!p.odin_host_set) notes.push(t('price.noOdinHost'));
    if (st.grid_from_cache || st.system_from_cache) notes.push(t('price.cached'));
    setText('price.note', notes.join(' '));

    // Odin's own settings as Odin reports them (its token only as set / not set).
    var oc = ((p.odin || {}).current) || {};
    var srcText = !oc.known ? '—' : (oc.price_mode === 'fixed' ? t('price.odinMode.fixed') : priceSourceText(oc.price_source));
    setText('price.odin.mode', srcText);
    setText('price.odin.zone', oc.known && oc.ec_bzn ? oc.ec_bzn : '—');
    setBind('price.odin.fixed', oc.known && oc.fixed_price != null ? esc(num(oc.fixed_price, 3)) + ' <small>€/kWh</small>' : '—');
    setText('price.odin.token', oc.known ? t(oc.token_set ? 'price.tokenSet' : 'price.tokenUnset') : '—');

    // Today's all-in price: muted bars (price is not heat) + figures in HTML.
    var day = p.today;
    var cur = p.currency || 'DKK';
    var plot = qs('[data-bind-price-bars]');
    var total = (day && day.total) || [];
    if (plot) {
      var max = 0;
      total.forEach(function (v) { if (Number(v) > max) max = Number(v); });
      plot.innerHTML = total.length ? total.map(function (v, h) {
        var pct = max > 0 ? Math.max(2, Math.round(Math.max(0, Number(v)) / max * 100)) : 0;
        return '<div class="col"><i class="plan" style="--plan:' + pct + '" title="' +
          esc(t('price.atHour', { v: num(v, 2) + ' ' + cur + '/kWh', h: String(h).padStart(2, '0') })) + '"></i></div>';
      }).join('') : '';
    }
    setText('price.inclAll', t('price.inclAll', { cur: cur }));
    var unit = ' <small>' + esc(cur) + '/kWh</small>';
    if (total.length === 24) {
      var lo = 0, hi = 0, peak = 0;
      for (var h = 1; h < 24; h++) {
        if (total[h] < total[lo]) lo = h;
        if (total[h] > total[hi]) hi = h;
      }
      for (var k = 17; k < 21; k++) peak += Number(total[k]) / 4;
      var hh = function (x) { return String(x).padStart(2, '0'); };
      setBind('price.now', esc(num(total[new Date().getHours()], 2)) + unit);
      setText('price.min', t('price.atHour', { v: num(total[lo], 2) + ' ' + cur + '/kWh', h: hh(lo) }));
      setText('price.max', t('price.atHour', { v: num(total[hi], 2) + ' ' + cur + '/kWh', h: hh(hi) }));
      setBind('price.peak', esc(num(peak, 2)) + unit);
    } else {
      setText('price.now', t('price.noData'));
      ['price.min', 'price.max', 'price.peak'].forEach(function (k2) { setText(k2, '—'); });
    }
    var tok = qs('#price_token');
    if (tok) tok.placeholder = p.entsoe_token_set ? t('price.tokenSaved') : '';

    // Settings: never overwrite what the user is editing.
    var f = priceForm();
    if (!f || f.dataset.dirty != null || f.dataset.state === 'saving') return;
    fillPriceForm(f, p);
  }

  function fillPriceForm(f, p) {
    var en = f.querySelector('input[name="enabled"]');
    if (en && document.activeElement !== en) en.checked = !!p.enabled;
    function check(name, val) {
      var r = f.querySelector('input[name="' + name + '"][value="' + val + '"]');
      if (r) r.checked = true;
    }
    function setIf(name, val, dec) {
      var el = f.querySelector('[name="' + name + '"]');
      if (el && document.activeElement !== el && val != null) el.value = dec != null ? Number(val).toFixed(dec) : val;
    }
    var g = p.grid || {}, sy = p.system || {}, sp = p.spot || {}, o = p.odin || {};
    check('model', p.model || 'touch');
    setIf('zone', p.zone || 'DK1');
    check('spot_source', sp.source || 'eds');
    setIf('spot_fixed_eur', sp.fixed_eur, 3);
    setIf('currency', p.currency || 'DKK');
    setIf('fx', p.fx != null ? Math.round(p.fx * 10000) / 10000 : null);
    check('grid_source', g.source || 'datahub');
    check('system_source', sy.source || 'datahub');
    setIf('grid_gln', g.gln || '');
    setIf('grid_code', g.code || '');
    setIf('system_fixed', sy.fixed, 3);
    setIf('energy_tax', p.energy_tax, 3);
    setIf('markup', p.markup, 3);
    setIf('vat_pct', p.vat_pct, 1);
    check('odin_mode', o.mode || 'dynamic');
    check('odin_source', o.source || 'energy_charts');
    setIf('odin_fixed_price', o.fixed_price, 3);
    var tok = f.querySelector('input[name="entsoe_token"]');
    if (tok) tok.value = '';
    var sched = g.schedule || [];
    var hidden = f.querySelector('input[name="grid_schedule"]');
    if (hidden) hidden.value = JSON.stringify(sched.map(function (b) { return { h: b.h, v: b.v }; }));
    priceSchedRows(sched);
    priceSyncForm();
    resnapForms(['prices']);
  }

  async function applyZoneDefaults() {
    var f = priceForm();
    if (!f) return;
    var zone = (qs('#price_zone') || {}).value || 'DK1';
    var d = await get('/prices/zone-defaults/' + encodeURIComponent(zone));
    function check(name, val) {
      var r = f.querySelector('input[name="' + name + '"][value="' + val + '"]');
      if (r && !r.checked) { r.checked = true; r.dispatchEvent(new Event('change', { bubbles: true })); }
    }
    function put(name, val, dec) {
      var el = f.querySelector('[name="' + name + '"]');
      if (!el || val == null) return;
      el.value = dec != null ? Number(val).toFixed(dec) : val;
      el.dispatchEvent(new Event('input', { bubbles: true }));
    }
    check('spot_source', d.spot_source);
    put('currency', d.currency);
    put('fx', Math.round(d.fx * 10000) / 10000);
    put('energy_tax', d.energy_tax, 3);
    put('vat_pct', d.vat_pct, 1);
    check('grid_source', d.grid_source);
    check('system_source', d.system_source);
    if (d.system_source === 'fixed') put('system_fixed', d.system_fixed, 3);
    priceSyncForm();
    setText('price.defaultsNote', t(d.known ? 'price.defaultsApplied' : 'price.defaultsUnknown', { zone: zone }));
  }

  // Push (Touch model) or write Odin's settings (Odin model), then follow GET /prices.
  async function runPricePush(btn, odinWrite) {
    var box = qs(odinWrite ? '[data-bind-price-odin-result]' : '[data-bind-price-result]');
    function paint(ok, l1, l2) {
      if (!box) return;
      box.removeAttribute('data-state');
      box.innerHTML = '<div class="msg ' + (ok ? 'ok' : 'bad') + '"><span><b>' + esc(l1) + '</b>' + esc(l2) + '</span></div>';
    }
    var fail = odinWrite ? 'price.odinWriteFail' : 'price.pushFail';
    if (box) { box.setAttribute('data-state', 'running'); box.textContent = t('price.state.running'); }
    btn.setAttribute('aria-busy', 'true');
    var before = ((state.prices || {}).status || {}).last_attempt_epoch || 0;
    try {
      try {
        await post('/prices/push', {});
      } catch (e) {
        var p0 = state.prices || {};
        paint(false, t(fail, { time: clockNow() }),
          !p0.enabled ? t('price.pushDisabled') : (!p0.odin_host_set ? t('price.noOdinHost') : t('price.err.other')));
        return;
      }
      for (var i = 0; i < 25; i++) {
        await new Promise(function (r) { setTimeout(r, 2000); });
        var p = await get('/prices').catch(function () { return null; });
        if (!p) continue;
        var s2 = p.status || {};
        if (odinWrite) {
          if (s2.odin_write_pending) continue;
          applyPrices(p);
          paint(true, t('price.odinWriteOk', { time: clockNow() }), priceSourceText(((p.odin || {}).current || {}).price_source));
          return;
        }
        if (s2.state === 'running' || !(s2.last_attempt_epoch > before)) continue;
        applyPrices(p);
        if (s2.state === 'ok') paint(true, t('price.pushOk', { time: clockNow() }), t('price.pushOkBody', { n: s2.hours_pushed || 24 }));
        else paint(false, t(fail, { time: clockNow() }), priceErrText(s2.last_error) || t('price.err.other'));
        return;
      }
      paint(false, t(fail, { time: clockNow() }), t('price.pushPending'));
    } finally {
      btn.removeAttribute('aria-busy');
    }
  }

  document.addEventListener('input', function (e) {
    if (e.target && e.target.closest && e.target.closest('[data-bind-price-sched]')) priceSchedSync();
  });
  document.addEventListener('change', function (e) {
    var n = e.target && e.target.id;
    if (n !== 'price_zone' && n !== 'price_currency') return;
    if (n === 'price_currency') {
      // A new currency starts from its default rate (still editable).
      var opt = e.target.selectedOptions && e.target.selectedOptions[0];
      var fx = qs('#fx');
      if (opt && fx && opt.getAttribute('data-fx')) {
        fx.value = Number(opt.getAttribute('data-fx'));
        fx.dispatchEvent(new Event('input', { bubbles: true }));
      }
    }
    priceSyncForm();
  });
  // Fortryd restores HTML defaults; put the saved price settings back instead.
  document.addEventListener('reset', function (e) {
    var f = e.target;
    if (f === priceForm() && state.prices) setTimeout(function () { fillPriceForm(f, state.prices); }, 0);
  });

  function applyWifi(w) {
    if (!w) return;
    setText('wifi.current', w.connected && w.ssid ? w.ssid : '—');
    var sw = w['switch'];
    var status = (sw && sw !== 'none')
      ? t('wifi.switch.' + sw, { ssid: w.target_ssid || '' })
      : (w.connected ? t('wifi.connectedTo') : (w.ap_active ? t('wifi.apActive') : t('wifi.notConnected')));
    setText('wifi.status', status);
    var ssidEl = qs('#wifi_ssid');
    if (ssidEl && document.activeElement !== ssidEl && !ssidEl.value && w.ssid) ssidEl.value = w.ssid;
    resnapForms(['wifi']);
  }

  function setShow(key, on) {
    qsa('[data-bind-show="' + key + '"]').forEach(function (n) { n.hidden = !on; });
  }

  // What the comfort target sent to Asgard does right now — explicit, because
  // with Odin in control it is a backup, not the thing that drives heating.
  function paintTargetRole() {
    var hs = state.heat || {};
    var ctl = state.heatControl || {};
    var link = ctl.link || {};
    var d = ctl.demand || {};
    var sync = heatSyncOn(hs);
    var odin = d.route === 'odin_schedule' || !!link.forwarder_active || !!((ctl.odin || {}).enabled);
    var txt;
    if (hs.type === 'generic_http') txt = '—';
    else if (odin) {
      if (!sync) txt = t('role.offOdin');
      else if (link.takeover === true) txt = t('role.reserveNow');
      else if (link.takeover === false) txt = t('role.asgardNow');
      else txt = t('role.reserve');
    } else txt = sync ? t('role.drives', { c: num(d.target_uplift_c || 0) }) : t('role.off');
    setText('dash.targetRole', txt);
    setText('dash.hsName', hs.type === 'generic_http' ? t('hs.typeHttp') : t('hs.typeAsgard'));
  }

  // Touch's heating plan, 24 h from now (GET /plan → .plan in the LDS).
  function applyHeatPlan(p) {
    var host = qs('[data-bind-plan]');
    if (!p) return;
    state.plan = p;
    var odin = p.odin || {};
    var heat = odin.heat_kw || [];
    var mode = odin.mode || [];
    var now = '—';
    if (odin.available && heat.length) {
      var kw = Number(heat[0]);
      now = Number(mode[0]) === 1 ? t('now.dhw') : (kw > 0.05 ? t('now.heat', { kw: num(kw) }) : t('now.off'));
    }
    setText('dash.odinNow', now);
    if (!host) return;
    var H = Number(p.hours) || 24;
    var rooms = (p.rooms || []).filter(function (r) { return r && (r.preload || r.charge); });
    var empty = !odin.available && !rooms.length;
    setShow('planG.empty', empty);
    if (empty) { host.innerHTML = ''; return; }
    var start = Number(p.start_hour) || 0;
    var clamp = function (v) { return Math.max(0, Math.min(H, Number(v) || 0)); };
    var span = function (cls, a, b, extra, tip) {
      a = clamp(a); b = clamp(b);
      if (b <= a) return '';
      return '<span class="' + cls + '" style="--a:' + a + ';--b:' + b + '"' + (extra || '') +
        (tip ? ' title="' + esc(tip) + '"' : '') + '></span>';
    };
    var lang = (i18n._lang || document.documentElement.lang || 'en');
    var x = '';
    for (var j = 0; j <= H; j++) {
      var hh = (start + j) % 24;
      if (j === 0) { x += '<span style="left:0%">' + esc(t('fc.now')) + '</span>'; continue; }
      if (hh % 3 !== 0 || j < 2 || j > H - 2) continue;  // keep clear of the "Now" label
      var left = (j / H * 100).toFixed(2) + '%';
      x += hh === 0
        ? '<span class="d" style="left:' + left + '">' + esc(new Date(Date.now() + j * 3600000).toLocaleDateString(lang, { weekday: 'short' })) + '</span>'
        : '<span' + (hh % 6 ? ' class="m"' : '') + ' style="left:' + left + '">' + String(hh).padStart(2, '0') + '</span>';
    }
    var html = '<span></span><div class="plan-x">' + x + '</div>';
    // Odin lane: planned energy per hour + Touch's lift of Odin's comfort band.
    // Space heat = kWh heat (heat_kw); DHW / legionella hours carry no space
    // heat, so their bar is Odin's planned electricity (kWh el).
    if (odin.available) {
      var energy = odin.energy_kwh || [];
      var vals = [];
      var kinds = [];
      for (var q = 0; q < H; q++) {
        var m = Number(mode[q]);
        var kind = m === 1 ? 'dhw' : (m === 6 ? 'legionella' : 'heat');
        kinds.push(kind);
        vals.push(kind === 'heat' ? (Number(heat[q]) || 0) : (Number(energy[q]) || 0));
      }
      var max = 0;
      vals.forEach(function (v) { if (v > max) max = v; });
      // Round the axis up to 1 / 2 / 2.5 / 5 × 10^n.
      var top = 1;
      if (max > 0) {
        var mag = Math.pow(10, Math.floor(Math.log10(max)));
        top = [1, 2, 2.5, 5, 10].map(function (f) { return f * mag; }).filter(function (v) { return v >= max; })[0];
      }
      var bars = '';
      for (var h = 0; h < H; h++) {
        var v = vals[h];
        var pct = Math.round(v / top * 100);
        var label = String((start + h) % 24).padStart(2, '0') + ':00';
        bars += '<i class="plan-bar"' + (kinds[h] !== 'heat' ? ' data-mode="' + kinds[h] + '"' : '') + ' style="--v:' + pct + '" title="' +
          esc(t('planG.tip.' + kinds[h], { h: label, kwh: num(v) })) + '"></i>';
      }
      var lifts = '';
      var lc = odin.lift_c || [];
      for (var a = 0; a < H; a++) {
        if (!(Number(lc[a]) > 0)) continue;
        var b = a;
        while (b < H && Number(lc[b]) > 0) b++;
        lifts += span('plan-lift', a, b, '', t('planG.tipLift', { c: num(lc[a]) }));
        a = b;
      }
      var ticks = '<span class="plan-y"><i>' + esc(num(top)) + ' kWh</i><i>' + esc(num(top / 2)) + '</i><i>0</i></span>';
      html += '<span class="plan-lab plan-lab--y"><b>' + esc(t('planG.odin')) + '</b>' + ticks + '</span>' +
        '<div class="plan-lane plan-lane--odin">' + bars + lifts + '</div>';
    } else {
      html += '<span class="plan-lab">' + esc(t('planG.odin')) + '</span><span class="plan-empty-lane">' + esc(t('planG.noOdin')) + '</span>';
    }
    rooms.forEach(function (r) {
      var segs = '';
      if (r.preload) segs += span('plan-seg', r.preload.from, r.preload.to, ' data-kind="preload"', t('planG.tipPre', { c: num(r.preload.offset_c) }));
      if (r.charge) {
        var ins = !!r.charge.insufficient;
        segs += span('plan-seg', r.charge.from, r.charge.to, ' data-kind="charge"' + (ins ? ' data-insufficient' : ''),
          t(ins ? 'planG.tipInsufficient' : 'planG.tipCharge', { c: num(r.charge.store_c) }));
      }
      html += '<span class="plan-lab" title="' + esc(r.name) + '">' + esc(r.name) + '</span><div class="plan-lane">' + segs + '</div>';
    });
    host.innerHTML = html;
  }

  // Approximate split of the pump flow per manifold (LDS .dist): each loop's
  // conductance is taken as its valve opening; a manifold's share is the sum of
  // its open loops over all open loops. Pipe runs to each manifold are ignored.
  function renderDist() {
    var host = qs('[data-bind-dist]');
    if (!host) return;
    var zones = (state.zones || []).filter(function (z) { return z && !z.unassigned; });
    var byNode = {};
    var total = 0;
    zones.forEach(function (z) {
      var n = Number(z.node_index) || 0;
      var v = Math.max(0, Math.min(100, Number(z.valve_pct) || 0));
      if (!byNode[n]) byNode[n] = { kv: 0, zones: [] };
      byNode[n].kv += v / 100;
      byNode[n].zones.push({ name: z.name || ('Z' + ((Number(z.zone_index) || 0) + 1)), v: v });
      total += v / 100;
    });
    var circ = (state.heat && state.heat.circulation) || {};
    var m3h = circ.flow_m3h != null && isFinite(Number(circ.flow_m3h)) ? Number(circ.flow_m3h) : null;
    var lpmTotal = m3h != null ? m3h * 1000 / 60 : null;
    var nodes = Object.keys(byNode).map(Number).sort(function (a, b) { return a - b; });
    if (!nodes.length || !(total > 0)) {
      host.innerHTML = '<div class="dist-bar"></div><p class="dist-note">' + esc(t('flow.none')) + '</p>';
      return;
    }
    var bar = '';
    var rows = '';
    nodes.forEach(function (n, i) {
      var g = byNode[n];
      var pct = g.kv / total * 100;
      var node = state.nodes[n];
      var name = nodeLabel(node) || ('M' + (n + 1));
      var di = ' data-i="' + (i % 4) + '"';
      bar += '<span class="dist-seg"' + di + ' style="--w:' + pct.toFixed(1) + '" title="' + esc(name + ' · ' + num(pct, 0) + ' %') + '"></span>';
      var zbar = '';
      g.zones.forEach(function (z) {
        var zp = g.kv > 0 ? (z.v / 100) / g.kv * 100 : 0;
        if (zp <= 0) return;
        zbar += '<span class="dist-seg"' + di + ' style="--w:' + zp.toFixed(1) + '" title="' +
          esc(t('flow.zoneTip', { zone: z.name, v: num(z.v, 0), pct: num(zp, 0) })) + '"></span>';
      });
      rows += '<i class="dist-key"' + di + '></i><span class="dist-name">' + esc(name) + '</span>' +
        '<span class="num">' + esc(num(pct, 0)) + ' %</span>' +
        '<span class="num">' + (lpmTotal != null ? esc(num(lpmTotal * pct / 100, 1)) + ' l/min' : '') + '</span>' +
        '<div class="dist-bar dist-bar--thin">' + zbar + '</div>';
    });
    host.innerHTML = '<div class="dist-bar">' + bar + '</div><div class="dist-rows">' + rows + '</div>' +
      '<p class="dist-note">' + esc(t(lpmTotal != null ? 'flow.noteLpm' : 'flow.note')) + '</p>';
  }

  function applyCirculation(circ) {
    if (!circ) return;
    function setIf(id, val) {
      var el = qs('#' + id);
      if (el && document.activeElement !== el && val != null && val !== '') el.value = val;
    }
    setIf('pump_host', circ.host || '');
    setIf('pump_port', circ.port != null ? circ.port : 80);
    setIf('pump_flow_entity', circ.flow_entity || 'pump_flow');
    setIf('pump_head_entity', circ.head_entity || 'pump_head_pressure');
    setIf('pump_power_entity', circ.power_entity || 'pump_power');
  }

  function resnapForms(keys) {
    (keys || []).forEach(function (k) {
      var f = qs('form.panel[data-save="' + k + '"]');
      if (f && f.luneResnap) f.luneResnap();
    });
  }

  function forecastSky(hour) {
    var hod = 12;
    if (hour.timestamp_s) {
      var d = new Date(hour.timestamp_s * 1000);
      hod = d.getHours();
    }
    var cloud = hour.cloud_pct || 0;
    // Precipitation at or below ~0 °C falls as snow (no weather code in the feed).
    if ((hour.precip_mm || 0) > 0.2) return (hour.temp_c != null && hour.temp_c <= 0.5) ? 'snow' : 'rain';
    if (cloud > 70) return 'cloud';
    // Night: no sun above the horizon (falls back to the clock without irradiance).
    var night = hour.solar_wm2 != null ? hour.solar_wm2 < 5 : (hod < 6 || hod >= 20);
    if (night) return 'moon';
    if (cloud < 35) return 'sun';
    return 'partly';
  }

  // Slider over the forecast: a dashed line through both plots that follows the
  // pointer (mouse or finger), with the values for that hour beside it.
  function bindForecastScrub() {
    var root = qs('.fc');
    if (!root || root.dataset.scrubBound) return;
    root.dataset.scrubBound = '1';
    var line = qs('.fc-scrub', root);
    var box = qs('.fc-readout', root);
    var plot = qs('.fc-temp', root);
    if (!line || !box || !plot) return;
    var hide = function () {
      line.hidden = true; box.hidden = true;
      root.removeAttribute('data-scrubbing');
    };
    var show = function (ev) {
      var hours = state.fcHours || [];
      if (hours.length < 2) return;
      var pr = plot.getBoundingClientRect();
      var rr = root.getBoundingClientRect();
      var frac = (ev.clientX - pr.left) / pr.width;
      if (!(frac >= 0 && frac <= 1)) { hide(); return; }
      var i = Math.round(frac * (hours.length - 1));
      var h = hours[i] || {};
      var x = pr.left - rr.left + (i / (hours.length - 1)) * pr.width;
      line.style.left = x + 'px';
      var sky = forecastSky(h);
      var lang = (i18n._lang || document.documentElement.lang || 'en');
      var when = h.timestamp_s
        ? new Date(h.timestamp_s * 1000).toLocaleString(lang, { weekday: 'short', hour: '2-digit', minute: '2-digit' })
        : '';
      var dir = h.wind_dir_deg != null && isFinite(Number(h.wind_dir_deg)) ? ' ' + t('fc.windFrom', { dir: compassDir(Number(h.wind_dir_deg)) }) : '';
      box.innerHTML = '<span><b>' + esc(when) + '</b> · ' + esc(t('fc.sky.' + sky)) + '</span>' +
        '<span class="c-temp">' + esc(num(h.temp_c)) + ' °C</span>' +
        '<span class="c-sun">' + esc(num(h.solar_wm2, 0)) + ' W/m²</span>' +
        '<span class="c-wind">' + esc(num(h.wind_ms, 1)) + ' m/s' + esc(dir) + '</span>';
      box.style.left = x + 'px';
      box.classList.toggle('flip', x > rr.width * 0.66);
      line.hidden = false; box.hidden = false;
      root.setAttribute('data-scrubbing', '');
    };
    qsa('.fc-plot', root).forEach(function (p) {
      p.addEventListener('pointermove', show);
      p.addEventListener('pointerdown', show);
      p.addEventListener('pointerleave', hide);
    });
  }

  function applyForecastCharts(fc) {
    var hours = fc.hours || [];
    // No forecast → the chart collapses to one .empty line (DESIGN.md 5.9).
    var real = hours.filter(function (h) { return h && h.temp_c != null && isFinite(Number(h.temp_c)); }).length;
    var fcPanel = qs('#v-dash-house [data-panel="forecast"]');
    if (fcPanel) fcPanel.toggleAttribute('data-empty', real < 2);
    if (real < 2) return;
    state.fcHours = hours;
    bindForecastScrub();
    var start = (fc.cache && fc.cache.decision_start_index >= 0) ? fc.cache.decision_start_index : 0;
    if (start >= hours.length) start = 0;
    var temps = hours.map(function (h) { return h.temp_c; });
    var winds = hours.map(function (h) { return h.wind_ms; });
    var n = Math.max(temps.length - 1, 1);
    var W = 720;
    var tMin = 6, tMax = 20, wMax = 14;
    temps.forEach(function (v) {
      if (!isFinite(v)) return;
      if (v < tMin) tMin = Math.floor(v);
      if (v > tMax) tMax = Math.ceil(v);
    });
    if (tMax - tMin < 8) {
      var mid = (tMin + tMax) / 2;
      tMin = mid - 4;
      tMax = mid + 4;
    }
    winds.forEach(function (v) {
      if (isFinite(v) && v > wMax) wMax = Math.ceil(v / 2) * 2;
    });
    var tp = seriesPoints(temps, W, 100, tMin, tMax);
    var solar = hours.map(function (h) { return h.solar_wm2 != null ? Number(h.solar_wm2) : 0; });
    var sPeak = solar.reduce(function (m, v) { return isFinite(v) && v > m ? v : m; }, 0);
    var sMax = Math.max(200, Math.ceil(sPeak / 100) * 100);
    var sp = seriesPoints(solar, W, 100, 0, sMax);
    var wp = seriesPoints(winds, W, 60, 0, wMax);
    var nowX = (start / n) * W;
    var svgT = qs('[data-bind-fc="temp"]');
    var svgW = qs('[data-bind-fc="wind"]');
    if (svgT) {
      var tl = svgT.querySelector('polyline.tl');
      if (tl) tl.setAttribute('points', tp);
      var sa = svgT.querySelector('polygon.sa');
      if (sa) sa.setAttribute('points', '0,100 ' + sp + ' ' + W + ',100');
      var sl = svgT.querySelector('polyline.sl');
      if (sl) sl.setAttribute('points', sp);
      var past = svgT.querySelector('rect.past');
      if (past) past.setAttribute('width', String(nowX));
      var nowLine = svgT.querySelector('line.now');
      if (nowLine) {
        nowLine.setAttribute('x1', String(nowX));
        nowLine.setAttribute('x2', String(nowX));
      }
    }
    if (svgW) {
      var wl = svgW.querySelector('polyline.wl');
      if (wl) wl.setAttribute('points', wp);
      var wa = svgW.querySelector('polygon.wa');
      if (wa) wa.setAttribute('points', '0,60 ' + wp + ' ' + W + ',60');
      var pastW = svgW.querySelector('rect.past');
      if (pastW) pastW.setAttribute('width', String(nowX));
      var nowW = svgW.querySelector('line.now');
      if (nowW) {
        nowW.setAttribute('x1', String(nowX));
        nowW.setAttribute('x2', String(nowX));
      }
      var thr = svgW.querySelector('line.thr');
      if (thr) {
        var thrY = 60 - (8 / wMax) * 60;
        thr.setAttribute('y1', String(thrY));
        thr.setAttribute('y2', String(thrY));
      }
    }
    var fcRoot = qs('.fc');
    if (fcRoot) fcRoot.style.setProperty('--now', ((start / Math.max(hours.length, 1)) * 100).toFixed(3) + '%');

    var yTemp = qs('.fc-temp') && qs('.fc-temp').previousElementSibling;
    if (yTemp && yTemp.classList.contains('fc-y')) {
      yTemp.innerHTML = '<span>' + Math.round(tMax) + '°</span><span>' + Math.round((tMin + tMax) / 2) + '°</span><span>' + Math.round(tMin) + '°</span>';
    }
    var yWind = qs('.fc-wind') && qs('.fc-wind').previousElementSibling;
    if (yWind && yWind.classList.contains('fc-y')) {
      yWind.innerHTML = '<span>' + Math.round(wMax) + '</span><span>' + Math.round(wMax / 2) + '</span><span>0</span>';
    }

    var y2 = qs('.fc-temp') && qs('.fc-temp').nextElementSibling;
    if (y2 && y2.classList.contains('fc-y2')) {
      y2.innerHTML = '<span>' + sMax + ' W/m²</span><span>' + (sMax / 2) + '</span><span>0</span>';
    }

    // Header: one weather icon per hour (every 3rd on narrow screens, CSS).
    var icons = qs('.fc-icons');
    if (icons) {
      var hourly = icons.hasAttribute('data-hourly');
      var step = hourly ? 1 : 3;
      icons.style.setProperty('--fc-cols', String(Math.ceil(hours.length / step)));
      var html = '';
      for (var i = 0; i < hours.length; i += step) {
        var sky = forecastSky(hours[i]);
        html += '<svg class="ic ' + sky + '"><title>' + esc(t('fc.sky.' + sky)) + '</title><use href="#i-' + sky + '"/></svg>';
      }
      icons.innerHTML = html;
    }

    var clock = function (h) {
      if (!h || !h.timestamp_s) return '';
      var d = new Date(h.timestamp_s * 1000);
      return String(d.getHours()).padStart(2, '0') + ':00';
    };
    // Wind direction every 3 h. Meteorological degrees say where the wind comes
    // FROM; the arrow points where it blows TO.
    var dirs = qs('.fc-dirs');
    if (dirs) {
      dirs.style.setProperty('--fc-dirs', String(Math.ceil(hours.length / 3)));
      var dh = '';
      for (var k = 0; k < hours.length; k += 3) {
        var h = hours[k];
        var deg = h.wind_dir_deg != null ? Number(h.wind_dir_deg) : NaN;
        if (!isFinite(deg)) { dh += '<span></span>'; continue; }
        var tip = t('fc.dirTitle', { time: clock(h), speed: num(h.wind_ms, 0), dir: compassDir(deg) });
        dh += '<svg class="dir" viewBox="0 0 24 24" style="--deg:' + ((deg + 180) % 360) + 'deg"><title>' + esc(tip) + '</title><use href="#i-arrow"/></svg>';
      }
      dirs.innerHTML = dh;
    }

    // x-axis on the clock every 3 h: weekday at midnight, hour otherwise.
    // 03/09/15/21 are minor ticks (.m) that narrow screens hide.
    var xaxis = qs('.fc-x');
    if (xaxis && hours[0] && hours[0].timestamp_s) {
      var lang = (i18n._lang || document.documentElement.lang || 'en');
      var t0 = hours[0].timestamp_s;
      var span = Math.max(hours.length - 1, 1);
      var h0 = new Date(t0 * 1000).getHours();
      var xs = '';
      for (var j = (3 - (h0 % 3)) % 3; j <= span; j += 3) {
        var dd = new Date((t0 + j * 3600) * 1000);
        var hh = dd.getHours();
        var left = (j / span * 100).toFixed(2) + '%';
        xs += hh === 0
          ? '<span class="d" style="left:' + left + '">' + esc(dd.toLocaleDateString(lang, { weekday: 'short' })) + '</span>'
          : '<span' + (hh % 6 ? ' class="m"' : '') + ' style="left:' + left + '">' + String(hh).padStart(2, '0') + '</span>';
      }
      xaxis.classList.add('fc-x--abs');
      xaxis.innerHTML = xs;
    }

    // Preload window from active decisions (hours relative to decision start).
    var preStart = null, preEnd = null;
    (fc.decisions || []).forEach(function (d) {
      if (!d || !d.active) return;
      var a = start + (d.preload_start_h || 0);
      var b = start + (d.preload_end_h != null ? d.preload_end_h : (d.preload_start_h || 0) + 1);
      if (preStart == null || a < preStart) preStart = a;
      if (preEnd == null || b > preEnd) preEnd = b;
    });
    qsa('.fc .pre').forEach(function (rect) {
      if (preStart == null || preEnd == null || preEnd <= preStart) {
        rect.setAttribute('width', '0');
        return;
      }
      var x0 = (preStart / n) * W;
      var x1 = (preEnd / n) * W;
      rect.setAttribute('x', String(x0));
      rect.setAttribute('width', String(Math.max(0, x1 - x0)));
    });
  }

  function applyPlan(fc) {
    var plan = (fc && fc.plan_vs_reality) || [];
    if (plan.length) {
      var last = plan[plan.length - 1];
      var kw = function (v) { var x = num(v); return x === '—' ? x : x + ' <small>kW</small>'; };
      setBind('plan.planned', kw(last.planned_kw));
      setBind('plan.actual', kw(last.actual_kw));
    } else {
      setBind('plan.planned', '—');
      setBind('plan.actual', '—');
    }
    var max = 0;
    plan.forEach(function (p) {
      var a = Math.abs(Number(p.planned_kw) || 0);
      var b = Math.abs(Number(p.actual_kw) || 0);
      if (a > max) max = a;
      if (b > max) max = b;
    });
    if (max <= 0) max = 1;
    qsa('[data-bind-plan]').forEach(function (bar) {
      var i = Number(bar.getAttribute('data-bind-plan'));
      var row = plan[i];
      var v = row ? Math.round((Math.abs(Number(row.actual_kw) || Number(row.planned_kw) || 0) / max) * 100) : 0;
      bar.style.setProperty('--v', v + '%');
      bar.setAttribute('aria-valuenow', String(v));
    });
  }

  function applyForecast(fc) {
    if (!fc) return;
    state.forecast = fc;
    if (state.boardsByM) renderBoards(state.boardsByM);  // charge badges follow the forecast
    var hours = fc.hours || [];
    var start = (fc.cache && fc.cache.decision_start_index >= 0) ? fc.cache.decision_start_index : 0;
    var nowHour = hours[start] || hours[0];
    if (nowHour) {
      setBind('forecast.temp', num(nowHour.temp_c) + ' <small>°C</small>');
      if (nowHour.temp_c != null && isFinite(Number(nowHour.temp_c))) {
        setText('house.outdoor', num(nowHour.temp_c) + ' °C');
      }
      var dir = compassDir(nowHour.wind_dir_deg);
      var windHtml = num(nowHour.wind_ms, 0) + ' <small>m/s</small>';
      if (dir) {
        windHtml += ' <svg class="dir" viewBox="0 0 16 16" style="--deg:' + Number(nowHour.wind_dir_deg) + 'deg" aria-label="' +
          t('fc.windFrom', { dir: dir }) + '"><path d="M8 2v12M8 2l-4 4M8 2l4 4"/></svg><small>' + dir + '</small>';
      }
      qsa('[data-bind="forecast.wind"]').forEach(function (n) { n.innerHTML = windHtml; });
    }
    if (fc.cache) {
      setBind('forecast.windmax', num(fc.cache.max_wind_ms, 0) + ' <small>m/s</small>');
      setBind('forecast.tmin', num(fc.cache.min_temp_c) + ' <small>°C</small>');
    }
    var active = (fc.decisions || []).filter(function (d) { return d && d.active && d.offset_c > 0; });
    var head = qs('#v-dash-house [data-bind="fc.sub"]');
    // Preload badge only while a preload is active (never "+— °C").
    var fcBadge = qs('#v-dash-house [data-panel="forecast"] .badge.info');
    if (fcBadge) fcBadge.hidden = !active.length;
    if (active.length) {
      var maxOff = active.reduce(function (m, d) { return Math.max(m, Number(d.offset_c) || 0); }, 0);
      if (fcBadge) fcBadge.textContent = t('fc.badge', { v: num(maxOff) });
      var names = active.slice(0, 3).map(function (d) { return d.name || d.room_id; }).join(', ');
      var msg = qs('#v-dash-house [data-panel="forecast"] .msg span');
      if (msg) {
        msg.innerHTML = '<b>' + t('fc.badge', { v: num(maxOff) }) + '</b> ' + names;
      }
    }
    if (head && fc.cache) {
      var ageMin = fc.last_fetch_age_s != null ? Math.round(Number(fc.last_fetch_age_s) / 60) : null;
      var when = ageMin != null ? (ageMin + ' min') : '—';
      head.textContent = t('fc.sub', { model: fc.cache.provider_timezone || 'Open-Meteo', time: when });
    }
    applyForecastCharts(fc);
    applyPlan(fc);
  }

  function applyCommands(data) {
    var commands = (data && data.commands) || [];
    var log = qs('[data-bind="log"]');
    if (log) {
      if (!commands.length) {
        log.textContent = '—';
      } else {
        log.innerHTML = commands.slice(0, 12).map(function (c) {
          var ts = '—';
          if (c.created_at_epoch_s) {
            var d = new Date(Number(c.created_at_epoch_s) * 1000);
            ts = String(d.getHours()).padStart(2, '0') + ':' + String(d.getMinutes()).padStart(2, '0');
          }
          var kind = c.source || 'cmd';
          var detail = c.reason || c.result || '';
          if (c.room_id) detail += (detail ? ' · ' : '') + c.room_id;
          var cls = kind === 'forecast' ? 'info' : (c.result && String(c.result).indexOf('fail') === 0 ? 'bad' : '');
          return ts + '  <span class="' + cls + '">' + kind + '</span> ' + detail;
        }).join('\n');
      }
    }
    var activity = qs('[data-bind-activity]');
    if (activity) {
      var by = {};
      commands.forEach(function (c) {
        var k = c.source || 'other';
        by[k] = (by[k] || 0) + 1;
      });
      activity.innerHTML = Object.keys(by).map(function (k) {
        return '<div class="metric"><dt>' + k + '</dt><dd>' + by[k] + '</dd></div>';
      }).join('') || '';
    }
  }

  function applySettings(s) {
    if (!s) return;
    state.settings = s;
    var coord = s.coordinator || {};
    var name = coord.name || s.device_name || s.name || 'Lune Touch';
    var place = coord.site_label || s.location || s.place || name;
    setText('device.about.name', name);
    setText('device.about.place', place);
    var dn = qs('#dev_name');
    if (dn && document.activeElement !== dn) dn.value = name;
    var idle = qs('#dev_idle');
    var idleS = (s.display && s.display.idle_timeout_s != null) ? Number(s.display.idle_timeout_s)
      : (s.display_idle_min != null ? Number(s.display_idle_min) * 60 : NaN);
    if (idle && document.activeElement !== idle && isFinite(idleS)) {
      idle.value = String(Math.round(idleS / 60));
    }
    var wx = s.weather || s.forecast || {};
    var lat = qs('#wx_lat'); var lon = qs('#wx_lon'); var boost = qs('#wx_boost');
    if (s.forecast) {
      if (lat && document.activeElement !== lat && s.forecast.latitude != null) lat.value = s.forecast.latitude;
      if (lon && document.activeElement !== lon && s.forecast.longitude != null) lon.value = s.forecast.longitude;
    }
    if (boost && document.activeElement !== boost && wx.max_boost_c != null) {
      boost.value = Number(wx.max_boost_c).toFixed(1);
    }
  }

  function formatUptime(sec) {
    if (sec == null || sec !== sec) return '—';
    var s = Math.max(0, Math.floor(Number(sec)));
    var d = Math.floor(s / 86400);
    if (d >= 1) return d + ' <small>' + t('common.days') + '</small>';
    var h = Math.floor(s / 3600);
    if (h >= 1) return h + ' <small>h</small>';
    return Math.floor(s / 60) + ' <small>min</small>';
  }

  function applyDiagnostics(d) {
    if (!d) return;
    state.diagnostics = d;
    var net = d.network || d;
    // Never show raw firmware strings (DESIGN.md 6.8): translate, keep the raw
    // error only as a tooltip for installers.
    var com = d.commissioning || {};
    setText('diag.nodes', com.trusted_nodes != null
      ? String(com.reachable_trusted_nodes || 0) + ' / ' + String(com.trusted_nodes) : '—');
    var poll = d.polling || {};
    var pollEl = qs('[data-bind="diag.poll"]');
    if (pollEl) {
      var fails = Number(poll.fail) || 0;
      pollEl.textContent = poll.fail == null ? '—' : (fails ? t('diag.pollFail', { n: fails }) : t('diag.pollOk'));
      pollEl.className = fails ? 'c-warn' : '';
      if (poll.last_error) pollEl.title = poll.last_error; else pollEl.removeAttribute('title');
    }
    var ota = d.ota;
    var otaState = typeof ota === 'string' ? ota : (ota && ota.state);
    setText('diag.ota', otaState ? (i18n['diag.ota.' + otaState] || t('status.unknown')) : '—');
    if (net.ip) {
      setText('device.about.ip', net.ip);
      if (state.ownIp !== net.ip) { state.ownIp = net.ip; renderDeviceMenu(); }
    }
    if (net.mac) setText('device.about.mac', net.mac);
    if (net.version || net.firmware) {
      var fw = net.version || net.firmware;
      state.fwInstalled = fw;
      setText('device.about.firmware', fw);
      setText('fw.installed', fw);
    }
    if (net.esphome) setText('device.about.esphome', net.esphome);
    if (net.uptime_s != null || net.uptime != null) {
      setBind('device.about.uptime', formatUptime(net.uptime_s != null ? net.uptime_s : net.uptime));
    }
  }

  function copyDiagnostics(btn) {
    var lines = [];
    qsa('.device-about .kv div').forEach(function (row) {
      var dt = row.querySelector('dt');
      var dd = row.querySelector('dd');
      if (dt && dd) lines.push(dt.textContent + ': ' + dd.textContent.replace(/\s+/g, ' ').trim());
    });
    var text = lines.join('\n');
    function done() {
      if (!btn) return;
      var prev = btn.textContent;
      btn.textContent = t('device.copied');
      setTimeout(function () { btn.textContent = prev; }, 1600);
    }
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(done).catch(function () {
        window.prompt(t('device.copyDiag'), text);
      });
    } else {
      window.prompt(t('device.copyDiag'), text);
      done();
    }
  }

  function applyDeviceList(nodes) {
    var nav = qs('[data-bind-devices]');
    if (!nav) return;
    var self = '<a href="/" aria-current="page"><i></i>Lune Touch<small>' + t('device.this') + '</small></a>';
    var others = (nodes || []).filter(function (n) { return n.hostname || n.ip; }).map(function (n) {
      var host = n.hostname || n.ip;
      var href = host.indexOf('http') === 0 ? host : 'http://' + host + '/';
      var label = nodeLabel(n) || t('common.unnamed');
      return '<a href="' + esc(href) + '"><i></i>' + esc(label) + '<small>' + esc(host) + '</small></a>';
    }).join('');
    nav.innerHTML = self + others;
  }

  async function loadComfortCharts(rooms) {
    // Fetch forward comfort charts for visible rooms (one at a time budget).
    var selected = qs('input[name="scope"]:checked');
    var want = [];
    if (selected && selected.id && selected.id.indexOf('s-r') === 0) {
      var idx = Number(selected.id.slice(3));
      if (rooms[idx - 1]) want.push({ index: idx, room: rooms[idx - 1] });
    }
    if (!want.length && rooms[0]) want.push({ index: 1, room: rooms[0] });
    for (var i = 0; i < want.length; i++) {
      var item = want[i];
      if (!item.room.room_id) continue;
      try {
        var data = await get('/zones/' + encodeURIComponent(item.room.room_id) + '/comfort-chart');
        var chart = data && data.comfort_chart;
        if (!chart || !chart.hours) continue;
        var expected = chart.expected_temp_c || [];
        var scheduled = chart.scheduled_setpoint_c || [];
        if (expected.length) {
          updateSvgSeries(qs('[data-bind-trend="r' + item.index + '"]'), expected, scheduled);
          updateSvgSeries(qs('[data-bind-spark="r' + item.index + '"]'), expected, scheduled);
          state.chartsLoaded[item.room.room_id] = true;
        }
      } catch (e) { /* keep live flat series */ }
    }
  }

  async function refresh() {
    try {
      var paths = ['/overview', '/nodes', '/zones', '/strategy', '/forecast', '/plan', '/heat-source', '/heat-source/control', '/odin/mqtt', '/prices', '/settings', '/diagnostics', '/commands', '/wifi'];
      var results = await Promise.all(paths.map(function (p) {
        return get(p).then(function (v) { return { p: p, v: v }; }).catch(function () { return { p: p, v: null }; });
      }));
      var map = {};
      results.forEach(function (r) { map[r.p] = r.v; });
      applyOverview(map['/overview']);
      state.strategy = map['/strategy'] || state.strategy;
      var nodes = (map['/nodes'] && map['/nodes'].nodes) || [];
      applyNodes(nodes);
      applyHierarchy(map['/zones'] || {}, nodes);
      applyForecast(map['/forecast']);
      applyHeat(map['/heat-source']);
      applyHeatPlan(map['/plan']);
      applyHeatControl(map['/heat-source/control']);
      applyOdinMqtt(map['/odin/mqtt']);
      applyPrices(map['/prices']);
      applyWifi(map['/wifi']);
      applySettings(map['/settings']);
      applyDiagnostics(map['/diagnostics']);
      applyCommands(map['/commands']);
      fillHouseClimateFallback();
      loadComfortCharts(state.rooms);
    } catch (e) {
      console.warn('refresh failed', e);
    }
  }

  function fillHouseClimateFallback() {
    var el = qs('[data-bind="house.temp"]');
    var text = el ? (el.textContent || '').replace(/\s+/g, '') : '';
    var blank = !el || text === '' || text === '—' || text === '—°C' || text === '—°';
    if (!blank) return;
    var zones = state.zones || [];
    var sum = 0, n = 0, tSum = 0, tN = 0;
    zones.forEach(function (z) {
      var incl = z.include_in_house_temperature;
      if (incl == null && z.room) incl = z.room.include_in_house_temperature;
      if (incl === false) return;
      if (z.temperature_c != null && isFinite(Number(z.temperature_c))) {
        sum += Number(z.temperature_c);
        n++;
      }
      var sp = (z.comfort && z.comfort.setpoint_c != null && Number(z.comfort.setpoint_c) > 5)
        ? z.comfort.setpoint_c
        : ((z.comfort && z.comfort.effective_setpoint_c != null && Number(z.comfort.effective_setpoint_c) > 5)
          ? z.comfort.effective_setpoint_c
          : z.setpoint_c);
      if (sp != null && isFinite(Number(sp)) && Number(sp) > 5) {
        tSum += Number(sp);
        tN++;
      }
    });
    if (n > 0) {
      applyOverview({
        house_temp_c: sum / n,
        house_target_c: tN > 0 ? tSum / tN : (state.overview && state.overview.house_target_c),
        calling_rooms: state.overview && state.overview.calling_rooms,
        contributing_manifolds: state.overview && state.overview.contributing_manifolds,
        expected_manifolds: state.overview && state.overview.expected_manifolds,
        authority: state.overview && state.overview.authority,
        summary: state.overview && state.overview.summary
      });
    }
  }

  function flash(form, ok) {
    var foot = form && form.querySelector('.panel-foot');
    if (!foot) return;
    var msg = document.createElement('span');
    msg.className = 'msg ' + (ok ? 'ok' : 'bad');
    msg.textContent = ok ? t('rt.savedOk') : t('rt.saveFailed');
    foot.appendChild(msg);
    setTimeout(function () { msg.remove(); }, 2500);
  }

  function formStatus(form, msg, ok) {
    if (!form) return;
    var foot = form.querySelector('.panel-foot') || form.querySelector('.actions');
    if (!foot) return;
    var old = form.querySelector('.fw-status');
    if (old) old.remove();
    if (!msg) return;
    var el = document.createElement('span');
    el.className = 'msg ' + (ok === false ? 'bad' : (ok ? 'ok' : ''));
    el.classList.add('fw-status');
    el.textContent = msg;
    if (foot.classList.contains('actions')) foot.parentNode.insertBefore(el, foot.nextSibling);
    else foot.insertBefore(el, foot.firstChild);
  }

  function normVer(s) {
    return String(s || '').replace(/^v/i, '').trim();
  }

  function verNewer(latest, installed) {
    var a = normVer(latest).split(/[.+-]/);
    var b = normVer(installed).split(/[.+-]/);
    var n = Math.max(a.length, b.length);
    for (var i = 0; i < n; i++) {
      var x = parseInt(a[i], 10); var y = parseInt(b[i], 10);
      if (!isFinite(x)) x = 0;
      if (!isFinite(y)) y = 0;
      if (x > y) return true;
      if (x < y) return false;
    }
    return normVer(latest) !== normVer(installed) && String(latest || '').length > 0;
  }

  function pickReleaseAsset(assets, tag) {
    var list = Array.isArray(assets) ? assets : [];
    function named(re) {
      for (var i = 0; i < list.length; i++) {
        if (re.test(String(list[i] && list[i].name || ''))) return list[i];
      }
      return null;
    }
    var asset = named(/^lune-touch.*\.ota\.bin$/i) || named(/\.ota\.bin$/i) || named(/\.bin$/i);
    if (asset && asset.browser_download_url) {
      return { name: String(asset.name), url: String(asset.browser_download_url) };
    }
    var name = 'lune-touch-' + (tag || 'latest') + '.ota.bin';
    return {
      name: name,
      url: 'https://github.com/birkemosen/lune-coordinator/releases/latest/download/' + name
    };
  }

  async function fetchLatestRelease() {
    if (window.LUNE_TOUCH_MOCK) {
      return {
        tag: 'v0.2.0',
        asset: {
          name: 'lune-touch-v0.2.0.ota.bin',
          url: 'mock://lune-touch-v0.2.0.ota.bin'
        }
      };
    }
    var res = await fetch(RELEASE_LATEST_API, {
      cache: 'no-store',
      headers: { Accept: 'application/vnd.github+json' }
    });
    if (res.status === 404) {
      var err404 = new Error('no_releases');
      err404.code = 'no_releases';
      throw err404;
    }
    if (!res.ok) {
      var err = new Error('http_' + res.status);
      err.code = 'http';
      throw err;
    }
    var payload = await res.json();
    var tag = String(payload && payload.tag_name || '');
    if (!tag) {
      var empty = new Error('no_releases');
      empty.code = 'no_releases';
      throw empty;
    }
    return { tag: tag, asset: pickReleaseAsset(payload.assets, tag) };
  }

  function syncFwBackupButtons() {
    var ota = qs('#ota_file');
    var up = qs('form[data-save="firmware"] button[value="upload"]');
    if (up) up.disabled = !(ota && ota.files && ota.files[0]);
    var bf = qs('#backup_file');
    var imp = qs('form[data-save="backup"] button[value="import"]');
    if (imp) imp.disabled = !(bf && bf.files && bf.files[0]);
    var inst = qs('form[data-save="firmware"] button[value="install"]');
    if (inst) inst.disabled = !(state.fwAsset && state.fwAsset.url && verNewer(state.fwLatest, state.fwInstalled));
  }

  function uploadFirmware(file, onProgress) {
    if (window.LUNE_TOUCH_MOCK) {
      return new Promise(function (resolve) {
        var pct = 0;
        var step = setInterval(function () {
          pct = Math.min(100, pct + 25);
          if (onProgress) onProgress(pct);
          if (pct >= 100) { clearInterval(step); resolve('ok'); }
        }, 120);
      });
    }
    return new Promise(function (resolve, reject) {
      var body = new FormData();
      body.append('update', file, file.name || 'firmware.bin');
      var req = new XMLHttpRequest();
      req.open('POST', OTA_UPLOAD_PATH);
      req.upload.onprogress = function (ev) {
        if (onProgress && ev.lengthComputable) onProgress(Math.min(100, Math.round(ev.loaded / ev.total * 100)));
      };
      req.onload = function () {
        var text = String(req.responseText || '');
        if (req.status >= 200 && req.status < 300 && !/fail/i.test(text)) resolve(text || 'ok');
        else reject(new Error('OTA upload rejected: ' + req.status + ' ' + text));
      };
      req.onerror = function () { reject(new Error('OTA upload connection lost')); };
      req.send(body);
    });
  }

  async function buildBackupEnvelope() {
    var paths = ['/settings', '/heat-source', '/circulation', '/forecast'];
    var parts = await Promise.all(paths.map(function (p) {
      return get(p).then(function (v) { return { p: p, v: v }; }).catch(function () { return { p: p, v: null }; });
    }));
    var map = {};
    parts.forEach(function (r) { map[r.p] = r.v; });
    return {
      _type: BACKUP_TYPE,
      _version: BACKUP_VERSION,
      exported_at: new Date().toISOString(),
      firmware: state.fwInstalled || '',
      settings: map['/settings'] || {},
      heat_source: map['/heat-source'] || {},
      circulation: map['/circulation'] || {},
      forecast: map['/forecast'] || {}
    };
  }

  function downloadJson(filename, obj) {
    var blob = new Blob([JSON.stringify(obj, null, 2)], { type: 'application/json' });
    var a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = filename;
    a.click();
    setTimeout(function () { URL.revokeObjectURL(a.href); }, 1000);
  }

  function unwrapBackup(payload) {
    if (payload && payload._type) return payload;
    if (payload && payload.data && payload.data._type) return payload.data;
    return payload && payload.data ? payload.data : payload;
  }

  async function applyBackupEnvelope(envelope) {
    var env = unwrapBackup(envelope);
    if (!env || env._type !== BACKUP_TYPE) throw new Error('not_a_lune_backup');
    var applied = 0;
    var s = env.settings || {};
    var coord = s.coordinator || {};
    var display = s.display || {};
    if (coord.name != null || display.idle_timeout_s != null) {
      await post('/settings', {
        name: coord.name || '',
        display_idle_timeout_s: display.idle_timeout_s != null ? display.idle_timeout_s : 300
      });
      applied++;
    }
    var hs = env.heat_source || {};
    if (hs && (hs.type || hs.host || (hs.asgard && hs.asgard.host))) {
      var uiType = (hs.type === 'generic_http' || hs.type === 'http') ? 'generic_http' : 'asgard';
      var payload = {
        type: uiType,
        enabled: hs.enabled ? '1' : '0',
        host: hs.host || (hs.asgard && hs.asgard.host) || '',
        port: hs.port || 80,
        weighted_temperature_variable: hs.weighted_temperature_variable || '',
        push_interval_s: hs.push_interval_s || 60
      };
      if (uiType === 'generic_http') {
        payload.write_url_template = hs.write_url_template || '';
        payload.read_url_template = hs.read_url_template || '';
      } else {
        payload.climate_entity = hs.climate_entity || '';
        payload.target_sync_enabled = hs.target_sync_enabled ? '1' : '0';
        payload.odin_plan_enabled = hs.odin_plan_enabled ? '1' : '0';
      }
      if (payload.host) { await post('/heat-source/settings', payload); applied++; }
    }
    var circ = env.circulation || {};
    if (circ.host) {
      await post('/circulation/settings', {
        enabled: '1',
        host: circ.host || '',
        port: circ.port || 80,
        flow_entity: circ.flow_entity || '',
        head_entity: circ.head_entity || '',
        power_entity: circ.power_entity || ''
      });
      applied++;
    }
    var fc = env.forecast || {};
    var wx = (env.settings && env.settings.weather) || {};
    if (fc.latitude != null || fc.longitude != null || wx.max_boost_c != null) {
      await post('/forecast/settings', {
        latitude: fc.latitude != null ? fc.latitude : '',
        longitude: fc.longitude != null ? fc.longitude : '',
        max_boost_c: fc.max_boost_c != null ? fc.max_boost_c : (wx.max_boost_c != null ? wx.max_boost_c : 1.5)
      });
      applied++;
    }
    return { applied: applied };
  }

  async function onSave(ev) {
    var form = ev.target;
    var key = form.getAttribute('data-save');
    var data = formObj(new FormData(form, ev.submitter));
    var room = form.getAttribute('data-room');
    try {
      if (key === 'house-target') {
        await post('/strategy', { house_target_c: data.house_target });
      } else if (key === 'room-target' && room) {
        var roomId = (state.rooms[Number(room) - 1] || {}).room_id || ('room-' + String(room).padStart(2, '0'));
        await post('/zones/' + encodeURIComponent(roomId) + '/comfort', { setpoint_c: data['r' + room + '_target'] });
      } else if (key === 'room' && room) {
        var rid = (state.rooms[Number(room) - 1] || {}).room_id || ('room-' + String(room).padStart(2, '0'));
        await post('/zones/' + encodeURIComponent(rid) + '/room', {
          name: data.name, area_m2: data['r' + room + '_area'], merge: data.merge
        });
      } else if (key === 'room-factors' && room) {
        var rid2 = (state.rooms[Number(room) - 1] || {}).room_id || ('room-' + String(room).padStart(2, '0'));
        var walls = 0;
        ['n', 'e', 's', 'w'].forEach(function (k, i) { if (data['wall_' + k]) walls |= (1 << i); });
        await post('/zones/' + encodeURIComponent(rid2) + '/forecast-profile', {
          exterior_walls: walls,
          wind_exposure: data['r' + room + '_wind'],
          solar_gain: data['r' + room + '_solar'],
          include_in_house_temperature: data['r' + room + '_house'] ? '1' : '0'
        });
      } else if (key === 'rooms') {
        await saveRooms(data);
        await refresh();
      } else if (key === 'add-node') {
        var addr = String(data.host || '').trim();
        if (!addr) throw new Error(t('ctrl.host'));
        var nodePayload = /^\d{1,3}(\.\d{1,3}){3}$/.test(addr) ? { ip: addr } : { hostname: addr };
        var storedNode = await post('/nodes', nodePayload);
        var newId = storedNode && storedNode.node_id;
        if (data.name && newId) await post('/nodes/' + encodeURIComponent(newId) + '/profile', { name: String(data.name).trim() });
      } else if (key === 'heat-source') {
        syncHsTypedFields();
        var uiType = data.hs_type || (qs('input[name="hs_type"]:checked') || {}).value || 'asgard';
        var apiType = uiType === 'http' ? 'generic_http' : 'asgard';
        var payload = {
          type: apiType,
          enabled: data.enabled ? '1' : '0',
          host: data[uiType === 'http' ? 'http_host' : 'asgard_host'] || data.host || '',
          port: data[uiType === 'http' ? 'http_port' : 'asgard_port'] || data.port || 80,
          weighted_temperature_variable: data[uiType === 'http' ? 'http_weighted_temperature_variable' : 'asgard_weighted_temperature_variable'] || data.weighted_temperature_variable || '',
          push_interval_s: data[uiType === 'http' ? 'http_push_interval_s' : 'asgard_push_interval_s'] || 60
        };
        if (!payload.host) throw new Error(t('hs.host') + ': —');
        if (apiType === 'generic_http') {
          payload.write_url_template = data.write_url_template || '';
          payload.read_url_template = data.read_url_template || '';
          if (!payload.write_url_template || !payload.read_url_template) {
            throw new Error(t('hs.writeUrl') + ' / ' + t('hs.readUrl'));
          }
        } else {
          payload.climate_entity = data.climate_entity || '';
          payload.target_sync_enabled = data.target_sync_enabled ? '1' : '0';
          payload.odin_plan_enabled = data.odin_plan_enabled ? '1' : '0';
          payload.odin_host = String(data.odin_host || '').trim();
          payload.mqtt_enabled = data.mqtt_enabled ? '1' : '0';
          payload.mqtt_host = String(data.mqtt_host || '').trim();
          payload.mqtt_port = data.mqtt_port || 1883;
          payload.mqtt_username = String(data.mqtt_username || '');
          payload.mqtt_topic_prefix = String(data.mqtt_topic_prefix || '').trim();
          payload.mqtt_hp_id = String(data.mqtt_hp_id || '').trim();
          // Empty password field = keep the stored one (never echoed back).
          if (data.mqtt_password) payload.mqtt_password = String(data.mqtt_password);
          payload.write_url_template = '';
          payload.read_url_template = '';
        }
        await post('/heat-source/settings', payload);
        var control = apiType === 'generic_http' ? {
          target_url_template: data.target_url_template || '',
          heat_request_url_template: data.heat_request_url_template || '',
          curve_offset_url_template: data.curve_offset_url_template || '',
          curve_gain: data.curve_gain != null ? data.curve_gain : 2,
          curve_max_offset_c: data.curve_max_offset_c != null ? data.curve_max_offset_c : 5
        } : {
          odin_enabled: data.odin_control_enabled ? '1' : '0',
          odin_max_lift_c: data.odin_max_lift_c != null ? data.odin_max_lift_c : 1.5
        };
        var saved = await post('/heat-source/control', control);
        if (saved && saved.control) applyHeatControl(saved.control);
      } else if (key === 'zone-target') {
        var zc = currentZone();
        if (zc && zc.room_id) await post('/zones/' + encodeURIComponent(zc.room_id) + '/comfort', { setpoint_c: data.zone_target });
      } else if (key === 'zone') {
        var z1 = currentZone();
        if (z1 && z1.room_id) await post('/zones/' + encodeURIComponent(z1.room_id) + '/room', { name: data.name, area_m2: data.zone_area });
      } else if (key === 'zone-factors') {
        var z2 = currentZone();
        if (z2 && z2.room_id) {
          var walls = 0;
          ['n', 'e', 's', 'w'].forEach(function (k, i) { if (data['wall_' + k]) walls |= (1 << i); });
          await post('/zones/' + encodeURIComponent(z2.room_id) + '/forecast-profile', {
            exterior_walls: walls,
            wind_exposure: data.zone_wind,
            solar_gain: data.zone_solar,
            include_in_house_temperature: data.zone_house ? '1' : '0'
          });
        }
      } else if (key === 'manifold') {
        var mn = state.selected.m;
        var node = mn != null ? state.nodes[mn - 1] : null;
        if (node && node.id) await post('/nodes/' + encodeURIComponent(node.id) + '/profile', { name: data.name });
      } else if (key === 'circulation') {
        await post('/circulation/settings', {
          enabled: '1',
          host: data.host || '',
          port: data.pump_port || data.port || 80,
          flow_entity: data.flow_entity || '',
          head_entity: data.head_entity || '',
          power_entity: data.power_entity || ''
        });
      } else if (key === 'prices') {
        priceSchedSync();
        var hiddenSched = form.querySelector('input[name="grid_schedule"]');
        var pricePayload = {
          enabled: data.enabled ? '1' : '0',
          model: data.model || 'touch',
          zone: data.zone || 'DK1',
          spot_source: data.spot_source || 'eds',
          spot_fixed_eur: data.spot_fixed_eur,
          currency: data.currency || 'DKK',
          fx: data.fx,
          grid_source: data.grid_source || 'none',
          grid_gln: String(data.grid_gln || '').trim(),
          grid_code: String(data.grid_code || '').trim(),
          grid_schedule: hiddenSched ? hiddenSched.value : '',
          system_source: data.system_source || 'fixed',
          system_fixed: data.system_fixed,
          energy_tax: data.energy_tax,
          markup: data.markup,
          vat_pct: data.vat_pct,
          odin_mode: data.odin_mode || 'dynamic',
          odin_source: data.odin_source || 'energy_charts',
          odin_fixed_price: data.odin_fixed_price
        };
        var token = String(data.entsoe_token || '').trim();
        if (token) pricePayload.entsoe_token = token;
        await post('/prices/settings', pricePayload);
        var tokEl = form.querySelector('input[name="entsoe_token"]');
        if (tokEl) tokEl.value = '';
      } else if (key === 'weather') {
        await post('/forecast/settings', { latitude: data.latitude, longitude: data.longitude, max_boost_c: data.wx_boost });
      } else if (key === 'wifi') {
        var ssid = String(data.ssid || '').trim();
        if (!ssid) { formStatus(form, t('wifi.needSsid'), false); return; }
        applyWifi(await post('/wifi', { ssid: ssid, password: data.password || '' }));
        var pwEl = qs('#wifi_password');
        if (pwEl) pwEl.value = '';
        setText('wifi.status', t('wifi.sent'));
        // Follow the switch; the page may drop while the radio reconnects.
        var tries = 0;
        var follow = setInterval(function () {
          get('/wifi').then(function (w) {
            applyWifi(w);
            if (!w || w['switch'] !== 'pending' || ++tries > 30) clearInterval(follow);
          }).catch(function () { if (++tries > 30) clearInterval(follow); });
        }, 2000);
      } else if (key === 'settings') {
        var idleMin = Number(data.dev_idle);
        await post('/settings', {
          name: data.name || '',
          display_idle_timeout_s: isFinite(idleMin) ? Math.round(idleMin * 60) : 300
        });
      } else if (key === 'firmware') {
        var fwForm = form;
        var action = data.action || (ev.submitter && ev.submitter.value) || '';
        if (action === 'check') {
          formStatus(fwForm, t('csys.fwChecking'));
          try {
            var info = await fetchLatestRelease();
            state.fwLatest = info.tag;
            state.fwAsset = info.asset;
            setText('fw.latest', info.tag || '—');
            syncFwBackupButtons();
            var newer = verNewer(info.tag, state.fwInstalled);
            formStatus(fwForm, newer ? t('csys.fwAvailable') : t('csys.fwUpToDate'), true);
          } catch (ce) {
            state.fwLatest = null;
            state.fwAsset = null;
            setText('fw.latest', '—');
            syncFwBackupButtons();
            formStatus(fwForm, ce && ce.code === 'no_releases' ? t('csys.fwNoReleases') : t('csys.fwCheckFailed'), false);
          }
          return;
        }
        if (action === 'install') {
          if (!state.fwAsset || !state.fwAsset.url) throw new Error(t('csys.fwCheckFailed'));
          formStatus(fwForm, t('csys.fwInstalling'));
          var bin;
          if (window.LUNE_TOUCH_MOCK) {
            bin = new Blob(['mock-ota'], { type: 'application/octet-stream' });
            bin.name = state.fwAsset.name || 'firmware.bin';
          } else {
            var binRes = await fetch(state.fwAsset.url, { cache: 'no-store' });
            if (!binRes.ok) throw new Error(t('csys.fwUploadFailed'));
            bin = await binRes.blob();
            bin.name = state.fwAsset.name || 'firmware.bin';
          }
          await uploadFirmware(bin, function (pct) {
            formStatus(fwForm, t('csys.fwUploading') + ' ' + pct + '%');
          });
          formStatus(fwForm, t('csys.fwUploadDone'), true);
          return;
        }
        if (action === 'upload') {
          var fileInput = qs('#ota_file');
          var file = fileInput && fileInput.files && fileInput.files[0];
          if (!file) throw new Error(t('csys.fwNoFile'));
          formStatus(fwForm, t('csys.fwUploading'));
          await uploadFirmware(file, function (pct) {
            formStatus(fwForm, t('csys.fwUploading') + ' ' + pct + '%');
          });
          formStatus(fwForm, t('csys.fwUploadDone'), true);
          return;
        }
        return;
      } else if (key === 'backup') {
        var bakForm = form;
        var bakAction = data.action || (ev.submitter && ev.submitter.value) || '';
        if (bakAction === 'export') {
          formStatus(bakForm, t('csys.backupExporting'));
          var envelope = await buildBackupEnvelope();
          var stamp = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
          downloadJson('lune-touch-settings-' + stamp + '.json', envelope);
          formStatus(bakForm, t('csys.backupExported'), true);
          return;
        }
        if (bakAction === 'import') {
          var bakInput = qs('#backup_file');
          var bakFile = bakInput && bakInput.files && bakInput.files[0];
          if (!bakFile) throw new Error(t('csys.backupInvalid'));
          formStatus(bakForm, t('csys.backupImporting'));
          var text = await bakFile.text();
          var parsed = JSON.parse(text);
          await applyBackupEnvelope(parsed);
          formStatus(bakForm, t('csys.backupImported'), true);
          refresh();
          return;
        }
        return;
      }
      if (form && form.luneSaved) form.luneSaved(true);
      else flash(form, true);
      refresh();
    } catch (e) {
      if (key === 'firmware' || key === 'backup') {
        formStatus(form, (e && e.message) || (key === 'firmware' ? t('csys.fwUploadFailed') : t('csys.backupFailed')), false);
        return;
      }
      if (form && form.luneSaved) form.luneSaved(false, (e && e.message) || undefined);
      else flash(form, false);
    }
  }

  function formatHsProbe(r) {
    if (!r) return t('hs.testFail');
    var ok = r.result === 'ok';
    var bits = [ok ? t('hs.testOk') : t('hs.testFail')];
    if (r.value_c != null && isFinite(Number(r.value_c))) bits.push(num(r.value_c) + '°C');
    if (r.confirmed_value_c != null && isFinite(Number(r.confirmed_value_c))) bits.push(num(r.confirmed_value_c) + '°C');
    else if (r.requested_value_c != null && isFinite(Number(r.requested_value_c))) bits.push(num(r.requested_value_c) + '°C');
    else if (r.preview_value_c != null && isFinite(Number(r.preview_value_c))) bits.push(num(r.preview_value_c) + '°C');
    if (r.http_status) bits.push('HTTP ' + r.http_status);
    if (r.status && r.status !== 'ok') bits.push(r.status);
    if (r.error) bits.push(r.error);
    return (ok ? '✓ ' : '✗ ') + bits.join(' · ');
  }

  async function onAction(btn) {
    var action = btn.getAttribute('data-action');
    var id = btn.getAttribute('data-id');
    try {
      if (action === 'price-push') { await runPricePush(btn, false); return; }
      if (action === 'price-odin-write') { await runPricePush(btn, true); return; }
      if (action === 'price-zone-defaults') { await applyZoneDefaults(); return; }
      if (action === 'price-sched-add') {
        var cur = priceSchedRead();
        var used = {};
        cur.forEach(function (b) { used[b.h] = true; });
        var nh = 0;
        while (used[nh] && nh < 23) nh++;
        if (used[nh]) return;
        cur.push({ h: nh, v: cur.length ? cur[cur.length - 1].v : 0 });
        cur.sort(function (a, b) { return a.h - b.h; });
        priceSchedRows(cur);
        priceSchedSync();
        return;
      }
      if (action === 'price-sched-remove') {
        var tr = btn.closest('tr');
        if (tr && qsa('[data-bind-price-sched] tr').length > 1) tr.remove();
        priceSchedSync();
        return;
      }
      if (action === 'copy-diag') {
        copyDiagnostics(btn);
        return;
      }
      if (action === 'edit-node-name' || action === 'edit-node-host') { beginRename(btn); return; }
      if (action === 'scan-nodes') {
        btn.setAttribute('aria-busy', 'true');
        try {
          await post('/nodes/scan', {});
          var scan = await get('/nodes/scan');
          var guard = 0;
          while (scan && scan.poll_pending && guard < 8) {
            await new Promise(function (r) { setTimeout(r, 400); });
            scan = await get('/nodes/scan');
            guard++;
          }
          state.scanFound = (scan && scan.found) || [];
          renderScan(state.scanFound);
        } finally {
          btn.removeAttribute('aria-busy');
        }
        return;
      }
      if (action === 'add-found') {
        var host = btn.getAttribute('data-host') || '';
        var foundPayload = /^\d{1,3}(\.\d{1,3}){3}$/.test(host) ? { ip: host } : { hostname: host };
        // With the fingerprint the Touch moves an already paired V6 instead of adding it twice.
        var fp = btn.getAttribute('data-fp');
        if (fp) foundPayload.pairing_fingerprint = fp;
        await post('/nodes', foundPayload);
        state.scanFound = (state.scanFound || []).filter(function (f) {
          return (f.hostname || f.ip) !== host;
        });
        refresh();
        return;
      }
      else if (action === 'trust-node') await post('/nodes/' + encodeURIComponent(id) + '/trust', { trust: 2, confirm: id });
      else if (action === 'remove-node') await post('/nodes/' + encodeURIComponent(id) + '/remove', { confirm: id });
      else if (action === 'hs-test-read' || action === 'hs-test-push') {
        await runHeatTest(btn, action);
        return;
      }
      else if (action === 'wx-geo') await post('/forecast/estimate-location', {});
      else if (action === 'reset-registry') await post('/recovery/reset-registry', { confirm: 'reset' });
      refresh();
    } catch (e) {
      if (action === 'hs-test-read' || action === 'hs-test-push') {
        paintTestResult(false, t('hs.testFailLine', {
          status: t('hs.testStatus.failed'), time: clockNow(), reason: t('status.unknown')
        }), t('hs.testCheckHost'));
        btn.removeAttribute('aria-busy');
      }
      console.warn(action, e);
    }
  }

  function clockNow() {
    var d = new Date();
    function p(n) { return n < 10 ? '0' + n : String(n); }
    return p(d.getHours()) + ':' + p(d.getMinutes()) + ':' + p(d.getSeconds());
  }

  // One Test block per adapter type; use the one that is visible.
  function testBox() {
    var boxes = qsa('.test-result');
    for (var i = 0; i < boxes.length; i++) if (boxes[i].offsetParent !== null) return boxes[i];
    return boxes[0] || null;
  }

  function paintTestRunning() {
    var box = testBox();
    if (!box) return;
    box.setAttribute('data-state', 'running');
    box.textContent = t('hs.testing');
  }

  function paintTestResult(ok, line1, line2) {
    var box = testBox();
    if (!box) return;
    box.removeAttribute('data-state');
    box.innerHTML = '<div class="msg ' + (ok ? 'ok' : 'bad') + '"><span><b>' + esc(line1) + '</b>' + esc(line2) + '</span></div>';
  }

  function probeReason(r) {
    var err = String((r && r.error) || '');
    var http = r && r.http_status;
    if (/timeout/i.test(err) || err === 'read unreachable' || err === 'probe_timeout') {
      return { reason: t('hs.testReason.timeout'), check: t('hs.testCheckHost') };
    }
    if (/dns|resolve|ENOTFOUND/i.test(err)) return { reason: t('hs.testReason.dns'), check: t('hs.testCheckDns') };
    var m = /http\s+(\d+)/i.exec(err);
    var code = m ? m[1] : (http && Number(http) >= 400 ? String(http) : '');
    if (code) return { reason: t('hs.testReason.http', { code: code }), check: t('hs.testCheckHttp') };
    if (/address_missing/.test(err)) return { reason: t('hs.testReason.missing'), check: t('hs.testCheckHost') };
    if (/network_down/.test(err)) return { reason: t('hs.testReason.network'), check: t('hs.testCheckHost') };
    if (/invalid_|endpoint|value missing/.test(err)) return { reason: t('hs.testReason.endpoint'), check: t('hs.testCheckHttp') };
    if (/not_ready/.test(err)) return { reason: t('hs.testReason.notReady'), check: t('hs.testCheckHost') };
    if (/busy/.test(err)) return { reason: t('hs.testReason.busy'), check: t('hs.testCheckHost') };
    if (r && r.result === 'rejected') return { reason: t('hs.testReason.rejected'), check: t('hs.testCheckHost') };
    return { reason: t('status.unknown'), check: t('hs.testCheckHost') };
  }

  async function runHeatTest(btn, action) {
    paintTestRunning();
    btn.setAttribute('aria-busy', 'true');
    var started = (window.performance && performance.now) ? performance.now() : Date.now();
    try {
      var result = await post(action === 'hs-test-read' ? '/heat-source/test-read' : '/heat-source/test-push', {});
      var elapsed = Math.max(0, Math.round(((window.performance && performance.now) ? performance.now() : Date.now()) - started));
      var time = clockNow();
      var code = result && result.http_status ? result.http_status : (result && result.result === 'ok' ? 200 : 0);
      var read = action === 'hs-test-read';
      if (result && result.result === 'ok') {
        var entity = result.entity || '—';
        var value = read ? result.value_c : (result.confirmed_value_c != null ? result.confirmed_value_c : result.requested_value_c);
        // TODO: test-read does not return flow, return or operating state — only value_c and entity.
        paintTestResult(true,
          t(read ? 'hs.testReadOk' : 'hs.testPushOk', { time: time, code: code, ms: elapsed }),
          t(read ? 'hs.testReadBody' : 'hs.testPushBody', { value: num(value), entity: entity }));
      } else {
        var why = probeReason(result || {});
        paintTestResult(false, t('hs.testFailLine', {
          status: t('hs.testStatus.failed'), time: time, reason: why.reason
        }), why.check);
      }
    } finally {
      btn.removeAttribute('aria-busy');
    }
  }

  function beginRename(btn) {
    var cell = btn.closest('.name-edit');
    if (!cell) return;
    var input = cell.querySelector('input');
    var label = cell.querySelector('[data-name]');
    if (!input) return;
    input.hidden = false;
    if (label) label.hidden = true;
    btn.hidden = true;
    input.focus();
    input.select();
  }

  async function commitRename(input) {
    if (!input || input.dataset.saving) return;
    var id = input.getAttribute('data-rename');
    var name = String(input.value || '').trim();
    if (!name) { applyNodes(state.nodes); return; }
    input.dataset.saving = '1';
    try {
      await post('/nodes/' + encodeURIComponent(id) + '/profile', { name: name });
      refresh();
    } catch (err) {
      delete input.dataset.saving;
    }
  }

  async function commitHost(input) {
    if (!input || input.dataset.saving) return;
    var id = input.getAttribute('data-rehost');
    var host = String(input.value || '').trim();
    var node = state.nodes.find(function (n) { return String(n.id) === id; });
    if (!host || (node && host === (node.ip || node.hostname || ''))) { applyNodes(state.nodes); return; }
    input.dataset.saving = '1';
    try {
      await post('/nodes/' + encodeURIComponent(id) + '/host', { host: host });
      refresh();
    } catch (err) {
      delete input.dataset.saving;
      input.setAttribute('aria-invalid', 'true');
      input.title = t('ctrl.hostInvalid');
    }
  }

  // Progressive enhancement: stepper, help placement, file name, dismiss device menu, save hook, actions
  var helpBtn = null;
  function placeHelp(pop, btn) {
    if (!pop || !btn || window.matchMedia('(max-width:599.98px)').matches) return;
    var r = btn.getBoundingClientRect(), gap = 8;
    pop.style.position = 'fixed';
    pop.style.inset = 'unset';
    pop.style.right = 'auto';
    pop.style.bottom = 'auto';
    pop.style.margin = '0';
    pop.style.top = '0px';
    pop.style.left = '0px';
    var w = pop.offsetWidth || 280, h = pop.offsetHeight || 120;
    var left = Math.min(Math.max(8, r.left), Math.max(8, window.innerWidth - w - 8));
    var top = r.bottom + gap;
    if (top + h > window.innerHeight - 8) top = Math.max(8, r.top - h - gap);
    pop.style.top = top + 'px';
    pop.style.left = left + 'px';
  }

  document.addEventListener('pointerdown', function (e) {
    var h = e.target.closest && e.target.closest('.help-btn');
    if (h) helpBtn = h;
  }, true);

  document.addEventListener('beforetoggle', function (e) {
    if (e.newState !== 'open' || !e.target.classList || !e.target.classList.contains('help-pop')) return;
    var id = e.target.id;
    var btn = (helpBtn && helpBtn.getAttribute('popovertarget') === id) ? helpBtn : null;
    if (!btn && document.activeElement && document.activeElement.getAttribute && document.activeElement.getAttribute('popovertarget') === id)
      btn = document.activeElement;
    if (!btn) btn = document.querySelector('[popovertarget="' + id + '"]');
    if (btn) { helpBtn = btn; placeHelp(e.target, btn); }
  }, true);

  document.addEventListener('toggle', function (e) {
    if (e.newState !== 'open' || !e.target.classList || !e.target.classList.contains('help-pop')) return;
    var id = e.target.id;
    var btn = (helpBtn && helpBtn.getAttribute('popovertarget') === id) ? helpBtn : document.querySelector('[popovertarget="' + id + '"]');
    if (btn) placeHelp(e.target, btn);
  }, true);

  document.addEventListener('click', function (e) {
    var h = e.target.closest && e.target.closest('.help-btn');
    if (h) {
      helpBtn = h;
      var id = h.getAttribute('popovertarget');
      var pop = id && document.getElementById(id);
      if (pop) requestAnimationFrame(function () { if (pop.matches(':popover-open')) placeHelp(pop, h); });
    }
    var b = e.target.closest('[data-step]');
    if (b && !b.disabled) {
      var i = b.parentNode.querySelector('input');
      if (i) {
        if (Number(b.dataset.step) > 0) i.stepUp(); else i.stepDown();
        i.dispatchEvent(new Event('change', { bubbles: true }));
      }
    }
    var d = document.querySelector('.device[open]');
    if (d && !d.contains(e.target)) d.open = false;
    var act = e.target.closest('[data-action]');
    if (act) {
      e.preventDefault();
      onAction(act);
    }
  });

  document.addEventListener('focusout', function (e) {
    var input = e.target;
    if (!input || !input.getAttribute || input.hidden) return;
    if (input.getAttribute('data-rename')) commitRename(input);
    else if (input.getAttribute('data-rehost')) commitHost(input);
  });
  document.addEventListener('keydown', function (e) {
    var input = e.target;
    if (!input || !input.getAttribute) return;
    var rename = input.getAttribute('data-rename'), rehost = input.getAttribute('data-rehost');
    if (!rename && !rehost) return;
    if (e.key === 'Enter') { e.preventDefault(); if (rename) commitRename(input); else commitHost(input); }
    if (e.key === 'Escape') { e.preventDefault(); applyNodes(state.nodes); }
  });

  document.addEventListener('change', function (e) {
    var inp = e.target;
    if (inp && inp.name === 'scope') {
      syncScope();
      loadComfortCharts(state.rooms);
    }
    if (inp && inp.name === 'hs_type') {
      syncHsType(inp.value);
      if (state.heat) {
        state.heat.type = inp.value === 'http' ? 'generic_http' : 'asgard';
        renderHeatDelivery(state.heat);
        paintHeatBadge(state.heat);
      }
    }
    if (!inp || inp.type !== 'file') return;
    var lab = inp.closest('label.file');
    if (!lab) return;
    var name = lab.querySelector('.file-name');
    if (!name) return;
    name.textContent = (inp.files && inp.files[0]) ? inp.files[0].name : (name.dataset.empty || '');
    syncFwBackupButtons();
  });


  /* ---- Clean/dirty + Fortryd (DESIGN.md 6.1) ---- */
  function snap(f) {
    var o = {};
    f.querySelectorAll('input,select,textarea').forEach(function (el) {
      if (!el.name || /^(submit|reset|button|file)$/.test(el.type)) return;
      var k = el.name + (el.type === 'radio' || el.type === 'checkbox' ? ':' + el.value : '');
      o[k] = el.type === 'checkbox' || el.type === 'radio' ? el.checked : el.value;
    });
    return o;
  }
  function wrap(el) { return el.closest('.field,.switch,.seg,.compass'); }
  function btn(f) { return f.querySelector('.panel-foot .btn.primary[type="submit"]'); }
  function st(f) { return f.querySelector('.save-status'); }
  function auto(f) { return !!f.querySelector('.climate'); }
  function track(f) { return !!st(f); }
  function cross() {
    qsa('.tile[data-dirty], .mode label[data-dirty]').forEach(function (el) { el.removeAttribute('data-dirty'); });
    var conf = false;
    qsa('form.panel[data-save][data-dirty]').forEach(function (f) {
      var v = f.closest('.view');
      if (!v || !v.id) return;
      var m = /^v-(dash|conf)-(.+)$/.exec(v.id);
      if (!m) return;
      if (m[1] === 'conf') conf = true;
      var scope = m[2];
      var tile = null;
      if (scope === 'house' || scope === 'manifold' || scope === 'zone') {
        var sel = qs('input[name="scope"]:checked');
        if (sel) tile = qs('label.tile[for="' + sel.id + '"]');
      } else {
        tile = qs('label.tile[for="s-' + scope + '"]');
      }
      if (tile) tile.setAttribute('data-dirty', '');
    });
    if (conf) {
      var lab = qs('.mode label[for="m-conf"]');
      if (lab) lab.setAttribute('data-dirty', '');
    }
  }
  function paint(f) {
    if (auto(f) || !track(f) || f.dataset.state === 'saving' || f.dataset.state === 'saved') return;
    var s0 = f._snap || {}, c = snap(f), n = 0, seen = {}, sid = (st(f) || {}).id;
    f.querySelectorAll('[data-dirty]').forEach(function (w) {
      w.removeAttribute('data-dirty');
      if (w.getAttribute('aria-describedby') === sid) w.removeAttribute('aria-describedby');
    });
    Object.keys(Object.assign({}, s0, c)).forEach(function (k) {
      if (s0[k] === c[k]) return;
      var name = k.split(':')[0];
      if (seen[name]) return;
      seen[name] = 1;
      n++;
      var el = f.querySelector('[name="' + name + '"]'), w = el && wrap(el);
      if (w) {
        w.setAttribute('data-dirty', '');
        if (sid) w.setAttribute('aria-describedby', sid);
      }
    });
    f.dataset.changes = String(n);
    var b = btn(f), sEl = st(f);
    if (n) {
      f.dataset.dirty = '';
      if (b) { b.removeAttribute('aria-disabled'); b.removeAttribute('title'); }
      if (sEl && f.dataset.state !== 'error') sEl.textContent = n === 1 ? t('rt.unsaved.one', { n: 1 }) : t('rt.unsaved.other', { n: n });
    } else {
      delete f.dataset.dirty;
      if (b) { b.setAttribute('aria-disabled', 'true'); b.title = t('rt.nothingToSave'); }
      if (sEl && f.dataset.state !== 'saved' && f.dataset.state !== 'error') sEl.textContent = '';
    }
    if (f.getAttribute('data-save') === 'add-node' && b) {
      var hostEl = f.querySelector('[name="host"]');
      if (!hostEl || !String(hostEl.value || '').trim()) {
        b.setAttribute('aria-disabled', 'true');
        b.title = t('rt.nothingToSave');
      }
    }
    cross();
  }
  function bindForm(f) {
    f.dataset.js = '1';
    f._snap = snap(f);
    f._label = (btn(f) || {}).textContent || '';
    f.luneResnap = function () { f._snap = snap(f); if (track(f) && !auto(f)) paint(f); };
    f.luneSaved = function (ok, msg) {
      var b = btn(f), sEl = st(f), a = f.querySelector('.autosave'), sid = (sEl || {}).id;
      delete f.dataset.state;
      delete f.dataset.autoPending;
      if (b) b.removeAttribute('aria-busy');
      if (ok) {
        f._snap = snap(f);
        if (auto(f)) {
          if (a) {
            a.textContent = t('rt.autoSaved');
            setTimeout(function () { if (a.textContent === t('rt.autoSaved')) a.textContent = ''; }, 2000);
          }
          return;
        }
        f.querySelectorAll('[data-dirty]').forEach(function (w) {
          w.removeAttribute('data-dirty');
          if (sid && w.getAttribute('aria-describedby') === sid) w.removeAttribute('aria-describedby');
        });
        f.dataset.state = 'saved';
        if (b) b.textContent = t('rt.savedOk') + ' ✓';
        if (sEl) sEl.textContent = '';
        delete f.dataset.dirty;
        cross();
        setTimeout(function () { delete f.dataset.state; if (b) b.textContent = f._label; paint(f); }, 3000);
      } else if (auto(f)) {
        if (a) a.innerHTML = t('rt.autoFailed') + '<button type="button" class="btn" data-autosave-retry>' + t('rt.retry') + '</button>';
      } else {
        f.dataset.state = 'error';
        if (sEl) sEl.textContent = msg || t('rt.saveFailed');
        if (b) b.textContent = f._label;
        paint(f);
      }
    };
    if (track(f) && !auto(f)) paint(f);
  }
  qsa('form.panel[data-save]').forEach(bindForm);

  function onEdit(e) {
    var f = e.target && e.target.closest && e.target.closest('form.panel[data-save]');
    if (!f || !f.dataset.js) return;
    if (auto(f)) {
      if (e.target.disabled) return;
      var a = f.querySelector('.autosave');
      if (a) a.textContent = t('rt.autoSaving');
      f.dataset.autoPending = '1';
      clearTimeout(f._autoT);
      f._autoT = setTimeout(function () {
        document.dispatchEvent(new CustomEvent('lune:save', { detail: { key: f.dataset.save, data: new FormData(f), auto: true, form: f } }));
      }, 1500);
    } else if (track(f)) {
      delete f.dataset.state;
      paint(f);
      var sw = e.target;
      if (e.type === 'change' && sw && sw.getAttribute && sw.getAttribute('role') === 'switch') {
        if (f.getAttribute('data-save') === 'heat-source' && state.heat) renderHeatDelivery(state.heat);
        document.dispatchEvent(new CustomEvent('lune:save', { detail: { key: f.dataset.save, data: new FormData(f), auto: true, form: f } }));
      }
    }
  }
  document.addEventListener('input', onEdit, true);
  document.addEventListener('change', onEdit, true);
  document.addEventListener('change', function (e) {
    var t = e.target;
    if (t && t.name === 'hs_type') {
      syncHsTypedFields();
      var f = t.closest && t.closest('form.panel[data-save="heat-source"]');
      if (f && f.dataset.js) paint(f);
    }
  }, true);
  document.addEventListener('reset', function (e) {
    var f = e.target;
    if (f && f.matches && f.matches('form.panel[data-save]')) setTimeout(function () { delete f.dataset.state; paint(f); }, 0);
  });
  document.addEventListener('click', function (e) {
    var retry = e.target.closest && e.target.closest('[data-autosave-retry]');
    if (retry) {
      var f = retry.closest('form.panel[data-save]');
      if (f) {
        var a = f.querySelector('.autosave');
        if (a) a.textContent = t('rt.autoSaving');
        document.dispatchEvent(new CustomEvent('lune:save', { detail: { key: f.dataset.save, data: new FormData(f), auto: true, form: f } }));
      }
      return;
    }
    var b = e.target.closest && e.target.closest('.btn.primary[type="submit"]');
    if (b && b.getAttribute('aria-disabled') === 'true') {
      e.preventDefault();
      e.stopPropagation();
    }
  }, true);

  document.addEventListener('submit', function (e) {
    var f = e.target;
    if (!f || !f.matches || !f.matches('form.panel[data-save]')) return;
    e.preventDefault();
    var sub = e.submitter, b = btn(f);
    var primary = !sub || sub === b || (sub.classList && sub.classList.contains('primary'));
    if (primary && b && b.getAttribute('aria-disabled') === 'true') return;
    if (primary && f.dataset.state === 'saving') return;
    if (primary && track(f) && !auto(f)) {
      f.dataset.state = 'saving';
      if (b) { b.setAttribute('aria-busy', 'true'); b.textContent = t('rt.saving'); }
    }
    document.dispatchEvent(new CustomEvent('lune:save', { detail: { key: f.dataset.save, data: new FormData(f, sub), auto: !!auto(f) && primary === false ? false : false, form: f, submitter: sub } }));
  });

  document.addEventListener('lune:save', function (e) {
    onSave({ target: e.detail.form, submitter: e.detail.submitter });
  });

  window.addEventListener('beforeunload', function (e) {
    if (qs('form.panel[data-save][data-dirty]')) {
      e.preventDefault();
      e.returnValue = t('rt.leaveUnsaved');
    }
  });

  // Remember language cookie when visiting /en/ or /da/
  var m = location.pathname.match(/^\/(en|da)\/?/);
  if (m) document.cookie = 'lune_lang=' + m[1] + ';path=/;max-age=31536000';

  syncScope();
  syncHsType((qs('input[name="hs_type"]:checked') || {}).value || 'asgard');
  syncFwBackupButtons();
  refresh();
  setInterval(function () {
    if (document.querySelector('details[open]')) return;
    if (document.activeElement && /^(INPUT|TEXTAREA|SELECT)$/.test(document.activeElement.tagName)) return;
    if (qs('form.panel[data-save][data-dirty]')) return;
    refresh();
  }, POLL_MS);
})();
