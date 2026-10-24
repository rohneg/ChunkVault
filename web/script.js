"use strict";

const $ = (id) => document.getElementById(id);
const main = $("page");
const state = { files: [] };
let token = 0;

const esc = (value) => String(value ?? "").replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

function fmtSize(bytes) {
    let value = Number(bytes);
    if (!Number.isFinite(value) || value < 0) return "—";
    const units = ["B", "KB", "MB", "GB"];
    let i = 0;
    while (value >= 1024 && i < 3) { value /= 1024; i++; }
    return (i ? value.toFixed(1) : value) + " " + units[i];
}

function safePath(path) {
    const parts = String(path || "").split(/[\\/]+/).filter(Boolean);
    const i = parts.lastIndexOf("storage");
    return (i >= 0 ? parts.slice(i + 1) : parts.slice(-2)).join("/") || "—";
}

const note = (text, type = "") => `<div class="msg ${type}">${esc(text)}</div>`;
const empty = (text) => `<p class="muted">${esc(text)}</p>`;
const badge = (status) => `<span class="badge ${esc(status)}">${esc(status)}</span>`;

function table(heads, rows, emptyText = "Nothing to show yet.") {
    if (!rows.length) return empty(emptyText);
    return `<div class="wrap"><table><thead><tr>${heads.map((h) => `<th>${esc(h)}</th>`).join("")}</tr></thead><tbody>` +
        rows.map((r) => `<tr>${r.map((c) => `<td>${c}</td>`).join("")}</tr>`).join("") + "</tbody></table></div>";
}

function toast(text, type = "") {
    const el = document.createElement("div");
    el.className = "toast " + type;
    el.textContent = text;
    $("toasts").appendChild(el);
    setTimeout(() => el.remove(), 4000);
}

async function api(path, options = {}) {
    let response;
    try {
        response = await fetch(path, { cache: "no-store", ...options });
    } catch {
        throw new Error("Could not reach the ChunkVault server. Is it running?");
    }
    const text = await response.text();
    let data = null;
    try { data = text ? JSON.parse(text) : null; } catch {}
    if (!response.ok) throw new Error(data?.error || text || `Request failed (HTTP ${response.status}).`);
    return data;
}

const post = (path, params) => api(path, { method: "POST", body: new URLSearchParams(params) });

const mono = (value) => `<span class="mono">${esc(value)}</span>`;
const shortHash = (hash) => (hash ? `<span class="mono" title="${esc(hash)}">${esc(hash.slice(0, 12))}…</span>` : "—");
const downloadLink = (f) => `<a class="button" href="/api/files/${encodeURIComponent(f.id)}/download" download>Download</a>`;

const fileTable = (files, withHash = false) => table(
    ["File", "ID", "Type", "Size", "Chunks", ...(withHash ? ["SHA-256"] : [])],
    files.map((f) => [esc(f.name), mono(f.id), mono(f.type), fmtSize(f.size), f.chunkCount, ...(withHash ? [shortHash(f.sha256)] : [])]),
    "No files stored yet.");

const activityTable = (items, selectable = false) => table(
    [...(selectable ? [""] : []), "Action", "Details", "Time"],
    items.map((a) => [
        ...(selectable ? [`<input type="checkbox" class="pick" value="${esc(a.id)}" aria-label="Select log">`] : []),
        esc(a.action.replace(/_/g, " ")), esc(a.details), `<span class="mono">${esc(a.time)}</span>`]),
    "No activity recorded yet.");

const bulkBar = '<div class="row"><button class="all">Select all</button><button class="danger del" disabled>Delete selected</button></div>';

function wireBulk(root, remove) {
    const all = root.querySelector(".all"), del = root.querySelector(".del");
    if (!all) return;
    const picks = () => [...root.querySelectorAll(".pick")];
    const chosen = () => picks().filter((c) => c.checked).map((c) => c.value);
    const sync = () => {
        const n = chosen().length;
        del.disabled = !n;
        del.textContent = n ? `Delete selected (${n})` : "Delete selected";
        all.textContent = n && n === picks().length ? "Deselect all" : "Select all";
    };
    root.onchange = (e) => e.target.classList.contains("pick") && sync();
    all.onclick = () => { const on = picks().some((c) => !c.checked); picks().forEach((c) => (c.checked = on)); sync(); };
    del.onclick = () => {
        const ids = chosen();
        if (ids.length && confirm(`Permanently delete ${ids.length} item(s)?`)) remove(ids);
    };
}

async function deleteEach(ids, urlPrefix) {
    let done = 0;
    try {
        for (const id of ids) { await api(urlPrefix + encodeURIComponent(id), { method: "DELETE" }); done++; }
        toast(`${done} deleted.`, "success");
    } catch (e) {
        toast(`${done} deleted. ${e.message}`, "error");
    }
}

async function dashboardPage(t) {
    const d = await api("/api/dashboard");
    if (t !== token) return;
    const stats = [["Stored files", d.fileCount], ["Unique chunks", d.chunkCount], ["Copies on disk", d.copyCount],
        ["Healthy copies", d.healthyCopies], ["Corrupted copies", d.corruptedCopies], ["Recoveries", d.successfulRecoveries]];
    main.innerHTML =
        `<div class="stats">${stats.map(([label, value]) => `<div class="card"><span class="muted">${label}</span><b>${Number(value).toLocaleString()}</b></div>`).join("")}</div>` +
        `<div class="card"><h2>Stored files</h2>${fileTable(d.files.slice(0, 6))}</div>` +
        `<div class="card"><h2>Recent activity</h2>${activityTable(d.activities.slice(0, 6))}</div>`;
}

function upload(file, mode, count, onProgress) {
    return new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        xhr.open("POST", "/api/files");
        xhr.timeout = 180000;
        xhr.setRequestHeader("Content-Type", file.type || "application/octet-stream");
        xhr.setRequestHeader("X-File-Name", encodeURIComponent(file.name));
        xhr.setRequestHeader("X-Chunk-Mode", mode);
        if (mode === "manual") xhr.setRequestHeader("X-Chunk-Count", String(count));
        xhr.upload.onprogress = (e) => e.lengthComputable && onProgress(Math.round((e.loaded * 100) / e.total));
        xhr.onload = () => {
            let data = null;
            try { data = JSON.parse(xhr.responseText); } catch {}
            if (xhr.status >= 200 && xhr.status < 300 && data) resolve(data);
            else reject(new Error(data?.error || `Storage failed (HTTP ${xhr.status}).`));
        };
        xhr.onerror = () => reject(new Error("The upload connection failed."));
        xhr.ontimeout = () => reject(new Error("The upload timed out."));
        xhr.send(file);
    });
}

async function loadFiles(t) {
    const files = await api("/api/files");
    const box = $("files");
    if (t !== token || !box) return;
    state.files = files;
    $("filecount").textContent = files.length;
    box.innerHTML = files.length
        ? bulkBar + table(["", "File", "ID", "Type", "Size", "Chunks", ""], files.map((f) => [
            `<input type="checkbox" class="pick" value="${esc(f.id)}" aria-label="Select ${esc(f.name)}">`,
            esc(f.name), mono(f.id), mono(f.type), fmtSize(f.size), f.chunkCount, downloadLink(f)]))
        : empty("No files stored yet.");
    wireBulk(box, async (ids) => {
        await deleteEach(ids, "/api/files/");
        loadFiles(t).catch((e) => toast(e.message, "error"));
    });
}

async function storagePage(t) {
    main.innerHTML = `
        <div class="card"><h2>Store a file</h2>
            <input type="file" id="file">
            <div class="row">
                <select id="mode"><option value="automatic">Automatic chunk size</option><option value="manual">Manual number of chunks</option></select>
                <input id="count" type="number" min="1" step="1" value="2" aria-label="Number of chunks" hidden>
                <button id="store" class="primary" disabled>Store file</button>
            </div>
            <progress id="bar" max="100" value="0" hidden></progress>
            <div id="result"></div>
        </div>
        <div class="card"><h2>Files <span id="filecount" class="muted"></span></h2><div id="files"></div></div>`;

    const file = $("file"), mode = $("mode"), count = $("count"), store = $("store"), bar = $("bar"), result = $("result");
    file.onchange = () => { store.disabled = !file.files[0]; };
    mode.onchange = () => { count.hidden = mode.value !== "manual"; };
    store.onclick = async () => {
        const chosen = file.files[0];
        if (!chosen) return;
        const n = Number(count.value);
        if (chosen.size === 0) return (result.innerHTML = note("The file is empty.", "error"));
        if (mode.value === "manual" && !(Number.isInteger(n) && n >= 1 && n <= chosen.size && n <= 4294967295)) {
            return (result.innerHTML = note("Enter a whole number of chunks between 1 and the file size in bytes.", "error"));
        }
        store.disabled = true;
        bar.hidden = false;
        bar.value = 0;
        result.innerHTML = note("Storing… keep this page open until it finishes.");
        try {
            const r = await upload(chosen, mode.value, n, (percent) => { bar.value = percent; });
            result.innerHTML = note(`${r.name} (${r.type}) stored · ${r.chunkCount} chunk(s) · ${r.id}`, "success");
            file.value = "";
            await loadFiles(t);
        } catch (e) {
            result.innerHTML = note(e.message, "error");
        }
        bar.hidden = true;
        store.disabled = !file.files[0];
    };
    await loadFiles(t);
}

const sim = { processes: [], resources: [], requests: [], nextPid: 1, schedule: null, cycle: null };

const say = (text, type = "") => { const el = $("simnote"); if (el) el.innerHTML = text ? note(text, type) : ""; };
const attempt = (fn) => async () => { try { await fn(); } catch (e) { say(e.message, "warning"); } };
const logActivity = (action, details) => post("/api/activity", { action, details }).catch((e) => toast("Could not save the activity log: " + e.message, "error"));

function graphParams() {
    const p = { processCount: sim.processes.length, resourceCount: sim.resources.length, requestCount: sim.requests.length };
    sim.processes.forEach((x, i) => { p["processPid" + i] = x.pid; });
    sim.resources.forEach((x, i) => { p["resourceId" + i] = x.id; p["resourceOwner" + i] = x.ownerPid || 0; });
    sim.requests.forEach((x, i) => { p["requestPid" + i] = x.pid; p["requestResource" + i] = x.resourceId; });
    return p;
}

async function simAction(action, pid = 0, resourceId = 0) {
    const r = await post("/api/os/resources", { action, pid, resourceId, ...graphParams() });
    sim.resources = r.resources.map((x) => ({ id: x.id, ownerPid: x.ownerPid || null }));
    sim.requests = r.requests;
    sim.cycle = null;
    return r;
}

function fill(id, items, emptyLabel) {
    const el = $(id), previous = el.value;
    el.innerHTML = items.length ? items.map(([value, label]) => `<option value="${esc(value)}">${esc(label)}</option>`).join("") : `<option value="">${emptyLabel}</option>`;
    if (items.some(([value]) => String(value) === previous)) el.value = previous;
}

function graphSvg() {
    const height = Math.max(sim.processes.length, sim.resources.length, 2) * 70 + 40;
    const place = (list, x) => new Map(list.map((item, i) => [item.pid ?? item.id, { x, y: 30 + ((height - 30) / (list.length + 1)) * (i + 1) }]));
    const procs = place(sim.processes, 120), ress = place(sim.resources, 480);
    const stuck = new Set(sim.cycle || []);
    const line = (a, b, cls) => (a && b ? `<line class="${cls}" x1="${a.x}" y1="${a.y}" x2="${b.x}" y2="${b.y}" marker-end="url(#arrow)"/>` : "");
    const box = (pt, label, sub, cls) => `<g class="node ${cls}"><rect x="${pt.x - 70}" y="${pt.y - 22}" width="140" height="44" rx="6"/>` +
        `<text x="${pt.x}" y="${pt.y - 3}" text-anchor="middle">${label}</text><text class="sub" x="${pt.x}" y="${pt.y + 14}" text-anchor="middle">${esc(sub)}</text></g>`;
    const edges = sim.resources.filter((r) => r.ownerPid).map((r) => line(ress.get(r.id), procs.get(r.ownerPid), "own")).join("") +
        sim.requests.map((q) => line(procs.get(q.pid), ress.get(q.resourceId), "wait" + (stuck.has(q.pid) ? " bad" : ""))).join("");
    const nodes = sim.processes.map((p) => box(procs.get(p.pid), "P" + p.pid, p.name, "")).join("") +
        sim.resources.map((r) => box(ress.get(r.id), "R" + r.id, r.ownerPid ? "held by P" + r.ownerPid : "available", "res")).join("");
    return `<svg class="graph" viewBox="0 0 600 ${height}" role="img" aria-label="Resource allocation graph"><defs>` +
        `<marker id="arrow" markerWidth="8" markerHeight="8" refX="7" refY="3" orient="auto"><path d="M0,0 L0,6 L7,3z" fill="currentColor"/></marker></defs>${edges}${nodes}</svg>` +
        '<p class="muted">Solid line: resource → owner. Dashed line: process waiting for a resource.</p>';
}

function drawSchedule() {
    const s = sim.schedule;
    $("sched").innerHTML = !s ? "" :
        `<div class="gantt">${s.gantt.map((g) => `<div class="${g.idle ? "idle" : ""}" style="flex:${Math.max(1, g.end - g.start)} 1 60px"><b>${g.idle ? "Idle" : "P" + g.pid}</b><br><span class="mono">${g.start}–${g.end}</span></div>`).join("")}</div>` +
        `<p>Average waiting <b>${s.averageWaiting.toFixed(2)}</b> · average turnaround <b>${s.averageTurnaround.toFixed(2)}</b> · CPU <b>${s.cpuUtilization.toFixed(1)}%</b> · throughput <b>${s.throughput.toFixed(3)}</b> per unit</p>` +
        table(["PID", "File", "Completion", "Waiting", "Turnaround"], s.results.map((r) => ["P" + r.pid, esc(r.name), r.completionTime, r.waitingTime, r.turnaroundTime]));
}

function drawOS() {
    const used = new Set(sim.processes.map((p) => p.fileId));
    $("pcount").textContent = sim.processes.length;
    $("picker").innerHTML = state.files.length
        ? state.files.map((f) => `<label class="pick-row"><input type="checkbox" class="fpick" value="${esc(f.id)}" ${used.has(f.id) ? "disabled" : ""}> ${esc(f.name)} ` +
            `<span class="muted mono">${esc(f.id)} · ${f.chunkCount} chunks${used.has(f.id) ? " · process created" : ""}</span></label>`).join("")
        : empty("Store a file first. Each stored file becomes one process, and its chunk count is the burst time.");
    $("create").disabled = !state.files.some((f) => !used.has(f.id));
    $("procs").innerHTML = table(["PID", "File", "Burst (chunks)", "Status"],
        sim.processes.map((p) => ["P" + p.pid, `${esc(p.name)} <span class="muted mono">${esc(p.fileId)}</span>`, p.burstTime, `<span class="badge ${p.status}">${p.status}</span>`]), "No processes yet.");
    drawSchedule();

    const procs = sim.processes.map((p) => [p.pid, `P${p.pid} · ${p.name}`]);
    fill("rproc", procs, "No processes");
    fill("kill", procs, "No processes");
    fill("rres", sim.resources.map((r) => [r.id, `R${r.id} · ${r.ownerPid ? "held by P" + r.ownerPid : "available"}`]), "No resources");
    const hasP = sim.processes.length > 0, hasR = sim.resources.length > 0;
    $("run").disabled = $("addres").disabled = $("detect").disabled = $("killbtn").disabled = !hasP;
    $("alloc").disabled = $("req").disabled = !hasP || !hasR;
    $("rcount").textContent = sim.resources.length;
    $("graph").innerHTML = hasP ? graphSvg() : empty("Create processes to see the resource graph.");
}

async function osPage(t) {
    const files = await api("/api/files");
    if (t !== token) return;
    state.files = files;
    main.innerHTML = `
        <div id="simnote"></div>
        <div class="card"><h2>1. Processes <span><span id="pcount" class="muted"></span> <button id="reset">Reset</button></span></h2>
            <div id="picker"></div>
            <div class="row"><button id="create">Create processes</button></div>
            <div id="procs"></div></div>
        <div class="card"><h2>2. Scheduling</h2>
            <div class="row">
                <select id="algo" aria-label="Algorithm"><option value="FCFS">FCFS</option><option value="SJF">SJF</option><option value="RR">Round Robin</option></select>
                <input id="quantum" type="number" min="1" step="1" value="2" aria-label="Time quantum" hidden>
                <button id="run" class="primary">Run scheduler</button>
            </div>
            <div id="sched"></div></div>
        <div class="card"><h2>3. Resources and deadlocks <span id="rcount" class="muted"></span></h2>
            <div class="row">
                <button id="addres">Add resource</button>
                <select id="rproc" aria-label="Process"></select><select id="rres" aria-label="Resource"></select>
                <button id="alloc">Allocate</button><button id="req">Request</button>
                <button id="detect" class="primary">Detect deadlock</button>
            </div>
            <div id="graph"></div>
            <div class="row"><select id="kill" aria-label="Process to terminate"></select>
                <button id="killbtn" class="danger">Terminate and release resources</button></div></div>`;

    $("algo").onchange = () => { $("quantum").hidden = $("algo").value !== "RR"; };

    $("create").onclick = () => {
        const chosen = [...document.querySelectorAll(".fpick:checked")].map((c) => state.files.find((f) => f.id === c.value)).filter(Boolean);
        if (!chosen.length) return say("Select one or more stored files.", "warning");
        if (chosen.some((f) => !(f.chunkCount > 0))) return say("A selected file has an invalid chunk count.", "error");
        for (const f of chosen) sim.processes.push({ pid: sim.nextPid++, fileId: f.id, name: f.name, arrivalTime: 0, burstTime: Number(f.chunkCount), status: "READY" });
        sim.schedule = sim.cycle = null;
        drawOS();
        say(`Created ${chosen.length} process(es).`, "success");
        logActivity("SIMULATION_PROCESS_CREATE", `Created ${chosen.length} process(es) from stored files.`);
    };

    $("run").onclick = attempt(async () => {
        const algorithm = $("algo").value, quantum = Number($("quantum").value);
        if (algorithm === "RR" && !(Number.isInteger(quantum) && quantum >= 1)) return say("Round Robin needs a whole-number time quantum of at least 1.", "error");
        const params = { algorithm, quantum, count: sim.processes.length };
        sim.processes.forEach((p, i) => Object.assign(params, { ["pid" + i]: p.pid, ["arrival" + i]: p.arrivalTime, ["burst" + i]: p.burstTime, ["name" + i]: p.name }));
        sim.schedule = await post("/api/os/schedule", params);
        sim.processes.forEach((p) => { p.status = "COMPLETED"; });
        sim.cycle = null;
        drawOS();
        const label = algorithm === "RR" ? "Round Robin" : algorithm;
        say(`${label} schedule calculated by the C++ backend (simulation units).`, "success");
        logActivity("SIMULATION_SCHEDULE", `Ran ${label} in C++ for ${sim.processes.length} process(es).`);
    });

    const resourceButton = (id, action, logName) => {
        $(id).onclick = attempt(async () => {
            const r = await simAction(action, Number($("rproc").value), Number($("rres").value));
            drawOS();
            say(r.message, "success");
            logActivity(logName, r.message);
        });
    };
    resourceButton("addres", "add", "SIMULATION_RESOURCE_ADD");
    resourceButton("alloc", "allocate", "SIMULATION_RESOURCE_ALLOCATE");
    resourceButton("req", "request", "SIMULATION_RESOURCE_REQUEST");

    $("detect").onclick = attempt(async () => {
        const r = await post("/api/os/deadlock", graphParams());
        sim.cycle = r.deadlocked ? r.cycle : [];
        drawOS();
        const path = (r.cycle || []).map((pid) => "P" + pid).join(" → ");
        say(r.deadlocked ? `Wait-for cycle found: ${path}. Terminate one of these processes.` : "No wait-for cycle was found.", r.deadlocked ? "error" : "success");
        logActivity("SIMULATION_DEADLOCK", r.deadlocked ? `Detected wait-for cycle ${path}.` : "Ran deadlock detection; no wait-for cycle was found.");
    });

    $("killbtn").onclick = attempt(async () => {
        const pid = Number($("kill").value);
        const proc = sim.processes.find((p) => p.pid === pid);
        if (!proc) return say("Choose a process to terminate.", "warning");
        const r = await simAction("terminate", pid);
        sim.processes = sim.processes.filter((p) => p.pid !== pid);
        sim.schedule = null;
        drawOS();
        const released = (r.releasedResourceIds || []).map((id) => "R" + id).join(", ") || "none";
        say(`${r.message} Released: ${released}. Run detection again.`, "success");
        logActivity("SIMULATION_PROCESS_TERMINATE", `Terminated P${pid} (${proc.name}); released ${released}.`);
    });

    $("reset").onclick = attempt(async () => {
        await simAction("reset");
        Object.assign(sim, { processes: [], nextPid: 1, schedule: null });
        drawOS();
        say("OS simulation reset. Stored files were not changed.", "success");
        logActivity("SIMULATION_RESET", "Reset the OS simulation state; stored files were retained.");
    });

    drawOS();
}

function decodeContent(hex) {
    const bytes = Uint8Array.from(hex.match(/../g) || [], (h) => parseInt(h, 16));
    const text = new TextDecoder().decode(bytes);
    const odd = (text.match(/[\u0000-\u0008\u000B\u000C\u000E-\u001F\uFFFD]/g) || []).length;
    if (bytes.length && odd / bytes.length > 0.02) {
        return { format: "Hex", text: (hex.match(/.{1,32}/g) || []).map((line) => line.match(/../g).join(" ")).join("\n") };
    }
    return { format: "Text", text };
}

async function viewFile(file) {
    const type = file.type || "";
    const url = `/api/files/${encodeURIComponent(file.id)}/download?inline=1`;
    $("vtitle").textContent = file.name;
    $("vmedia").innerHTML = "";
    $("vcontent").textContent = "";
    $("vcontent").hidden = false;
    $("viewer").showModal();
    if (/^image\/(png|jpeg|gif|webp|bmp)$/.test(type) || type === "application/pdf") {
        $("vstatus").textContent = type;
        $("vcontent").hidden = true;
        $("vmedia").innerHTML = type === "application/pdf"
            ? `<iframe src="${url}" title="${esc(file.name)}"></iframe>`
            : `<img src="${url}" alt="${esc(file.name)}" onerror="this.replaceWith('Could not load the image. Run Replicate and try again.')">`;
        return;
    }
    $("vstatus").textContent = "Loading from stored chunks…";
    try {
        const d = await api(`/api/files/${encodeURIComponent(file.id)}/content`);
        const view = decodeContent(d.contentHex || "");
        $("vstatus").textContent = `${d.verified ? "Assembled from checksum-verified copies" : "At least one chunk has no verified copy"} · ${view.format}${d.truncated ? " · first 1 MB shown" : ""}`;
        $("vcontent").textContent = view.text || "(empty file)";
    } catch (e) {
        $("vstatus").textContent = e.message;
    }
}

async function integrityPage(t) {
    const data = await api("/api/database");
    if (t !== token) return;
    state.files = data.files;
    let chunks = data.chunks, busy = false;
    main.innerHTML = `
        <div id="intmsg"></div>
        <div class="card"><h2>Files, chunks and replicas</h2><div id="summary"></div><div id="report"></div></div>
        <dialog id="viewer"><h3 id="vtitle"></h3><p id="vstatus" class="muted"></p><div id="vmedia"></div><pre id="vcontent"></pre><button id="vclose">Close</button></dialog>`;
    $("vclose").onclick = () => $("viewer").close();
    $("viewer").onclose = () => { $("vmedia").innerHTML = ""; };

    const draw = () => {
        const statuses = chunks.flatMap((c) => [c.main.status, c.node1.status, c.node2.status]);
        const count = (s) => statuses.filter((x) => x === s).length;
        $("summary").innerHTML = `<p>Healthy <b>${count("HEALTHY")}</b> · Corrupted <b>${count("CORRUPTED")}</b> · Missing <b>${count("MISSING")}</b> · Errors <b>${count("ERROR")}</b></p>`;
        $("report").innerHTML = state.files.length ? state.files.map((f) => {
            const own = chunks.filter((c) => c.fileId === f.id);
            const button = (act, label) => `<button data-act="${act}" data-id="${esc(f.id)}">${label}</button>`;
            return `<div class="file"><div class="row"><b>${esc(f.name)}</b><span class="muted mono">${esc(f.type)} · ${esc(f.id)} · ${fmtSize(f.size)} · ${own.length} chunk(s)</span>` +
                `<span class="spacer"></span>${button("view", "View")}${downloadLink(f)}${button("verify", "Verify")}${button("recover", "Replicate")}</div>` +
                table(["Chunk", "Size", "Main", "Node 1", "Node 2"], own.map((c) => [c.number, fmtSize(c.size), badge(c.main.status), badge(c.node1.status), badge(c.node2.status)]), "No chunk records for this file.") + "</div>";
        }).join("") : empty("No files stored yet.");
    };
    draw();

    $("report").onclick = async (e) => {
        const button = e.target.closest("button[data-act]");
        const file = button && state.files.find((f) => f.id === button.dataset.id);
        if (!file || busy) return;
        const act = button.dataset.act;
        if (act === "view") return viewFile(file);

        busy = true;
        $("intmsg").innerHTML = note(`${act === "verify" ? "Checking" : "Replicating"} ${file.name}…`);
        try {
            const r = await api(`/api/files/${encodeURIComponent(file.id)}/${act}`, { method: "POST" });
            chunks = chunks.filter((c) => c.fileId !== file.id).concat(r.chunks);
            draw();
            const restored = Number(r.restoredCopies) || 0;
            $("intmsg").innerHTML = act === "verify" ? note(`Verified ${r.chunks.length} chunk(s) on disk for ${file.name}.`, "success")
                : restored ? note(`${restored} copy/copies replicated and verified for ${file.name}.`, "success")
                : note("No copies were restored. Each chunk needs at least one healthy copy.", "warning");
        } catch (err) {
            $("intmsg").innerHTML = note(err.message, "error");
        }
        busy = false;
    };
}

async function databasePage(t) {
    const d = await api("/api/database");
    if (t !== token) return;
    const copy = (c) => `${esc(safePath(c.path))}<br>${badge(c.status)}`;
    main.innerHTML =
        `<div class="card"><h2>Files <span class="muted">${d.files.length}</span></h2>${fileTable(d.files, true)}</div>` +
        `<div class="card"><h2>Chunks and replicas <span class="muted">${d.chunks.length}</span></h2>` +
        table(["Chunk", "File", "#", "Size", "SHA-256", "Main", "Node 1", "Node 2"], d.chunks.map((c) => [
            mono(c.id), mono(c.fileId), c.number, fmtSize(c.size),
            `<span class="mono" title="${esc(c.checksum)}">${esc(c.checksum.slice(0, 12))}…</span>`, copy(c.main), copy(c.node1), copy(c.node2)]), "No chunk records.") + "</div>" +
        '<div class="card"><h2>Activity log</h2><div id="logs"></div></div>';

    const drawLogs = (items) => {
        const box = $("logs");
        box.innerHTML = (items.length ? bulkBar : "") + activityTable(items, true);
        wireBulk(box, async (ids) => {
            await deleteEach(ids, "/api/activity/");
            try {
                const fresh = await api("/api/database");
                if (t === token) drawLogs(fresh.activities);
            } catch (e) { toast(e.message, "error"); }
        });
    };
    drawLogs(d.activities);
}

const pages = { dashboard: dashboardPage, storage: storagePage, os: osPage, integrity: integrityPage, database: databasePage };

async function show(name) {
    const t = ++token;
    document.querySelectorAll("nav button").forEach((b) => b.classList.toggle("active", b.dataset.page === name));
    main.innerHTML = '<p class="muted">Loading…</p>';
    try {
        await pages[name](t);
    } catch (e) {
        if (t === token) main.innerHTML = note(e.message, "error");
    }
}

document.querySelector("nav").onclick = (e) => {
    const button = e.target.closest("button[data-page]");
    if (button) show(button.dataset.page);
};
show("dashboard");