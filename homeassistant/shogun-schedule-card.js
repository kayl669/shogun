/*
 * Shogun schedule card for Home Assistant.
 *
 * Shows the four weekly schedule programs of the Shogun, one line per day, and edits them (a click opens a day) through
 * the entities created by the firmware:
 *   text.<prefix>_schedule_<program>_<day>            "06:00 09:00 11:30 13:30 17:00 23:00" ("--:--" = not used)
 *   select.<prefix>_zone_<N>_schedule_program         program assigned to a zone
 *
 * Installation: see README.md in this folder.
 */

const DAYS = [["mon", "Lundi"], ["tue", "Mardi"], ["wed", "Mercredi"], ["thu", "Jeudi"], ["fri", "Vendredi"],
              ["sat", "Samedi"], ["sun", "Dimanche"]];
const PROGRAMS = [1, 2, 3, 4];
const DEFAULT_TIMES = ["06:00", "09:00", "11:30", "13:30", "17:00", "23:00"];
const TIME = /^([01]\d|2[0-3]):[0-5]\d$/;
// Time left to the Shogun to publish the new value of a day after it has been sent.
const CONFIRM_TIMEOUT_MS = 15000;

const STYLE = `
  ha-card { display: block; container-type: inline-size; overflow: hidden; }
  header, .zones, footer, .message { padding: 10px 16px; }
  header { display: flex; flex-wrap: wrap; gap: 12px; align-items: center; justify-content: space-between; }
  h1 { font-size: var(--ha-card-header-font-size, 22px); font-weight: 400; margin: 0; }
  button, select, input { font: inherit; color: inherit; }
  button, select { cursor: pointer; }
  button:disabled, select:disabled, input:disabled { cursor: default; opacity: .45; }
  :focus-visible { outline: 2px solid var(--primary-color); outline-offset: 2px; }
  [hidden] { display: none !important; }
  .tabs { display: flex; gap: 4px; background: var(--secondary-background-color); padding: 4px; border-radius: 10px; }
  .tabs button { border: 0; background: none; padding: 6px 14px; border-radius: 7px; position: relative; }
  .tabs button[aria-pressed="true"] { background: var(--primary-color); color: var(--text-primary-color, #fff); }
  .tabs .dot::after { content: ""; position: absolute; top: 3px; right: 3px; width: 6px; height: 6px; border-radius: 50%;
    background: var(--warning-color, #ffa600); }
  .zones { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; border-top: 1px solid var(--divider-color); }
  .label { color: var(--secondary-text-color); font-size: 13px; }
  .chip { border: 1px solid var(--divider-color); background: none; border-radius: 16px; padding: 4px 12px; font-size: 13px; }
  .chip[aria-pressed="true"] { border-color: var(--primary-color); background: var(--primary-color);
    color: var(--text-primary-color, #fff); }
  .chip small { opacity: .75; }
  .day { border-top: 1px solid var(--divider-color); }
  .day.changed { background: rgba(255, 166, 0, .12); }
  .head { display: grid; grid-template-columns: 80px minmax(0, 1fr) 260px 12px; gap: 12px; align-items: center; width: 100%;
    min-height: 40px; padding: 4px 16px; border: 0; background: none; text-align: left; }
  .head:hover:enabled { background: var(--secondary-background-color); }
  .head:focus-visible { outline-offset: -2px; }
  .name { font-weight: 500; }
  .name small { display: block; font-weight: 400; color: var(--secondary-text-color); font-size: 12px; }
  .summary { font-size: 13px; font-variant-numeric: tabular-nums; }
  .chevron { color: var(--secondary-text-color); }
  .editor { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)) 96px; gap: 8px; align-items: end;
    padding: 4px 16px 12px; }
  .group .label { display: block; margin-bottom: 2px; }
  .period { display: flex; align-items: center; gap: 4px; min-width: 0; }
  .arrow { color: var(--secondary-text-color); }
  .slot { flex: 1; display: flex; min-width: 0; height: 36px; background: var(--secondary-background-color);
    border: 1px solid var(--divider-color); border-radius: 8px; }
  .slot input { flex: 1; min-width: 0; width: 100%; border: 0; background: none; padding: 0 0 0 4px;
    font-variant-numeric: tabular-nums; }
  .slot input::-webkit-calendar-picker-indicator { width: 12px; height: 12px; margin: 0; padding: 0; }
  .slot button { border: 0; background: none; color: var(--secondary-text-color); padding: 0 4px; }
  .slot.empty { border-style: dashed; background: none; }
  .slot.empty button { flex: 1; font-size: 13px; }
  .slot.bad { border-color: var(--error-color); }
  .copy { height: 36px; width: 100%; border: 1px solid var(--divider-color); border-radius: 8px;
    background: var(--card-background-color); font-size: 13px; }
  .bar { display: block; position: relative; height: 10px; border-radius: 5px; overflow: hidden;
    background: rgba(76, 175, 80, .28); }
  .bar i { position: absolute; top: 0; bottom: 0; background: var(--warning-color, #ffa600); }
  .error { padding: 0 16px 8px; color: var(--error-color); font-size: 13px; }
  .link { border: 0; background: none; padding: 4px; color: var(--primary-color); font-size: 13px; }
  footer { display: flex; flex-wrap: wrap; gap: 12px; align-items: center; justify-content: space-between;
    border-top: 1px solid var(--divider-color); }
  .actions { display: flex; gap: 8px; }
  .btn { border: 1px solid var(--divider-color); background: none; border-radius: 8px; padding: 8px 16px; }
  .btn.primary { background: var(--primary-color); border-color: var(--primary-color); color: var(--text-primary-color, #fff); }
  @container (max-width: 700px) {
    .editor { grid-template-columns: minmax(0, 1fr); }
  }
  @container (max-width: 560px) {
    .head { grid-template-columns: 80px minmax(0, 1fr) 12px; row-gap: 4px; padding-block: 8px; }
    .summary { grid-row: 2; grid-column: 1 / -1; }
    .chevron { grid-row: 1; grid-column: 3; }
  }
`;

function el(tag, props = {}, ...children) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(props)) {
    if (key.startsWith("aria-")) node.setAttribute(key, value);
    else node[key] = value;
  }
  node.append(...children);
  return node;
}

// "06:00 09:00 --:-- ..." -> ["06:00", "09:00", "", ...], or null when the text is not a schedule day.
function parseDay(text) {
  const parts = String(text ?? "").trim().split(/\s+/);
  if (parts.length !== 6 || !parts.every((p) => p === "--:--" || TIME.test(p))) return null;
  return parts.map((p) => (p === "--:--" ? "" : p));
}

const formatDay = (times) => times.map((t) => t || "--:--").join(" ");
const minutes = (time) => Number(time.slice(0, 2)) * 60 + Number(time.slice(3));
const same = (a, b) => !!a && !!b && formatDay(a) === formatDay(b);

function dayError(times) {
  for (let i = 0; i < 6; i += 2) {
    if (!times[i] !== !times[i + 1]) return "Chaque période a besoin d'un début et d'une fin.";
  }
  let last = -1;
  for (const time of times) {
    if (!time) continue;
    if (minutes(time) <= last) return "Les heures doivent se suivre dans l'ordre.";
    last = minutes(time);
  }
  return "";
}

// The Comfort periods of a day as text. An incomplete period keeps its "--:--".
function summarize(times) {
  const periods = [0, 2, 4]
    .filter((i) => times[i] || times[i + 1])
    .map((i) => `${times[i] || "--:--"}–${times[i + 1] || "--:--"}`);
  return periods.join(" · ") || "Aucune période Confort";
}

class ShogunScheduleCard extends HTMLElement {
  static getStubConfig() {
    return {};
  }

  setConfig(config) {
    this._config = { title: "Plannings Shogun", ...config };
    this._program = 1;
    this._drafts = new Map();     // "<program>_<day>" -> six times not saved yet
    this._failed = new Map();     // "<program>_<day>" -> why the last sending of this day failed
    this._saving = false;
    this._notice = "";
    this._open = null;            // day being edited
    this._zonesOpen = false;
    this._prefix = null;
    this._built = false;
    if (this.shadowRoot) this.shadowRoot.replaceChildren();
    if (this._hass) this._update();
  }

  set hass(hass) {
    this._hass = hass;
    if (this._config) this._update();
  }

  getCardSize() {
    return 6;
  }

  getGridOptions() {
    return { columns: 12, min_columns: 6 };
  }

  // The prefix is the device name used in the entity ids. It is found automatically when there is a single Shogun.
  _findPrefix() {
    if (this._config.prefix) return { prefix: this._config.prefix };
    const found = Object.keys(this._hass.states)
      .map((id) => id.match(/^text\.(.+)_schedule_1_mon$/)?.[1])
      .filter(Boolean);
    if (found.length === 1) return { prefix: found[0] };
    if (found.length === 0) {
      return { error: "Aucun planning Shogun trouvé. Vérifiez la découverte MQTT ou indiquez l'option « prefix »." };
    }
    return { error: `Plusieurs Shogun trouvés. Indiquez l'option « prefix » : ${found.join(", ")}.` };
  }

  _entityId(key) {
    return `text.${this._prefix}_schedule_${key}`;
  }

  _remote(key) {
    return parseDay(this._hass.states[this._entityId(key)]?.state);
  }

  _build() {
    const root = this.shadowRoot ?? this.attachShadow({ mode: "open" });
    this._tabs = PROGRAMS.map((program) => {
      const tab = el("button", { type: "button", textContent: String(program), "aria-label": `Planning ${program}` });
      tab.addEventListener("click", () => {
        this._program = program;
        this._open = null;
        this._zonesOpen = false;
        this._update();
      });
      return tab;
    });
    this._zones = el("div", { className: "zones" });
    this._rows = DAYS.map(([day, name], index) => this._buildRow(day, name, index));
    this._status = el("div", { className: "label", "aria-live": "polite" });
    this._cancel = el("button", { type: "button", className: "btn", textContent: "Annuler" });
    this._cancel.addEventListener("click", () => {
      this._drafts.clear();
      this._failed.clear();
      this._notice = "";
      this._open = null;
      this._update();
    });
    this._save = el("button", { type: "button", className: "btn primary", textContent: "Enregistrer" });
    this._save.addEventListener("click", () => this._send());

    this._actions = el("div", { className: "actions" }, this._cancel, this._save);
    this._footer = el("footer", {}, this._status, this._actions);

    this._message = el("div", { className: "message" });
    this._content = el("div", {}, this._zones, ...this._rows.map((row) => row.node), this._footer);
    this._tabBar = el("div", { className: "tabs", role: "group", "aria-label": "Planning affiché" }, ...this._tabs);

    root.replaceChildren(
      el("style", { textContent: STYLE }),
      el("ha-card", {},
        el("header", {}, el("h1", { textContent: this._config.title }), this._tabBar),
        this._message, this._content));
    this._built = true;
  }

  _buildRow(day, name, index) {
    const state = el("small");
    const periods = [0, 1, 2].map(() => el("div", { className: "period" }));
    const groups = periods.map((period, i) => el("div", { className: "group" },
      el("span", { className: "label", textContent: `Confort ${i + 1} : début → fin` }), period));
    const copy = el("select", { className: "copy", "aria-label": `Copier ${name} vers` });
    for (const [value, text] of [["", "Copier vers…"], ["0123456", "Toute la semaine"], ["01234", "Lundi à vendredi"],
                                 ["56", "Le week-end"]]) {
      copy.append(el("option", { value, textContent: text }));
    }
    copy.addEventListener("change", () => {
      const targets = copy.value;
      copy.value = "";
      this._copy(index, targets);
    });
    const bar = el("span", { className: "bar", "aria-hidden": "true" });
    const summary = el("span", { className: "summary" });
    const chevron = el("span", { className: "chevron", "aria-hidden": "true" });
    const head = el("button", { type: "button", className: "head" },
      el("span", { className: "name" }, name, state), bar, summary, chevron);
    head.addEventListener("click", () => {
      this._open = this._open === day ? null : day;
      this._update();
    });
    const editor = el("div", { className: "editor" }, ...groups, copy);
    const error = el("div", { className: "error" });
    const node = el("div", { className: "day" }, head, editor, error);
    return { day, name, node, state, head, summary, chevron, editor, periods, copy, bar, error, drawn: null, slots: [] };
  }

  _update() {
    if (!this._hass) return;
    if (!this._built) this._build();
    // The native time pickers and lists follow the Home Assistant theme.
    this.style.colorScheme = this._hass.themes?.darkMode ? "dark" : "light";

    const { prefix, error } = this._findPrefix();
    this._message.hidden = !error;
    this._content.hidden = !!error;
    this._tabBar.hidden = !!error;
    this._message.textContent = error ?? "";
    if (error) return;
    this._prefix = prefix;

    // A draft that the Shogun now holds is no longer a modification.
    for (const [key, times] of this._drafts) {
      if (same(times, this._remote(key))) {
        this._drafts.delete(key);
        this._failed.delete(key);
      }
    }

    this._tabs.forEach((tab, i) => {
      tab.setAttribute("aria-pressed", String(PROGRAMS[i] === this._program));
      tab.classList.toggle("dot", DAYS.some(([day]) => this._drafts.has(`${PROGRAMS[i]}_${day}`)));
    });
    this._updateZones();
    this._rows.forEach((row) => this._updateRow(row));
    this._updateFooter();
  }

  _zoneList() {
    const pattern = new RegExp(`^select\\.${this._prefix.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}_zone_(\\d+)_schedule_program$`);
    return Object.keys(this._hass.states)
      .map((id) => ({ id, number: Number(id.match(pattern)?.[1]) }))
      .filter((zone) => zone.number)
      .sort((a, b) => a.number - b.number)
      .map((zone) => {
        const state = this._hass.states[zone.id];
        const device = this._hass.devices?.[this._hass.entities?.[zone.id]?.device_id];
        const deviceName = device?.name_by_user || device?.name;
        let name = state.attributes.friendly_name || `Zone ${zone.number}`;
        if (deviceName && name.startsWith(`${deviceName} `)) name = name.slice(deviceName.length + 1);
        name = name.replace(/ Schedule program$/, "");
        return { id: zone.id, name, program: Number(state.state) || 0, available: state.state !== "unavailable" };
      });
  }

  _updateZones() {
    const zones = this._zoneList();
    // Rebuilt only when something changed, so that the keyboard focus is kept.
    const drawn = JSON.stringify([this._program, this._zonesOpen, zones]);
    if (drawn === this._drawnZones && this._zones.childElementCount) return;
    this._drawnZones = drawn;
    this._zones.hidden = zones.length === 0;

    const action = this._zonesOpen ? "Fermer" : "Modifier";
    const toggle = el("button", {
      type: "button", className: "link", textContent: action,
      "aria-expanded": String(this._zonesOpen), "aria-label": `${action} les zones`,
    });
    toggle.addEventListener("click", () => {
      this._zonesOpen = !this._zonesOpen;
      this._update();
      this._zones.querySelector(".link").focus();
    });
    if (!this._zonesOpen) {
      const names = zones.filter((zone) => zone.program === this._program).map((zone) => zone.name);
      this._zones.replaceChildren(
        el("span", { className: "label", textContent: `Zones : ${names.join(", ") || "aucune"}` }), toggle);
      return;
    }
    const chips = zones.map((zone) => {
      const assigned = zone.program === this._program;
      const detail = assigned ? "✓" : zone.program ? `(P${zone.program})` : "";
      const chip = el("button", {
        type: "button", className: "chip", disabled: !zone.available, "aria-pressed": String(assigned),
        title: !zone.available ? "Indisponible (bâtiment à l'arrêt ou appareil hors ligne)"
          : assigned ? "Pour changer, ouvrez un autre planning puis choisissez la zone" : `Assigner au planning ${this._program}`,
      }, `${zone.name} `, el("small", { textContent: detail }));
      chip.addEventListener("click", () => {
        if (!assigned) this._assign(zone, this._program);
      });
      return chip;
    });
    this._zones.replaceChildren(el("span", { className: "label", textContent: "Zones sur ce planning :" }), ...chips, toggle);
  }

  async _assign(zone, program) {
    this._notice = "";
    try {
      await this._hass.callService("select", "select_option", { entity_id: zone.id, option: String(program) });
    } catch (err) {
      this._notice = `${zone.name} : assignation refusée (${err?.message ?? err}).`;
    }
    this._update();
  }

  _updateRow(row) {
    const key = `${this._program}_${row.day}`;
    const remote = this._remote(key);
    const draft = this._drafts.get(key);
    const times = remote ? draft ?? remote : null;
    const disabled = !times || this._saving;

    // The fields are rebuilt only when their content changes, never while the user is typing in them.
    const drawn = JSON.stringify([key, times, disabled]);
    if (drawn !== row.drawn) {
      row.drawn = drawn;
      row.slots = DEFAULT_TIMES.map((_, slot) => this._buildSlot(row, key, times?.[slot] ?? "", slot, disabled));
      row.periods.forEach((period, i) => {
        period.replaceChildren(row.slots[2 * i], el("span", { className: "arrow", textContent: "→" }), row.slots[2 * i + 1]);
      });
    }
    this._decorateRow(row, key, times, !!draft && !!remote);
    row.copy.disabled = disabled;

    const open = !!times && this._open === row.day;
    row.head.disabled = !times;
    row.head.setAttribute("aria-expanded", String(open));
    row.head.title = open ? "Fermer" : "Modifier";
    row.chevron.textContent = open ? "▴" : "▾";
    row.editor.hidden = !open;
  }

  _decorateRow(row, key, times, changed) {
    let state = changed ? "modifié" : "";
    if (!times) {
      const raw = this._hass.states[this._entityId(key)]?.state;
      state = raw === undefined ? "introuvable" : raw === "unavailable" ? "indisponible" : "en attente";
    }
    row.state.textContent = state;
    row.node.classList.toggle("changed", changed);

    const invalid = changed ? dayError(times) : "";
    const message = invalid || (changed ? this._failed.get(key) ?? "" : "");
    row.error.textContent = message;
    row.error.hidden = !message;
    row.slots.forEach((slot) => slot.classList.toggle("bad", !!invalid && !slot.classList.contains("empty")));

    row.bar.replaceChildren();
    for (let i = 0; times && i < 6; i += 2) {
      if (!times[i] || !times[i + 1] || minutes(times[i + 1]) <= minutes(times[i])) continue;
      const mark = el("i");
      mark.style.left = `${minutes(times[i]) / 14.4}%`;
      mark.style.width = `${(minutes(times[i + 1]) - minutes(times[i])) / 14.4}%`;
      row.bar.append(mark);
    }
    row.summary.textContent = times ? summarize(times) : "";
  }

  _buildSlot(row, key, value, slot, disabled) {
    const label = `${row.name}, ${slot % 2 ? "fin" : "début"} de la période ${Math.floor(slot / 2) + 1}`;
    const box = el("div", { className: value ? "slot" : "slot empty" });
    if (!value) {
      const add = el("button", { type: "button", textContent: "+ heure", disabled, "aria-label": `Ajouter : ${label}` });
      add.addEventListener("click", () => {
        this._edit(key, slot, DEFAULT_TIMES[slot]);
        this._update();
        row.slots[slot].querySelector("input")?.focus();
      });
      box.append(add);
      return box;
    }
    const input = el("input", { type: "time", value, disabled, required: true, "aria-label": label });
    input.addEventListener("input", () => {
      if (!TIME.test(input.value)) return;     // incomplete entry
      this._edit(key, slot, input.value);
      // Keep these fields as they are: only the rest of the card is refreshed.
      row.drawn = JSON.stringify([key, this._drafts.get(key) ?? this._remote(key), disabled]);
      this._update();
    });
    input.addEventListener("blur", () => {
      const current = (this._drafts.get(key) ?? this._remote(key))?.[slot];
      if (!input.value && current) input.value = current;
    });
    const remove = el("button", { type: "button", textContent: "×", disabled, "aria-label": `Effacer : ${label}` });
    remove.addEventListener("click", () => {
      this._edit(key, slot, "");
      this._update();
    });
    box.append(input, remove);
    return box;
  }

  _edit(key, slot, value) {
    const remote = this._remote(key);
    if (!remote) return;
    const times = [...(this._drafts.get(key) ?? remote)];
    times[slot] = value;
    this._setDraft(key, times);
  }

  _setDraft(key, times) {
    this._failed.delete(key);
    this._notice = "";
    if (same(times, this._remote(key))) this._drafts.delete(key);
    else this._drafts.set(key, times);
  }

  _copy(source, targets) {
    const sourceKey = `${this._program}_${DAYS[source][0]}`;
    const times = this._drafts.get(sourceKey) ?? this._remote(sourceKey);
    if (!times) return;
    for (const index of targets) {
      const key = `${this._program}_${DAYS[Number(index)][0]}`;
      if (this._remote(key)) this._setDraft(key, [...times]);
    }
    this._update();
  }

  _updateFooter() {
    const changed = [...this._drafts.values()];
    const invalid = changed.filter((times) => dayError(times)).length;
    let status = "Aucune modification en attente";
    if (this._saving) status = `Envoi au Shogun… ${changed.length} jour(s) restant(s)`;
    else if (invalid) status = `${invalid} jour(s) à corriger avant l'enregistrement`;
    else if (this._failed.size) status = `${this._failed.size} jour(s) non confirmé(s) par le Shogun`;
    else if (this._notice) status = this._notice;
    else if (changed.length) status = `${changed.length} jour(s) modifié(s), non enregistré(s)`;
    this._status.textContent = status;
    // The footer only appears when there is something to save or to report.
    this._footer.hidden = !changed.length && !this._saving && !this._notice;
    this._actions.hidden = !changed.length;
    this._save.disabled = this._saving || !changed.length || invalid > 0;
    this._cancel.disabled = this._saving || !changed.length;
  }

  // Days are sent one at a time, each one waiting for the value published back by the Shogun.
  async _send() {
    const pending = [...this._drafts].filter(([, times]) => !dayError(times));
    this._saving = true;
    this._open = null;
    this._notice = "";
    this._failed.clear();
    this._update();
    let sent = 0;
    try {
      for (const [key, times] of pending) {
        const entityId = this._entityId(key);
        const value = formatDay(times);
        try {
          await this._hass.callService("text", "set_value", { entity_id: entityId, value });
        } catch (err) {
          this._failed.set(key, `Envoi refusé : ${err?.message ?? err}`);
          this._update();
          continue;
        }
        if (await this._confirmed(entityId, value)) {
          this._drafts.delete(key);
          sent++;
        } else {
          const kept = this._hass.states[entityId]?.state ?? "inconnue";
          this._failed.set(key, `Non confirmé par le Shogun. Valeur actuelle : ${kept}`);
        }
        this._update();
      }
    } finally {
      this._saving = false;
      if (!this._failed.size) this._notice = `${sent} jour(s) enregistré(s)`;
      this._update();
    }
  }

  _confirmed(entityId, value) {
    return new Promise((resolve) => {
      const start = Date.now();
      const check = () => {
        if (this._hass.states[entityId]?.state === value) resolve(true);
        else if (Date.now() - start >= CONFIRM_TIMEOUT_MS) resolve(false);
        else setTimeout(check, 200);
      };
      check();
    });
  }
}

if (!customElements.get("shogun-schedule-card")) {
  customElements.define("shogun-schedule-card", ShogunScheduleCard);
  window.customCards = window.customCards || [];
  window.customCards.push({
    type: "shogun-schedule-card",
    name: "Plannings Shogun",
    description: "Saisie des plages horaires des plannings Shogun et assignation des zones.",
  });
}
