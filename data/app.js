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

function onTimeChange(input, program, day, slot) {
    const btn = input.parentNode.querySelector('.time-clear');
    if (btn) btn.hidden = !input.value;
    return setScheduleTime(program, day, slot, input.value);
}

function clearTime(btn, program, day, slot) {
    const input = btn.parentNode.querySelector('.time-input');
    if (input) input.value = '';
    btn.hidden = true;
    btn.blur();
    return setScheduleTime(program, day, slot, '');
}

async function setScheduleTime(program, day, slot, value) {
    try {
        const body = new URLSearchParams({
            cmd: 'set_schedule_time',
            program: String(program),
            day: String(day),
            slot: String(slot),
            time: value ? String(value) : 'none'
        });
        const r = await fetch('/action', {
            method: 'POST',
            headers: {'Content-Type': 'application/x-www-form-urlencoded'},
            body
        });
        if (!r.ok) throw new Error(await r.text());
        await refreshStatus();
    } catch (e) {
        alert('Failed to update schedule time: ' + (e.message || 'unknown error'));
        await refreshStatus();
    }
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
        const ae = document.activeElement;
        const editingTime = ae && ae.classList && ae.classList.contains('time-input');
        if (!editingTime) renderSchedules(s);
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

const collapsedSchedules = new Set();

function toggleSchedule(i, isOpen) {
    if (isOpen) collapsedSchedules.delete(i); else collapsedSchedules.add(i);
}

function renderSchedules(data) {
    const body = document.getElementById('schedules-body');
    if (!data.schedules || !data.schedule_assignments || !data.zones) return;
    var result = '';
    var days = ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"];
    for (let i = 0; i < 4; i++) {
        collapsedSchedules.add(i);
        var assigned = '';
        for (let j = 0; j < data.schedule_assignments.length; j++) {
            const scheduleAssignment = data.schedule_assignments[j];
            if (i + 1 == scheduleAssignment) {
                if (assigned != '') {
                    assigned += ', ';
                }
                assigned += data.zones[j].name;
            }
        }
        if (assigned == '') {
            assigned = 'No zone';
        }
        result += `
            <details class='schedule-card' ${collapsedSchedules.has(i) ? '' : 'open'} ontoggle='toggleSchedule(${i}, this.open)'>
                <summary class='schedule-card-head'>
                    <div><strong>Schedule ${i + 1}</strong><small>Zones: ${assigned}</small></div>
                    <span class='badge'>P${i + 1}</span>
                </summary>
                <div class='table-wrap'>
                <table class='schedule-table'>
                    <thead>
                    <tr>
                        <th>Day</th>
                        <th>Comfort 1</th>
                        <th>Eco 1</th>
                        <th>Comfort 2</th>
                        <th>Eco 2</th>
                        <th>Comfort 3</th>
                        <th>Eco 3</th>
                    </tr>
                    </thead>
                    <tbody>
            `;
        for (let day = 0; day < 7; day++) {
            result += `<tr><th>${days[day]}</th>`;
            for (let j = 0; j < 6; j++) {
                const tv = timeValue(data.schedules[i][day * 6 + j]);
                result += `<td class="time-cell"><input type="time" class="time-input" value="${tv}" onchange="onTimeChange(this, ${i + 1}, ${day}, ${j})"><button type="button" class="time-clear" title="Unset" ${tv ? '' : 'hidden'} onclick="clearTime(this, ${i + 1}, ${day}, ${j})">×</button></td>`;
            }
            result += `</tr>`;
        }
        result += `    
                    </tbody>
                    </table>
                </div>
                </details>`;
    }
    body.innerHTML = result;
}

document.addEventListener('DOMContentLoaded', () => {
    refreshStatus();
    setInterval(refreshStatus, 60000)
})