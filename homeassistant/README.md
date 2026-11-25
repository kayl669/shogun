# Shogun schedule card

A compact Home Assistant dashboard card to read and edit the four weekly schedule programs of the Shogun. The card texts
are in French.

It uses the entities created by MQTT Discovery (`text.<prefix>_schedule_<program>_<day>` and
`select.<prefix>_zone_<N>_schedule_program`), so the firmware does not need any change.

## Installation

1. Copy `shogun-schedule-card.js` to the `www` folder of the Home Assistant configuration (`/config/www/`). Create the folder
   if it does not exist, and restart Home Assistant in that case.
2. Declare the resource: **Settings → Dashboards → ⋮ → Resources → Add resource**, URL `/local/shogun-schedule-card.js`,
   type **JavaScript module**. The *Resources* menu is only shown when the advanced mode of your user profile is on, in the
   Home Assistant versions that have this setting.
3. Reload the browser page.
4. Edit a dashboard, add a card and choose **Plannings Shogun**, or add it in YAML:

```yaml
type: custom:shogun-schedule-card
```

The card fits a dashboard section. On a narrow card the times are shown under each day and the periods are stacked.

## Options

| Option | Default | Description |
|---|---|---|
| `prefix` | found automatically | Device name as used in the entity ids, for example `shogun` for `text.shogun_schedule_1_mon`. Needed only with several Shogun, or if the card finds none. |
| `title` | `Plannings Shogun` | Title of the card |

## Use

- Choose a program with the tabs **1** to **4**. Each day is shown on one line, with its Comfort periods on a 24 h bar and as
  text.
- Click a day to edit it, and click it again to close it. One day is open at a time.
- Each day has three Comfort periods. Pick a start and an end time, remove a time with **×**, add one with **+ heure**.
- **Copier vers…** copies a day to the whole week, Monday to Friday or the week-end.
- Nothing is sent before **Enregistrer**. A dot on a tab shows that the program has unsaved changes, which are lost if the
  page is left.
- The days are sent one at a time. A day is saved when the Shogun publishes back the same times. Without this confirmation
  after 15 seconds, the day stays marked with the current value: save again, or use **Annuler** to drop the change. The
  [debug topic](../MQTT_HOME_ASSISTANT.md#schedule-commands) gives the reason.
- The card lists the zones of the program shown. Click **Modifier**, then a zone name, to assign the zone to this program:
  this is applied at once, without **Enregistrer**. Zones cannot be assigned while the building mode is off.

The card only saves complete periods (a start and an end) with times in ascending order. This is stricter than the
[MQTT command](../MQTT_HOME_ASSISTANT.md#schedule-commands).

## Updating

After replacing the file, change the resource URL (for example `/local/shogun-schedule-card.js?v=2`) so that the browser
does not keep the old version.
