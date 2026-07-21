import {
    MidiNoteOn,
    MoveMainKnob, MoveMainButton, MoveBack, MovePlay, MoveRec, MoveMaster,
    MoveKnob1, MoveKnob2, MoveKnob3, MoveKnob4, MoveKnob5, MoveKnob6, MoveKnob7, MoveKnob8, MovePads,
    MoveStep1, MoveStep2, MoveStep3, MoveStep4,
    MoveRow1, MoveRow2, MoveRow3, MoveRow4,
    White, Black, BrightGreen, Cyan, OrangeRed, VividYellow,
    BrightRed, SkyBlue, LightGrey, DarkGrey, WhiteLedBright, WhiteLedDim
} from '/data/UserData/schwung/shared/constants.mjs';

import { decodeDelta, shouldFilterMessage, setLED, setButtonLED, clearAllLEDs }
    from '/data/UserData/schwung/shared/input_filter.mjs';
import { announce, announceView, announceParameter }
    from '/data/UserData/schwung/shared/screen_reader.mjs';

const SCREEN_W = 128;
const PAD_TABS = MovePads.slice(0, 8);
const TABS = [
    { id: 'cpu', label: 'CPU', color: BrightGreen },
    { id: 'mem', label: 'MEM', color: SkyBlue },
    { id: 'proc', label: 'PROC', color: VividYellow },
    { id: 'wifi', label: 'WIFI', color: Cyan },
    { id: 'net', label: 'NET', color: OrangeRed },
    { id: 'disk', label: 'DISK', color: LightGrey },
    { id: 'up', label: 'UP', color: White },
    { id: 'orch', label: 'ORCH', color: BrightRed }
];

let selectedTab = 0;
let overview = {};
let topCpu = [];
let topMem = [];
let wifiScan = [];
let sonify = { enabled: 0, gain: 0.25, voices: 0 };
let tickCounter = 0;
let needsRedraw = true;
let statusMessage = '';
let statusTicks = 0;
let lastPadPress = { note: -1, time: 0 };
let lastAutoRefreshAt = 0;
let knobAccum = { k1: 0, k2: 0, k3: 0, k4: 0, k5: 0, k6: 0, k7: 0, k8: 0, master: 0 };

const KNOB_TOUCH_NAMES = [
    'Waveform',
    'Key',
    'Repeat Rate',
    'Decay',
    'Pan 1',
    'Pan 2',
    'Pan 3',
    'Pan 4'
];
const ROOT_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const RATE_NAMES = ['1/1', '1/2', '1/4', '1/8', '1/16', '1/32'];
const SOURCE_NAMES = ['WiFi', 'Process'];
const WAVE_NAMES = ['Sine', 'Triangle', 'Square', 'Saw'];
const SCALE_NAMES = ['Maj7', 'Min7', 'Sus7', 'Penta'];

function init() {
    clearAllLEDs();
    refreshAll(false);
    updateLeds();
    announceView('SignalScope');
    announce(tabAnnouncement());
    setStatus('Play refresh', false);
}

function safeParse(key, fallback) {
    try {
        const raw = host_module_get_param(key);
        if (!raw) return fallback;
        return JSON.parse(raw);
    } catch (e) {
        return fallback;
    }
}

function refreshAll(scanWifi) {
    host_module_set_param('refresh', '1');
    if (scanWifi) host_module_set_param('scan_wifi', '1');
    overview = safeParse('overview_json', {});
    topCpu = safeParse('top_cpu_json', []);
    topMem = safeParse('top_mem_json', []);
    wifiScan = safeParse('wifi_scan_json', []);
    sonify = safeParse('sonify_status', sonify);
    needsRedraw = true;
}

function announceRefreshResult(prefix) {
    const summary = tabAnnouncement();
    const msg = prefix ? `${prefix}. ${summary}` : summary;
    setStatus(msg.slice(0, 20), false);
    announce(msg);
}

function setStatus(msg, speak) {
    statusMessage = msg || '';
    statusTicks = statusMessage ? 120 : 0;
    if (speak && statusMessage) announce(statusMessage);
    needsRedraw = true;
}

function tabAnnouncement() {
    const tab = TABS[selectedTab].id;
    switch (tab) {
        case 'cpu':
            return `CPU ${(overview.cpu_pct || 0).toFixed(1)} percent, ${temperatureAnnouncement()}, load ${(overview.load1 || 0).toFixed(2)}`;
        case 'mem':
            return `Memory ${overview.mem_used_mb || 0} of ${overview.mem_total_mb || 0} megabytes used`;
        case 'proc':
            return processAnnouncement();
        case 'wifi':
            return `WiFi ${overview.wifi_ssid || 'offline'}, signal ${overview.wifi_signal_dbm || -100} d b m`;
        case 'net':
            return `Network receive ${formatBytes(overview.wifi_rx_bytes || 0)}, transmit ${formatBytes(overview.wifi_tx_bytes || 0)}`;
        case 'disk':
            return `Storage ${(overview.data_used_pct || 0).toFixed(1)} percent used`;
        case 'up':
            return `Uptime ${formatUptime(overview.uptime_seconds || 0)}`;
        case 'orch':
            return `Orchestra ${sonify.enabled ? 'on' : 'off'}, ${sonify.voices || wifiScan.length} voices, ${WAVE_NAMES[parseInt(host_module_get_param('sonify_waveform') || '0', 10)] || 'Sine'}, ${SCALE_NAMES[parseInt(host_module_get_param('sonify_scale') || '0', 10)] || 'Maj7'}`;
    }
    return TABS[selectedTab].label;
}

function processAnnouncement() {
    if (!topCpu.length) return 'Processes';
    const names = topCpu.slice(0, 3).map(function (p) { return p.name; });
    if (names.length === 1) return `Processes, top is ${names[0]}`;
    if (names.length === 2) return `Processes, top are ${names[0]} and ${names[1]}`;
    return `Processes, top are ${names[0]}, ${names[1]}, and ${names[2]}`;
}

function updateLeds() {
    for (let i = 0; i < PAD_TABS.length; i++) {
        setLED(PAD_TABS[i], i === selectedTab ? TABS[i].color : DarkGrey);
    }
    setButtonLED(MovePlay, WhiteLedBright);
    setButtonLED(MoveRec, host_module_get_param('auto_refresh') === '1' ? WhiteLedBright : WhiteLedDim);
    setButtonLED(MoveMainButton, WhiteLedDim);
    setButtonLED(MoveBack, WhiteLedBright);
    const scale = parseInt(host_module_get_param('sonify_scale') || '0', 10);
    setButtonLED(MoveRow1, scale === 0 ? WhiteLedBright : WhiteLedDim);
    setButtonLED(MoveRow2, scale === 1 ? WhiteLedBright : WhiteLedDim);
    setButtonLED(MoveRow3, scale === 2 ? WhiteLedBright : WhiteLedDim);
    setButtonLED(MoveRow4, scale === 3 ? WhiteLedBright : WhiteLedDim);
}

function formatBytes(bytes) {
    if (bytes === undefined || bytes === null) return '0';
    if (bytes >= 1000000000) return (bytes / 1000000000).toFixed(1) + 'G';
    if (bytes >= 1000000) return (bytes / 1000000).toFixed(1) + 'M';
    if (bytes >= 1000) return (bytes / 1000).toFixed(1) + 'K';
    return String(bytes);
}

function formatUptime(sec) {
    sec = sec || 0;
    const d = Math.floor(sec / 86400);
    const h = Math.floor((sec % 86400) / 3600);
    const m = Math.floor((sec % 3600) / 60);
    if (d > 0) return `${d}d ${h}h`;
    return `${h}h ${m}m`;
}

function temperatureDisplay() {
    if (!overview.cpu_temp_available || typeof overview.cpu_temp_c !== 'number') return '--C';
    return `${overview.cpu_temp_c.toFixed(1)}C`;
}

function temperatureAnnouncement() {
    if (!overview.cpu_temp_available || typeof overview.cpu_temp_c !== 'number') return 'temperature unavailable';
    return `temperature ${overview.cpu_temp_c.toFixed(1)} degrees Celsius`;
}

function printLine(y, left, right) {
    print(2, y, left, 1);
    if (right !== undefined) {
        const x = Math.max(2, SCREEN_W - (String(right).length * 6) - 2);
        print(x, y, String(right), 1);
    }
}

function drawHeader() {
    const tab = TABS[selectedTab];
    print(2, 2, `SignalScope ${tab.label}`, 1);
    fill_rect(0, 12, SCREEN_W, 1, 1);
}

function drawFooter(left, right) {
    fill_rect(0, 55, SCREEN_W, 1, 1);
    if (left) print(2, 57, left, 1);
    if (right) {
        const x = Math.max(2, SCREEN_W - (String(right).length * 6) - 2);
        print(x, 57, String(right), 1);
    }
}

function drawCpu() {
    printLine(16, 'CPU', `${(overview.cpu_pct || 0).toFixed(1)}% ${temperatureDisplay()}`);
    printLine(26, 'Load', `${(overview.load1 || 0).toFixed(2)} ${(overview.load5 || 0).toFixed(2)}`);
    for (let i = 0; i < Math.min(2, topCpu.length); i++) {
        printLine(38 + i * 8, topCpu[i].name.slice(0, 12), `${(topCpu[i].cpu || 0).toFixed(1)}%`);
    }
    drawFooter('Play refresh', 'Jog tabs');
}

function drawMem() {
    printLine(16, 'Used', `${overview.mem_used_mb || 0}/${overview.mem_total_mb || 0}M`);
    printLine(26, 'Free', `${overview.mem_free_mb || 0}M`);
    printLine(36, 'Avail', `${overview.mem_available_mb || 0}M`);
    for (let i = 0; i < Math.min(2, topMem.length); i++) {
        printLine(48 + i * 8, topMem[i].name.slice(0, 12), `${(topMem[i].mem_mb || 0).toFixed(0)}M`);
    }
    drawFooter('K1 auto', 'Jog tabs');
}

function drawProc() {
    for (let i = 0; i < Math.min(5, topCpu.length); i++) {
        const p = topCpu[i];
        printLine(16 + i * 9, `${p.pid} ${p.name}`.slice(0, 16), `${(p.cpu || 0).toFixed(1)}%`);
    }
    drawFooter('Play refresh', 'Jog tabs');
}

function drawWifi() {
    printLine(16, 'SSID', (overview.wifi_ssid || 'offline').slice(0, 14));
    printLine(26, 'Signal', `${overview.wifi_signal_dbm || -100} dBm`);
    printLine(36, 'RX/TX', `${(overview.wifi_rx_mbps || 0).toFixed(0)}/${(overview.wifi_tx_mbps || 0).toFixed(0)} M`);
    for (let i = 0; i < Math.min(2, wifiScan.length); i++) {
        printLine(48 + i * 8, wifiScan[i].ssid.slice(0, 14), `${wifiScan[i].signal_dbm}d`);
    }
    drawFooter('Play scan', 'Push rescan');
}

function drawNet() {
    printLine(16, 'RX', formatBytes(overview.wifi_rx_bytes || 0));
    printLine(26, 'TX', formatBytes(overview.wifi_tx_bytes || 0));
    printLine(36, 'Pkts', `${overview.wifi_rx_packets || 0}/${overview.wifi_tx_packets || 0}`);
    printLine(46, 'Drops', `${overview.wifi_rx_drop || 0}/${overview.wifi_tx_drop || 0}`);
    drawFooter('Play refresh', 'Jog tabs');
}

function drawDisk() {
    printLine(16, '/data', `${(overview.data_used_pct || 0).toFixed(1)}%`);
    printLine(26, 'Used', `${(overview.data_used_gb || 0).toFixed(1)}G`);
    printLine(36, 'Free', `${(overview.data_free_gb || 0).toFixed(1)}G`);
    printLine(46, 'Root', `${(overview.root_used_pct || 0).toFixed(1)}%`);
    printLine(56, 'RootFree', `${(overview.root_free_gb || 0).toFixed(2)}G`);
    drawFooter('Play refresh', 'Jog tabs');
}

function drawUp() {
    printLine(16, 'Uptime', formatUptime(overview.uptime_seconds || 0));
    printLine(26, 'Load1', (overview.load1 || 0).toFixed(2));
    printLine(36, 'AutoRef', host_module_get_param('auto_refresh') === '1' ? 'On' : 'Off');
    printLine(46, 'WiFi', overview.wifi_ssid || 'offline');
    drawFooter('K1 auto', 'Jog tabs');
}

function drawOrch() {
    printLine(16, 'Orch', sonify.enabled ? 'On' : 'Off');
    printLine(24, 'Src', SOURCE_NAMES[parseInt(host_module_get_param('sonify_source') || '0', 10)] || 'WiFi');
    printLine(32, 'Wave', WAVE_NAMES[parseInt(host_module_get_param('sonify_waveform') || '0', 10)] || 'Sine');
    printLine(40, 'Scale', SCALE_NAMES[parseInt(host_module_get_param('sonify_scale') || '0', 10)] || 'Maj7');
    printLine(48, 'Dec', formatDecay(host_module_get_param('sonify_decay') || '900'));
    printLine(56, 'V/G', `${sonify.voices || wifiScan.length}/${Math.round(parseFloat(host_module_get_param('sonify_gain') || '0.90') * 100)}%`);
    drawFooter(host_module_get_param('auto_refresh') === '1' ? 'Rec auto on' : 'Rec auto off', 'Play once');
}

function draw() {
    clear_screen();
    drawHeader();
    switch (TABS[selectedTab].id) {
        case 'cpu': drawCpu(); break;
        case 'mem': drawMem(); break;
        case 'proc': drawProc(); break;
        case 'wifi': drawWifi(); break;
        case 'net': drawNet(); break;
        case 'disk': drawDisk(); break;
        case 'up': drawUp(); break;
        case 'orch': drawOrch(); break;
    }
    if (statusTicks > 0 && statusMessage) {
        fill_rect(0, 46, SCREEN_W, 9, 1);
        print(2, 47, statusMessage.slice(0, 20), 0);
    }
}

function changeTab(idx) {
    selectedTab = idx;
    updateLeds();
    setStatus(TABS[selectedTab].label, false);
    announce(tabAnnouncement());
    needsRedraw = true;
}

function formatDecay(v) {
    const n = parseFloat(v || '900');
    return n >= 1000 ? (n / 1000).toFixed(1) + 's' : Math.round(n) + 'ms';
}

function formatRate(index) {
    return RATE_NAMES[index] || '1/4';
}

function takeStep(accKey, delta, threshold) {
    knobAccum[accKey] += delta;
    if (Math.abs(knobAccum[accKey]) < threshold) return 0;
    const step = knobAccum[accKey] > 0 ? 1 : -1;
    knobAccum[accKey] = 0;
    return step;
}

function adjustRelative(current, delta, min, max, step) {
    let next = current + (delta > 0 ? step : -step);
    if (next < min) next = min;
    if (next > max) next = max;
    return next;
}

function formatPan(v) {
    const n = parseFloat(v || '0');
    if (n < -0.05) return 'L' + Math.round(Math.abs(n) * 100);
    if (n > 0.05) return 'R' + Math.round(Math.abs(n) * 100);
    return 'C';
}

function onMidiMessageInternal(msg) {
    if (!msg || msg.length < 3) return;
    const status = msg[0] & 0xF0;
    const cc = msg[1];
    const value = msg[2];

    if (status === MidiNoteOn && value > 0 && cc >= 0 && cc <= 7) {
        if (cc <= 7) {
            const label = KNOB_TOUCH_NAMES[cc];
            const val = knobTouchValue(cc);
            setStatus(`${label} ${val}`.slice(0, 20), false);
            announce(`${label} ${val}`);
        }
        return;
    }

    if (status === MidiNoteOn && value > 0 && TABS[selectedTab].id === 'orch' && cc >= MoveStep1 && cc <= MoveStep4) {
        if (cc === MoveStep1) {
            host_module_set_param('sonify_source', '0');
            setStatus('Source WiFi', true);
            announceParameter('Source', 'WiFi');
        } else if (cc === MoveStep2) {
            host_module_set_param('sonify_source', '1');
            setStatus('Source Process', true);
            announceParameter('Source', 'Process');
        } else if (cc === MoveStep3) {
            refreshAll(true);
            setStatus('Refresh and scan', true);
            announce('Refresh and scan');
        } else if (cc === MoveStep4) {
            const next = host_module_get_param('sonify_enable') === '1' ? '0' : '1';
            host_module_set_param('sonify_enable', next);
            sonify.enabled = next === '1' ? 1 : 0;
            setStatus(next === '1' ? 'Orchestra on' : 'Orchestra off', true);
            announceParameter('Orchestra', next === '1' ? 'On' : 'Off');
        }
        needsRedraw = true;
        return;
    }

    if (status === 0xB0 && value > 0 && TABS[selectedTab].id === 'orch' && cc >= MoveRow4 && cc <= MoveRow1) {
        let scale = 0;
        if (cc === MoveRow1) scale = 0;
        else if (cc === MoveRow2) scale = 1;
        else if (cc === MoveRow3) scale = 2;
        else if (cc === MoveRow4) scale = 3;
        host_module_set_param('sonify_scale', String(scale));
        updateLeds();
        setStatus('Scale ' + SCALE_NAMES[scale], true);
        announceParameter('Scale', SCALE_NAMES[scale]);
        needsRedraw = true;
        return;
    }

    if (shouldFilterMessage(msg)) return;

    if (status === MidiNoteOn && value > 0) {
        const idx = PAD_TABS.indexOf(cc);
        if (idx >= 0) {
            const now = Date.now();
            const isDoubleTap = lastPadPress.note === cc && (now - lastPadPress.time) < 450;
            lastPadPress = { note: cc, time: now };
            if (isDoubleTap) {
                const tabId = TABS[idx].id;
                if (tabId === 'orch') {
                    const next = host_module_get_param('sonify_enable') === '1' ? '0' : '1';
                    host_module_set_param('sonify_enable', next);
                    sonify.enabled = next === '1' ? 1 : 0;
                    setStatus(next === '1' ? 'Orchestra on' : 'Orchestra off', true);
                    announceParameter('Orchestra', next === '1' ? 'On' : 'Off');
                } else {
                    const scan = tabId === 'wifi';
                    refreshAll(scan);
                    setStatus(scan ? 'Pad refresh + scan' : 'Pad refresh', true);
                }
                return;
            }
            changeTab(idx);
            return;
        }
    }

    if (status !== 0xB0) return;

    if (cc === MovePlay && value > 0) {
        const scan = TABS[selectedTab].id === 'wifi' || TABS[selectedTab].id === 'orch';
        refreshAll(scan);
        announceRefreshResult(scan ? 'Refreshed and scanned' : 'Refreshed');
        return;
    }
    if (cc === MoveRec && value > 0) {
        const next = host_module_get_param('auto_refresh') === '1' ? '0' : '1';
        host_module_set_param('auto_refresh', next);
        updateLeds();
        setStatus(next === '1' ? 'Auto refresh on' : 'Auto refresh off', true);
        announceParameter('Auto Refresh', next === '1' ? 'On' : 'Off');
        return;
    }
    if (cc === MoveBack && value > 0) {
        if (typeof host_exit_module === 'function') host_exit_module();
        return;
    }

    let delta = decodeDelta(value);
    if (cc === MoveMainKnob && delta) {
        const next = (selectedTab + delta + TABS.length) % TABS.length;
        changeTab(next);
        return;
    }
    if (cc === MoveMainButton && value > 0) {
        if (TABS[selectedTab].id === 'orch') {
            const next = host_module_get_param('sonify_enable') === '1' ? '0' : '1';
            host_module_set_param('sonify_enable', next);
            const on = next === '1';
            sonify.enabled = on ? 1 : 0;
            setStatus(on ? 'Orchestra on' : 'Orchestra off', true);
            announceParameter('Orchestra', on ? 'On' : 'Off');
        } else {
            const scan = TABS[selectedTab].id === 'wifi';
            refreshAll(scan);
            announceRefreshResult(scan ? 'Refreshed and scanned' : 'Refreshed');
        }
        return;
    }
    if (!delta) return;

    if (cc === MoveMaster) {
        delta = takeStep('master', delta, 4);
        if (!delta) return;
        const current = parseFloat(host_module_get_param('sonify_gain') || '0.90');
        const next = adjustRelative(current, delta, 0.0, 2.0, 0.05);
        host_module_set_param('sonify_gain', next.toFixed(3));
        sonify.gain = next;
        announceParameter('Gain', Math.round(next * 100) + '%');
        setStatus('Gain ' + Math.round(next * 100) + '%', false);
        needsRedraw = true;
    } else if (cc === MoveKnob1) {
        delta = takeStep('k1', delta, 6);
        if (!delta) return;
        const current = parseInt(host_module_get_param('sonify_waveform') || '0', 10);
        let next = current + (delta > 0 ? 1 : -1);
        if (next < 0) next = 0;
        if (next >= WAVE_NAMES.length) next = WAVE_NAMES.length - 1;
        host_module_set_param('sonify_waveform', String(next));
        setStatus('Wave ' + WAVE_NAMES[next], true);
        announceParameter('Waveform', WAVE_NAMES[next]);
        needsRedraw = true;
    } else if (cc === MoveKnob2) {
        delta = takeStep('k2', delta, 4);
        if (!delta) return;
        const current = parseInt(host_module_get_param('sonify_root') || '0', 10);
        const next = (current + delta + 12) % 12;
        host_module_set_param('sonify_root', String(next));
        sonify.root = next;
        setStatus('Key ' + ROOT_NAMES[next], false);
        announceParameter('Key', ROOT_NAMES[next]);
        needsRedraw = true;
    } else if (cc === MoveKnob3) {
        delta = takeStep('k3', delta, 6);
        if (!delta) return;
        const current = parseInt(host_module_get_param('sonify_rate') || '0', 10);
        let next = current + (delta > 0 ? 1 : -1);
        if (next < 0) next = 0;
        if (next >= RATE_NAMES.length) next = RATE_NAMES.length - 1;
        host_module_set_param('sonify_rate', String(next));
        sonify.rate = next;
        announceParameter('Repeat Rate', RATE_NAMES[next]);
        setStatus('Rate ' + RATE_NAMES[next], false);
        needsRedraw = true;
    } else if (cc === MoveKnob4) {
        delta = takeStep('k4', delta, 4);
        if (!delta) return;
        const current = parseFloat(host_module_get_param('sonify_decay') || '900');
        const next = adjustRelative(current, delta, 50, 4000, 100);
        host_module_set_param('sonify_decay', next.toFixed(1));
        announceParameter('Decay', formatDecay(String(next)));
        setStatus('Decay ' + formatDecay(String(next)), false);
        needsRedraw = true;
    } else if (cc >= MoveKnob5 && cc <= MoveKnob8) {
        const panIndex = cc - MoveKnob5 + 1;
        const key = 'sonify_pan_' + panIndex;
        const accKey = 'k' + (panIndex + 4);
        delta = takeStep(accKey, delta, 4);
        if (!delta) return;
        const current = parseFloat(host_module_get_param(key) || '0');
        const next = adjustRelative(current, delta, -1.0, 1.0, 0.02);
        host_module_set_param(key, next.toFixed(3));
        announceParameter('Pan ' + panIndex, formatPan(String(next)));
        setStatus('Pan ' + panIndex + ' ' + formatPan(String(next)), false);
        needsRedraw = true;
    }
}

function onMidiMessageExternal(msg) {
    onMidiMessageInternal(msg);
}

function knobTouchValue(index) {
    if (index === 0) {
        const waveform = parseInt(host_module_get_param('sonify_waveform') || '0', 10);
        return WAVE_NAMES[waveform] || 'Sine';
    }
    if (index === 1) {
        const root = parseInt(host_module_get_param('sonify_root') || '0', 10);
        return ROOT_NAMES[root] || 'C';
    }
    if (index === 2) {
        const rate = parseInt(host_module_get_param('sonify_rate') || '0', 10);
        return formatRate(rate);
    }
    if (index === 3) {
        return formatDecay(host_module_get_param('sonify_decay') || '900');
    }
    if (index >= 4 && index <= 7) {
        return formatPan(host_module_get_param('sonify_pan_' + (index - 3)) || '0');
    }
    return '';
}

function tick() {
    tickCounter++;
    const now = Date.now();
    if (host_module_get_param('auto_refresh') === '1' && now - lastAutoRefreshAt >= 1000) {
        host_module_set_param('refresh_light', '1');
        overview = safeParse('overview_json', overview);
        topCpu = safeParse('top_cpu_json', topCpu);
        topMem = safeParse('top_mem_json', topMem);
        sonify = safeParse('sonify_status', sonify);
        lastAutoRefreshAt = now;
        needsRedraw = true;
    }
    if (statusTicks > 0) {
        statusTicks--;
        if (statusTicks === 0) needsRedraw = true;
    }
    if (needsRedraw) {
        draw();
        needsRedraw = false;
    }
}

globalThis.init = init;
globalThis.tick = tick;
globalThis.onMidiMessageInternal = onMidiMessageInternal;
globalThis.onMidiMessageExternal = onMidiMessageExternal;
