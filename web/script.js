"use strict";

const API_BASE_URL = "";
const MAX_MANUAL_CHUNKS = 4294967295;

const PAGE_INFO = {
    dashboard: {
        title: "Dashboard",
        crumb: "OVERVIEW"
    },
    storage: {
        title: "File Storage",
        crumb: "STORAGE"
    },
    "os-simulator": {
        title: "OS Simulator",
        crumb: "SIMULATION"
    },
    integrity: {
        title: "Integrity & Recovery",
        crumb: "INTEGRITY"
    },
    database: {
        title: "Database & Logs",
        crumb: "METADATA"
    }
};

const state = {
    page: "dashboard",
    pageRevision: 0,
    files: [],
    selectedUpload: null,
    uploading: false,
    storageDeleting: false,
    activityDeleting: false,
    storageResult: null,
    integrityChunks: [],
    integrityBusy: false,
    sim: {
        processes: [],
        resources: [],
        requests: [],
        nextPid: 1,
        schedule: null,
        deadlock: null,
        notice: null,
        auditMessage: ""
    }
};

const navigation = document.getElementById("navigation");
const pageContent = document.getElementById("page-content");
const pageTitle = document.getElementById("page-title");
const breadcrumbPage = document.getElementById("breadcrumb-page");

class ApiError extends Error {
    constructor(message, status) {
        super(message);
        this.name = "ApiError";
        this.status = status;
    }
}

function escapeHTML(value) {
    return String(value == null ? "" : value).replace(/[&<>"']/g, function (character) {
        return {
            "&": "&amp;",
            "<": "&lt;",
            ">": "&gt;",
            '"': "&quot;",
            "'": "&#39;"
        }[character];
    });
}

function formatSize(bytes) {
    const amount = Number(bytes);
    if (!Number.isFinite(amount) || amount < 0) return "—";
    if (amount < 1024) return amount + " B";
    if (amount < 1024 * 1024) return (amount / 1024).toFixed(1) + " KB";
    if (amount < 1024 * 1024 * 1024) return (amount / (1024 * 1024)).toFixed(1) + " MB";
    return (amount / (1024 * 1024 * 1024)).toFixed(2) + " GB";
}

function formatNumber(value) {
    const number = Number(value);
    return Number.isFinite(number) ? number.toLocaleString() : "—";
}

async function apiJSON(path, options) {
    let response;
    try {
        response = await fetch(API_BASE_URL + path, Object.assign({
            cache: "no-store",
            headers: { "Accept": "application/json" }
        }, options || {}));
    } catch (error) {
        throw new ApiError("Could not reach the ChunkVault API. Check that the C++ web server is running.", 0);
    }

    let text = "";
    let data = null;
    try {
        text = await response.text();
        data = text ? JSON.parse(text) : null;
    } catch (error) {
        data = null;
    }

    if (!response.ok) {
        const message = data && data.error
            ? String(data.error)
            : (text || ("Request failed with HTTP " + response.status + "."));
        throw new ApiError(message, response.status);
    }

    if (data === null && text) {
        throw new ApiError("The API returned a response that was not valid JSON.", response.status);
    }
    return data;
}

function buttonIcon(name) {
    const paths = {
        arrow: '<path d="M4 10h12m-5-5 5 5-5 5"/>',
        upload: '<path d="M10 13V3m-4 4 4-4 4 4M3 13v3a1.5 1.5 0 0 0 1.5 1.5h11A1.5 1.5 0 0 0 17 16v-3"/>',
        file: '<path d="M5 2.8h6l4 4v10.4H5V2.8Z"/><path d="M11 2.8v4h4M7.5 11h5M7.5 14h5"/>',
        close: '<path d="m5 5 10 10M15 5 5 15"/>',
        clock: '<circle cx="10" cy="10" r="7"/><path d="M10 6v4l2.6 1.6"/>',
        database: '<ellipse cx="10" cy="4.7" rx="6.5" ry="2.2"/><path d="M3.5 4.7v10.6c0 1.2 2.9 2.2 6.5 2.2s6.5-1 6.5-2.2V4.7M3.5 10c0 1.2 2.9 2.2 6.5 2.2s6.5-1 6.5-2.2"/>'
    };
    return '<svg viewBox="0 0 20 20" aria-hidden="true">' + (paths[name] || paths.file) + "</svg>";
}

function statusClass(status) {
    const value = String(status || "").toUpperCase();
    const allowed = ["HEALTHY", "CORRUPTED", "MISSING", "ERROR", "PENDING", "COMPLETED", "READY", "RUNNING", "ACTIVE"];
    return allowed.indexOf(value) >= 0 ? "status-" + value.toLowerCase() : "status-neutral";
}

function statusBadge(status, label) {
    const value = String(label || status || "UNKNOWN");
    return '<span class="status-badge ' + statusClass(status) + '">' + escapeHTML(value) + "</span>";
}

function noticeMarkup(message, type) {
    const extra = type ? " " + type : "";
    return '<div class="notice' + extra + '" role="status">' + escapeHTML(message) + "</div>";
}

function emptyState(title, message, icon, action) {
    const button = action
        ? '<button type="button" class="button-secondary" data-page="' + escapeHTML(action.page) + '">' +
            buttonIcon("arrow") + escapeHTML(action.label) + "</button>"
        : "";
    return '<div class="empty-state"><div class="empty-state-inner">' +
        '<span class="empty-state-icon">' + buttonIcon(icon || "file") + "</span>" +
        "<strong>" + escapeHTML(title) + "</strong><p>" + escapeHTML(message) + "</p>" + button +
        "</div></div>";
}

function fileGlyph() {
    return '<span class="file-glyph" aria-hidden="true">' + buttonIcon("file") + "</span>";
}

function tableHTML(headers, rows, emptyTitle, emptyText) {
    if (!rows.length) return emptyState(emptyTitle || "No records yet", emptyText || "Records will appear here when the backend returns them.", "database");
    return '<div class="table-wrap"><table><thead><tr>' +
        headers.map(function (header) { return "<th>" + escapeHTML(header) + "</th>"; }).join("") +
        "</tr></thead><tbody>" +
        rows.map(function (row) {
            return "<tr>" + row.map(function (cell) { return "<td>" + String(cell == null ? "" : cell) + "</td>"; }).join("") + "</tr>";
        }).join("") + "</tbody></table></div>";
}

function setPageBusy(isBusy) {
    pageContent.setAttribute("aria-busy", isBusy ? "true" : "false");
}

function loadingMarkup(lines) {
    const count = lines || 3;
    return '<div class="loading-skeleton" aria-label="Loading">' +
        Array.from({ length: count }, function (_, index) {
            return '<div class="skeleton' + (index === 0 ? " tall" : "") + '"></div>';
        }).join("") + "</div>";
}

function showToast(message, type) {
    const region = document.getElementById("toast-region");
    const toast = document.createElement("div");
    toast.className = "toast" + (type ? " " + type : "");
    toast.textContent = message;
    region.appendChild(toast);
    window.setTimeout(function () { toast.remove(); }, 4200);
}

function pageError(title, message) {
    return '<div class="panel">' +
        '<div class="panel-header"><h3 class="panel-title">' + escapeHTML(title) + '</h3></div>' +
        '<div class="notice error">' + escapeHTML(message) + '</div>' +
        "</div>";
}

function updateNavigation() {
    navigation.querySelectorAll(".nav-item").forEach(function (button) {
        const active = button.dataset.page === state.page;
        button.classList.toggle("active", active);
        if (active) button.setAttribute("aria-current", "page");
        else button.removeAttribute("aria-current");
    });
}

function renderPage(pageName) {
    const page = PAGE_INFO[pageName] ? pageName : "dashboard";
    state.page = page;
    state.pageRevision += 1;
    const revision = state.pageRevision;
    const info = PAGE_INFO[page];
    pageTitle.textContent = info.title;
    breadcrumbPage.textContent = info.crumb;
    updateNavigation();
    setPageBusy(true);

    if (page === "dashboard") renderDashboardPage(revision);
    if (page === "storage") renderStoragePage(revision);
    if (page === "os-simulator") renderOSPage(revision);
    if (page === "integrity") renderIntegrityPage(revision);
    if (page === "database") renderDatabasePage(revision);
}

function isCurrentPage(page, revision) {
    return state.page === page && state.pageRevision === revision;
}

function dashboardTemplate() {
    return '<div id="dashboard-error"></div>' +
        '<div id="dashboard-stats" class="stats-grid">' + loadingMarkup(4) + "</div>" +
            '<section class="panel"><div class="panel-header"><h3 class="panel-title">Stored files</h3>' +
                '<button type="button" class="button-quiet" data-page="storage">Open storage ' + buttonIcon("arrow") + "</button></div>" +
                '<div id="dashboard-files">' + loadingMarkup(2) + "</div></section>" +
            '<section class="panel"><div class="panel-header"><h3 class="panel-title">Recent activity</h3>' +
                '<button type="button" class="button-quiet" data-page="database">View log ' + buttonIcon("arrow") + "</button></div>" +
                '<div id="dashboard-activity">' + loadingMarkup(2) + "</div></section>";
}

function statCard(label, value) {
    return '<article class="stat-card"><p class="stat-label"><span class="stat-mark"></span>' +
        escapeHTML(label) + '</p><div class="stat-value">' + escapeHTML(formatNumber(value)) +
        "</div></article>";
}

function activityMarkup(activities, limit, selectable) {
    const entries = Array.isArray(activities) ? activities.slice(0, limit || activities.length) : [];
    if (!entries.length) return emptyState("No persisted activity", "Storage, verification, recovery, and simulation events will appear after the API records them.", "clock");
    return '<div class="activity-list">' + entries.map(function (item) {
        const action = String(item.action || "Activity").replace(/_/g, " ");
        return '<div class="activity-item' + (selectable ? " is-selectable" : "") + '">' +
            (selectable ? '<input class="activity-select" type="checkbox" value="' + escapeHTML(item.id) + '" aria-label="Select ' + escapeHTML(action) + ' log">' : "") +
            '<span class="activity-marker">' + buttonIcon("clock") +
            '</span><div class="activity-copy"><strong>' + escapeHTML(action) + '</strong><small>' +
            escapeHTML(item.details || "No details returned.") + '</small></div><time class="activity-time">' +
            escapeHTML(item.time || "Time unavailable") + "</time></div>";
    }).join("") + "</div>";
}

function renderDashboardData(data) {
    const stats = [
        ["Stored files", data.fileCount],
        ["Unique chunks", data.chunkCount],
        ["Physical copies", data.copyCount],
        ["Healthy copies", data.healthyCopies],
        ["Corrupted copies", data.corruptedCopies],
        ["Successful recoveries", data.successfulRecoveries]
    ];
    document.getElementById("dashboard-stats").innerHTML = stats.map(function (item) {
        return statCard(item[0], item[1]);
    }).join("");

    const files = Array.isArray(data.files) ? data.files.slice(0, 6) : [];
    const fileRows = files.map(function (file) {
        return [
            '<div class="file-cell">' + fileGlyph() + '<span><strong class="table-primary">' + escapeHTML(file.name) +
                '</strong><small class="table-secondary mono">' + escapeHTML(file.id) + "</small></span></div>",
            '<span class="mono">' + escapeHTML(formatSize(file.size)) + "</span>",
            '<span class="mono">' + escapeHTML(file.chunkCount) + "</span>"
        ];
    });
    document.getElementById("dashboard-files").innerHTML = files.length
        ? tableHTML(["File", "Size", "Chunks"], fileRows)
        : emptyState("No files stored yet", "Choose a file to create its chunks and replicas through the C++ backend.", "upload",
            { page: "storage", label: "Go to File Storage" });

    document.getElementById("dashboard-activity").innerHTML = activityMarkup(data.activities, 6);
}

async function renderDashboardPage(revision) {
    pageContent.innerHTML = dashboardTemplate();
    try {
        const dashboard = await apiJSON("/api/dashboard");
        if (!isCurrentPage("dashboard", revision)) return;
        renderDashboardData(dashboard);
        setPageBusy(false);
    } catch (error) {
        if (!isCurrentPage("dashboard", revision)) return;
        document.getElementById("dashboard-stats").innerHTML = "";
        document.getElementById("dashboard-files").innerHTML = "";
        document.getElementById("dashboard-activity").innerHTML = "";
        document.getElementById("dashboard-error").innerHTML = pageError("Live data unavailable", error.message);
        setPageBusy(false);
    }
}

function storageTemplate() {
    return '<section class="panel"><div class="panel-header"><h3 class="panel-title">Store one file</h3></div>' +
            '<form id="storage-form"><div class="field"><label for="file-input">File</label>' +
                '<label class="dropzone" id="dropzone" for="file-input"><input id="file-input" type="file">' +
                    '<span class="dropzone-inner"><span class="upload-icon">' + buttonIcon("upload") + '</span>' +
                    '<strong>Drop a file here or <span class="browse-label">browse files</span></strong>' +
                    '<small>One file per storage operation.</small></span></label><div id="selected-file-wrap"></div></div>' +
                '<div class="form-grid"><div class="field"><label for="chunk-mode">Chunking</label><select id="chunk-mode">' +
                    '<option value="automatic">Automatic</option><option value="manual">Manual</option></select></div>' +
                    '<div class="field" id="manual-count-wrap" hidden><label for="chunk-count">Number of chunks</label>' +
                        '<input id="chunk-count" type="number" min="1" max="4294967295" step="1" value="2"></div></div>' +
                '<div id="storage-form-message" class="inline-message" hidden role="status"></div>' +
                '<div class="button-row"><button id="store-button" class="button" type="submit" disabled>' +
                    '<span id="store-button-label">Choose a file</span></button>' +
                    '<button id="clear-file-button" class="button-secondary" type="button">Clear</button>' +
                    '<span class="spacer"></span><span id="upload-progress-label" class="muted small" aria-live="polite"></span></div>' +
                '<div id="upload-progress" class="progress-track" hidden><div id="upload-progress-bar" class="progress-bar"></div></div>' +
                '<div id="storage-result" style="margin-top:12px"></div>' +
            '</form></section>' +
        '<section class="panel"><div class="panel-header"><h3 class="panel-title">Files</h3>' +
            '<span id="stored-file-count" class="count-badge">—</span></div>' +
            '<div id="stored-files-list">' + loadingMarkup(2) + '</div></section>';
}

function renderSelectedUpload() {
    const container = document.getElementById("selected-file-wrap");
    const button = document.getElementById("store-button");
    const label = document.getElementById("store-button-label");
    if (!container || !button || !label) return;
    const file = state.selectedUpload;
    if (!file) {
        container.innerHTML = "";
        button.disabled = true;
        label.textContent = "Choose a file to continue";
        return;
    }
    container.innerHTML = '<div class="selected-file"><div class="selected-file-main">' + fileGlyph() +
        '<div class="selected-file-copy"><strong>' + escapeHTML(file.name) + '</strong><small>' +
        escapeHTML(formatSize(file.size)) + " · " + escapeHTML(file.type || "File type unavailable") +
        '</small></div></div><button id="remove-selected-file" class="remove-file" type="button" aria-label="Remove selected file">' +
        buttonIcon("close") + "</button></div>";
    button.disabled = state.uploading;
    label.textContent = state.uploading ? "Storage in progress…" : "Store file";
    document.getElementById("remove-selected-file").addEventListener("click", clearUpload);
}

function clearUpload() {
    state.selectedUpload = null;
    const input = document.getElementById("file-input");
    if (input) input.value = "";
    const message = document.getElementById("storage-form-message");
    if (message) {
        message.hidden = true;
        message.textContent = "";
    }
    renderSelectedUpload();
}

function setStorageMessage(message, type) {
    const element = document.getElementById("storage-form-message");
    if (!element) return;
    element.className = "inline-message" + (type ? " " + type : "");
    element.textContent = message;
    element.hidden = !message;
}

function renderStorageResult(result) {
    const target = document.getElementById("storage-result");
    if (!target) return;
    if (!result) {
        target.innerHTML = "";
    } else if (!result.ok) {
        target.innerHTML = noticeMarkup("Storage failed: " + result.error, "error");
    } else {
        const file = result.response;
        target.innerHTML = noticeMarkup(file.name + " stored · " + file.chunkCount + " chunk(s) · " + file.id, "success");
    }
}

function xhrStore(file, mode, count, onProgress) {
    return new Promise(function (resolve, reject) {
        const request = new XMLHttpRequest();
        request.open("POST", API_BASE_URL + "/api/files");
        request.timeout = 180000;
        request.setRequestHeader("Accept", "application/json");
        request.setRequestHeader("Content-Type", file.type || "application/octet-stream");
        request.setRequestHeader("X-File-Name", encodeURIComponent(file.name));
        request.setRequestHeader("X-Chunk-Mode", mode);
        if (mode === "manual") request.setRequestHeader("X-Chunk-Count", String(count));
        request.upload.addEventListener("progress", function (event) {
            if (event.lengthComputable && onProgress) onProgress(Math.round(event.loaded * 100 / event.total));
        });
        request.addEventListener("load", function () {
            let data = null;
            try { data = request.responseText ? JSON.parse(request.responseText) : null; } catch (error) { data = null; }
            if (request.status >= 200 && request.status < 300 && data) {
                resolve(data);
                return;
            }
            const message = data && data.error
                ? String(data.error)
                : (request.responseText || "The storage request failed with HTTP " + request.status + ".");
            reject(new ApiError(message, request.status));
        });
        request.addEventListener("error", function () {
            reject(new ApiError("The upload connection failed. The file was not confirmed as stored.", 0));
        });
        request.addEventListener("timeout", function () {
            reject(new ApiError("The upload timed out before the backend confirmed storage.", 0));
        });
        request.send(file);
    });
}

function wireStoragePage() {
    const input = document.getElementById("file-input");
    const mode = document.getElementById("chunk-mode");
    const dropzone = document.getElementById("dropzone");
    const form = document.getElementById("storage-form");

    input.addEventListener("change", function () {
        state.selectedUpload = input.files && input.files[0] ? input.files[0] : null;
        setStorageMessage("", "");
        renderSelectedUpload();
    });
    mode.addEventListener("change", function () {
        document.getElementById("manual-count-wrap").hidden = mode.value !== "manual";
    });
    document.getElementById("clear-file-button").addEventListener("click", clearUpload);
    dropzone.addEventListener("dragover", function (event) {
        event.preventDefault();
        dropzone.classList.add("is-dragging");
    });
    dropzone.addEventListener("dragleave", function () { dropzone.classList.remove("is-dragging"); });
    dropzone.addEventListener("drop", function (event) {
        event.preventDefault();
        dropzone.classList.remove("is-dragging");
        const file = event.dataTransfer && event.dataTransfer.files ? event.dataTransfer.files[0] : null;
        if (!file) return;
        state.selectedUpload = file;
        input.value = "";
        setStorageMessage("", "");
        renderSelectedUpload();
    });
    form.addEventListener("submit", async function (event) {
        event.preventDefault();
        const file = state.selectedUpload;
        const modeValue = mode.value;
        const count = Number(document.getElementById("chunk-count").value);
        if (!file) {
            setStorageMessage("Choose one file before starting storage.", "warning");
            return;
        }
        if (file.size === 0) {
            setStorageMessage("The backend cannot store an empty file. Choose a file with content.", "error");
            return;
        }
        if (modeValue === "manual" && (!Number.isInteger(count) || count < 1 || count > file.size || count > MAX_MANUAL_CHUNKS)) {
            setStorageMessage("Enter a positive whole number of chunks within the file size and the backend limit of 4,294,967,295.", "error");
            return;
        }

        state.uploading = true;
        state.storageResult = null;
        document.getElementById("upload-progress").hidden = false;
        document.getElementById("upload-progress-bar").style.width = "0%";
        document.getElementById("upload-progress-label").textContent = "Sending file to backend…";
        document.getElementById("store-button").disabled = true;
        document.getElementById("store-button-label").textContent = "Storage in progress…";
        setStorageMessage("The backend is storing chunks, replicas, and metadata. Keep this page open until it responds.", "");
        try {
            const response = await xhrStore(file, modeValue, count, function (percent) {
                const bar = document.getElementById("upload-progress-bar");
                const label = document.getElementById("upload-progress-label");
                if (bar) bar.style.width = percent + "%";
                if (label) label.textContent = "Uploading · " + percent + "%";
            });
            state.storageResult = { ok: true, response: response };
            state.selectedUpload = null;
            input.value = "";
            setStorageMessage("", "");
            renderSelectedUpload();
            renderStorageResult(state.storageResult);
            showToast("Backend confirmed storage for " + response.name + ".", "success");
            await loadStoredFiles(state.pageRevision);
        } catch (error) {
            state.storageResult = { ok: false, error: error.message };
            setStorageMessage(error.message, "error");
            renderStorageResult(state.storageResult);
        } finally {
            state.uploading = false;
            const progress = document.getElementById("upload-progress");
            const progressLabel = document.getElementById("upload-progress-label");
            if (progress) progress.hidden = true;
            if (progressLabel) progressLabel.textContent = "";
            const button = document.getElementById("store-button");
            if (button) {
                button.disabled = !state.selectedUpload;
                document.getElementById("store-button-label").textContent = state.selectedUpload ? "Store file" : "Choose a file to continue";
            }
        }
    });

    renderSelectedUpload();
    renderStorageResult(state.storageResult);
}

async function deleteStoredFiles(fileIds, revision) {
    if (!fileIds.length || state.storageDeleting) return;
    const prompt = fileIds.length === 1
        ? "Permanently delete this file, its chunks, replicas, metadata and storage logs?"
        : "Permanently delete " + fileIds.length + " files and all their chunks, replicas, metadata and storage logs?";
    if (!window.confirm(prompt)) return;
    state.storageDeleting = true;
    document.querySelectorAll("#select-all-files, #delete-selected-files").forEach(function (button) {
        button.disabled = true;
    });
    let removed = 0;
    let failure = "";
    for (const fileId of fileIds) {
        try {
            await apiJSON("/api/files/" + encodeURIComponent(fileId), { method: "DELETE" });
            removed += 1;
        } catch (error) {
            failure = error.message;
            break;
        }
    }
    state.storageDeleting = false;
    if (state.storageResult && state.storageResult.ok && fileIds.indexOf(state.storageResult.response.id) >= 0) {
        state.storageResult = null;
        renderStorageResult(null);
    }
    await loadStoredFiles(revision);
    if (failure) showToast(removed + " file(s) deleted. " + failure, "error");
    else showToast(removed + " file(s) and their stored data deleted.", "success");
}

async function loadStoredFiles(revision) {
    const target = document.getElementById("stored-files-list");
    if (!target) return;
    target.innerHTML = loadingMarkup(2);
    try {
        const files = await apiJSON("/api/files");
        if (!isCurrentPage("storage", revision)) return;
        state.files = Array.isArray(files) ? files : [];
        document.getElementById("stored-file-count").textContent = String(state.files.length);
        const groups = state.files.map(function (file) {
            return '<article class="stored-file"><div class="stored-file-head"><label class="stored-file-check">' +
                '<input class="storage-delete-checkbox" type="checkbox" value="' + escapeHTML(file.id) + '">' +
                '<span class="file-cell">' + fileGlyph() + '<span><strong class="table-primary">' + escapeHTML(file.name) +
                    '</strong><small class="table-secondary mono">' + escapeHTML(file.id) + '</small></span></span></label>' +
                '<span class="stored-file-meta">' + escapeHTML(formatSize(file.size)) + ' · ' + escapeHTML(file.chunkCount) + ' chunks</span>' +
                '</div></article>';
        }).join("");
        target.innerHTML = state.files.length
            ? '<div class="stored-file-actions"><button id="select-all-files" class="button-secondary" type="button">Select all</button>' +
                '<button id="delete-selected-files" class="button-danger" type="button" disabled>Delete selected</button></div>' + groups
            : emptyState("No file is present", "Upload a file to get started.", "database");
        const selectAll = document.getElementById("select-all-files");
        const bulkButton = document.getElementById("delete-selected-files");
        const updateBulkButton = function () {
            if (!selectAll || !bulkButton) return;
            const selected = document.querySelectorAll(".storage-delete-checkbox:checked").length;
            selectAll.disabled = state.files.length === 0 || state.storageDeleting;
            bulkButton.disabled = selected === 0 || state.storageDeleting;
            selectAll.textContent = selected === state.files.length && selected > 0 ? "Deselect all" : "Select all";
            bulkButton.textContent = selected ? "Delete selected (" + selected + ")" : "Delete selected";
        };
        if (selectAll) selectAll.addEventListener("click", function () {
            const checkboxes = Array.from(document.querySelectorAll(".storage-delete-checkbox"));
            const select = checkboxes.some(function (checkbox) { return !checkbox.checked; });
            checkboxes.forEach(function (checkbox) {
                checkbox.checked = select;
            });
            updateBulkButton();
        });
        document.querySelectorAll(".storage-delete-checkbox").forEach(function (checkbox) {
            checkbox.addEventListener("change", updateBulkButton);
        });
        if (bulkButton) bulkButton.addEventListener("click", function () {
            const ids = Array.from(document.querySelectorAll(".storage-delete-checkbox:checked"))
                .map(function (checkbox) { return checkbox.value; });
            deleteStoredFiles(ids, revision);
        });
        updateBulkButton();
    } catch (error) {
        if (!isCurrentPage("storage", revision)) return;
        target.innerHTML = '<div class="notice error">' + escapeHTML(error.message) + "</div>";
        document.getElementById("stored-file-count").textContent = "—";
    } finally {
        if (isCurrentPage("storage", revision)) setPageBusy(false);
    }
}

function renderStoragePage(revision) {
    pageContent.innerHTML = storageTemplate();
    wireStoragePage();
    loadStoredFiles(revision);
}

function osTemplate() {
    return '<div class="os-workflow">' +
            '<section class="panel"><div class="panel-header"><h3 class="panel-title">1. Create processes</h3>' +
                '<div class="panel-header-actions"><span id="process-count" class="count-badge">0</span>' +
                    '<button type="button" id="reset-simulation" class="button-quiet">Reset</button></div></div>' +
                '<div id="os-file-list">' + loadingMarkup(2) + "</div>" +
                '<div class="button-row"><button id="create-processes" class="button-secondary" type="button">Create processes</button></div>' +
                '<div id="os-notice" style="margin-top:13px"></div><div id="os-audit-notice"></div>' +
                '<div id="process-table" style="margin-top:14px"></div></section>' +
            '<section class="panel"><div class="panel-header"><h3 class="panel-title">2. Run scheduling</h3></div>' +
                '<div class="form-grid"><div class="field"><label for="schedule-algorithm">Algorithm</label>' +
                    '<select id="schedule-algorithm"><option value="FCFS">FCFS</option><option value="SJF">SJF</option><option value="RR">Round Robin</option></select></div>' +
                    '<div class="field" id="quantum-wrap" hidden><label for="time-quantum">Time quantum</label>' +
                    '<input id="time-quantum" type="number" min="1" max="2147483647" step="1" value="2" inputmode="numeric"></div></div>' +
                '<div class="button-row"><button id="run-schedule" class="button" type="button">Run scheduler</button></div>' +
                '<div id="gantt-chart" style="margin-top:14px"></div><div id="schedule-stats"></div></section>' +
            '<section class="panel"><div class="panel-header"><h3 class="panel-title">3. Resources and deadlocks</h3>' +
                '<span id="resource-count" class="count-badge">0</span></div>' +
                '<div class="button-row" style="margin-top:0"><button id="add-resource" class="button-secondary" type="button">Add resource</button></div>' +
                '<div class="form-grid" style="margin-top:14px"><div class="field"><label for="resource-process">Process</label><select id="resource-process"></select></div>' +
                    '<div class="field"><label for="resource-choice">Resource</label><select id="resource-choice"></select></div></div>' +
                '<div class="button-row"><button id="allocate-resource" class="button-secondary" type="button">Allocate</button>' +
                    '<button id="request-resource" class="button-secondary" type="button">Request</button>' +
                    '<button id="detect-deadlock" class="button" type="button">Detect deadlock</button></div>' +
            '<div id="dependency-graph" style="margin-top:13px"></div>' +
                '<div id="deadlock-result" style="margin-top:13px"></div>' +
                '<div class="form-grid" style="margin-top:14px"><div class="field"><label for="terminate-process">Process to terminate</label><select id="terminate-process"></select></div>' +
                    '<div class="field field-action"><button id="terminate-process-button" class="button-danger" type="button">Terminate and release resources</button></div></div>' +
            '</section>' +
        '</div>';
}

function processFileRows(files) {
    const existing = new Set(state.sim.processes.map(function (process) { return process.fileId; }));
    if (!files.length) return emptyState("No stored files available", "Store a file first. Browser file selection alone cannot create an OS process.", "database",
        { page: "storage", label: "Open File Storage" });
    return '<div class="file-picker-list">' + files.map(function (file) {
        const alreadyUsed = existing.has(file.id);
        const input = '<input class="process-file-checkbox" type="checkbox" value="' + escapeHTML(file.id) + '"' +
            (alreadyUsed ? " disabled" : "") + ">";
        const usedLabel = alreadyUsed ? " · process created" : "";
        return '<label class="file-picker-option">' + input + '<span class="file-picker-option-copy"><strong>' +
            escapeHTML(file.name) + '</strong><small>' + escapeHTML(file.id) + usedLabel +
            '</small></span><small class="file-size">' + escapeHTML(file.chunkCount) + " chunks</small></label>";
    }).join("") + "</div>";
}

function renderProcessFileList() {
    const target = document.getElementById("os-file-list");
    if (target) target.innerHTML = processFileRows(state.files);
    const createButton = document.getElementById("create-processes");
    if (createButton) createButton.disabled = !state.files.some(function (file) {
        return !state.sim.processes.some(function (process) { return process.fileId === file.id; });
    });
}
function simNotice(message, type) {
    state.sim.notice = message ? { message: message, type: type || "" } : null;
    const target = document.getElementById("os-notice");
    if (target) target.innerHTML = state.sim.notice ? noticeMarkup(message, type) : "";
}

function optionMarkup(value, label) {
    return '<option value="' + escapeHTML(value) + '">' + escapeHTML(label) + "</option>";
}

function renderSimulationControls() {
    const processSelect = document.getElementById("resource-process");
    const resourceSelect = document.getElementById("resource-choice");
    const terminateSelect = document.getElementById("terminate-process");
    const previousProcess = processSelect.value;
    const previousResource = resourceSelect.value;
    const previousTerminate = terminateSelect.value;
    const processes = state.sim.processes;

    processSelect.innerHTML = processes.length
        ? processes.map(function (process) { return optionMarkup(process.pid, "P" + process.pid + " · " + process.name); }).join("")
        : '<option value="">No processes</option>';
    resourceSelect.innerHTML = state.sim.resources.length
        ? state.sim.resources.map(function (resource) {
            const label = resource.ownerPid ? "R" + resource.id + " · held by P" + resource.ownerPid : "R" + resource.id + " · available";
            return optionMarkup(resource.id, label);
        }).join("")
        : '<option value="">No resources</option>';
    terminateSelect.innerHTML = processes.length
        ? processes.map(function (process) { return optionMarkup(process.pid, "P" + process.pid + " · " + process.name); }).join("")
        : '<option value="">No processes</option>';
    if (Array.from(processSelect.options).some(function (option) { return option.value === previousProcess; })) processSelect.value = previousProcess;
    if (Array.from(resourceSelect.options).some(function (option) { return option.value === previousResource; })) resourceSelect.value = previousResource;
    if (Array.from(terminateSelect.options).some(function (option) { return option.value === previousTerminate; })) terminateSelect.value = previousTerminate;
    const hasProcesses = processes.length > 0;
    const hasResources = state.sim.resources.length > 0;
    document.getElementById("run-schedule").disabled = !hasProcesses;
    document.getElementById("add-resource").disabled = !hasProcesses;
    document.getElementById("allocate-resource").disabled = !hasProcesses || !hasResources;
    document.getElementById("request-resource").disabled = !hasProcesses || !hasResources;
    document.getElementById("detect-deadlock").disabled = !hasProcesses;
    document.getElementById("terminate-process-button").disabled = !hasProcesses;
}

function renderProcessTable() {
    const target = document.getElementById("process-table");
    document.getElementById("process-count").textContent = String(state.sim.processes.length);
    if (!state.sim.processes.length) {
        target.innerHTML = emptyState("No processes created", "Select actual stored files above and create one process for each file.", "file");
        return;
    }
    const rows = state.sim.processes.map(function (process) {
        return [
            '<strong class="table-primary mono">P' + escapeHTML(process.pid) + "</strong>",
            '<span class="table-primary">' + escapeHTML(process.name) + '</span><small class="table-secondary mono">' + escapeHTML(process.fileId) + "</small>",
            '<span class="mono">' + process.burstTime + "</span>",
            statusBadge(process.status, process.status)
        ];
    });
    target.innerHTML = tableHTML(["PID", "File", "Burst (chunks)", "Status"], rows);
}

function renderSchedule() {
    const chart = document.getElementById("gantt-chart");
    const stats = document.getElementById("schedule-stats");
    const schedule = state.sim.schedule;
    if (!schedule) {
        chart.innerHTML = emptyState("No schedule yet", "The process table remains available while you choose an algorithm.", "clock");
        stats.innerHTML = "";
        return;
    }
    const blocks = schedule.gantt.map(function (item) {
        const width = Math.max(1, item.end - item.start);
        const label = item.idle ? "Idle" : "P" + item.pid;
        return '<div class="gantt-block' + (item.idle ? " gantt-idle" : "") + '" style="flex:' + width + ' 1 0"><strong>' + label +
            '</strong><small>' + item.start + " → " + item.end + "</small></div>";
    }).join("");
    chart.innerHTML = '<div class="gantt-track">' + blocks + '</div><div class="gantt-axis"><span>0</span><span>End · ' +
        schedule.totalTime + "</span></div>";
    stats.innerHTML = '<div class="process-result-grid">' +
        '<div class="mini-metric"><span>Average waiting</span><strong>' + schedule.averageWaiting.toFixed(2) + "</strong></div>" +
        '<div class="mini-metric"><span>Average turnaround</span><strong>' + schedule.averageTurnaround.toFixed(2) + "</strong></div>" +
        '<div class="mini-metric"><span>CPU utilization</span><strong>' + Number(schedule.cpuUtilization).toFixed(1) + "%</strong></div>" +
        '<div class="mini-metric"><span>Throughput</span><strong>' + Number(schedule.throughput).toFixed(3) + " process/unit</strong></div>" +
        "</div>" +
        tableHTML(["PID", "File", "Completion", "Waiting", "Turnaround"], schedule.results.map(function (item) {
            return [
                '<span class="mono">P' + item.pid + "</span>",
                escapeHTML(item.name),
                '<span class="mono">' + item.completionTime + "</span>",
                '<span class="mono">' + item.waitingTime + "</span>",
                '<span class="mono">' + item.turnaroundTime + "</span>"
            ];
        }));
}

function renderResourceState() {
    document.getElementById("resource-count").textContent = String(state.sim.resources.length);
    const graph = document.getElementById("dependency-graph");
    const processes = state.sim.processes;
    const resources = state.sim.resources;
    if (!processes.length) {
        graph.innerHTML = emptyState("No processes in the graph", "Select stored files and create processes first.", "database");
        renderSimulationControls();
        return;
    }
    const height = Math.max(210, Math.max(processes.length, resources.length) * 76 + 54);
    const processPoints = new Map(processes.map(function (process, index) {
        return [process.pid, { x: 135, y: 35 + height / (processes.length + 1) * (index + 1) }];
    }));
    const resourcePoints = new Map(resources.map(function (resource, index) {
        return [resource.id, { x: 565, y: 35 + height / (resources.length + 1) * (index + 1) }];
    }));
    const cycle = state.sim.deadlock && state.sim.deadlock.cycle ? state.sim.deadlock.cycle : [];
    const edges = [];
    resources.forEach(function (resource) {
        if (!resource.ownerPid) return;
        const from = resourcePoints.get(resource.id);
        const to = processPoints.get(resource.ownerPid);
        if (from && to) edges.push('<line class="graph-edge allocation-edge" x1="' + from.x + '" y1="' + from.y +
            '" x2="' + to.x + '" y2="' + to.y + '" marker-end="url(#graph-arrow)" />');
    });
    state.sim.requests.forEach(function (request) {
        const from = processPoints.get(request.pid);
        const to = resourcePoints.get(request.resourceId);
        if (!from || !to) return;
        const deadlocked = cycle.indexOf(request.pid) >= 0;
        edges.push('<line class="graph-edge request-edge' + (deadlocked ? ' deadlocked-edge' : '') + '" x1="' +
            from.x + '" y1="' + from.y + '" x2="' + to.x + '" y2="' + to.y +
            '" marker-end="url(#graph-arrow)" />');
    });
    const processNodes = processes.map(function (process) {
        const point = processPoints.get(process.pid);
        return '<g class="graph-process"><rect x="' + (point.x - 72) + '" y="' + (point.y - 25) +
            '" width="144" height="50" rx="8"/><text x="' + point.x + '" y="' + (point.y - 3) +
            '" text-anchor="middle">P' + process.pid + '</text><text class="graph-node-sub" x="' + point.x +
            '" y="' + (point.y + 14) + '" text-anchor="middle">' + escapeHTML(process.name) + '</text></g>';
    }).join("");
    const resourceNodes = resources.map(function (resource) {
        const point = resourcePoints.get(resource.id);
        const owner = resource.ownerPid ? "held by P" + resource.ownerPid : "available";
        return '<g class="graph-resource' + (resource.ownerPid ? ' resource-allocated' : '') + '"><rect x="' +
            (point.x - 65) + '" y="' + (point.y - 25) + '" width="130" height="50" rx="8"/><text x="' +
            point.x + '" y="' + (point.y - 3) + '" text-anchor="middle">R' + resource.id +
            '</text><text class="graph-node-sub" x="' + point.x + '" y="' + (point.y + 14) +
            '" text-anchor="middle">' + owner + '</text></g>';
    }).join("");
    graph.innerHTML = '<div class="graph-legend"><span><i class="process-dot"></i>Process</span><span><i class="resource-dot"></i>Resource</span>' +
        '<span>R → P allocated</span><span>P → R waiting</span></div><svg class="resource-graph" viewBox="0 0 700 ' + height +
        '" role="img" aria-label="Process and resource allocation graph"><defs><marker id="graph-arrow" markerWidth="8" markerHeight="8" refX="7" refY="3" orient="auto">' +
        '<path d="M0,0 L0,6 L7,3 z"/></marker></defs><text class="graph-group-title" x="135" y="20" text-anchor="middle">PROCESSES</text>' +
        '<text class="graph-group-title" x="565" y="20" text-anchor="middle">RESOURCES</text>' + edges.join("") + processNodes + resourceNodes + '</svg>';
    renderSimulationControls();
}

function renderDeadlockResult() {
    const target = document.getElementById("deadlock-result");
    const result = state.sim.deadlock;
    if (!result) {
        target.innerHTML = "";
        return;
    }
    if (!result.cycle) {
        target.innerHTML = noticeMarkup("No deadlock cycle was found in the current wait-for graph.", "success");
        return;
    }
    target.innerHTML = noticeMarkup("Deadlock cycle: " + result.cycle.map(function (pid) { return "P" + pid; }).join(" → ") +
        ". Choose a process below to release its resources.", "error");
}

function renderOSState() {
    if (!document.getElementById("process-table")) return;
    renderProcessTable();
    renderSchedule();
    renderResourceState();
    renderDeadlockResult();
    const audit = document.getElementById("os-audit-notice");
    audit.innerHTML = state.sim.auditMessage ? noticeMarkup(state.sim.auditMessage, "warning") : "";
    simNotice(state.sim.notice ? state.sim.notice.message : "", state.sim.notice ? state.sim.notice.type : "");
}

async function scheduleWithCpp(algorithm, quantum) {
    const body = new URLSearchParams({
        algorithm: algorithm,
        quantum: String(quantum),
        count: String(state.sim.processes.length)
    });
    state.sim.processes.forEach(function (process, index) {
        body.set("pid" + index, String(process.pid));
        body.set("arrival" + index, String(process.arrivalTime));
        body.set("burst" + index, String(process.burstTime));
        body.set("name" + index, process.name);
    });
    return apiJSON("/api/os/schedule", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: body.toString()
    });
}

function appendResourceGraph(body) {
    body.set("processCount", String(state.sim.processes.length));
    body.set("resourceCount", String(state.sim.resources.length));
    body.set("requestCount", String(state.sim.requests.length));
    state.sim.processes.forEach(function (process, index) {
        body.set("processPid" + index, String(process.pid));
    });
    state.sim.resources.forEach(function (resource, index) {
        body.set("resourceId" + index, String(resource.id));
        body.set("resourceOwner" + index, String(resource.ownerPid || 0));
    });
    state.sim.requests.forEach(function (request, index) {
        body.set("requestPid" + index, String(request.pid));
        body.set("requestResource" + index, String(request.resourceId));
    });
    return body;
}

async function resourceActionWithCpp(action, pid, resourceId) {
    const body = appendResourceGraph(new URLSearchParams({
        action: action,
        pid: String(pid || 0),
        resourceId: String(resourceId || 0)
    }));
    const result = await apiJSON("/api/os/resources", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: body.toString()
    });
    state.sim.resources = Array.isArray(result.resources) ? result.resources.map(function (resource) {
        return { id: Number(resource.id), ownerPid: Number(resource.ownerPid) || null };
    }) : [];
    state.sim.requests = Array.isArray(result.requests) ? result.requests : [];
    return result;
}

async function detectDeadlockWithCpp() {
    const body = appendResourceGraph(new URLSearchParams());
    const result = await apiJSON("/api/os/deadlock", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: body.toString()
    });
    return Array.isArray(result.cycle) && result.cycle.length
        ? { cycle: result.cycle }
        : { cycle: null };
}

async function persistSimulationActivity(action, details) {
    try {
        const body = new URLSearchParams({ action: action, details: details });
        await apiJSON("/api/activity", {
            method: "POST",
            headers: { "Content-Type": "application/x-www-form-urlencoded" },
            body: body.toString()
        });
        state.sim.auditMessage = "";
        return true;
    } catch (error) {
        state.sim.auditMessage = "Could not save the activity log: " + error.message;
        const target = document.getElementById("os-audit-notice");
        if (target) target.innerHTML = noticeMarkup(state.sim.auditMessage, "warning");
        return false;
    }
}

function wireOSPage(revision) {
    const algorithm = document.getElementById("schedule-algorithm");
    algorithm.addEventListener("change", function () {
        document.getElementById("quantum-wrap").hidden = algorithm.value !== "RR";
    });
    document.getElementById("create-processes").addEventListener("click", async function () {
        const selected = Array.from(document.querySelectorAll(".process-file-checkbox:checked"));
        if (!selected.length) {
            simNotice("Select one or more stored files.", "warning");
            return;
        }
        const additions = selected.map(function (checkbox) {
            const file = state.files.find(function (item) { return item.id === checkbox.value; });
            const burst = file ? Number(file.chunkCount) : NaN;
            return file && Number.isInteger(burst) && burst > 0
                ? { file: file, burst: burst }
                : null;
        });
        if (additions.some(function (entry) { return !entry; })) {
            simNotice("A selected file has an invalid chunk count.", "error");
            return;
        }
        additions.forEach(function (entry) {
            state.sim.processes.push({
                pid: state.sim.nextPid++,
                fileId: entry.file.id,
                name: entry.file.name,
                arrivalTime: 0,
                burstTime: entry.burst,
                remainingTime: entry.burst,
                status: "READY",
                completionTime: null,
                waitingTime: null,
                turnaroundTime: null
            });
        });
        state.sim.schedule = null;
        state.sim.deadlock = null;
        simNotice("Created " + additions.length + " process(es).", "success");
        renderProcessFileList();
        renderOSState();
        await persistSimulationActivity("SIMULATION_PROCESS_CREATE", "Created " + additions.length + " process(es) from stored files.");
        renderOSState();
    });
    document.getElementById("run-schedule").addEventListener("click", async function () {
        const value = algorithm.value;
        const quantum = Number(document.getElementById("time-quantum").value);
        if (value === "RR" && (!Number.isInteger(quantum) || quantum < 1)) {
            simNotice("Round Robin requires a positive whole-number time quantum.", "error");
            return;
        }
        try {
            const schedule = await scheduleWithCpp(value, quantum);
            state.sim.schedule = schedule;
            const byPid = new Map(schedule.results.map(function (process) { return [process.pid, process]; }));
            state.sim.processes.forEach(function (process) {
                const result = byPid.get(process.pid);
                if (!result) return;
                process.remainingTime = 0;
                process.status = "COMPLETED";
                process.completionTime = result.completionTime;
                process.waitingTime = result.waitingTime;
                process.turnaroundTime = result.turnaroundTime;
            });
            state.sim.deadlock = null;
            const algorithmName = schedule.algorithm === "RR" ? "Round Robin" : schedule.algorithm;
            simNotice(algorithmName + " schedule returned by the C++ backend. These values are simulation units.", "success");
            renderOSState();
            await persistSimulationActivity("SIMULATION_SCHEDULE", "Ran " + algorithmName + " in C++ for " + schedule.results.length + " process(es).");
            renderOSState();
        } catch (error) {
            simNotice(error.message, "warning");
        }
    });

    document.getElementById("add-resource").addEventListener("click", async function () {
        try {
            const result = await resourceActionWithCpp("add", 0, 0);
            state.sim.deadlock = null;
            renderOSState();
            simNotice(result.message, "success");
            await persistSimulationActivity("SIMULATION_RESOURCE_ADD", result.message);
            renderOSState();
        } catch (error) {
            simNotice(error.message, "error");
        }
    });

    document.getElementById("allocate-resource").addEventListener("click", async function () {
        const pid = Number(document.getElementById("resource-process").value);
        const resourceId = Number(document.getElementById("resource-choice").value);
        const process = state.sim.processes.find(function (item) { return item.pid === pid; });
        const resource = state.sim.resources.find(function (item) { return item.id === resourceId; });
        if (!process || !resource) {
            simNotice("Create a process and add a resource before allocating.", "warning");
            return;
        }
        try {
            const result = await resourceActionWithCpp("allocate", pid, resourceId);
            state.sim.deadlock = null;
            renderOSState();
            simNotice(result.message, "success");
            await persistSimulationActivity("SIMULATION_RESOURCE_ALLOCATE", result.message);
            renderOSState();
        } catch (error) {
            simNotice(error.message, "warning");
        }
    });

    document.getElementById("request-resource").addEventListener("click", async function () {
        const pid = Number(document.getElementById("resource-process").value);
        const resourceId = Number(document.getElementById("resource-choice").value);
        const process = state.sim.processes.find(function (item) { return item.pid === pid; });
        const resource = state.sim.resources.find(function (item) { return item.id === resourceId; });
        if (!process || !resource) {
            simNotice("Create a process and add a resource before requesting.", "warning");
            return;
        }
        try {
            const result = await resourceActionWithCpp("request", pid, resourceId);
            state.sim.deadlock = null;
            renderOSState();
            simNotice(result.message, "");
            await persistSimulationActivity("SIMULATION_RESOURCE_REQUEST", result.message);
            renderOSState();
        } catch (error) {
            simNotice(error.message, "warning");
        }
    });

    document.getElementById("detect-deadlock").addEventListener("click", async function () {
        try {
            state.sim.deadlock = await detectDeadlockWithCpp();
        } catch (error) {
            simNotice("The C++ backend could not check the resource graph: " + error.message, "error");
            return;
        }
        renderDeadlockResult();
        renderSimulationControls();
        if (state.sim.deadlock.cycle) {
            simNotice("The C++ backend found a wait-for cycle. Choose one of its processes to terminate.", "error");
            await persistSimulationActivity("SIMULATION_DEADLOCK", "Detected wait-for cycle " + state.sim.deadlock.cycle.join(" -> ") + ".");
        } else {
            simNotice("No wait-for cycle was found.", "success");
            await persistSimulationActivity("SIMULATION_DEADLOCK", "Ran deadlock detection; no wait-for cycle was found.");
        }
        renderOSState();
    });

    document.getElementById("terminate-process-button").addEventListener("click", async function () {
        const pid = Number(document.getElementById("terminate-process").value);
        const terminated = state.sim.processes.find(function (process) { return process.pid === pid; });
        if (!terminated) {
            simNotice("Choose a process to terminate.", "warning");
            return;
        }
        let result;
        try {
            result = await resourceActionWithCpp("terminate", pid, 0);
        } catch (error) {
            simNotice(error.message, "error");
            return;
        }
        state.sim.processes = state.sim.processes.filter(function (process) { return process.pid !== pid; });
        state.sim.schedule = null;
        state.sim.deadlock = null;
        renderProcessFileList();
        renderOSState();
        const released = result.releasedResourceIds || [];
        const releasedText = released.length ? released.map(function (id) { return "R" + id; }).join(", ") : "none";
        simNotice(result.message + " Released resources: " + releasedText + ". Run detection again.", "success");
        await persistSimulationActivity("SIMULATION_PROCESS_TERMINATE",
            "Terminated P" + pid + (terminated ? " (" + terminated.name + ")" : "") + "; released " +
            (released.length ? releasedText : "no resources") + ".");
        renderOSState();
    });

    document.getElementById("reset-simulation").addEventListener("click", async function () {
        try {
            await resourceActionWithCpp("reset", 0, 0);
        } catch (error) {
            simNotice("The C++ backend could not reset the resource state: " + error.message, "error");
            return;
        }
        state.sim.processes = [];
        state.sim.nextPid = 1;
        state.sim.schedule = null;
        state.sim.deadlock = null;
        renderProcessFileList();
        renderOSState();
        simNotice("C++ OS simulation reset. Stored files and MySQL metadata were not changed.", "success");
        await persistSimulationActivity("SIMULATION_RESET", "Reset the OS simulation state; stored files were retained.");
        renderOSState();
    });
}

async function loadOSFiles(revision) {
    try {
        const files = await apiJSON("/api/files");
        if (!isCurrentPage("os-simulator", revision)) return;
        state.files = Array.isArray(files) ? files : [];
        renderProcessFileList();
        renderOSState();
    } catch (error) {
        if (!isCurrentPage("os-simulator", revision)) return;
        document.getElementById("os-file-list").innerHTML = '<div class="notice error">' +
            escapeHTML(error.message) + "</div>";
    } finally {
        if (isCurrentPage("os-simulator", revision)) setPageBusy(false);
    }
}

function renderOSPage(revision) {
    pageContent.innerHTML = osTemplate();
    wireOSPage(revision);
    renderOSState();
    loadOSFiles(revision);
}

function integrityTemplate() {
    return '<div id="integrity-message" style="margin-bottom:12px"></div>' +
        '<section class="panel"><div class="panel-header"><h3 class="panel-title">Files, chunks and replicas</h3>' +
            '<span id="integrity-chunk-count" class="count-badge">—</span></div>' +
            '<div id="integrity-summary"></div><div id="integrity-report">' + loadingMarkup(2) + '</div></section>' +
        '<div id="chunk-modal" class="chunk-modal" hidden><div class="chunk-modal-backdrop"></div>' +
            '<section class="chunk-modal-card" role="dialog" aria-modal="true" aria-labelledby="chunk-modal-title">' +
                '<header><div><h3 id="chunk-modal-title">File contents</h3><p id="chunk-modal-status"></p></div>' +
                    '<button id="close-chunk-modal" class="modal-close" type="button" aria-label="Close">×</button></header>' +
                '<pre id="chunk-modal-content" class="content-preview"></pre>' +
            '</section></div>';
}

function setIntegrityMessage(message, type) {
    const target = document.getElementById("integrity-message");
    if (target) target.innerHTML = message ? noticeMarkup(message, type) : "";
}

function renderIntegritySummary(chunks) {
    const allStatuses = (chunks || []).flatMap(function (chunk) {
        return [chunk.main, chunk.node1, chunk.node2].map(function (copy) { return copy && copy.status; });
    });
    const count = function (status) { return allStatuses.filter(function (item) { return item === status; }).length; };
    document.getElementById("integrity-chunk-count").textContent = String((chunks || []).length) + " chunks";
    document.getElementById("integrity-summary").innerHTML =
        '<div class="integrity-summary">' +
            '<div class="mini-metric"><span>Healthy copies</span><strong>' + count("HEALTHY") + "</strong></div>" +
            '<div class="mini-metric"><span>Corrupted copies</span><strong>' + count("CORRUPTED") + "</strong></div>" +
            '<div class="mini-metric"><span>Missing copies</span><strong>' + count("MISSING") + "</strong></div>" +
            '<div class="mini-metric"><span>Errors</span><strong>' + count("ERROR") + "</strong></div>" +
        "</div>";
}

function refreshIntegrityControls() {
    document.querySelectorAll("[data-view-file], [data-verify-file], [data-recover-file]").forEach(function (button) {
        button.disabled = state.integrityBusy;
    });
}

function renderIntegrityReport() {
    const target = document.getElementById("integrity-report");
    if (!target) return;
    renderIntegritySummary(state.integrityChunks);
    if (!state.files.length) {
        target.innerHTML = emptyState("No files stored", "Store a file to see its chunks and replicas here.", "database");
        return;
    }
    target.innerHTML = '<div class="integrity-file-list">' + state.files.map(function (file) {
        const chunks = state.integrityChunks.filter(function (chunk) { return chunk.fileId === file.id; });
        const chunkRows = chunks.map(function (chunk) {
            const copyCard = function (copy, label) {
                const info = chunk[copy] || {};
                return '<div class="integrity-copy"><div class="integrity-copy-head"><strong>' + label + '</strong>' +
                    statusBadge(info.status || "ERROR") + '</div><small>' + escapeHTML(safeLocation(info.path)) + '</small></div>';
            };
            return '<article class="integrity-chunk"><div class="integrity-chunk-heading"><strong>Chunk ' +
                escapeHTML(chunk.number) + '</strong><small>' + escapeHTML(chunk.id) + ' · ' +
                escapeHTML(formatSize(chunk.size)) + '</small></div><div class="integrity-copies">' +
                copyCard("main", "Main") + copyCard("node1", "Node 1") + copyCard("node2", "Node 2") +
                '</div></article>';
        }).join("");
        return '<article class="integrity-file"><div class="integrity-file-heading"><div><strong>' + escapeHTML(file.name) +
            '</strong><small>' + escapeHTML(file.id) + ' · ' + escapeHTML(formatSize(file.size)) +
            ' · ' + chunks.length + ' chunk(s)</small></div><div class="integrity-file-actions">' +
            '<button class="button-quiet" type="button" data-view-file="' + escapeHTML(file.id) + '">View</button>' +
            '<button class="button-quiet" type="button" data-verify-file="' + escapeHTML(file.id) + '">Verify</button>' +
            '<button class="button-secondary" type="button" data-recover-file="' + escapeHTML(file.id) + '">Replicate</button>' +
            '</div></div><div class="integrity-file-chunks">' + (chunkRows || '<p class="field-hint">No chunk records for this file.</p>') +
            '</div></article>';
    }).join("") + '</div>';
    refreshIntegrityControls();
}

function replaceIntegrityFileChunks(fileId, chunks) {
    state.integrityChunks = state.integrityChunks.filter(function (chunk) { return chunk.fileId !== fileId; })
        .concat(chunks || []).sort(function (first, second) {
            return first.fileId.localeCompare(second.fileId) || first.number - second.number;
        });
    renderIntegrityReport();
}

function closeChunkModal() {
    const modal = document.getElementById("chunk-modal");
    if (modal) modal.hidden = true;
}

function chunkDisplay(hex) {
    const bytes = (hex.match(/.{2}/g) || []).map(function (pair) { return parseInt(pair, 16); });
    const decoded = new TextDecoder("utf-8").decode(new Uint8Array(bytes));
    const controls = (decoded.match(/[\u0000-\u0008\u000B\u000C\u000E-\u001F\uFFFD]/g) || []).length;
    if (bytes.length && controls / bytes.length > 0.02) {
        const pairs = hex.match(/.{1,2}/g) || [];
        const lines = [];
        for (let index = 0; index < pairs.length; index += 16) lines.push(pairs.slice(index, index + 16).join(" "));
        return { text: lines.join("\n"), format: "Hex" };
    }
    return { text: decoded, format: "Text" };
}

async function showFileContents(file, revision) {
    const modal = document.getElementById("chunk-modal");
    const title = document.getElementById("chunk-modal-title");
    const status = document.getElementById("chunk-modal-status");
    const content = document.getElementById("chunk-modal-content");
    title.textContent = file.name;
    status.textContent = "Loading from stored chunks…";
    content.textContent = "";
    modal.hidden = false;
    document.getElementById("close-chunk-modal").focus();
    try {
        const data = await apiJSON("/api/files/" + encodeURIComponent(file.id) + "/content");
        if (!isCurrentPage("integrity", revision) || modal.hidden) return;
        const display = chunkDisplay(data.contentHex || "");
        status.textContent = (data.verified ? "Assembled from checksum-verified copies" : "At least one chunk has no verified copy") +
            " · " + display.format + (data.truncated ? " · first 1 MB shown" : "");
        content.textContent = display.text || "(empty file)";
    } catch (error) {
        if (modal.hidden) return;
        status.textContent = "Could not read this file";
        content.textContent = error.message;
    }
}

async function runIntegrityAction(fileId, action) {
    const file = state.files.find(function (item) { return item.id === fileId; });
    if (!file || state.integrityBusy) return;
    const label = action === "verify" ? "Checking" : "Replicating";
    state.integrityBusy = true;
    refreshIntegrityControls();
    setIntegrityMessage(label + " " + file.name + "…", "");
    try {
        const result = await apiJSON("/api/files/" + encodeURIComponent(fileId) + "/" +
            (action === "verify" ? "verify" : "recover"), { method: "POST" });
        replaceIntegrityFileChunks(fileId, result.chunks);
        if (action === "verify") {
            setIntegrityMessage("Verified " + result.chunks.length + " chunk(s) on disk for " + file.name + ".", "success");
        } else {
            const restored = Number(result.restoredCopies) || 0;
            setIntegrityMessage(restored
                ? restored + " copy/copies replicated and verified for " + file.name + "."
                : "No copies were restored. Each chunk needs at least one healthy copy.", restored ? "success" : "warning");
        }
    } catch (error) {
        setIntegrityMessage(error.message, "error");
    } finally {
        state.integrityBusy = false;
        refreshIntegrityControls();
    }
}

function wireIntegrityPage(revision) {
    document.getElementById("integrity-report").addEventListener("click", function (event) {
        const button = event.target.closest("[data-view-file], [data-verify-file], [data-recover-file]");
        if (!button || button.disabled) return;
        const fileId = button.dataset.viewFile || button.dataset.verifyFile || button.dataset.recoverFile;
        const file = state.files.find(function (item) { return item.id === fileId; });
        if (!file) return;
        if (button.hasAttribute("data-view-file")) showFileContents(file, revision);
        else runIntegrityAction(fileId, button.hasAttribute("data-verify-file") ? "verify" : "recover");
    });
    document.getElementById("close-chunk-modal").addEventListener("click", closeChunkModal);
    document.getElementById("chunk-modal").addEventListener("click", function (event) {
        if (event.target.classList.contains("chunk-modal-backdrop")) closeChunkModal();
    });
    document.getElementById("chunk-modal").addEventListener("keydown", function (event) {
        if (event.key === "Escape") closeChunkModal();
    });
}

async function loadIntegrityFiles(revision) {
    try {
        const data = await apiJSON("/api/database");
        if (!isCurrentPage("integrity", revision)) return;
        state.files = Array.isArray(data.files) ? data.files : [];
        state.integrityChunks = Array.isArray(data.chunks) ? data.chunks : [];
        renderIntegrityReport();
    } catch (error) {
        if (!isCurrentPage("integrity", revision)) return;
        state.files = [];
        state.integrityChunks = [];
        document.getElementById("integrity-report").innerHTML = '<div class="notice error">' + escapeHTML(error.message) + "</div>";
        setIntegrityMessage("Could not load file data from MySQL.", "error");
    } finally {
        if (isCurrentPage("integrity", revision)) setPageBusy(false);
    }
}

function renderIntegrityPage(revision) {
    pageContent.innerHTML = integrityTemplate();
    wireIntegrityPage(revision);
    renderIntegritySummary([]);
    loadIntegrityFiles(revision);
}

function safeLocation(path) {
    const value = String(path || "");
    const parts = value.split(/[\\/]+/).filter(Boolean);
    const storageIndex = parts.lastIndexOf("storage");
    const safeParts = storageIndex >= 0 ? parts.slice(storageIndex + 1) : parts.slice(-2);
    return safeParts.join("/") || "Location unavailable";
}

function databaseTemplate() {
    return '<div id="database-error"></div>' +
        '<section class="panel"><div class="panel-header"><h3 class="panel-title">Files</h3><span id="database-file-count" class="count-badge">—</span></div>' +
            '<div id="database-files">' + loadingMarkup(2) + "</div></section>" +
        '<section class="panel"><div class="panel-header"><h3 class="panel-title">Chunks and replicas</h3>' +
            '<span id="database-chunk-count" class="count-badge">—</span></div><div id="database-chunks">' + loadingMarkup(2) + "</div></section>" +
        '<section class="panel"><div class="panel-header activity-log-header"><h3 class="panel-title">Persisted activity log</h3>' +
            '<div class="activity-log-actions"><button id="select-all-activities" class="button-secondary" type="button">Select all</button>' +
                '<button id="delete-selected-activities" class="button-danger" type="button" disabled>Delete selected</button>' +
                '<span id="database-activity-count" class="count-badge">—</span></div></div>' +
            '<div id="activity-log">' + loadingMarkup(2) + "</div></section>";
}

function updateActivitySelection() {
    const checkboxes = Array.from(document.querySelectorAll(".activity-select"));
    const selected = checkboxes.filter(function (checkbox) { return checkbox.checked; }).length;
    const selectAll = document.getElementById("select-all-activities");
    const deleteButton = document.getElementById("delete-selected-activities");
    if (!selectAll || !deleteButton) return;
    selectAll.disabled = checkboxes.length === 0 || state.activityDeleting;
    deleteButton.disabled = selected === 0 || state.activityDeleting;
    selectAll.textContent = selected > 0 && selected === checkboxes.length ? "Deselect all" : "Select all";
    deleteButton.textContent = selected ? "Delete selected (" + selected + ")" : "Delete selected";
}

async function deleteActivityLogs(ids, revision) {
    if (!ids.length || state.activityDeleting) return;
    if (!window.confirm("Permanently delete " + ids.length + " selected activity log(s)?")) return;
    state.activityDeleting = true;
    updateActivitySelection();
    let deleted = 0;
    let failure = "";
    for (const id of ids) {
        try {
            await apiJSON("/api/activity/" + encodeURIComponent(id), { method: "DELETE" });
            deleted += 1;
        } catch (error) {
            if (!failure) failure = error.message;
        }
    }
    state.activityDeleting = false;
    updateActivitySelection();
    if (!isCurrentPage("database", revision)) return;
    try {
        const data = await apiJSON("/api/database");
        if (!isCurrentPage("database", revision)) return;
        renderDatabaseData(data, revision);
    } catch (error) {
        showToast(deleted + " log(s) deleted. " + error.message, "error");
        return;
    }
    if (failure) showToast(deleted + " log(s) deleted. " + failure, "error");
    else showToast(deleted + " activity log(s) deleted.", "success");
}

function wireActivityLog(revision) {
    const selectAll = document.getElementById("select-all-activities");
    const deleteButton = document.getElementById("delete-selected-activities");
    if (!selectAll || !deleteButton) return;
    selectAll.addEventListener("click", function () {
        const checkboxes = Array.from(document.querySelectorAll(".activity-select"));
        const select = checkboxes.some(function (checkbox) { return !checkbox.checked; });
        checkboxes.forEach(function (checkbox) { checkbox.checked = select; });
        updateActivitySelection();
    });
    document.querySelectorAll(".activity-select").forEach(function (checkbox) {
        checkbox.addEventListener("change", updateActivitySelection);
    });
    deleteButton.addEventListener("click", function () {
        const ids = Array.from(document.querySelectorAll(".activity-select:checked"))
            .map(function (checkbox) { return checkbox.value; });
        deleteActivityLogs(ids, revision);
    });
    updateActivitySelection();
}

function renderDatabaseData(data, revision) {
    const files = Array.isArray(data.files) ? data.files : [];
    const chunks = Array.isArray(data.chunks) ? data.chunks : [];
    const activities = Array.isArray(data.activities) ? data.activities : [];
    document.getElementById("database-file-count").textContent = String(files.length);
    document.getElementById("database-chunk-count").textContent = String(chunks.length);
    document.getElementById("database-activity-count").textContent = String(activities.length);

    const fileRows = files.map(function (file) {
        return [
            '<span class="mono">' + escapeHTML(file.id) + "</span>",
            '<span class="table-primary">' + escapeHTML(file.name) + "</span>",
            '<span class="mono">' + escapeHTML(formatSize(file.size)) + "</span>",
            '<span class="mono">' + escapeHTML(file.chunkCount) + "</span>"
        ];
    });
    document.getElementById("database-files").innerHTML = tableHTML(
        ["File ID", "File name", "Original size", "Chunk count"],
        fileRows,
        "No file rows",
        "MySQL returned no stored file records."
    );

    const chunkRows = chunks.map(function (chunk) {
        return [
            '<span class="mono">' + escapeHTML(chunk.id) + "</span>",
            '<span class="mono">' + escapeHTML(chunk.fileId) + "</span>",
            '<span class="mono">' + escapeHTML(chunk.number) + "</span>",
            '<span class="mono">' + escapeHTML(formatSize(chunk.size)) + "</span>",
            '<span class="checksum-cell" title="' + escapeHTML(chunk.checksum) + '">' + escapeHTML(chunk.checksum) + "</span>",
            '<span class="table-primary">' + escapeHTML(safeLocation(chunk.main && chunk.main.path)) + '</span><small class="table-secondary">' +
                escapeHTML(chunk.main && chunk.main.status || "UNKNOWN") + "</small>",
            '<span class="table-primary">' + escapeHTML(safeLocation(chunk.node1 && chunk.node1.path)) + '</span><small class="table-secondary">' +
                escapeHTML(chunk.node1 && chunk.node1.status || "UNKNOWN") + "</small>",
            '<span class="table-primary">' + escapeHTML(safeLocation(chunk.node2 && chunk.node2.path)) + '</span><small class="table-secondary">' +
                escapeHTML(chunk.node2 && chunk.node2.status || "UNKNOWN") + "</small>"
        ];
    });
    document.getElementById("database-chunks").innerHTML = tableHTML(
        ["Chunk ID", "File ID", "#", "Size", "SHA-256", "Main", "Node 1", "Node 2"],
        chunkRows,
        "No chunk rows",
        "MySQL returned no chunk metadata."
    );
    document.getElementById("activity-log").innerHTML = activityMarkup(activities, null, true);
    wireActivityLog(revision);
}

async function renderDatabasePage(revision) {
    pageContent.innerHTML = databaseTemplate();
    try {
        const data = await apiJSON("/api/database");
        if (!isCurrentPage("database", revision)) return;
        renderDatabaseData(data, revision);
    } catch (error) {
        if (!isCurrentPage("database", revision)) return;
        document.getElementById("database-error").innerHTML = pageError("Database data unavailable", error.message);
        document.getElementById("database-files").innerHTML = "";
        document.getElementById("database-chunks").innerHTML = "";
        document.getElementById("activity-log").innerHTML = "";
    } finally {
        if (isCurrentPage("database", revision)) setPageBusy(false);
    }
}

navigation.addEventListener("click", function (event) {
    const button = event.target.closest("[data-page]");
    if (button && PAGE_INFO[button.dataset.page]) renderPage(button.dataset.page);
});

pageContent.addEventListener("click", function (event) {
    const button = event.target.closest("[data-page]");
    if (button && PAGE_INFO[button.dataset.page]) renderPage(button.dataset.page);
});

document.querySelectorAll("[data-page-link]").forEach(function (link) {
    link.addEventListener("click", function (event) {
        event.preventDefault();
        renderPage(link.dataset.pageLink);
    });
});

renderPage("dashboard");
