async function api(url, options = {}) {
    const r = await fetch(url, options);
    // const r = await fetch('http://192.168.0.16' + url, {});
    if (!r.ok) throw new Error(await r.text());
    return await r.json();
}

function setText(id, v) {
    const e = document.getElementById(id);
    if (e) e.textContent = v
}

async function setBuildingMode(mode) {
    try {
        const body = new URLSearchParams({cmd: 'set_building_mode', mode: String(mode)});
        const r = await fetch('/action', {
            method: 'POST',
            headers: {'Content-Type': 'application/x-www-form-urlencoded'},
            body
        });
        if (!r.ok) throw new Error(await r.text());
        await refreshStatus();
    } catch (e) {
        alert('Failed to update building mode: ' + (e.message || 'unknown error'));
        await refreshStatus();
    }
}

async function setZoneActiveSetpoint(event, zone) {
    event.preventDefault();
    const form = event.currentTarget;
    const temp = form.elements.temp.value;
    try {
        const body = new URLSearchParams({cmd: 'set_active_temp', zone: String(zone), temp: String(temp)});
        const r = await fetch('/action', {
            method: 'POST',
            headers: {'Content-Type': 'application/x-www-form-urlencoded'},
            body
        });
        if (!r.ok) throw new Error(await r.text());
        await refreshStatus();
    } catch (e) {
        alert('Failed to update active setpoint: ' + (e.message || 'unknown error'));
        await refreshStatus();
    }
}

async function setAutoChangeover(value) {
    try {
        const body = new URLSearchParams({cmd: 'set_auto_changeover', value: String(value)});
        const r = await fetch('/action', {
            method: 'POST',
            headers: {'Content-Type': 'application/x-www-form-urlencoded'},
            body
        });
        if (!r.ok) throw new Error(r.status);
        await refreshStatus();
    } catch (e) {
        alert('Failed to update automatic heating/cooling changeover');
        await refreshStatus();
    }
}

async function setZoneTemps(event, zone) {
    event.preventDefault();
    const form = event.currentTarget;
    const comfort = form.elements.comfort.value;
    const eco = form.elements.eco.value;
    if (comfort === '' && eco === '') {
        alert('Enter a comfort and/or eco temperature');
        return;
    }
    if (comfort !== '' && eco !== '') {
        const cooling = /cool|refro/i.test(String(window.lastBuildingMode || ''));
        const c = Number(comfort), e = Number(eco);
        if (!cooling && e > c) {
            alert('Heating mode: eco must be lower than or equal to comfort.');
            return;
        }
        if (cooling && e < c) {
            alert('Cooling mode: eco must be higher than or equal to comfort.');
            return;
        }
    }
    if (zone === 'all' && !confirm('Apply to ALL zones?')) return;
    try {
        const body = new URLSearchParams({cmd: 'set_zone_temps', zone: String(zone), comfort: comfort, eco: eco});
        const r = await fetch('/action', {
            method: 'POST',
            headers: {'Content-Type': 'application/x-www-form-urlencoded'},
            body
        });
        if (!r.ok) throw new Error(await r.text());
        if (zone === 'all') form.reset();
        await refreshStatus();
    } catch (e) {
        alert('Failed to update comfort/eco: ' + (e.message || 'unknown error'));
        await refreshStatus();
    }
}

async function setZoneSchedule(zone, program) {
    try {
        const body = new URLSearchParams({
            cmd: 'set_schedule_program',
            zone: String(zone),
            program: String(program)
        });
        const r = await fetch('/action', {
            method: 'POST',
            headers: {'Content-Type': 'application/x-www-form-urlencoded'},
            body
        });
        if (!r.ok) throw new Error(await r.text());
        await refreshStatus();
    } catch (e) {
        alert('Failed to update zone schedule: ' + (e.message || 'unknown error'));
        await refreshStatus();
    }
}


async function setZonePower(zone, on) {
    try {
        const body = new URLSearchParams({cmd: on ? 'zone_on' : 'zone_off', zone: String(zone - 1)});
        const r = await fetch('/action', {
            method: 'POST',
            headers: {'Content-Type': 'application/x-www-form-urlencoded'},
            body
        });
        if (!r.ok) throw new Error(await r.text());
        await refreshStatus();
    } catch (e) {
        alert('Failed to update zone power: ' + (e.message || 'unknown error'));
        await refreshStatus();
    }
}

async function setZoneMode(zone, mode) {
    try {
        const body = new URLSearchParams({cmd: 'set_mode', zone: String(zone), mode: String(mode)});
        const r = await fetch('/action', {
            method: 'POST',
            headers: {'Content-Type': 'application/x-www-form-urlencoded'},
            body
        });
        if (!r.ok) throw new Error(r.status);
        await refreshStatus();
    } catch (e) {
        alert('Failed to update zone mode');
        await refreshStatus();
    }
}

function time(raw) {
    raw = Number(raw);
    if (raw === 65535 || raw < 0) return '—';
    const min = (raw >> 8) & 255, hour = raw & 255;
    return hour > 23 || min > 59 ? '—' : String(hour).padStart(2, '0') + ':' + String(min).padStart(2, '0');
}

function timeValue(raw) {
    const t = time(raw);
    return t === '—' ? '' : t;
}

async function refreshStatus() {
    try {
        const s = await api('/api/status');
        setText('datetime', s.datetime);
        setText('configured-zones', s.configured_zones);
        const m = document.getElementById('building-mode');
        if (m) m.value = (s.building_mode || 'off');
        window.lastBuildingMode = s.building_mode || 'off';
        const ac = document.getElementById('auto-changeover');
        if (ac) ac.value = s.auto_changeover || 'off';
        setText('outdoor-temp', Number(s.outdoor_temperature).toFixed(1));
        setText('active-fault', s.active_fault ? '⚠️' : '(None)');
        const zonesOk = zonesUsable(s);
        setText('zones-notice', zonesNotice(s));
        document.querySelectorAll('#global-temps input, #global-temps button').forEach(el => { el.disabled = !zonesOk; });
        const activeEl = document.activeElement;
        const zonesBody = document.getElementById('zones-body');
        const editingZone = activeEl && activeEl.tagName === 'INPUT' && zonesBody && zonesBody.contains(activeEl);
        if (!editingZone) renderZones(s);
        renderSchedules(s);
        const c = document.getElementById('connection');
        if (c) {
            c.innerHTML = '<span class="dot"></span>Communication ' + (s.modbus_error ? 'unavailable' : 'OK');
            c.className = 'badge ' + (s.modbus_error ? 'off' : 'on');
        }
    } catch (e) {
        const c = document.getElementById('connection');
        if (c) {
            c.textContent = '<span class="dot"></span>Communication unavailable';
            c.className = 'badge off';
        }
    }
}

// The Shogun's active setpoint is read-only: editing it changes the setpoint that currently drives the zone
// (manual setpoint in manual mode, Comfort or Eco setpoint in schedule mode). Off / unused zones are not editable.
function activeSetpointKind(z) {
    const state = Number(z.heating_state);
    if (state === 0 || state === 3 || Number(z.program_status) === 3) return null;
    if (Number(z.control_mode) === 1 || state === 4) return 'manual';
    return state === 1 ? 'comfort' : (state === 2 ? 'eco' : null);
}

// Building mode off (stand-by, frost protection only) or not known yet: the zones are stopped, so their power, mode,
// program and setpoints have no effect. Building states: 1 heating, 2 cooling, 3 dehumidification, 4 off, 0 unused.
function zonesUsable(data) {
    const state = Number(data.building_state);
    return state === 1 || state === 2 || state === 3;
}

function zonesNotice(data) {
    if (zonesUsable(data)) return '';
    return Number(data.building_state) === 0
        ? 'The building mode is not known yet: the zone controls are disabled.'
        : 'Building mode is off: the zones are stopped, so their controls are disabled.';
}

function activeSetpointCell(z, usable) {
    const value = Number(z.target).toFixed(1);
    const kind = activeSetpointKind(z);
    const off = usable && kind ? '' : ' disabled';
    const min = kind === 'manual' && Number(z.min_setpoint) > 0 ? Number(z.min_setpoint) : 10;
    const max = kind === 'manual' && Number(z.max_setpoint) > 0 ? Number(z.max_setpoint) : 30;
    const title = kind ? `Edits the ${kind} setpoint` : 'The zone is off';
    const note = kind || (usable ? 'off' : '');
    return `<form class="inline-form" onsubmit="setZoneActiveSetpoint(event, ${z.zone - 1})"><input type="number" name="temp" step="0.5" min="${min}" max="${max}" title="${title}" value="${value}"${off}><button type="submit" class="schedule-table btn btn-light temp-btn"${off}>Set</button></form><small class="muted">${note}</small>`;
}

function renderZones(data) {
    const body = document.getElementById('zones-body');
    if (!body || !data.zones) return;
    body.innerHTML = data.zones.map(z => {
        const program = Number(z.schedule_program);
        const controlMode = Number(z.control_mode);
        const usable = zonesUsable(data);
        const off = usable ? '' : ' disabled';
        const isOn = Number(z.program_status) !== 3;
        return `
      <tr>
        <td><strong>${z.name || ('Zone ' + z.zone)}</strong></td>
        <td class="big-value">${Number(z.temp).toFixed(1)} °C</td>
        <td class="zone-setpoint">${activeSetpointCell(z, usable)}</td>
        <td class="zone-setpoint"><form class="inline-form" onsubmit="setZoneTemps(event, ${z.zone})"><input type="number" name="comfort" class="temp-in"${off} step="0.5" min="10" max="30" title="Comfort" value="${Number(z.comfort_setpoint).toFixed(1)}"><input type="number" name="eco" class="temp-in"${off} step="0.5" min="10" max="30" title="Eco" value="${Number(z.eco_setpoint).toFixed(1)}"><button type="submit" class="schedule-table btn btn-light temp-btn"${off}>Set</button></form></td>
        <td><button type="button" class="power-toggle ${isOn ? 'on' : 'off'}"${off} title="${isOn ? 'Turn off' : 'Turn on'}" onclick="setZonePower(${z.zone}, ${!isOn})">${isOn ? 'On' : 'Off'}</button></td>
        <td><select onchange="setZoneMode(${z.zone - 1}, this.value)" class="select"${off}><option value="0" ${controlMode === 0 ? 'selected' : ''}>Schedule</option><option value="1" ${controlMode === 1 ? 'selected' : ''}>Manual</option></select></td>
        <td><select onchange="setZoneSchedule(${z.zone}, this.value)" class="select"${off}><option value="1" ${program === 1 ? 'selected' : ''}>Schedule 1</option><option value="2" ${program === 2 ? 'selected' : ''}>Schedule 2</option><option value="3" ${program === 3 ? 'selected' : ''}>Schedule 3</option><option value="4" ${program === 4 ? 'selected' : ''}>Schedule 4</option></select></td>
      </tr>`;
    }).join('');
}

// ---------- Schedules: one line per day, a click opens a day, nothing is sent before "Save" ----------

const DAYS = ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday'];
const DEFAULT_TIMES = ['06:00', '09:00', '11:30', '13:30', '17:00', '23:00'];
const TIME = /^([01]\d|2[0-3]):[0-5]\d$/;

const sched = {
    data: null,             // last /api/status answer
    program: 1,
    open: null,             // index of the day being edited
    zonesOpen: false,
    drafts: new Map(),      // "<program>-<day>" -> six times not saved yet
    failed: new Map(),      // "<program>-<day>" -> why the last saving of this day failed
    saving: false,
    notice: '',
    view: null
};

function el(tag, props = {}, ...children) {
    const node = document.createElement(tag);
    for (const [key, value] of Object.entries(props)) {
        if (key.startsWith('aria-')) node.setAttribute(key, value);
        else node[key] = value;
    }
    node.append(...children);
    return node;
}

const dayKey = (day) => `${sched.program}-${day}`;
const minutes = (t) => Number(t.slice(0, 2)) * 60 + Number(t.slice(3));
const formatDay = (times) => times.map(t => t || '--:--').join(' ');

// The six times held by the Shogun for a "<program>-<day>" key ('' = not used), or null when they are not read yet.
function savedDay(key) {
    const data = sched.data;
    if (!data || !data.schedules || data.schedules_read === false) return null;
    const [program, day] = key.split('-').map(Number);
    return data.schedules[program - 1].slice(day * 6, day * 6 + 6).map(timeValue);
}

function dayError(times) {
    for (let i = 0; i < 6; i += 2) {
        if (!times[i] !== !times[i + 1]) return 'Each period needs a start and an end.';
    }
    let last = -1;
    for (const t of times) {
        if (!t) continue;
        if (minutes(t) <= last) return 'The times must be in ascending order.';
        last = minutes(t);
    }
    return '';
}

// The Comfort periods of a day as text. An incomplete period keeps its "--:--".
function summarize(times) {
    const periods = [0, 2, 4]
        .filter(i => times[i] || times[i + 1])
        .map(i => `${times[i] || '--:--'}–${times[i + 1] || '--:--'}`);
    return periods.join(' · ') || 'No Comfort period';
}

async function postAction(params) {
    const r = await fetch('/action', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: new URLSearchParams(params)
    });
    if (!r.ok) throw new Error(await r.text() || r.status);
}

function setDraft(key, times) {
    sched.failed.delete(key);
    sched.notice = '';
    const saved = savedDay(key);
    if (saved && formatDay(saved) === formatDay(times)) sched.drafts.delete(key);
    else sched.drafts.set(key, times);
}

function editTime(key, slot, value) {
    const saved = savedDay(key);
    if (!saved) return;
    const times = [...(sched.drafts.get(key) || saved)];
    times[slot] = value;
    setDraft(key, times);
}

function copyDay(source, targets) {
    const key = dayKey(source);
    const times = sched.drafts.get(key) || savedDay(key);
    if (!times) return;
    for (const day of targets) setDraft(dayKey(Number(day)), [...times]);
    updateSchedules();
}

function buildDayRow(name, day) {
    const state = el('small');
    const bar = el('span', {className: 'day-bar', 'aria-hidden': 'true'});
    const summary = el('span', {className: 'day-summary'});
    const chevron = el('span', {className: 'muted', 'aria-hidden': 'true'});
    const head = el('button', {type: 'button', className: 'day-head'},
        el('span', {className: 'day-name'}, name, state), bar, summary, chevron);
    head.addEventListener('click', () => {
        sched.open = sched.open === day ? null : day;
        updateSchedules();
    });

    const periods = [0, 1, 2].map(() => el('div', {className: 'period'}));
    const groups = periods.map((period, i) =>
        el('div', {}, el('small', {className: 'muted', textContent: `Comfort ${i + 1}: start → end`}), period));
    const copy = el('select', {className: 'day-copy', 'aria-label': `Copy ${name} to`});
    [['', 'Copy to…'], ['0123456', 'Whole week'], ['01234', 'Monday to Friday'], ['56', 'Week-end']]
        .forEach(([value, text]) => copy.append(el('option', {value, textContent: text})));
    copy.addEventListener('change', () => {
        const targets = copy.value;
        copy.value = '';
        copyDay(day, targets);
    });

    const editor = el('div', {className: 'day-editor'}, ...groups, copy);
    const error = el('div', {className: 'day-error'});
    const node = el('div', {className: 'day'}, head, editor, error);
    return {name, day, node, state, bar, summary, chevron, head, periods, copy, editor, error, drawn: null, slots: []};
}

function buildSlot(row, key, value, slot, disabled) {
    const label = `${row.name}, ${slot % 2 ? 'end' : 'start'} of period ${Math.floor(slot / 2) + 1}`;
    const box = el('div', {className: value ? 'slot' : 'slot empty'});
    if (!value) {
        const add = el('button', {type: 'button', textContent: '+ time', disabled, 'aria-label': `Add: ${label}`});
        add.addEventListener('click', () => {
            editTime(key, slot, DEFAULT_TIMES[slot]);
            updateSchedules();
            const input = row.slots[slot].querySelector('input');
            if (input) input.focus();
        });
        box.append(add);
        return box;
    }
    const input = el('input', {type: 'time', value, disabled, required: true, 'aria-label': label});
    input.addEventListener('input', () => {
        if (!TIME.test(input.value)) return;        // incomplete entry
        editTime(key, slot, input.value);
        // Keep these fields as they are while the user types: only the rest is refreshed.
        row.drawn = JSON.stringify([key, sched.drafts.get(key) || savedDay(key), disabled]);
        updateSchedules();
    });
    input.addEventListener('blur', () => {
        const times = sched.drafts.get(key) || savedDay(key);
        if (!input.value && times) input.value = times[slot];
    });
    const remove = el('button', {type: 'button', textContent: '×', disabled, 'aria-label': `Remove: ${label}`});
    remove.addEventListener('click', () => {
        editTime(key, slot, '');
        updateSchedules();
    });
    box.append(input, remove);
    return box;
}

function updateDayRow(row) {
    const key = dayKey(row.day);
    const saved = savedDay(key);
    const draft = saved ? sched.drafts.get(key) : undefined;
    const times = draft || saved;
    const disabled = !times || sched.saving;

    // The fields are rebuilt only when their content changes.
    const drawn = JSON.stringify([key, times, disabled]);
    if (drawn !== row.drawn) {
        row.drawn = drawn;
        row.slots = DEFAULT_TIMES.map((_, slot) => buildSlot(row, key, times ? times[slot] : '', slot, disabled));
        row.periods.forEach((period, i) => period.replaceChildren(
            row.slots[2 * i], el('span', {className: 'muted', textContent: '→'}), row.slots[2 * i + 1]));
    }

    row.state.textContent = !times ? 'waiting for data' : (draft ? 'modified' : '');
    row.node.classList.toggle('changed', !!draft);
    row.summary.textContent = times ? summarize(times) : '';

    row.bar.replaceChildren();
    for (let i = 0; times && i < 6; i += 2) {
        if (!times[i] || !times[i + 1] || minutes(times[i + 1]) <= minutes(times[i])) continue;
        const mark = el('i');
        mark.style.left = `${minutes(times[i]) / 14.4}%`;
        mark.style.width = `${(minutes(times[i + 1]) - minutes(times[i])) / 14.4}%`;
        row.bar.append(mark);
    }

    const invalid = draft ? dayError(draft) : '';
    const message = invalid || (draft ? sched.failed.get(key) || '' : '');
    row.error.textContent = message;
    row.error.hidden = !message;
    row.slots.forEach(s => s.classList.toggle('bad', !!invalid && !s.classList.contains('empty')));

    const open = !!times && sched.open === row.day;
    row.head.disabled = !times;
    row.head.setAttribute('aria-expanded', String(open));
    row.head.title = open ? 'Close' : 'Edit';
    row.chevron.textContent = open ? '▴' : '▾';
    row.editor.hidden = !open;
    row.copy.disabled = disabled;
}

// Zones of the program shown. "Edit" reveals them all: a click on a zone assigns it to this program at once.
function updateScheduleZones(box) {
    const data = sched.data;
    const usable = zonesUsable(data);
    const zones = data.zones.map((z, i) => ({
        zone: i + 1, name: z.name || `Zone ${i + 1}`, program: Number(data.schedule_assignments[i])
    }));
    const drawn = JSON.stringify([sched.program, sched.zonesOpen, usable, zones]);
    if (drawn === box.dataset.drawn) return;
    box.dataset.drawn = drawn;

    const toggle = el('button', {
        type: 'button', className: 'link', textContent: sched.zonesOpen ? 'Close' : 'Edit',
        'aria-expanded': String(sched.zonesOpen), 'aria-label': `${sched.zonesOpen ? 'Close' : 'Edit'} zones`
    });
    toggle.addEventListener('click', () => {
        sched.zonesOpen = !sched.zonesOpen;
        updateSchedules();
        box.querySelector('.link').focus();
    });
    if (!sched.zonesOpen) {
        const names = zones.filter(z => z.program === sched.program).map(z => z.name);
        box.replaceChildren(el('span', {textContent: `Zones: ${names.join(', ') || 'none'}`}), toggle);
        return;
    }
    const chips = zones.map(z => {
        const assigned = z.program === sched.program;
        const chip = el('button', {
            type: 'button', className: 'chip', disabled: !usable, 'aria-pressed': String(assigned),
            textContent: `${z.name} ${assigned ? '✓' : `(P${z.program})`}`
        });
        chip.addEventListener('click', async () => {
            if (assigned) return;
            sched.notice = '';
            try {
                await postAction({cmd: 'set_schedule_program', zone: z.zone, program: sched.program});
            } catch (e) {
                sched.notice = `${z.name}: ${e.message}`;
            }
            await refreshStatus();
            updateSchedules();
        });
        return chip;
    });
    box.replaceChildren(el('span', {textContent: 'Zones on this schedule:'}), ...chips, toggle);
}

function buildSchedules(body) {
    const tabs = [1, 2, 3, 4].map(program => {
        const tab = el('button', {type: 'button', 'aria-label': `Schedule ${program}`},
            el('span', {className: 'tab-word', textContent: 'Schedule '}), String(program));
        tab.addEventListener('click', () => {
            sched.program = program;
            sched.open = null;
            sched.zonesOpen = false;
            updateSchedules();
        });
        return tab;
    });
    const zones = el('div', {className: 'schedule-zones'});
    const rows = DAYS.map(buildDayRow);
    const status = el('span', {className: 'muted', 'aria-live': 'polite'});
    const cancel = el('button', {type: 'button', className: 'btn btn-light', textContent: 'Cancel'});
    cancel.addEventListener('click', () => {
        sched.drafts.clear();
        sched.failed.clear();
        sched.notice = '';
        sched.open = null;
        updateSchedules();
    });
    const save = el('button', {type: 'button', className: 'btn btn-blue', textContent: 'Save'});
    save.addEventListener('click', saveSchedules);
    const actions = el('div', {className: 'schedule-actions'}, cancel, save);
    const footer = el('div', {className: 'schedule-footer'}, status, actions);

    body.replaceChildren(
        el('div', {className: 'tabs', role: 'group', 'aria-label': 'Schedule shown'}, ...tabs),
        zones, ...rows.map(r => r.node), footer);
    return {tabs, zones, rows, status, cancel, save, actions, footer};
}

function updateSchedules() {
    const body = document.getElementById('schedules-body');
    const data = sched.data;
    if (!body || !data || !data.schedules || !data.schedule_assignments || !data.zones) return;
    if (!sched.view) sched.view = buildSchedules(body);
    const view = sched.view;

    // A draft that the Shogun now holds is no longer a modification.
    for (const [key, times] of sched.drafts) {
        const saved = savedDay(key);
        if (saved && formatDay(saved) === formatDay(times)) {
            sched.drafts.delete(key);
            sched.failed.delete(key);
        }
    }

    view.tabs.forEach((tab, i) => {
        tab.setAttribute('aria-pressed', String(sched.program === i + 1));
        tab.classList.toggle('dot-mark', DAYS.some((_, day) => sched.drafts.has(`${i + 1}-${day}`)));
    });
    updateScheduleZones(view.zones);
    view.rows.forEach(updateDayRow);

    const changed = [...sched.drafts.values()];
    const invalid = changed.filter(dayError).length;
    let status = '';
    if (sched.saving) status = `Saving… ${changed.length} day(s) left`;
    else if (invalid) status = `${invalid} day(s) to correct before saving`;
    else if (sched.failed.size) status = `${sched.failed.size} day(s) not saved`;
    else if (sched.notice) status = sched.notice;
    else if (changed.length) status = `${changed.length} day(s) modified, not saved`;
    view.status.textContent = status;
    // The footer only appears when there is something to save or to report.
    view.footer.hidden = !status;
    view.actions.hidden = !changed.length;
    view.save.disabled = sched.saving || !changed.length || invalid > 0;
    view.cancel.disabled = sched.saving;
}

// The days are sent one at a time: the Shogun answers once it has read the times back.
async function saveSchedules() {
    const pending = [...sched.drafts].filter(([, times]) => !dayError(times));
    sched.saving = true;
    sched.open = null;
    sched.notice = '';
    sched.failed.clear();
    updateSchedules();
    let saved = 0;
    for (const [key, times] of pending) {
        const [program, day] = key.split('-');
        try {
            await postAction({cmd: 'set_schedule_day', program, day, times: formatDay(times)});
            await refreshStatus();
            sched.drafts.delete(key);
            saved++;
        } catch (e) {
            sched.failed.set(key, `Not saved: ${e.message || 'unknown error'}`);
            await refreshStatus();
        }
        updateSchedules();
    }
    sched.saving = false;
    if (!sched.failed.size) sched.notice = `${saved} day(s) saved`;
    updateSchedules();
}

function renderSchedules(data) {
    sched.data = data;
    updateSchedules();
}

window.addEventListener('beforeunload', event => {
    if (sched.drafts.size) event.preventDefault();
});

document.addEventListener('DOMContentLoaded', () => {
    refreshStatus();
    setInterval(refreshStatus, 60000)
})
