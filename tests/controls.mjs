import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

let source = readFileSync(new URL('../src/ui.js', import.meta.url), 'utf8');
const constants = Object.fromEntries([...source.matchAll(/^import\s*\{([\s\S]+?)\}\s*from .+?;/gm)]
    .flatMap(m => m[1].split(',').map(name => name.trim()).filter(Boolean))
    .map((name, index) => [name, 64 + index]));
source = source.replace(/^import[\s\S]*?;\r?\n/gm, '');
Object.assign(constants, {
    MidiNoteOn: 144, MoveStep1: 16, MoveStep2: 17, MoveStep3: 18,
    MoveStep4: 19, MoveStep5: 20, MoveStep6: 21, MovePads: [68,69,70,71,72,73,74,75]
});
let overview = { cpu_pct: 31, cpu_temp_available: true, cpu_temp_c: 40 };
let disk = { available: true, write_bps: 2048, read_bps: 1024 };
let state = { enabled: 0, voices: 3, frozen: false };
const calls = [], spoken = [];
const empty = () => {};
const context = vm.createContext({
    ...constants,
    announce: text => spoken.push(text),
    announceView: empty, announceParameter: empty, print: empty,
    decodeDelta: () => 0, shouldFilterMessage: () => false,
    clearAllLEDs: empty, setLED: empty, setButtonLED: empty,
    host_module_set_param: (key, val) => {
        calls.push([key, val]);
        if (key === 'sonify_frozen') state.frozen = val === '1' && state.voices > 0;
        if (key === 'sonify_source') state.frozen = false;
    },
    host_module_get_param: key => ({
        overview_json: JSON.stringify(overview), sonify_status: JSON.stringify(state),
        disk_activity_json: JSON.stringify(disk), top_cpu_json: '[]', top_mem_json: '[]',
        wifi_scan_json: '[]', sonify_frozen: state.frozen ? '1' : '0'
    })[key] ?? ''
});
vm.runInContext(source, context);
const press = note => context.onMidiMessageInternal([144, note, 127]);
const external = note => context.onMidiMessageExternal([144, note, 127]);
context.init();
overview = { cpu_pct: 10, cpu_temp_available: true, cpu_temp_c: 35 };
press(20);
assert.match(spoken.at(-1), /peak 31\.0 percent/i);
assert.match(spoken.at(-1), /peak 40\.0 degrees/i);
press(21);
assert.match(spoken.at(-1), /peaks reset/i);
press(20);
assert.match(spoken.at(-1), /peak 10\.0 percent/i);
overview.cpu_temp_available = false;
press(21);
press(20);
assert.match(spoken.at(-1), /temperature unavailable/i);
assert.doesNotMatch(spoken.at(-1), /35\.0 degrees/);

press(75);
press(20);
assert.equal(state.frozen, true);
assert.match(spoken.at(-1), /frozen.*3 voices/i);
press(17);
assert.equal(state.frozen, false);
assert.match(spoken.at(-1), /live/i);
press(20);
press(20);
assert.equal(state.frozen, false);
assert.match(spoken.at(-1), /live/i);
state.voices = 0;
press(20);
assert.match(spoken.at(-1), /no voices/i);
external(20);
assert.equal(state.frozen, false);
assert.equal(calls.filter(([key]) => key === 'sonify_frozen').length, 4);

press(73);
press(20);
assert.match(spoken.at(-1), /read.*1\.0 kilobytes per second/i);
press(21);
assert.match(spoken.at(-1), /write.*2\.0 kilobytes per second/i);
disk = { available: false };
press(21);
assert.match(spoken.at(-1), /activity unavailable/i);
console.log('SignalScope control tests passed.');
