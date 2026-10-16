const state = {
    files: [],
    jobs: [],
    processes: [],
    resources: [],
    allocations: [],
    requests: [],
    logs: [],
    recoveries: 0,
    nextPid: 1,
    nextResource: 1,
    selectedFileId: null
};

const pages = {
    dashboard: {
        title: "Dashboard",
        description: "Overview of ChunkVault storage and replica health.",
        content: `
            <div class="stats-grid" id="dashboard-stats"></div>
            <div class="content-grid">
                <div class="panel">
                    <div class="panel-header"><h3>Recently Uploaded Files</h3></div>
                    <div id="recent-files" class="empty-state">No files added yet.</div>
                </div>
                <div class="panel">
                    <div class="panel-header"><h3>Storage Overview</h3></div>
                    <div id="storage-overview"></div>
                </div>
            </div>
            <div class="panel">
                <div class="panel-header"><h3>Recent Activity</h3></div>
                <div id="recent-activity"></div>
            </div>
            <div class="notice">Frontend simulation only. These statistics are based on this browser session, not your C++ storage or MySQL database.</div>`
    },

    storage: {
        title: "File Storage",
        description: "Prepare storage jobs using the same chunking choices as the terminal application.",
        content: `
            <div class="panel">
                <div class="panel-header"><h3>Select Files</h3><span class="tag">Storage</span></div>
                <form id="storage-form">
                    <div class="field">
                        <label for="files">Choose one or multiple files</label>
                        <input id="files" type="file" multiple required>
                    </div>
                    <div class="form-grid">
                        <div class="field">
                            <label for="chunk-mode">Chunking mode</label>
                            <select id="chunk-mode">
                                <option value="automatic">Automatic</option>
                                <option value="manual">Manual number of chunks</option>
                            </select>
                        </div>
                        <div class="field" id="chunk-count-field" hidden>
                            <label for="chunk-count">Chunks per file</label>
                            <input id="chunk-count" type="number" min="1" value="2">
                        </div>
                        <div class="field">
                            <label for="storage-algorithm">CPU scheduling algorithm</label>
                            <select id="storage-algorithm">
                                <option>FCFS</option>
                                <option>SJF</option>
                                <option>Round Robin</option>
                            </select>
                        </div>
                        <div class="field">
                            <label for="storage-quantum">Round Robin quantum</label>
                            <input id="storage-quantum" type="number" min="1" value="2">
                        </div>
                    </div>
                    <div class="button-row">
                        <button class="primary-button" type="submit">Prepare Storage Jobs</button>
                        <button class="secondary-button" type="reset">Clear Selection</button>
                    </div>
                </form>
                <div id="storage-notice" class="notice">Select files to prepare jobs.</div>
            </div>
            <div class="panel">
                <div class="panel-header"><h3>Storage Job Queue</h3><span class="tag" id="job-count">0 jobs</span></div>
                <div id="job-table" class="empty-state">No jobs prepared.</div>
            </div>`
    },

    "os-simulator": {
        title: "OS Simulator",
        description: "Simulate CPU scheduling, resource allocation, deadlock detection and recovery.",
        content: `
            <div class="panel">
                <div class="panel-header"><h3>Create Processes</h3></div>
                <div class="field">
                    <label for="process-files">Select files to create processes</label>
                    <select id="process-files" multiple size="4"></select>
                </div>
                <div class="form-grid">
                    <div class="field">
                        <label for="burst-time">CPU burst time</label>
                        <input id="burst-time" type="number" min="1" value="5">
                    </div>
                    <div class="field">
                        <label for="os-algorithm">Scheduling algorithm</label>
                        <select id="os-algorithm">
                            <option>FCFS</option>
                            <option>SJF</option>
                            <option>Round Robin</option>
                        </select>
                    </div>
                    <div class="field">
                        <label for="time-quantum">Time quantum</label>
                        <input id="time-quantum" type="number" min="1" value="2">
                    </div>
                </div>
                <div class="button-row">
                    <button class="primary-button" id="create-processes">Create Processes</button>
                    <button class="secondary-button" id="run-scheduler">Run CPU Scheduling</button>
                    <button class="secondary-button" id="reset-os">Reset Simulation</button>
                </div>
                <div id="os-notice" class="notice">Select files to create processes.</div>
            </div>
            <div class="panel">
                <div class="panel-header"><h3>Process Table</h3></div>
                <div id="process-table" class="empty-state">No processes created.</div>
            </div>
            <div class="panel">
                <div class="panel-header"><h3>CPU Gantt Chart</h3></div>
                <div id="gantt-chart" class="empty-state">Run scheduling to see execution order.</div>
                <div id="schedule-stats"></div>
            </div>
            <div class="content-grid">
                <div class="panel">
                    <div class="panel-header"><h3>Resource Management</h3></div>
                    <div class="form-grid">
                        <div class="field">
                            <label for="resource-process">Process</label>
                            <select id="resource-process"></select>
                        </div>
                        <div class="field">
                            <label for="resource-id">Resource</label>
                            <select id="resource-id"></select>
                        </div>
                    </div>
                    <div class="button-row">
                        <button class="primary-button" id="add-resource">Add Resource</button>
                        <button class="secondary-button" id="allocate-resource">Allocate</button>
                        <button class="secondary-button" id="request-resource">Request</button>
                    </div>
                    <div id="resource-notice" class="notice">Add resources and create processes to begin.</div>
                </div>
                <div class="panel">
                    <div class="panel-header"><h3>Deadlock Detection</h3></div>
                    <button class="primary-button" id="detect-deadlock">Detect Deadlock</button>
                    <div id="deadlock-results" class="notice">No detection run yet.</div>
                    <div class="field">
                        <label for="terminate-process">Process to terminate</label>
                        <select id="terminate-process"></select>
                    </div>
                    <button class="secondary-button" id="terminate-deadlock-process">Terminate Process & Release Resources</button>
                </div>
            </div>`
    },

    integrity: {
        title: "Integrity & Recovery",
        description: "Inspect chunk copies, simulate corruption, and demonstrate recovery.",
        content: `
            <div class="panel">
                <div class="panel-header"><h3>Select File</h3></div>
                <div class="field">
                    <label for="integrity-file">Stored file</label>
                    <select id="integrity-file"></select>
                </div>
                <div class="button-row">
                    <button class="primary-button" id="verify-file">Verify Copies</button>
                    <button class="secondary-button" id="simulate-corruption">Simulate Corruption</button>
                    <button class="secondary-button" id="simulate-missing">Simulate Missing Copy</button>
                    <button class="secondary-button" id="recover-file">Recover Unhealthy Copies</button>
                </div>
                <div id="integrity-notice" class="notice">Select a file to inspect its copies.</div>
            </div>
            <div class="panel">
                <div class="panel-header"><h3>Chunk Integrity Report</h3></div>
                <div id="integrity-table" class="empty-state">No file selected.</div>
            </div>
            <div class="panel">
                <div class="panel-header"><h3>View File Content</h3></div>
                <div class="field">
                    <label for="content-copy">Copy to inspect</label>
                    <select id="content-copy">
                        <option value="main">Main chunk</option>
                        <option value="node1">Node 1 replica</option>
                        <option value="node2">Node 2 replica</option>
                    </select>
                </div>
                <button class="secondary-button" id="view-content">View Content</button>
                <pre id="file-content" class="notice">Choose a file and copy to inspect.</pre>
            </div>
            <div class="notice">Corruption, deletion and recovery on this page are simulated in browser memory. They do not modify your actual storage files.</div>`
    },

    database: {
        title: "Database & Logs",
        description: "Review simulated file metadata and activity from this browser session.",
        content: `
            <div class="panel">
                <div class="panel-header"><h3>File Metadata</h3></div>
                <div id="database-files" class="empty-state">No files available.</div>
            </div>
            <div class="panel">
                <div class="panel-header"><h3>Chunk Metadata</h3></div>
                <div id="database-chunks" class="empty-state">No chunks available.</div>
            </div>
            <div class="panel">
                <div class="panel-header"><h3>Activity Log</h3></div>
                <div id="activity-log" class="empty-state">No activity recorded.</div>
            </div>
            <div class="notice">This page currently shows session data, not live MySQL records.</div>`
    }
};

const navigation = document.getElementById("navigation");
const pageTitle = document.getElementById("page-title");
const pageContent = document.getElementById("page-content");

function escapeHTML(value) {
    return String(value).replace(/[&<>"']/g, character => ({
        "&": "&amp;",
        "<": "&lt;",
        ">": "&gt;",
        '"': "&quot;",
        "'": "&#39;"
    })[character]);
}

function formatSize(bytes) {
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 ** 2) return `${(bytes / 1024).toFixed(1)} KB`;
    if (bytes < 1024 ** 3) return `${(bytes / 1024 ** 2).toFixed(1)} MB`;
    return `${(bytes / 1024 ** 3).toFixed(2)} GB`;
}

function addLog(message) {
    state.logs.unshift({
        time: new Date().toLocaleTimeString(),
        message
    });
    state.logs = state.logs.slice(0, 100);
}

function getFile(fileId) {
    return state.files.find(file => file.id === fileId);
}

function getAllCopies() {
    return state.files.flatMap(file =>
        file.chunks.flatMap(chunk => [
            { file, chunk, name: "Main", copy: chunk.main },
            { file, chunk, name: "Node 1", copy: chunk.node1 },
            { file, chunk, name: "Node 2", copy: chunk.node2 }
        ])
    );
}

function renderPage(pageName) {
    const page = pages[pageName] || pages.dashboard;

    pageTitle.textContent = page.title;
    pageContent.innerHTML = `
        <div class="page-intro">
            <h2>${page.title}</h2>
            <p>${page.description}</p>
        </div>
        ${page.content}
    `;

    navigation.querySelectorAll(".nav-item").forEach(button => {
        button.classList.toggle("active", button.dataset.page === pageName);
    });

    if (pageName === "dashboard") renderDashboard();
    if (pageName === "storage") connectStoragePage();
    if (pageName === "os-simulator") connectOSPage();
    if (pageName === "integrity") connectIntegrityPage();
    if (pageName === "database") renderDatabasePage();
}

function renderDashboard() {
    const copies = getAllCopies();
    const healthy = copies.filter(item => item.copy.status === "HEALTHY").length;
    const corrupted = copies.filter(item => item.copy.status === "CORRUPTED").length;
    const missing = copies.filter(item => item.copy.status === "MISSING").length;

    const cards = [
        ["Uploaded Files", state.files.length, "Files in this session"],
        ["Total Chunks & Copies", `${state.files.reduce((sum, file) => sum + file.chunks.length, 0)} / ${copies.length}`, "Unique chunks / physical copies"],
        ["Healthy Copies", healthy, "Main chunks and replicas"],
        ["Corrupted Copies", corrupted, "Copies failing integrity checks"],
        ["Missing Copies", missing, "Copies not present"],
        ["Successful Recoveries", state.recoveries, "Copies restored in this session"]
    ];

    document.getElementById("dashboard-stats").innerHTML = cards.map(([label, value, note]) => `
        <div class="stat-card">
            <p class="stat-label">${escapeHTML(label)}</p>
            <div class="stat-value">${escapeHTML(value)}</div>
            <p class="stat-note">${escapeHTML(note)}</p>
        </div>
    `).join("");

    document.getElementById("recent-files").innerHTML = state.files.length
        ? tableHTML(["File", "File ID", "Size", "Chunks"], state.files.slice(-5).reverse().map(file => [
            file.name, file.id, formatSize(file.size), file.chunks.length
        ]))
        : `<div class="empty-state">No files added yet.</div>`;

    document.getElementById("storage-overview").innerHTML = `
        <div class="workflow">
            <div class="workflow-step"><div class="workflow-number">1</div><div><strong>${state.files.length} files</strong><p>Selected in this session</p></div></div>
            <div class="workflow-step"><div class="workflow-number">2</div><div><strong>${copies.length} physical copies</strong><p>Main + Node 1 + Node 2</p></div></div>
            <div class="workflow-step"><div class="workflow-number">3</div><div><strong>${healthy} healthy</strong><p>${corrupted} corrupted, ${missing} missing</p></div></div>
        </div>`;

    document.getElementById("recent-activity").innerHTML = renderLogs(state.logs.slice(0, 5));
}

function tableHTML(headers, rows) {
    if (!rows.length) return `<div class="empty-state">No records available.</div>`;

    return `<div class="table-wrap"><table>
        <thead><tr>${headers.map(header => `<th>${escapeHTML(header)}</th>`).join("")}</tr></thead>
        <tbody>${rows.map(row => `<tr>${row.map(value => `<td>${escapeHTML(value)}</td>`).join("")}</tr>`).join("")}</tbody>
    </table></div>`;
}

function renderLogs(logs) {
    if (!logs.length) return `<div class="empty-state">No activity recorded yet.</div>`;

    return `<div class="activity-list">${logs.map(log => `
        <div class="activity-item">
            <div class="activity-icon">•</div>
            <div><strong>${escapeHTML(log.message)}</strong><p>${escapeHTML(log.time)}</p></div>
        </div>
    `).join("")}</div>`;
}

function connectStoragePage() {
    const form = document.getElementById("storage-form");
    const mode = document.getElementById("chunk-mode");
    const countField = document.getElementById("chunk-count-field");

    mode.addEventListener("change", () => {
        countField.hidden = mode.value !== "manual";
    });

    form.addEventListener("reset", () => {
        setTimeout(() => {
            countField.hidden = true;
            document.getElementById("job-table").innerHTML = "No jobs prepared.";
            document.getElementById("job-count").textContent = "0 jobs";
            document.getElementById("storage-notice").textContent = "Select files to prepare jobs.";
        }, 0);
    });

    form.addEventListener("submit", async event => {
        event.preventDefault();

        const files = Array.from(document.getElementById("files").files);
        const modeValue = mode.value;
        const requestedCount = Number(document.getElementById("chunk-count").value);
        const algorithm = document.getElementById("storage-algorithm").value;

        if (!files.length) return;

        if (modeValue === "manual" &&
            (!Number.isInteger(requestedCount) || requestedCount < 1)) {
            document.getElementById("storage-notice").textContent = "Enter a valid positive chunk count.";
            return;
        }

        const newFiles = [];

        for (const file of files) {
            const content = await file.text();
            const chunkCount = modeValue === "manual"
                ? Math.min(requestedCount, Math.max(1, file.size || 1))
                : Math.max(1, Math.ceil(file.size / (256 * 1024 * 1024)));

            const fileId = `FILE_${Math.random().toString(36).substring(2, 6).toUpperCase()}`;
            const chunks = [];

            for (let index = 0; index < chunkCount; index++) {
                const start = Math.floor(content.length * index / chunkCount);
                const end = Math.floor(content.length * (index + 1) / chunkCount);
                const chunkContent = content.slice(start, end);
                const chunkId = `${fileId}_CH${String(index + 1).padStart(3, "0")}`;

                chunks.push({
                    id: chunkId,
                    number: index + 1,
                    content: chunkContent,
                    checksum: await checksum(chunkContent),
                    main: { status: "HEALTHY", content: chunkContent },
                    node1: { status: "HEALTHY", content: chunkContent },
                    node2: { status: "HEALTHY", content: chunkContent }
                });
            }

            const entry = {
                id: fileId,
                name: file.name,
                size: file.size,
                type: file.type || "unknown",
                chunks,
                algorithm,
                originalContent: content
            };

            newFiles.push(entry);
        }

        state.files.push(...newFiles);

        state.jobs = newFiles.map(file => ({
            pid: state.nextPid++,
            fileId: file.id,
            fileName: file.name,
            burstTime: Math.max(1, Math.ceil(file.size / 1024)),
            status: "Prepared"
        }));

        addLog(`Prepared ${newFiles.length} storage job(s).`);
        document.getElementById("job-count").textContent = `${state.jobs.length} jobs`;
        document.getElementById("job-table").innerHTML = tableHTML(
            ["PID", "File", "Size", "Chunks", "Algorithm", "Status"],
            state.jobs.map(job => {
                const file = getFile(job.fileId);
                return [job.pid, job.fileName, formatSize(file.size), file.chunks.length, algorithm, job.status];
            })
        );

        document.getElementById("storage-notice").textContent =
            "Jobs prepared in browser memory only. No real disk storage or MySQL write has occurred.";

        renderDashboard();
    });
}

async function checksum(value) {
    if (window.crypto && window.crypto.subtle) {
        const data = new TextEncoder().encode(value);
        const digest = await crypto.subtle.digest("SHA-256", data);
        return Array.from(new Uint8Array(digest))
            .map(byte => byte.toString(16).padStart(2, "0"))
            .join("");
    }

    return `SIM-${value.length}-${value.charCodeAt(0) || 0}`;
}

function connectOSPage() {
    const fileSelect = document.getElementById("process-files");

    fileSelect.innerHTML = state.files.map(file =>
        `<option value="${escapeHTML(file.id)}">${escapeHTML(file.name)} (${escapeHTML(file.id)})</option>`
    ).join("");

    refreshOSControls();

    document.getElementById("create-processes").addEventListener("click", () => {
        const selectedIds = Array.from(fileSelect.selectedOptions).map(option => option.value);
        const burstTime = Number(document.getElementById("burst-time").value);

        if (!selectedIds.length) {
            document.getElementById("os-notice").textContent = "Prepare files in File Storage first, then select them here.";
            return;
        }

        if (!Number.isInteger(burstTime) || burstTime < 1) {
            document.getElementById("os-notice").textContent = "Burst time must be a positive integer.";
            return;
        }

        selectedIds.forEach(fileId => {
            const file = getFile(fileId);

            if (!state.processes.some(process => process.fileId === fileId)) {
                state.processes.push({
                    pid: state.nextPid++,
                    fileId,
                    name: file.name,
                    burstTime,
                    remainingTime: burstTime,
                    status: "Ready",
                    completionTime: 0,
                    waitingTime: 0,
                    turnaroundTime: 0
                });
            }
        });

        addLog(`Created processes for ${selectedIds.length} selected file(s).`);
        document.getElementById("os-notice").textContent = "Processes created. Run CPU scheduling to see the execution.";
        renderProcesses();
        refreshOSControls();
    });

    document.getElementById("run-scheduler").addEventListener("click", runScheduling);

    document.getElementById("reset-os").addEventListener("click", () => {
        state.processes = [];
        state.resources = [];
        state.allocations = [];
        state.requests = [];
        addLog("OS simulation reset. Stored-file preview data was retained.");
        document.getElementById("os-notice").textContent = "Simulation reset. Files remain available.";
        document.getElementById("gantt-chart").innerHTML = "Run scheduling to see execution order.";
        document.getElementById("schedule-stats").innerHTML = "";
        document.getElementById("deadlock-results").textContent = "Simulation reset.";
        renderProcesses();
        refreshOSControls();
    });

    document.getElementById("add-resource").addEventListener("click", () => {
        const id = `R${state.nextResource++}`;
        state.resources.push({ id, available: true });
        addLog(`Added resource ${id}.`);
        refreshOSControls();
        renderResourceState();
    });

    document.getElementById("allocate-resource").addEventListener("click", () => {
        const pid = Number(document.getElementById("resource-process").value);
        const resourceId = document.getElementById("resource-id").value;
        const resource = state.resources.find(item => item.id === resourceId);

        if (!pid || !resource) {
            document.getElementById("resource-notice").textContent = "Create a process and add a resource first.";
            return;
        }

        if (!resource.available) {
            document.getElementById("resource-notice").textContent = "Resource is already allocated.";
            return;
        }

        state.allocations.push({ pid, resourceId });
        resource.available = false;
        state.requests = state.requests.filter(request =>
            !(request.pid === pid && request.resourceId === resourceId)
        );

        addLog(`Allocated ${resourceId} to P${pid}.`);
        renderResourceState();
        refreshOSControls();
    });

    document.getElementById("request-resource").addEventListener("click", () => {
        const pid = Number(document.getElementById("resource-process").value);
        const resourceId = document.getElementById("resource-id").value;

        if (!pid || !resourceId) {
            document.getElementById("resource-notice").textContent = "Create a process and add a resource first.";
            return;
        }

        if (!state.requests.some(request => request.pid === pid && request.resourceId === resourceId)) {
            state.requests.push({ pid, resourceId });
        }

        addLog(`P${pid} requested ${resourceId}.`);
        renderResourceState();
    });

    document.getElementById("detect-deadlock").addEventListener("click", () => {
        const result = detectDeadlock();
        document.getElementById("deadlock-results").textContent = result.message;
        addLog(result.message);
    });

    document.getElementById("terminate-deadlock-process").addEventListener("click", () => {
        const pid = Number(document.getElementById("terminate-process").value);

        if (!pid) {
            document.getElementById("resource-notice").textContent = "Choose a process to terminate.";
            return;
        }

        state.allocations = state.allocations.filter(allocation => {
            if (allocation.pid !== pid) return true;

            const resource = state.resources.find(item => item.id === allocation.resourceId);
            if (resource) resource.available = true;
            return false;
        });

        state.requests = state.requests.filter(request => request.pid !== pid);
        state.processes = state.processes.filter(process => process.pid !== pid);

        addLog(`Terminated P${pid} and released its allocated resources.`);
        document.getElementById("deadlock-results").textContent = `P${pid} terminated. Its resources were released. Run detection again.`;
        renderProcesses();
        refreshOSControls();
        renderResourceState();
    });
}

function runScheduling() {
    if (!state.processes.length) {
        document.getElementById("os-notice").textContent = "Create processes before running scheduling.";
        return;
    }

    const algorithm = document.getElementById("os-algorithm").value;
    const quantum = Number(document.getElementById("time-quantum").value);

    if (algorithm === "Round Robin" && (!Number.isInteger(quantum) || quantum < 1)) {
        document.getElementById("os-notice").textContent = "Time quantum must be a positive integer.";
        return;
    }

    const processes = state.processes.map(process => ({
        ...process,
        remainingTime: process.burstTime,
        waitingTime: 0,
        turnaroundTime: 0,
        completionTime: 0
    }));

    let time = 0;
    const gantt = [];

    if (algorithm === "Round Robin") {
        const queue = [...processes];
        while (queue.length) {
            const process = queue.shift();
            const duration = Math.min(quantum, process.remainingTime);

            gantt.push({ pid: process.pid, start: time, end: time + duration });
            time += duration;
            process.remainingTime -= duration;

            if (process.remainingTime > 0) {
                queue.push(process);
            } else {
                process.completionTime = time;
                process.turnaroundTime = process.completionTime;
                process.waitingTime = process.turnaroundTime - process.burstTime;
            }
        }
    } else {
        const ordered = algorithm === "SJF"
            ? processes.sort((a, b) => a.burstTime - b.burstTime || a.pid - b.pid)
            : processes.sort((a, b) => a.pid - b.pid);

        ordered.forEach(process => {
            gantt.push({ pid: process.pid, start: time, end: time + process.burstTime });
            time += process.burstTime;
            process.completionTime = time;
            process.turnaroundTime = time;
            process.waitingTime = process.turnaroundTime - process.burstTime;
        });
    }

    const results = new Map(processes.map(process => [process.pid, process]));

    document.getElementById("gantt-chart").innerHTML = `
        <div style="display:flex;gap:4px;align-items:stretch;overflow-x:auto;padding:12px 0">
            ${gantt.map(item => `
                <div style="min-width:70px;flex:1;border:1px solid var(--border);border-radius:6px;padding:12px;text-align:center">
                    <strong>P${item.pid}</strong>
                    <p>${item.start} – ${item.end}</p>
                </div>
            `).join("")}
        </div>
        <p class="muted">Timeline ends at time ${time}.</p>`;

    document.getElementById("schedule-stats").innerHTML = tableHTML(
        ["PID", "Process", "Burst", "Completion", "Waiting", "Turnaround"],
        [...results.values()].sort((a, b) => a.pid - b.pid).map(process => [
            `P${process.pid}`,
            process.name,
            process.burstTime,
            process.completionTime,
            process.waitingTime,
            process.turnaroundTime
        ])
    );

    state.processes.forEach(process => {
        const result = results.get(process.pid);
        process.completionTime = result.completionTime;
        process.waitingTime = result.waitingTime;
        process.turnaroundTime = result.turnaroundTime;
        process.status = "Completed";
    });

    addLog(`Ran ${algorithm} scheduling for ${processes.length} process(es).`);
    renderProcesses();
    document.getElementById("os-notice").textContent = `${algorithm} scheduling completed.`;
}

function renderProcesses() {
    document.getElementById("process-table").innerHTML = tableHTML(
        ["PID", "File", "Burst", "State", "Waiting", "Turnaround"],
        state.processes.map(process => [
            `P${process.pid}`,
            process.name,
            process.burstTime,
            process.status,
            process.waitingTime,
            process.turnaroundTime
        ])
    );
}

function refreshOSControls() {
    const options = state.processes.map(process =>
        `<option value="${process.pid}">P${process.pid} — ${escapeHTML(process.name)}</option>`
    ).join("");

    document.getElementById("resource-process").innerHTML = options;
    document.getElementById("terminate-process").innerHTML = options;

    document.getElementById("resource-id").innerHTML = state.resources.map(resource =>
        `<option value="${resource.id}">${resource.id} (${resource.available ? "Available" : "Allocated"})</option>`
    ).join("");
}

function renderResourceState() {
    const allocations = state.allocations.length
        ? state.allocations.map(item => `P${item.pid} → ${item.resourceId}`).join(", ")
        : "None";

    const requests = state.requests.length
        ? state.requests.map(item => `P${item.pid} → ${item.resourceId}`).join(", ")
        : "None";

    document.getElementById("resource-notice").textContent =
        `Resources: ${state.resources.length}. Allocations: ${allocations}. Requests: ${requests}.`;

    refreshOSControls();
}

function detectDeadlock() {
    const processes = state.processes.map(process => process.pid);
    const available = state.resources.filter(resource => resource.available).map(resource => resource.id);
    const allocationByPid = new Map();
    const requestByPid = new Map();

    state.allocations.forEach(item => {
        if (!allocationByPid.has(item.pid)) allocationByPid.set(item.pid, []);
        allocationByPid.get(item.pid).push(item.resourceId);
    });

    state.requests.forEach(item => {
        if (!requestByPid.has(item.pid)) requestByPid.set(item.pid, []);
        requestByPid.get(item.pid).push(item.resourceId);
    });

    const unfinished = new Set(processes);
    let progress = true;

    while (progress) {
        progress = false;

        for (const pid of [...unfinished]) {
            const requests = requestByPid.get(pid) || [];
            const canProceed = requests.every(resourceId =>
                available.includes(resourceId) ||
                !state.resources.some(resource => resource.id === resourceId && !resource.available)
            );

            if (canProceed) {
                unfinished.delete(pid);
                (allocationByPid.get(pid) || []).forEach(resourceId => available.push(resourceId));
                progress = true;
            }
        }
    }

    const deadlocked = [...unfinished];

    if (!deadlocked.length) {
        return { deadlocked: false, message: "No deadlock detected in the current resource state." };
    }

    return {
        deadlocked: true,
        processes: deadlocked,
        message: `Potential deadlock detected involving ${deadlocked.map(pid => `P${pid}`).join(", ")}. Terminate a selected process to release its resources.`
    };
}

function connectIntegrityPage() {
    const fileSelect = document.getElementById("integrity-file");

    fileSelect.innerHTML = state.files.map(file =>
        `<option value="${escapeHTML(file.id)}">${escapeHTML(file.name)} (${escapeHTML(file.id)})</option>`
    ).join("");

    if (!state.files.length) {
        document.getElementById("integrity-notice").textContent = "Prepare files in File Storage first.";
    }

    function selectedFile() {
        return getFile(fileSelect.value);
    }

    function renderIntegrity() {
        const file = selectedFile();

        if (!file) {
            document.getElementById("integrity-table").innerHTML = `<div class="empty-state">No file selected.</div>`;
            return;
        }

        document.getElementById("integrity-table").innerHTML = tableHTML(
            ["Chunk ID", "Copy", "Status", "Checksum"],
            file.chunks.flatMap(chunk => [
                [chunk.id, "Main", chunk.main.status, chunk.checksum],
                [chunk.id, "Node 1", chunk.node1.status, chunk.checksum],
                [chunk.id, "Node 2", chunk.node2.status, chunk.checksum]
            ])
        );
    }

    document.getElementById("verify-file").addEventListener("click", () => {
        const file = selectedFile();
        if (!file) return;

        file.chunks.forEach(chunk => {
            ["main", "node1", "node2"].forEach(copyName => {
                const copy = chunk[copyName];
                if (copy.status !== "MISSING") {
                    copy.status = copy.content === chunk.content ? "HEALTHY" : "CORRUPTED";
                }
            });
        });

        document.getElementById("integrity-notice").textContent = "Verification completed for the selected file.";
        addLog(`Verified copies for ${file.name}.`);
        renderIntegrity();
        renderDashboard();
    });

    document.getElementById("simulate-corruption").addEventListener("click", () => {
        const file = selectedFile();
        if (!file || !file.chunks.length) return;

        const chunk = file.chunks[0];
        chunk.main.content = `${chunk.main.content}CORRUPTED`;
        chunk.main.status = "CORRUPTED";

        document.getElementById("integrity-notice").textContent = `Simulated corruption in main copy of ${chunk.id}.`;
        addLog(`Simulated corruption in ${chunk.id} main copy.`);
        renderIntegrity();
        renderDashboard();
    });

    document.getElementById("simulate-missing").addEventListener("click", () => {
        const file = selectedFile();
        if (!file || !file.chunks.length) return;

        const chunk = file.chunks[0];
        chunk.node1.status = "MISSING";

        document.getElementById("integrity-notice").textContent = `Simulated missing Node 1 copy for ${chunk.id}.`;
        addLog(`Simulated missing Node 1 copy for ${chunk.id}.`);
        renderIntegrity();
        renderDashboard();
    });

    document.getElementById("recover-file").addEventListener("click", () => {
        const file = selectedFile();
        if (!file) return;

        let restored = 0;

        file.chunks.forEach(chunk => {
            const copies = [chunk.main, chunk.node1, chunk.node2];
            const healthyCopy = copies.find(copy =>
                copy.status === "HEALTHY" && copy.content === chunk.content
            );

            if (!healthyCopy) return;

            copies.forEach(copy => {
                if (copy.status !== "HEALTHY" || copy.content !== chunk.content) {
                    copy.content = healthyCopy.content;
                    copy.status = "HEALTHY";
                    restored++;
                }
            });
        });

        state.recoveries += restored;
        document.getElementById("integrity-notice").textContent = restored
            ? `Recovery completed. ${restored} copy/copies restored and marked healthy.`
            : "No unhealthy copies could be recovered from a healthy copy.";

        addLog(`Recovery finished for ${file.name}: ${restored} copy/copies restored.`);
        renderIntegrity();
        renderDashboard();
    });

    document.getElementById("view-content").addEventListener("click", () => {
        const file = selectedFile();
        if (!file) return;

        const copyName = document.getElementById("content-copy").value;
        const content = file.chunks.map(chunk => {
            const copy = chunk[copyName];
            return `--- ${chunk.id} | ${copyName} | ${copy.status} ---\n${copy.content}`;
        }).join("\n\n");

        document.getElementById("file-content").textContent = content || "No chunk content available.";
    });

    fileSelect.addEventListener("change", renderIntegrity);
    renderIntegrity();
}

function renderDatabasePage() {
    document.getElementById("database-files").innerHTML = tableHTML(
        ["File ID", "File Name", "Size", "Chunk Count", "Type"],
        state.files.map(file => [file.id, file.name, formatSize(file.size), file.chunks.length, file.type])
    );

    document.getElementById("database-chunks").innerHTML = tableHTML(
        ["Chunk ID", "File ID", "Number", "Size", "Main", "Node 1", "Node 2"],
        state.files.flatMap(file => file.chunks.map(chunk => [
            chunk.id,
            file.id,
            chunk.number,
            formatSize(new Blob([chunk.content]).size),
            chunk.main.status,
            chunk.node1.status,
            chunk.node2.status
        ]))
    );

    document.getElementById("activity-log").innerHTML = renderLogs(state.logs);
}

navigation.addEventListener("click", event => {
    const button = event.target.closest(".nav-item");
    if (button) renderPage(button.dataset.page);
});

renderPage("dashboard");