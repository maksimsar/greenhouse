"use strict";

const sensors = {
  air: {
    name: "Температура воздуха", unit: "°C", point: [333, 246],
    rule: "Форточка открывается от 28 °C. Она закрывается при температуре до 25 °C и влажности до 70%.",
  },
  humidity: {
    name: "Влажность воздуха", unit: "%", point: [456, 244],
    rule: "Форточка открывается от 80% влажности. Показание также участвует в расчёте точки росы.",
  },
  outside_light: {
    name: "Свет снаружи", unit: "%", point: [682, 126],
    rule: "Штора выдвигается от 75% наружной освещённости и убирается при падении до 55%.",
  },
  inside_light: {
    name: "Свет над грядкой", unit: "%", point: [368, 281],
    rule: "Сравните свет над грядкой с наружным освещением на графике. Выдвинутая штора снижает свет над растениями.",
  },
  soil: {
    name: "Влажность грунта", unit: "%", point: [298, 324],
    rule: "Грунт ниже 35% запускает трёхсекундный полив, если в баке больше 10% воды.",
  },
  water: {
    name: "Вода в баке", unit: "%", point: [658, 287],
    rule: "При уровне 10% и ниже или при потере показаний датчика новый цикл полива блокируется.",
  },
  glass: {
    name: "Температура стекла", unit: "°C", point: [256, 112],
    rule: "На стекле возможен конденсат, когда его температура падает до точки росы воздуха или ниже.",
  },
};

const $ = id => document.getElementById(id);
const svgNS = "http://www.w3.org/2000/svg";
const numbers = new Intl.NumberFormat("ru", { maximumFractionDigits: 1 });
const times = new Intl.DateTimeFormat("ru", {
  hour: "2-digit", minute: "2-digit", second: "2-digit", hourCycle: "h23",
});
const scenarioButtons = [...$("scenarios").querySelectorAll("button")];
const experimentSteps = [...document.querySelectorAll(".experiment-steps li")];
const sensorControls = new Map();
const colors = ["#316f54", "#a26540"];
const devices = {
  controller: { title: "МК 1 · STM32L031K6", detail: "Слева под теплицей. Принимает семь показаний и решает, когда включить приводы.", outgoing: "МК 1 → МК 2" },
  actuator: { title: "МК 2 · STM32L031K6", detail: "Справа под теплицей. Проверяет команды, управляет приводами и отвечает OK или ERROR.", outgoing: "МК 2 → МК 1" },
  vent: { title: "Форточка", detail: "Сервопривод МК 2 открывает форточку при температуре от 28 °C или влажности от 80%.", value: "vent-value" },
  shade: { title: "Штора", detail: "Шаговый двигатель МК 2 выдвигает штору при внешней освещённости от 75%.", value: "shade-value" },
  pump: { title: "Капельный полив", detail: "МК 2 включает полив на три секунды, когда грунт суше 35% и в баке больше 10% воды.", value: "pump-value" },
};
const deviceControls = [...document.querySelectorAll("[data-device]")];
let selectedSensor = "air";
let selectedDevice;
let snapshot;
let connected = false;
let changingScenario = false;
let reconnectDelay = 500;
let lastPlotTime;
let lastJournalEntry;
let lastPacket;
let packetAnimation;

function svg(tag, attributes = {}, content) {
  const node = document.createElementNS(svgNS, tag);
  for (const [name, value] of Object.entries(attributes)) {
    node.setAttribute(name, value);
  }
  if (content !== undefined) node.textContent = content;
  return node;
}

function reading(value, unit) {
  return Number.isFinite(value) ? `${numbers.format(value)} ${unit}` : "—";
}

function setText(node, value) {
  if (node.textContent !== value) node.textContent = value;
}

function selectSensor(key) {
  selectedSensor = key;
  for (const [id, { button, marker }] of sensorControls) {
    const active = String(id === key);
    button.setAttribute("aria-pressed", active);
    marker.setAttribute("aria-pressed", active);
  }
  setText($("sensor-detail-label"), `ДАТЧИК ${sensorControls.get(key).number} · ${sensors[key].name.toUpperCase()}`);
  setText($("sensor-detail"), sensors[key].rule);
  renderChart(snapshot?.history ?? []);
}

function selectDevice(key) {
  selectedDevice = key;
  $("greenhouse").dataset.selected = key;
  for (const control of deviceControls) {
    if (control.hasAttribute("aria-pressed")) {
      control.setAttribute("aria-pressed", String(control.dataset.device === key));
    }
  }
  renderDevice();
}

function renderDevice() {
  if (!selectedDevice) return;
  const device = devices[selectedDevice];
  setText($("device-title"), device.title);
  setText($("device-description"), device.detail);
  const live = $("device-live");
  live.hidden = false;
  if (device.outgoing) {
    const online = connected && snapshot?.link[selectedDevice];
    const entries = snapshot?.journal ?? [];
    const action = entries.findLast(entry => entry.route === device.outgoing && entry.frame !== "STATUS?" && !entry.frame.startsWith("STATE:"));
    const outgoing = action ?? entries.findLast(entry => entry.route === device.outgoing);
    const transport = snapshot?.mode === "demo" ? "локально" : "RFC2217";
    const frame = outgoing ? ` · ${action ? "Последняя команда или ответ" : "Последний кадр"}: ${outgoing.frame}` : "";
    setText(live, `${online ? "На связи" : "Нет связи"} · USART2 · ${transport}${frame}`);
  } else {
    const fresh = connected && snapshot?.link.fresh && snapshot.state;
    setText(live, fresh ? `Состояние по данным МК 2: ${$(device.value).textContent}` : "Ожидаем актуальное состояние МК 2");
  }
}

function showPacket(entries, fresh) {
  const packet = entries.findLast(entry => (
    (entry.route === devices.controller.outgoing && /^(SERV|STEP|PUMP):/.test(entry.frame))
    || (entry.route === devices.actuator.outgoing && (entry.frame === "OK" || entry.frame === "ERROR"))
  ));
  if (!packet || packet.time === lastPacket) return;
  lastPacket = packet.time;
  if (!fresh || Date.now() / 1000 - packet.time > 1 || matchMedia("(prefers-reduced-motion: reduce)").matches) return;
  const forward = packet.route === devices.controller.outgoing;
  const dot = $("serial-packet");
  dot.dataset.reply = String(!forward);
  packetAnimation?.cancel();
  packetAnimation = dot.animate([
    { transform: `translateX(${forward ? 0 : 238}px)`, opacity: 1 },
    { transform: `translateX(${forward ? 238 : 0}px)`, opacity: 1 },
  ], { duration: 520, easing: "ease-out" });
}

for (const control of deviceControls) {
  control.addEventListener("click", () => selectDevice(control.dataset.device));
  if (control.getAttribute("role") === "button") {
    control.addEventListener("keydown", event => {
      if (event.key !== "Enter" && event.key !== " ") return;
      event.preventDefault();
      selectDevice(control.dataset.device);
    });
  }
}

for (const [index, [key, sensor]] of Object.entries(sensors).entries()) {
  const number = String(index + 1).padStart(2, "0");
  const button = document.createElement("button");
  button.type = "button";
  button.className = "sensor-button";
  for (const [name, content] of [["number", number], ["name", sensor.name], ["reading", "—"]]) {
    const span = document.createElement("span");
    span.className = `sensor-${name}`;
    span.textContent = content;
    button.append(span);
  }
  $("sensor-list").append(button);
  button.addEventListener("click", () => selectSensor(key));

  const [x, y] = sensor.point;
  const marker = svg("g", {
    transform: `translate(${x} ${y})`, role: "button", tabindex: "0",
    "aria-label": `Датчик ${number}. ${sensor.name}`,
  });
  marker.append(
    svg("title", {}, sensor.name),
    svg("circle", { r: 17, class: "marker-halo" }),
    svg("circle", { r: 13, class: "marker-core" }),
    svg("text", { y: 1, "dominant-baseline": "middle", "text-anchor": "middle" }, index + 1),
  );
  $("scene-markers").append(marker);
  marker.addEventListener("click", () => selectSensor(key));
  marker.addEventListener("keydown", event => {
    if (event.key !== "Enter" && event.key !== " ") return;
    event.preventDefault();
    selectSensor(key);
  });
  sensorControls.set(key, { button, marker, number, value: button.lastElementChild });
}

function renderChart(history) {
  const graph = $("chart");
  const current = sensors[selectedSensor];
  const pair = {
    air: "glass", glass: "air", outside_light: "inside_light", inside_light: "outside_light",
  }[selectedSensor];
  const keys = pair ? [selectedSensor, pair] : [selectedSensor];
  const end = history.at(-1)?.time ?? 0;
  const elapsed = end - (history[0]?.time ?? end);
  const duration = [8, 20, 40, 60].find(seconds => seconds >= elapsed) ?? 60;
  const start = end - duration;
  const recent = history.filter(row => Number.isFinite(row.time) && row.time >= start);
  const values = recent.flatMap(row => keys.map(key => row[key])).filter(Number.isFinite);
  graph.replaceChildren();
  $("chart-legend").replaceChildren();
  $("chart-empty").hidden = values.length > 0;
  setText($("chart-note"), values.length ? `Последние ${duration} с` : "До 60 секунд");
  graph.setAttribute("aria-label", values.length
    ? `График за ${duration} с. ${keys.map(key => sensors[key].name).join(", ")}`
    : `Нет показаний. ${current.name}`);
  if (!values.length) return;

  const { left, right, top, bottom } = { left: 48, right: 532, top: 22, bottom: 180 };
  const low = Math.floor(Math.min(...values) - 1);
  const step = Math.max(1, Math.ceil((Math.max(...values) + 1 - low) / 4));
  const high = low + 4 * step;
  const x = time => left + (time - start) / duration * (right - left);
  const y = value => bottom - (value - low) / (high - low) * (bottom - top);
  for (let tick = 0; tick <= 4; tick++) {
    const value = low + tick * step;
    const py = y(value);
    graph.append(
      svg("line", { x1: left, x2: right, y1: py, y2: py, class: "chart-grid" }),
      svg("text", { x: left - 8, y: py + 4, class: "chart-axis", "text-anchor": "end" }, numbers.format(value)),
    );
  }
  for (let seconds = 0; seconds <= duration; seconds += duration / 4) {
    const px = x(start + seconds);
    graph.append(
      svg("line", { x1: px, x2: px, y1: top, y2: bottom, class: "chart-grid" }),
      svg("text", { x: px, y: bottom + 20, class: "chart-axis", "text-anchor": "middle" }, seconds === duration ? "0 с" : `−${duration - seconds}`),
    );
  }
  graph.append(svg("text", { x: left - 8, y: 10, "text-anchor": "end", class: "chart-axis" }, current.unit));
  for (const [index, key] of keys.entries()) {
    let drawing = "";
    let previousTime = null;
    let last = null;
    for (const row of recent) {
      if (!Number.isFinite(row[key])) {
        previousTime = null;
        continue;
      }
      const point = [x(row.time), y(row[key])];
      const command = previousTime === null || row.time - previousTime > 2.5 ? "M" : "L";
      drawing += `${command}${point[0].toFixed(1)},${point[1].toFixed(1)}`;
      if (command === "M") graph.append(svg("circle", { cx: point[0], cy: point[1], r: 2, fill: colors[index] }));
      previousTime = row.time;
      last = point;
    }
    if (last) {
      graph.append(
        svg("path", { d: drawing, stroke: colors[index], fill: "none", "stroke-width": 2.4, "stroke-linecap": "round", "stroke-linejoin": "round" }),
        svg("circle", { cx: last[0], cy: last[1], r: 3.5, fill: colors[index], stroke: "#fcfcf8", "stroke-width": 1.2 }),
      );
    }
    const legend = document.createElement("span");
    const dot = document.createElement("i");
    dot.style.setProperty("--series", colors[index]);
    legend.append(dot, sensors[key].name);
    $("chart-legend").append(legend);
  }
}

function renderJournal(entries) {
  const fragment = document.createDocumentFragment();
  const hideSensors = $("command-filter").checked;
  const visible = entries
    .filter(entry => !hideSensors || !entry.frame.startsWith("SENS:"))
    .slice(-35).reverse();
  for (const entry of visible) {
    const row = document.createElement("tr");
    const time = Number.isFinite(entry.time) ? times.format(new Date(entry.time * 1000)) : "—";
    for (const [index, value] of [time, entry.route, entry.frame].entries()) {
      const cell = document.createElement("td");
      cell.textContent = value;
      if (index === 2) {
        if (value === "OK") cell.dataset.kind = "ok";
        if (value === "ERROR" || value.startsWith("ERROR:")) cell.dataset.kind = "error";
      }
      row.append(cell);
    }
    fragment.append(row);
  }
  if (visible.length === 0) {
    const row = document.createElement("tr");
    const cell = document.createElement("td");
    cell.colSpan = 3;
    cell.textContent = "В этом журнале пока нет сообщений";
    row.append(cell);
    fragment.append(row);
  }
  $("journal-body").replaceChildren(fragment);
}

function renderConnection() {
  const ready = connected && Boolean(snapshot);
  const connection = $("connection");
  connection.dataset.state = ready ? "live" : snapshot ? "error" : "waiting";
  setText(connection, ready ? "Сервер подключён" : snapshot ? "Связь с сервером потеряна" : "Подключение к серверу");
  for (const button of scenarioButtons) {
    button.disabled = !ready || changingScenario;
  }
  $("start-demo").disabled = !ready || changingScenario;
}

function renderActuators(state, fresh) {
  const scene = $("greenhouse");
  scene.classList.toggle("stale", !fresh);
  scene.classList.toggle("vent-open", Boolean(state?.angle));
  scene.classList.toggle("pump-on", Boolean(state?.pump));
  $("vent-panel").style.transform = `rotate(${-Math.min(62, (state?.angle ?? 0) * .67)}deg)`;
  $("shade-fabric").style.transform = `scaleX(${.04 + .96 * (state?.steps ?? 0) / 200})`;
  setText($("vent-value"), state ? reading(state.angle, "°") : "—");
  setText($("shade-value"), state ? reading(state.steps / 2, "%") : "—");
  setText($("pump-value"), state ? state.pump ? "Работает" : "Выключен" : "—");
  $("scene-health").dataset.state = fresh ? "live" : "waiting";
  setText($("scene-health"), fresh ? state?.busy ? "Выполнение" : "МК на связи"
    : state ? "Данные устарели" : "Ожидание МК");
}

function renderConditions(data, fresh) {
  const { sensors: values, dew_point, condensation, irrigation_block, state } = data;
  const water = values.water ?? 0;
  $("tank-water").setAttribute("y", 321 - .72 * water);
  $("tank-water").setAttribute("height", .72 * water);
  $("greenhouse").classList.toggle("has-condensation", condensation);
  const dewKnown = Number.isFinite(dew_point) && Number.isFinite(values.glass);
  $("condensation-status").dataset.state = dewKnown ? condensation ? "alert" : "normal" : "waiting";
  setText($("condensation-detail"), dewKnown
    ? `Стекло ${reading(values.glass, "°C")} · точка росы ${reading(dew_point, "°C")}`
    : "Недостаточно показаний");
  setText($("condensation-value"), dewKnown ? condensation ? "Есть риск" : "Стекло сухое" : "—");

  $("irrigation-status").dataset.state = irrigation_block ? "alert" : fresh ? "normal" : "waiting";
  const irrigationText = {
    sensor: "Пропали показания грунта или бака",
    water: "Уровень воды в баке не выше 10%",
  };
  setText($("irrigation-detail"), irrigationText[irrigation_block]
    ?? (fresh ? state?.pump ? "Идёт трёхсекундный цикл" : "Автоматика следит за грунтом" : "Ожидаем состояние помпы"));
  setText($("irrigation-value"), irrigation_block ? "Блокировка" : fresh ? state?.pump ? "Включён" : "Готов" : "—");
}

function explainInput(experiment) {
  if (!experiment.input) return "";
  if (experiment.name === "invalid") return "Проверяем угол 181°";
  const [air, humidity, light, , soil, water, glass] = experiment.input.slice(5).split(",");
  switch (experiment.name) {
    case "heat": return `Воздух нагрелся до ${reading(Number(air) / 10, "°C")}`;
    case "sun": return `Свет снаружи ${light}%`;
    case "dry": return `Влажность грунта ${soil}%`;
    case "empty": return `В баке ${water}% воды`;
    case "fault": return "Датчик грунта перестал отвечать";
    case "morning": return `Стекло ${reading(Number(glass) / 10, "°C")}, влажность воздуха ${humidity}%`;
    default: return "";
  }
}

function renderExperiment(experiment) {
  const status = $("experiment-status");
  const phase = experiment?.phase ?? "idle";
  status.dataset.phase = phase;
  const labels = {
    idle: "Ожидание", preparing: "Подготовка", running: "Идёт опыт",
    passed: "Завершён", failed: "Требуется проверка",
  };
  setText(status, `${experiment ? experiment.title + " · " : ""}${labels[phase]}`);
  setText($("experiment-note"), experiment?.message ?? "Выберите опыт. Здесь появятся кадры обмена и измеренный результат.");
  const decisions = {
    heat: "Открыть форточку", sun: "Выдвинуть штору", dry: "Включить помпу на 3 секунды",
    morning: "Проветрить теплицу", empty: "Заблокировать полив", fault: "Заблокировать полив",
    invalid: "Передать проверочный угол",
  };
  const explanations = experiment ? {
    input: explainInput(experiment),
    command: experiment.command ? decisions[experiment.name] : "",
    response: experiment.response === "OK" ? "МК 2 выполнил команду"
      : experiment.response === "ERROR" ? "МК 2 отклонил команду" : "",
  } : {};
  let waiting = false;
  for (const step of experimentSteps) {
    const value = experiment?.[step.dataset.step];
    step.dataset.state = value ? "done" : waiting || phase === "idle" ? "waiting" : phase === "failed" ? "error" : "live";
    if (!value) waiting = true;
    setText(step.querySelector(".step-value"), value ?? "—");
    const context = step.querySelector(".step-context");
    if (context) {
      const explanation = explanations[step.dataset.step] ?? "";
      context.hidden = !explanation;
      setText(context, explanation);
    }
  }
}

function render() {
  renderConnection();
  if (!snapshot) return;
  const data = snapshot;
  const fresh = connected && data.link.fresh && Boolean(data.state);
  const demo = data.mode === "demo";
  setText($("mode-label"), demo ? "Локальная демонстрация" : "Стенд Wokwi");
  setText($("mode-detail"), demo ? "Две программы на C" : "Две STM32 · RFC2217");
  setText($("controller-link"), `МК 1 · ${connected && data.link.controller ? demo ? "локально" : "подключён" : "нет связи"}`);
  setText($("actuator-link"), `МК 2 · ${connected && data.link.actuator ? demo ? "локально" : "подключён" : "нет связи"}`);
  $("controller-link").dataset.connected = String(connected && data.link.controller);
  $("actuator-link").dataset.connected = String(connected && data.link.actuator);
  for (const board of document.querySelectorAll(".board-node")) {
    board.dataset.online = String(connected && data.link[board.dataset.device]);
  }
  for (const button of scenarioButtons) {
    button.setAttribute("aria-pressed", String(button.dataset.scenario === (data.experiment?.name ?? data.scenario)));
  }
  for (const [key, control] of sensorControls) {
    const value = data.sensors[key];
    const text = reading(value, sensors[key].unit);
    setText(control.value, text);
    control.value.dataset.missing = String(!Number.isFinite(value));
    control.marker.setAttribute("aria-label", `Датчик ${control.number}. ${sensors[key].name}. ${text}`);
  }
  renderActuators(data.state, fresh);
  renderDevice();
  renderConditions(data, fresh);
  renderExperiment(data.experiment);
  const plotTime = data.history.at(-1)?.time;
  if (plotTime !== lastPlotTime) {
    lastPlotTime = plotTime;
    renderChart(data.history);
  }
  const entry = data.journal.at(-1);
  const journalKey = entry && `${entry.time} ${entry.route} ${entry.frame}`;
  if (journalKey !== lastJournalEntry) {
    lastJournalEntry = journalKey;
    renderJournal(data.journal);
    showPacket(data.journal, fresh);
  }
}

$("command-filter").addEventListener("change", () => renderJournal(snapshot?.journal ?? []));

async function runExperiment(name, button) {
  if (changingScenario) return;
  changingScenario = true;
  $("scenario-error").hidden = true;
  renderConnection();
  try {
    const path = name === "normal" ? "/scenario/normal" : `/experiment/${name}`;
    const response = await fetch(path, { method: "POST", signal: AbortSignal.timeout(5000) });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const highlightedSensor = { heat: "air", sun: "inside_light", dry: "soil", empty: "water", fault: "soil", morning: "glass" };
    if (highlightedSensor[name]) selectSensor(highlightedSensor[name]);
    if (name === "invalid") selectDevice("actuator");
  } catch {
    setText($("scenario-error"), "Не удалось запустить опыт. Проверьте связь с сервером.");
    $("scenario-error").hidden = false;
  } finally {
    changingScenario = false;
    renderConnection();
    if (!button.disabled) button.focus({ preventScroll: true });
  }
}

for (const button of scenarioButtons) {
  button.addEventListener("click", () => runExperiment(button.dataset.scenario, button));
}
$("start-demo").addEventListener("click", event => runExperiment("heat", event.currentTarget));
$("view-toggle").addEventListener("click", event => {
  const depth = $("greenhouse").dataset.view === "flat";
  $("greenhouse").dataset.view = depth ? "depth" : "flat";
  event.currentTarget.setAttribute("aria-pressed", String(depth));
});

function connect() {
  const protocol = location.protocol === "https:" ? "wss" : "ws";
  const socket = new WebSocket(`${protocol}://${location.host}/ws`);
  let heartbeat;
  const expectSnapshot = () => {
    clearTimeout(heartbeat);
    heartbeat = setTimeout(() => socket.close(), 7000);
  };
  expectSnapshot();
  socket.addEventListener("message", event => {
    try {
      snapshot = JSON.parse(event.data);
    } catch {
      return;
    }
    connected = true;
    reconnectDelay = 500;
    expectSnapshot();
    render();
  });
  socket.addEventListener("error", () => socket.close());
  socket.addEventListener("close", () => {
    clearTimeout(heartbeat);
    connected = false;
    render();
    setTimeout(connect, reconnectDelay);
    reconnectDelay = Math.min(reconnectDelay * 2, 5000);
  });
}

selectSensor("air");
connect();
