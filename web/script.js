const pages = {
    dashboard: {
        title: "Dashboard",
        description: "Monitor your distributed storage system and operating system simulations.",
        content: `
            <div class="stats-grid">
                <div class="stat-card"><p class="stat-label">Files Stored</p><div class="stat-value">—</div><p class="stat-note">Awaiting backend connection</p></div>
                <div class="stat-card"><p class="stat-label">Total Chunks</p><div class="stat-value">—</div><p class="stat-note">Awaiting backend connection</p></div>
                <div class="stat-card"><p class="stat-label">Healthy Replicas</p><div class="stat-value">—</div><p class="stat-note">Awaiting backend connection</p></div>
                <div class="stat-card"><p class="stat-label">Recoveries</p><div class="stat-value">—</div><p class="stat-note">Awaiting backend connection</p></div>
            </div>
            <div class="content-grid">
                <div class="panel">
                    <div class="panel-header"><h3>Storage Activity</h3><span class="tag">Live-ready</span></div>
                    <div class="activity-list">
                        <div class="activity-item"><div class="activity-icon">↑</div><div><strong>File Upload</strong><p>Selected files will enter the storage queue.</p></div></div>
                        <div class="activity-item"><div class="activity-icon">▦</div><div><strong>Chunking</strong><p>Files will be divided using your selected chunking mode.</p></div></div>
                        <div class="activity-item"><div class="activity-icon">⧉</div><div><strong>Replication</strong><p>Chunks will be copied to Node 1 and Node 2.</p></div></div>
                        <div class="activity-item"><div class="activity-icon">✓</div><div><strong>Integrity & Recovery</strong><p>Checksums and replica health will be displayed.</p></div></div>
                    </div>
                </div>
                <div class="panel">
                    <div class="panel-header"><h3>System Workflow</h3><span class="tag">Overview</span></div>
                    <div class="workflow">
                        <div class="workflow-step"><div class="workflow-number">1</div><div><strong>Choose files</strong><p>Single or multiple file jobs</p></div></div>
                        <div class="workflow-step"><div class="workflow-number">2</div><div><strong>Schedule jobs</strong><p>FCFS, SJF or Round Robin</p></div></div>
                        <div class="workflow-step"><div class="workflow-number">3</div><div><strong>Monitor storage</strong><p>Chunks, replicas and checksums</p></div></div>
                        <div class="workflow-step"><div class="workflow-number">4</div><div><strong>Test OS features</strong><p>Resources and deadlock recovery</p></div></div>
                    </div>
                </div>
            </div>`
    },
    storage: {
        title: "File Storage",
        description: "Select files and configure the storage jobs that will eventually run through the C++ backend.",
        content: `
            <div class="panel">
                <div class="panel-header"><h3>Create Storage Jobs</h3><span class="tag">Preview</span></div>
                <form id="storage-form">
                    <div class="form-grid">
                        <div class="field"><label for="files">Select one or multiple files</label><input id="files" type="file" multiple required></div>
                        <div class="field"><label for="chunk-mode">Chunking mode</label><select id="chunk-mode"><option value="automatic">Automatic</option><option value="manual">Manual number of chunks</option></select></div>
                        <div class="field" id="chunk-count-field" hidden><label for="chunk-count">Chunks per file</label><input id="chunk-count" type="number" min="1" value="2"></div>
                        <div class="field"><label for="algorithm">Scheduling algorithm</label><select id="algorithm"><option>FCFS</option><option>SJF</option><option>Round Robin</option></select></div>
                        <div class="field"><label for="quantum">Round Robin time quantum</label><input id="quantum" type="number" min="1" value="2"></div>
                    </div>
                    <div class="button-row"><button class="primary-button" type="submit">Prepare Jobs</button><button class="secondary-button" type="reset">Clear Selection</button></div>
                </form>
                <div id="storage-notice" class="notice">No files selected. This preview does not upload or store files yet.</div>
            </div>
            <div class="panel"><div class="panel-header"><h3>Storage Job Queue</h3><span class="tag" id="job-count">0 jobs</span></div><div id="job-table" class="empty-state">Your selected jobs will appear here.</div></div>`
    },
    scheduler: {
        title: "OS Simulator",
        description: "Explore process scheduling and compare completion, waiting and turnaround statistics.",
        content: `
            <div class="panel"><div class="panel-header"><h3>CPU Scheduling</h3><span class="tag">Simulation</span></div>
            <div class="form-grid">
                <div class="field"><label for="sched-algorithm">Algorithm</label><select id="sched-algorithm"><option>FCFS</option><option>SJF</option><option>Round Robin</option></select></div>
                <div class="field"><label for="sched-quantum">Time quantum</label><input id="sched-quantum" type="number" min="1" value="2"></div>
            </div>
            <div class="notice">Storage-job scheduling will be connected to the C++ backend after the interface is ready. This page currently shows the design only.</div></div>
            <div class="panel"><div class="panel-header"><h3>Scheduling Results</h3></div><div class="empty-state">No scheduling run yet.<br>Gantt chart and process statistics will appear here.</div></div>`
    },
    deadlock: {
        title: "Deadlock Lab",
        description: "Create processes and resource dependencies to demonstrate deadlock detection and recovery.",
        content: `
            <div class="content-grid">
                <div class="panel"><div class="panel-header"><h3>Process & Resource Setup</h3><span class="tag">Simulation</span></div>
                    <div class="form-grid">
                        <div class="field"><label for="process-id">Process ID</label><input id="process-id" placeholder="P1"></div>
                        <div class="field"><label for="resource-id">Resource ID</label><input id="resource-id" placeholder="R1"></div>
                    </div>
                    <div class="button-row"><button class="primary-button" id="add-process">Add Process</button><button class="secondary-button" id="add-resource">Add Resource</button></div>
                    <div id="deadlock-notice" class="notice">No processes or resources added yet.</div>
                </div>
                <div class="panel"><div class="panel-header"><h3>Resource Allocation Graph</h3></div><div id="resource-graph" class="empty-state">Your process/resource graph will appear here.</div></div>
            </div>
            <div class="panel"><div class="panel-header"><h3>Deadlock Recovery</h3></div><p class="muted">Next, we'll add resource allocation, request tracking, circular-wait detection, and controlled process termination.</p></div>`
    },
    integrity: {
        title: "Integrity & Recovery",
        description: "Inspect chunk checksums, replica status, and recovery operations.",
        content: `
            <div class="panel"><div class="panel-header"><h3>Chunk Integrity</h3><span class="tag">Backend pending</span></div>
                <p class="muted">This page will use your existing checksum verification and replica recovery functions.</p>
                <div class="button-row"><button class="primary-button" id="refresh-integrity">Refresh Integrity</button></div>
                <div id="integrity-notice" class="notice">No live integrity data is connected yet.</div>
            </div>
            <div class="panel"><div class="panel-header"><h3>Recovery Activity</h3></div><div class="empty-state">Recovery history will appear here when connected to the backend.</div></div>`
    },
    database: {
        title: "Database & Logs",
        description: "Review stored file metadata and system operation history.",
        content: `
            <div class="panel"><div class="panel-header"><h3>MySQL Metadata</h3><span class="tag">Backend pending</span></div>
                <p class="muted">Your existing MySQL files and chunks tables will provide the data for this page.</p>
                <div class="table-wrap"><table><thead><tr><th>File ID</th><th>File Name</th><th>Size</th><th>Chunks</th><th>Status</th></tr></thead><tbody><tr><td colspan="5" class="empty-state">No live database records loaded.</td></tr></tbody></table></div>
            </div>
            <div class="panel"><div class="panel-header"><h3>Activity Log</h3></div><div class="empty-state">Storage, scheduling and recovery events will be shown here.</div></div>`
    }
};

const navigation = document.getElementById("navigation");
const pageTitle = document.getElementById("page-title");
const pageContent = document.getElementById("page-content");

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

    connectPageControls(pageName);
}

function connectPageControls(pageName) {
    if (pageName === "storage") {
        const form = document.getElementById("storage-form");
        const mode = document.getElementById("chunk-mode");
        const countField = document.getElementById("chunk-count-field");

        mode.addEventListener("change", () => {
            countField.hidden = mode.value !== "manual";
        });

        form.addEventListener("reset", () => {
            setTimeout(() => {
                countField.hidden = true;
                document.getElementById("job-table").innerHTML = "Your selected jobs will appear here.";
                document.getElementById("job-count").textContent = "0 jobs";
                document.getElementById("storage-notice").textContent = "No files selected. This preview does not upload or store files yet.";
            }, 0);
        });

        form.addEventListener("submit", event => {
            event.preventDefault();

            const files = Array.from(document.getElementById("files").files);
            const algorithm = document.getElementById("algorithm").value;
            const modeValue = mode.value;
            const count = Number(document.getElementById("chunk-count").value);

            if (files.length === 0) {
                return;
            }

            if (modeValue === "manual" && (!Number.isInteger(count) || count < 1)) {
                document.getElementById("storage-notice").textContent = "Enter a valid positive chunk count.";
                return;
            }

            document.getElementById("job-count").textContent = `${files.length} jobs`;
            document.getElementById("job-table").innerHTML = `
                <div class="table-wrap"><table>
                    <thead><tr><th>Process</th><th>File</th><th>Size</th><th>Algorithm</th><th>Status</th></tr></thead>
                    <tbody>${files.map((file, index) => `
                        <tr><td>P${index + 1}</td><td>${escapeHTML(file.name)}</td><td>${formatSize(file.size)}</td><td>${algorithm}</td><td>Prepared</td></tr>
                    `).join("")}</tbody>
                </table></div>`;

            document.getElementById("storage-notice").textContent =
                `${files.length} job(s) prepared using ${algorithm}. This is only a frontend preview; files have not been stored by ChunkVault.`;
        });
    }

    if (pageName === "deadlock") {
        const processes = new Set();
        const resources = new Set();

        function refreshGraph() {
            const processText = [...processes].join(", ") || "None";
            const resourceText = [...resources].join(", ") || "None";

            document.getElementById("deadlock-notice").textContent =
                `Processes: ${processText}. Resources: ${resourceText}. Allocation and deadlock detection are not implemented yet.`;

            document.getElementById("resource-graph").innerHTML = `
                <p><strong>Processes</strong><br>${escapeHTML(processText)}</p>
                <p style="margin-top:16px"><strong>Resources</strong><br>${escapeHTML(resourceText)}</p>`;
        }

        document.getElementById("add-process").addEventListener("click", () => {
            const id = document.getElementById("process-id").value.trim();
            if (!/^P[0-9]+$/i.test(id)) {
                document.getElementById("deadlock-notice").textContent = "Use a process ID such as P1.";
                return;
            }
            processes.add(id.toUpperCase());
            refreshGraph();
        });

        document.getElementById("add-resource").addEventListener("click", () => {
            const id = document.getElementById("resource-id").value.trim();
            if (!/^R[0-9]+$/i.test(id)) {
                document.getElementById("deadlock-notice").textContent = "Use a resource ID such as R1.";
                return;
            }
            resources.add(id.toUpperCase());
            refreshGraph();
        });
    }

    if (pageName === "integrity") {
        document.getElementById("refresh-integrity").addEventListener("click", () => {
            document.getElementById("integrity-notice").textContent =
                "Backend connection is not configured. Your existing C++ verification remains available in the terminal.";
        });
    }
}

function escapeHTML(value) {
    return value.replace(/[&<>"']/g, character => ({
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

navigation.addEventListener("click", event => {
    const button = event.target.closest(".nav-item");
    if (button) renderPage(button.dataset.page);
});

renderPage("dashboard");