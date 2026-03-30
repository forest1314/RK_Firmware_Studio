const state = {
  activeMode: "DI",
  flashData: null,
};

const modeMeta = [
  ["UF", "整包烧录"],
  ["DB", "下载 Boot"],
  ["UL", "烧录 Loader"],
  ["EF", "擦除 Flash"],
  ["DI", "分区烧录"],
];

async function getJson(url) {
  const res = await fetch(url, { cache: "no-store" });
  return res.json();
}

function renderStatus(overview, deviceState) {
  const root = document.getElementById("status-strip");
  root.innerHTML = "";
  const items = [
    ["设备状态", deviceState.primaryState],
    ["工程目录", overview.default_project || "-"],
    ["模板数量", overview.package_templates || "0"],
    ["芯片配置", overview.pack_profiles || "0"],
  ];
  for (const [label, value] of items) {
    const pill = document.createElement("div");
    pill.className = "status-pill";
    pill.innerHTML = `<strong>${label}</strong><span>${value}</span>`;
    root.appendChild(pill);
  }
}

function renderModes() {
  const root = document.getElementById("mode-grid");
  root.innerHTML = "";
  for (const [code, label] of modeMeta) {
    const button = document.createElement("button");
    button.className = `mode-button ${state.activeMode === code ? "active" : ""}`;
    button.innerHTML = `${code}<small>${label}</small>`;
    button.onclick = () => {
      state.activeMode = code;
      renderModes();
      renderPreview();
      renderToolbarSummary();
    };
    root.appendChild(button);
  }
}

function renderPreview() {
  const preview = document.getElementById("command-preview");
  if (!state.flashData?.previews) {
    preview.textContent = "等待工程数据...";
    return;
  }
  preview.textContent = state.flashData.previews[state.activeMode] || "当前模式暂无预览";
}

function renderPartitions() {
  const body = document.getElementById("partition-body");
  body.innerHTML = "";
  const rows = state.flashData?.rows || [];
  let enabledCount = 0;
  for (const row of rows) {
    if (row.enabled) {
      enabledCount += 1;
    }
    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td><span class="${row.enabled ? "tag-enabled" : "tag-disabled"}">${row.enabled ? "启用" : "关闭"}</span></td>
      <td>${row.name}</td>
      <td class="path-cell" title="${row.filePath || ""}">${row.filePath || "-"}</td>
      <td>${row.fileSizeText}</td>
      <td class="spec-cell">${row.partitionSpec}</td>
    `;
    body.appendChild(tr);
  }

  document.getElementById("mtdparts-target").textContent = `mtdparts=${state.flashData?.mtdpartsTarget ?? ""}`;
  document.getElementById("partition-count-chip").textContent = `${rows.length} 项`;
  document.getElementById("table-summary").textContent = `已加载 ${rows.length} 个分区，其中 ${enabledCount} 个已绑定镜像文件。`;

  const issueBox = document.getElementById("issue-box");
  const issues = state.flashData?.issues || [];
  if (!issues.length) {
    issueBox.className = "issue-box clean";
    issueBox.textContent = "当前分区表没有发现容量越界或镜像缺失问题。";
    return;
  }
  issueBox.className = "issue-box";
  issueBox.innerHTML = issues.map((item) => item.message).join("<br />");
}

function renderToolbarSummary() {
  const rows = state.flashData?.rows || [];
  const issues = state.flashData?.issues || [];
  const modeName = modeMeta.find((item) => item[0] === state.activeMode)?.[1] || "";
  const enabledCount = rows.filter((row) => row.enabled).length;
  document.getElementById("active-mode-label").textContent = `${state.activeMode} / ${modeName}`;
  document.getElementById("project-state-label").textContent = state.flashData?.projectDir ? "工程已装载" : "等待读取工程";
  document.getElementById("partition-summary-label").textContent = `${rows.length} 项 / ${enabledCount} 启用`;
  document.getElementById("issue-summary-label").textContent = issues.length ? issues[0].message : "当前未发现阻塞性问题";
}

function renderDiagnostics(text) {
  document.getElementById("diagnostics-output").textContent = text || "";
}

function renderDevicePanel(deviceState) {
  const root = document.getElementById("device-state-panel");
  root.innerHTML = "";
  const rows = [
    ["主状态", deviceState.primaryState],
    ["更新时间", deviceState.updatedAt],
    ["状态摘要", deviceState.detailText],
    ["异常", deviceState.lastError || "无"],
  ];
  for (const [title, body] of rows) {
    const card = document.createElement("div");
    card.className = "device-row";
    card.innerHTML = `<strong>${title}</strong><span>${body}</span>`;
    root.appendChild(card);
  }
}

async function loadAll() {
  const project = document.getElementById("project-input").value.trim();
  const query = project ? `?project=${encodeURIComponent(project)}` : "";
  const [overview, deviceState, diagnostics, flashData] = await Promise.all([
    getJson("/api/overview"),
    getJson("/api/device-state"),
    getJson("/api/diagnostics"),
    getJson(`/api/flash-center${query}`),
  ]);

  renderStatus(overview, deviceState);
  renderDiagnostics(diagnostics.text);
  renderDevicePanel(deviceState);
  state.flashData = flashData.ok ? flashData : { rows: [], issues: [{ message: flashData.error }], previews: {} };

  document.getElementById("parameter-file").value = flashData.parameterFile || "";
  document.getElementById("loader-file").value = flashData.loaderFile || "";
  document.getElementById("update-image").value = flashData.updateImage || "";

  renderModes();
  renderPartitions();
  renderPreview();
  renderToolbarSummary();
}

function bindTabs() {
  const buttons = document.querySelectorAll(".mini-button");
  const logsPanel = document.getElementById("logs-panel");
  const messagesPanel = document.getElementById("messages-panel");
  buttons.forEach((button) => {
    button.addEventListener("click", () => {
      buttons.forEach((item) => item.classList.remove("active"));
      button.classList.add("active");
      const logs = button.dataset.tab === "logs";
      logsPanel.classList.toggle("hidden", !logs);
      messagesPanel.classList.toggle("hidden", logs);
    });
  });
}

document.getElementById("refresh-button").addEventListener("click", loadAll);
bindTabs();
loadAll();
setInterval(loadAll, 5000);
