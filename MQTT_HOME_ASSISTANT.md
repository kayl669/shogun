# Shogun — MQTT / Home Assistant

The firmware publishes the Shogun state through MQTT and creates the Home Assistant entities with MQTT Discovery.
Everything below is relative to the base topic `shogun/<unique_id>`.

## Contents

1. [Topics](#topics)
2. [State messages](#state-messages)
3. [MQTT debug](#mqtt-debug)
4. [Home Assistant entities](#home-assistant-entities)
5. [Global commands](#global-commands)
6. [Zone commands](#zone-commands)
7. [Estimated electrical power and energy](#estimated-electrical-power-and-energy)
8. [Modbus register reference](#modbus-register-reference)
9. [Web page](#web-page)

## Topics

| Topic | Direction | Content |
|---|---|---|
| `shogun/<unique_id>/availability` | published, retained | `online` / `offline` (also the MQTT last will) |
| `shogun/<unique_id>/state/<section>` | published, retained | JSON state, one topic per section (see below) |
| `shogun/<unique_id>/debug` | published, not retained | JSON debug messages |
| `shogun/<unique_id>/debug/level` | published, retained | current debug level |
| `shogun/<unique_id>/debug/level/set` | command | set the debug level |
| `shogun/<unique_id>/debug/command` | command | `level` republishes the debug level |
| `shogun/<unique_id>/building/mode` | command | building mode |
| `shogun/<unique_id>/building/auto_changeover` | command | automatic heating/cooling changeover |
| `shogun/<unique_id>/system/reset_energy` | command | reset the estimated energy counter |
| `shogun/<unique_id>/zone/<zone>/<command>` | command | zone commands, `<zone>` = 1–8 (configured zones only) |

Zone commands: `setpoint`, `active_setpoint`, `mode`, `comfort`, `eco`, `min_setpoint`, `max_setpoint`,
`control_mode`, `schedule`, `power`, `sensor_lock`. Command payloads are case-insensitive where they are words.

The first publication of the state happens after the first complete Modbus read cycle; after that the state is republished
at the end of every successful cycle (about every 15 s) and immediately after every accepted command.

## State messages

All state topics are retained JSON.

| Topic | Fields |
|---|---|
| `state/system` | `central_fault` (active fault code, 0 = none), `detected_zone_count`, `outdoor_temperature` (°C), `building_state` (number), `building_mode` (`heat`, `cool`, `dry`, `off`, `unknown`), `auto_changeover` (`"on"` / `"off"`) |
| `state/ui` | `average_ambient_temperature`, `average_setpoint_temperature` (°C), `estimated_power_w`, `estimated_energy_kwh` |
| `state/schedule_assignments` | array with the program number (1–4) assigned to each configured zone |
| `state/schedules` | `program_1` … `program_4`, each an array of 42 raw values (7 days × 6 slots, see [schedules](#schedule-time-slots)) |
| `state/zone_<N>` | see the table below |

`state/zone_<N>` fields:

| Field | Meaning |
|---|---|
| `name` | zone name read from the Shogun |
| `temp` | ambient temperature (°C) |
| `target` | **active setpoint** the zone regulates to (°C, read-only on the Shogun) |
| `set` | manual setpoint (°C) |
| `mode` | `off` when the zone is off, otherwise `heat` or `cool` following the building mode |
| `control_mode` | `0` = follows its schedule, `1` = manual |
| `damper` | damper opening |
| `prog` | zone program status: `0` unused, `1` Comfort, `2` Eco, `3` Off, `4` Manual |
| `comfort`, `eco` | Comfort and Eco setpoints (°C) |
| `min_setpoint`, `max_setpoint` | zone limits for the manual setpoint (°C) |
| `sensor_lock` | `0` / `1` |
| `power` | `ON` / `OFF` (`OFF` when `prog` is `3`) |
| `schedule_program` | program number (1–4) assigned to the zone |

Temperatures are in °C with one decimal. The Shogun registers hold tenths of °C. Setpoint values that read `0.0` have not
been read yet.

## MQTT debug

Debug messages are published to `shogun/<unique_id>/debug` as JSON with a `ts` (uptime in ms), a `level` and an `event`.
They are not retained. The verbosity is stored in non-volatile configuration and survives a reboot.

Set the level with `shogun/<unique_id>/debug/level/set`; the current level is published retained on
`shogun/<unique_id>/debug/level`:

| Level | Payload | Messages |
|---|---|---|
| `off` | `off` or `0` | none |
| `error` | `error`, `errors` or `1` | errors and warnings (**default**) |
| `info` | `info` or `2` | + informational events |
| `trace` | `trace` or `3` | + every Modbus request and response |

The only command accepted on `debug/command` is `level` (republish the level). Any other payload gives a
`debug_command_unknown` warning.

Events you can see at the default level (warnings and errors):

| Event | Meaning |
|---|---|
| `mqtt_command_rejected` | command refused: payload out of range (10–30 °C) or not valid |
| `mqtt_setpoint_not_kept` | the Shogun kept another manual setpoint than the requested one (zone limits) |
| `mqtt_active_setpoint_not_applied` | the active setpoint could not be edited (zone off, other value kept) |
| `mqtt_setpoint_clamped` | the Shogun kept another Comfort / Eco value (ordering rule) |
| `mqtt_building_mode_not_applied` | the Shogun did not accept the requested building mode |
| `mqtt_zone_power_failed` | the zone power register could not be written (Modbus error) |
| `schedule_assignment_mismatch` | the program assigned to a zone is not the one requested |
| `debug_level_invalid`, `debug_command_unknown` | invalid payload on a debug topic |

Informational (`info`): `debug_level_changed`, `estimated_energy_reset`, `homeassistant_discovery_published`.
`trace`: `modbus_request`, `modbus_response` (with the register values read), `modbus_write_request` and `modbus_write_response`. Every response also gives the slave address, the result code and the elapsed time (`elapsed_ms`).

## Home Assistant entities

MQTT Discovery configurations are retained. They are published **once after each boot**, when MQTT is connected and all zone
names have been read. After renaming a zone or changing the number of zones, reboot the ESP32 to republish them.
Every discovery topic has the form `homeassistant/<component>/<unique_id>/<object_id>/config`, which is the only form Home Assistant accepts.
All entities use the availability topic and belong to the same Atlantic Shogun device.

**Global entities**

| Entity | Type | State | Command |
|---|---|---|---|
| Building mode | select (`heat`, `cool`, `dry`, `off`) | `state/system` → `building_mode` | `building/mode` |
| Automatic heating/cooling changeover | switch | `state/system` → `auto_changeover` | `building/auto_changeover` |
| Estimated power | sensor, W | `state/ui` → `estimated_power_w` | – |
| Estimated energy | sensor, kWh | `state/ui` → `estimated_energy_kwh` | – |
| Reset estimated energy | button (config) | – | `system/reset_energy` |

**Per zone** (state from `state/zone_<N>`)

| Entity | Type | State field | Command |
|---|---|---|---|
| `<zone>` | climate (`off`, `heat`, `cool`) | `temp`, `target`, `mode` | `setpoint`, `mode` |
| `<zone> Temperature` | sensor | `temp` | – |
| `<zone> Damper` | sensor | `damper` | – |
| `<zone> Program` | sensor | `prog` | – |
| `<zone> Comfort setpoint` | number 10–30 °C | `comfort` | `comfort` |
| `<zone> Eco setpoint` | number 10–30 °C | `eco` | `eco` |
| `<zone> Active setpoint` | number 10–30 °C | `target` | `active_setpoint` |
| `<zone> Manual setpoint` | number 10–30 °C | `set` | `setpoint` |
| `<zone> Min manual setpoint` | number (config) | `min_setpoint` | `min_setpoint` |
| `<zone> Max manual setpoint` | number (config) | `max_setpoint` | `max_setpoint` |
| `<zone> Control mode` | select (`schedule`, `manual`) | `control_mode` | `control_mode` |
| `<zone> Schedule program` | select (`1`–`4`) | `schedule_program` | `schedule` |
| `<zone> Power` | switch | `power` | `power` |
| `<zone> Sensor lock` | switch (config) | `sensor_lock` | `sensor_lock` |

The climate entity shows the **active setpoint** (`target`) and its temperature command writes the manual setpoint (see
[`setpoint`](#setpoint-manual-setpoint)). Its range is 10–30 °C, step 0.5; the number entities share the same fixed range, whereas the
Shogun applies its own zone limits (see `min_setpoint` / `max_setpoint`).

Not exposed on purpose: the Modbus slave communication flag (HR 15, switching it off would cut the Modbus link), the
date/time and vacation dates (HR 21–26, 52–55), and the 168 schedule time slots, which are edited from the web page.

## Global commands

### Building mode

```text
Topic: shogun/<unique_id>/building/mode
Payload: heat | cool | dry | off
```

Writes Holding Register `61` (1 = heating, 2 = cooling, 3 = dehumidification, 4 = off / frost protection). After a 200 ms
pause the register is read back, and the state is republished with the mode the Shogun really kept.

The Shogun does not always accept the value: with the **automatic changeover on, the controller chooses the building mode
itself**, so a manual change can be ignored or overwritten. In that case a `mqtt_building_mode_not_applied` warning is sent
(it states the mode that was kept and whether the automatic changeover is on), and the web page shows the same message.
Turn the automatic changeover off first to set the mode by hand.

### Automatic heating/cooling changeover

```text
Topic: shogun/<unique_id>/building/auto_changeover
Payload: ON | OFF
```

Writes Holding Register `62` (0 = no, 1 = yes). When enabled, the Shogun automatically selects the building state according to
the outdoor temperature and the zones' demand.

### Reset estimated energy

```text
Topic: shogun/<unique_id>/system/reset_energy
Payload: PRESS
```

Sets the estimated energy counter back to 0 (`ON` and `1` are also accepted). See [estimated power and energy](#estimated-electrical-power-and-energy).

## Zone commands

Temperature payloads are in °C between 10 and 30, for example `21.5`. The registers used depend on the building mode:
in cooling mode the cooling bank is used, otherwise the heating bank (see the [register reference](#modbus-register-reference)).
Every write of a temperature is read back after 200 ms; if the Shogun keeps another value, the state is republished with
the real value and a warning is sent on the debug topic.

### `setpoint` (manual setpoint)

```text
Topic: shogun/<unique_id>/zone/1/setpoint
Payload: 21.5
```

Writes the zone's manual setpoint and, unless the zone is off, switches it to manual mode so that the setpoint becomes active.
The Shogun limits the value to the zone's own range (`min_setpoint` … `max_setpoint`); otherwise `mqtt_setpoint_not_kept` is sent.
This is also the command of the climate entity.

### `active_setpoint`

```text
Topic: shogun/<unique_id>/zone/1/active_setpoint
Payload: 21.5
```

The active setpoint (Input Register `334`) is read-only on the Shogun, so the command edits the setpoint that currently drives the
zone and **leaves the zone mode unchanged**:

| Zone state | Register written |
|---|---|
| manual mode | manual setpoint |
| schedule mode, Comfort slot | Comfort setpoint (applies to every Comfort slot of the zone) |
| schedule mode, Eco slot | Eco setpoint (applies to every Eco slot of the zone) |
| off or unused | refused (`mqtt_active_setpoint_not_applied`) |

### `comfort` and `eco`

```text
Topic: shogun/<unique_id>/zone/1/comfort
Payload: 21.0

Topic: shogun/<unique_id>/zone/1/eco
Payload: 17.0
```

These are the temperatures applied by the schedules' Comfort and Eco slots. The Shogun enforces an ordering rule:
**heating** needs eco ≤ comfort, **cooling** needs eco ≥ comfort. A value that breaks it is kept at another value and
`mqtt_setpoint_clamped` is sent.

### `min_setpoint` and `max_setpoint`

Limits of the manual setpoint for the zone, 10–30 °C. The value is read back like the other temperatures.

### `control_mode`

```text
Payload: schedule | manual      (0 and 1 are also accepted)
```

`schedule` makes the zone follow its program, `manual` makes it use its manual setpoint.

### `schedule`

```text
Payload: 1 | 2 | 3 | 4
```

Assigns the schedule program to the zone (Holding Register `41 + (N − 1)`). The value is read back.

### `power`

```text
Payload: ON | OFF
```

Writes the zone power register of the current building mode. Only the power changes: the zone keeps its control mode (`schedule` or
`manual`), which is set separately with `control_mode`. The On / Off button of the web page does exactly the same.
A Modbus error gives a `mqtt_zone_power_failed` warning.

### `mode`

```text
Payload: heat | cool | off
```

`off` turns the zone off. `heat` or `cool` switches the zone to manual mode and turns it on, but only if it matches the current
building mode: `cool` is refused while the building is in heating mode and the reverse. Change the building mode first.
The published `mode` is `off` for a zone that is off, otherwise `heat` or `cool` following the building mode.

### `sensor_lock`

```text
Payload: ON | OFF
```

Locks or unlocks the zone's sensor (Holding Register `31 + (N − 1)`).

## Estimated electrical power and energy

The Shogun gives no electrical measurement and no usable outdoor unit telemetry, so the power is estimated from the zones' demand.
It is an estimate, not a metered value.

**Power** (`estimated_power_w`)

The power is a load between 0 and 100 % applied to the nominal electrical power at full load, set on the configuration page:
**heating** (default 790 W) in heating mode, **cooling** (default 600 W) in cooling mode. The load is the larger of two terms.

- **Steady load.** A heat pump that holds the temperature keeps running to cover the building's losses, which follow the gap between
  the indoor target and the outdoor temperature: 0 % when they are equal, 100 % at the design outdoor temperature (−7 °C in heating,
  35 °C in cooling; `DESIGN_OUTDOOR_HEATING_C` / `DESIGN_OUTDOOR_COOLING_C`). It is counted for the share of zones that still ask for heat
  (or cold), so one zone on out of four carries a quarter of the load. It is ignored when the outdoor temperature reading is not plausible.
- **Transient load.** A zone clearly below its target (above in cooling) asks for more: 0.5 K dead band, then steps of
  20 / 30 / 45 / 65 / 82 / 100 % (same curve as FujitsuAC). The most demanding zone sets it and each additional zone adds 5 %.

A zone that is off, has no valid temperature or is more than 0.5 K beyond its target (above in heating, below in cooling) asks for
nothing. When no zone asks, in building mode `dry` or `off`, or if the nominal power is 0, only the 10 W standby (`ELECTRICAL_STANDBY_W`)
is returned.

Examples with the default 790 W in heating, 4 zones at their 20 °C target:

| Outdoor temperature | 4 zones on | 2 zones on |
|---|---|---|
| 14 °C | 176 W | 88 W |
| 10 °C | 293 W | 146 W |
| 0 °C | 585 W | 293 W |
| −7 °C | 790 W | 395 W |

**Calibration.** The result is a model, not a measurement. The design temperatures and the load curve are fixed in the code, so the
simplest way to calibrate is the nominal power of the configuration page: compare the estimated energy with a real meter over a day or
more and multiply both nominal powers by the ratio measured / estimated.

**Energy** (`estimated_energy_kwh`)

- Integral of the estimated power, updated at each successful read cycle. A gap longer than 5 minutes (Modbus or Wi-Fi outage)
  counts for 5 minutes only.
- Saved in flash every 5 minutes (namespace `shogun_energy`, key `total-kwh`): a reboot loses at most that much, and the counter
  survives reboots and firmware updates.
- The Home Assistant sensor has `device_class: energy` and `state_class: total_increasing`, so it can be added to the Energy dashboard.
  The **Reset estimated energy** button sets it back to 0.

## Modbus register reference

`HR` = Holding Register (read / write), `IR` = Input Register (read only). Addresses are the register numbers of the Shogun
documentation (the firmware sends `address − 1` on the wire). `N` is the zone number (1–8). Temperatures are in tenths of °C.

**Global**

| Register | Content |
|---|---|
| IR `4` | active fault |
| IR `11` | number of configured zones (only these zones are exposed) |
| HR `21` | date / time |
| HR `31 + (N − 1)` | zone sensor lock |
| HR `41 + (N − 1)` | schedule program assigned to the zone (1–4) |
| HR `61` | building state (1 heating, 2 cooling, 3 dehumidification, 4 off) |
| HR `62` | automatic changeover (0 / 1) |
| IR `251 + (N − 1)` | zone program status (0 unused, 1 Comfort, 2 Eco, 3 Off, 4 Manual) |
| IR `320` | outdoor temperature |
| IR `332 + (N − 1)` | zone damper opening |
| IR `333 + (N − 1) × 3` | zone ambient temperature |
| IR `334 + (N − 1) × 3` | zone active setpoint |
| HR `401 …` | schedule time slots, see below |
| from `620` | zone names |

**Per zone**, heating bank / cooling bank (add `(N − 1) × 10`):

| Content | Heating | Cooling |
|---|---|---|
| Comfort setpoint (HR) | `90` | `170` |
| Eco setpoint (HR) | `91` | `171` |
| Manual setpoint (HR) | `92` | `172` |
| Heating state (IR) | `93` | `173` |
| Schedule / manual mode (HR, 0 / 1) | `94` | `174` |
| Min manual setpoint (HR) | `95` | `175` |
| Max manual setpoint (HR) | `96` | `176` |
| Power (HR) | `97` | `177` |

The heating state is read from the Input Register and is not writable: `0` unused, `1` Comfort, `2` Eco, `3` Off, `4` Manual.
Comfort and Eco follow the active schedule; Manual is selected when a zone is switched on manually.

### Schedule time slots

Holding Registers `401 + (program − 1) × 42 + day × 6 + slot`, with `program` 1–4, `day` 0 (Monday) to 6 (Sunday) and
`slot` 0–5 = Comfort 1, Eco 1, Comfort 2, Eco 2, Comfort 3, Eco 3. Each value is an hour and minute: the high byte holds the minutes,
the low byte the hours (for example `7691` = `0x1E0B` = 11:30). `65535` (−1) means the slot is not used.

## Web page

The status page (`/`) and the MQTT commands act on the same registers. The page reads `/api/status` (JSON, same data as the
state topics) and sends its commands to `/action`. `/config` is the configuration page and `/restart` restarts the ESP32.

| Status page | Equivalent |
|---|---|
| Building mode, Automatic heating/cooling changeover | [global commands](#global-commands); the page shows the reason when the Shogun refuses a building mode |
| Active setpoint (field + Set, the label under it says `manual`, `comfort` or `eco`) | `active_setpoint` |
| Comfort / Eco (field + Set, and **All zones** for every zone at once) | `comfort`, `eco` |
| Control mode | `control_mode` |
| Schedule (program of the zone) | `schedule` |
| Power (single On / Off button, green when on) | `power`: only the power changes, the zone keeps its control mode |
| Schedules: editable time slots (empty a slot with ×), collapsible per program | [schedule time slots](#schedule-time-slots) |

**Disabled controls.** The page only enables what can be used in the current configuration. With the **building mode off**
(*Arrêt (Hors-Gel)*, a stand-by where the Shogun only protects the house from frost) or not known yet, the zones are stopped, so the Power
button, the Active setpoint, Comfort / Eco, Control mode and Schedule controls of every zone and the **All zones** form are disabled
and a notice explains why. A zone that is off has no editable Active setpoint. The Building mode and Automatic changeover
controls and the schedule time slots stay available. Home Assistant has no such rule: the entities remain usable there, the Shogun just
ignores what has no effect.

The configuration page (`/config`) sets Wi-Fi, MQTT server, port, user and password, the device name, the OTA password and the
nominal heating and cooling power used for the [power estimate](#estimated-electrical-power-and-energy).