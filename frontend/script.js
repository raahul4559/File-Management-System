/* File Management System – Frontend */

const API = "";  // same origin

let currentPath = "";
let renameTarget = null;
let createType = "file";  // or "folder"

/* ---------- Helpers ---------- */

function formatSize(bytes) {
  if (bytes === 0) return "0 B";
  const units = ["B", "KB", "MB", "GB"];
  const i = Math.floor(Math.log(bytes) / Math.log(1024));
  return (bytes / Math.pow(1024, i)).toFixed(i ? 1 : 0) + " " + units[i];
}

function showToast(msg, type = "success") {
  const t = document.getElementById("toast");
  t.textContent = msg;
  t.className = "toast " + type;
  t.classList.remove("hidden");
  setTimeout(() => t.classList.add("hidden"), 3000);
}

async function api(path, options = {}) {
  try {
    const res = await fetch(API + path, options);
    const data = await res.json().catch(() => ({}));
    if (!res.ok || data.ok === false) {
      throw new Error(data.error || res.statusText || "Request failed");
    }
    return data;
  } catch (err) {
    showToast(err.message, "error");
    throw err;
  }
}

/* ---------- UI Updates ---------- */

function updateBreadcrumb() {
  const el = document.getElementById("breadcrumb");
  el.innerHTML = "";
  const home = document.createElement("span");
  home.className = "crumb";
  home.dataset.path = "";
  home.textContent = "Home";
  home.onclick = () => navigate("");
  el.appendChild(home);

  if (!currentPath) return;

  const parts = currentPath.split("/").filter(Boolean);
  let acc = "";
  parts.forEach((part, idx) => {
    acc += (acc ? "/" : "") + part;
    const span = document.createElement("span");
    span.className = "crumb";
    span.dataset.path = acc;
    span.textContent = part;
    span.onclick = () => navigate(acc);
    el.appendChild(span);
  });
}

function renderList(entries) {
  const list = document.getElementById("file-list");
  const empty = document.getElementById("empty-state");
  list.innerHTML = "";

  if (!entries || entries.length === 0) {
    empty.classList.remove("hidden");
    return;
  }
  empty.classList.add("hidden");

  // folders first, then files, alphabetical
  entries.sort((a, b) => {
    if (a.is_dir !== b.is_dir) return b.is_dir - a.is_dir;
    return a.name.localeCompare(b.name);
  });

  entries.forEach(e => {
    const row = document.createElement("div");
    row.className = "file-row";

    const nameDiv = document.createElement("div");
    nameDiv.className = "file-name" + (e.is_dir ? " clickable" : "");
    nameDiv.innerHTML = `<span class="icon">${e.is_dir ? "📁" : "📄"}</span><span>${escapeHtml(e.name)}</span>`;
    if (e.is_dir) {
      nameDiv.onclick = () => navigate(currentPath ? currentPath + "/" + e.name : e.name);
    }

    const size = document.createElement("div");
    size.className = "file-size";
    size.textContent = e.is_dir ? "—" : formatSize(e.size);

    const date = document.createElement("div");
    date.className = "file-date";
    date.textContent = e.mtime || "—";

    const actions = document.createElement("div");
    actions.className = "file-actions";

    if (!e.is_dir) {
      const previewBtn = document.createElement("button");
      previewBtn.className = "action-btn";
      previewBtn.title = "Preview";
      previewBtn.textContent = "👁️";
      previewBtn.onclick = () => previewFile(e.name);
      actions.appendChild(previewBtn);

      const dlBtn = document.createElement("button");
      dlBtn.className = "action-btn";
      dlBtn.title = "Download";
      dlBtn.textContent = "⬇️";
      dlBtn.onclick = () => downloadFile(e.name);
      actions.appendChild(dlBtn);
    }

    const renBtn = document.createElement("button");
    renBtn.className = "action-btn";
    renBtn.title = "Rename";
    renBtn.textContent = "✏️";
    renBtn.onclick = () => openRename(e.name);
    actions.appendChild(renBtn);

    const delBtn = document.createElement("button");
    delBtn.className = "action-btn danger";
    delBtn.title = "Delete";
    delBtn.textContent = "🗑️";
    delBtn.onclick = () => deleteItem(e.name);
    actions.appendChild(delBtn);

    row.appendChild(nameDiv);
    row.appendChild(size);
    row.appendChild(date);
    row.appendChild(actions);
    list.appendChild(row);
  });
}

function escapeHtml(str) {
  return str.replace(/[&<>"']/g, c => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;"
  })[c]);
}

/* ---------- Core actions ---------- */

async function loadList() {
  try {
    const data = await api(`/api/list?path=${encodeURIComponent(currentPath)}`);
    renderList(data.entries || []);
    document.getElementById("current-title").textContent =
      currentPath ? currentPath.split("/").pop() : "All Files";
    updateBreadcrumb();
  } catch (e) {
    // toast already shown
  }
}

async function loadUsage() {
  try {
    const data = await api("/api/usage");
    document.getElementById("storage-usage").textContent = formatSize(data.bytes);
    // visual bar (cap at 100 MB for demo feel)
    const pct = Math.min(100, (data.bytes / (100 * 1024 * 1024)) * 100);
    document.getElementById("storage-fill").style.width = pct + "%";
  } catch (e) {}
}

function navigate(path) {
  currentPath = path;
  loadList();
}

async function createItem() {
  const name = document.getElementById("create-input").value.trim();
  if (!name) {
    showToast("Name required", "error");
    return;
  }
  const full = currentPath ? currentPath + "/" + name : name;
  try {
    await api("/api/create", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ path: full, type: createType })
    });
    showToast(createType === "folder" ? "Folder created" : "File created");
    closeModal("create-modal");
    loadList();
    loadUsage();
  } catch (e) {}
}

async function deleteItem(name) {
  if (!confirm(`Delete "${name}"?`)) return;
  const full = currentPath ? currentPath + "/" + name : name;
  try {
    await api("/api/delete", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ path: full })
    });
    showToast("Deleted");
    loadList();
    loadUsage();
  } catch (e) {}
}

function openRename(name) {
  renameTarget = name;
  document.getElementById("rename-input").value = name;
  document.getElementById("rename-modal").classList.remove("hidden");
  document.getElementById("rename-input").focus();
}

async function doRename() {
  const newName = document.getElementById("rename-input").value.trim();
  if (!newName || newName === renameTarget) {
    closeModal("rename-modal");
    return;
  }
  const oldFull = currentPath ? currentPath + "/" + renameTarget : renameTarget;
  const newFull = currentPath ? currentPath + "/" + newName : newName;
  try {
    await api("/api/rename", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ old: oldFull, new: newFull })
    });
    showToast("Renamed");
    closeModal("rename-modal");
    loadList();
  } catch (e) {}
}

async function previewFile(name) {
  const full = currentPath ? currentPath + "/" + name : name;
  try {
    const data = await api(`/api/read?path=${encodeURIComponent(full)}`);
    document.getElementById("preview-title").textContent = name;
    if (data.is_text === false) {
      document.getElementById("preview-body").textContent =
        "This file is binary and cannot be previewed as text. Use Download to open or save it.";
    } else {
      document.getElementById("preview-body").textContent = data.content || "(empty)";
    }
    document.getElementById("preview-modal").classList.remove("hidden");
  } catch (e) {}
}

function downloadFile(name) {
  const full = currentPath ? currentPath + "/" + name : name;
  window.location.href = `/api/download?path=${encodeURIComponent(full)}`;
}

async function uploadFiles(files) {
  for (const file of files) {
    try {
      const res = await fetch("/api/upload", {
        method: "POST",
        headers: {
          "X-Filename": file.name,
          "X-Path": currentPath || "",
          "Content-Type": "application/octet-stream"
        },
        body: file
      });
      const data = await res.json();
      if (!res.ok || data.ok === false) throw new Error(data.error || "Upload failed");
      showToast(`Uploaded ${file.name}`);
    } catch (err) {
      showToast(err.message, "error");
    }
  }
  loadList();
  loadUsage();
}

async function doSearch() {
  const q = document.getElementById("search-input").value.trim();
  if (!q) {
    loadList();
    return;
  }
  try {
    const data = await api(`/api/search?q=${encodeURIComponent(q)}`);
    renderList(data.results || []);
    document.getElementById("current-title").textContent = `Search: "${q}"`;
  } catch (e) {}
}

/* ---------- Modal helpers ---------- */

function closeModal(id) {
  document.getElementById(id).classList.add("hidden");
}

function openCreate(type) {
  createType = type;
  document.getElementById("create-title").textContent =
    type === "folder" ? "New Folder" : "New File";
  document.getElementById("create-input").value = "";
  document.getElementById("create-modal").classList.remove("hidden");
  document.getElementById("create-input").focus();
}

/* ---------- Event bindings ---------- */

document.getElementById("btn-new-file").onclick = () => openCreate("file");
document.getElementById("btn-new-folder").onclick = () => openCreate("folder");
document.getElementById("btn-upload").onclick = () => document.getElementById("file-input").click();
document.getElementById("empty-upload").onclick = () => document.getElementById("file-input").click();
document.getElementById("file-input").onchange = (e) => {
  if (e.target.files.length) uploadFiles(e.target.files);
  e.target.value = "";
};

document.getElementById("refresh-btn").onclick = () => { loadList(); loadUsage(); };
document.getElementById("search-btn").onclick = doSearch;
document.getElementById("search-input").onkeydown = (e) => {
  if (e.key === "Enter") doSearch();
};

document.getElementById("preview-close").onclick = () => closeModal("preview-modal");
document.getElementById("rename-close").onclick = () => closeModal("rename-modal");
document.getElementById("rename-cancel").onclick = () => closeModal("rename-modal");
document.getElementById("rename-confirm").onclick = doRename;

document.getElementById("create-close").onclick = () => closeModal("create-modal");
document.getElementById("create-cancel").onclick = () => closeModal("create-modal");
document.getElementById("create-confirm").onclick = createItem;

document.getElementById("create-input").onkeydown = (e) => {
  if (e.key === "Enter") createItem();
};
document.getElementById("rename-input").onkeydown = (e) => {
  if (e.key === "Enter") doRename();
};

/* ---------- Init ---------- */

loadList();
loadUsage();
setInterval(loadUsage, 15000);
