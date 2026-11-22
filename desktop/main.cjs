"use strict";

const { app, BrowserWindow, dialog } = require("electron");
const { spawn } = require("node:child_process");
const http = require("node:http");
const path = require("node:path");

const port = 8080;
const origin = `http://127.0.0.1:${port}`;
let backend;

function getBackendPaths() {
  if (app.isPackaged) {
    return {
      executable: path.join(process.resourcesPath, "backend", "lumo-edge.exe"),
      webRoot: path.join(process.resourcesPath, "web"),
    };
  }
  const projectRoot = path.resolve(__dirname, "..");
  return {
    executable: path.join(projectRoot, "build", "Release", "lumo-edge.exe"),
    webRoot: path.join(projectRoot, "web"),
  };
}

function isHealthy() {
  return new Promise((resolve) => {
    const request = http.get(`${origin}/healthz`, (response) => {
      let body = "";
      response.setEncoding("utf8");
      response.on("data", (chunk) => {
        body += chunk;
      });
      response.on("end", () => {
        try {
          const health = JSON.parse(body);
          resolve(
            response.statusCode === 200 &&
              health.status === "ok" &&
              ["read-only", "guarded-auto-trading"].includes(health.mode),
          );
        } catch {
          resolve(false);
        }
      });
    });
    request.setTimeout(800, () => request.destroy());
    request.on("error", () => resolve(false));
  });
}

function waitForBackend() {
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => finish(new Error("NEXUS server did not start within 30 seconds.")), 30000);
    const interval = setInterval(async () => {
      if (await isHealthy()) finish();
    }, 250);
    let settled = false;

    function finish(error) {
      if (settled) return;
      settled = true;
      clearTimeout(timeout);
      clearInterval(interval);
      if (error) reject(error);
      else resolve();
    }

    backend.once("error", (error) => finish(error));
    backend.once("exit", (code) => {
      finish(new Error(`NEXUS server exited before becoming ready (code ${code ?? "unknown"}).`));
    });
  });
}

async function startBackend() {
  const paths = getBackendPaths();
  if (await isHealthy()) return;
  const userData = app.getPath("userData");
  backend = spawn(paths.executable, [], {
    cwd: userData,
    env: {
      ...process.env,
      LUMO_PORT: String(port),
      LUMO_WEB_ROOT: paths.webRoot,
      LUMO_LOG_PATH: path.join(userData, "logs", "lumo-edge.log"),
      DATABASE_PATH: path.join(userData, "data", "lumo-edge.db"),
    },
    stdio: "ignore",
    windowsHide: true,
  });
  await waitForBackend();
}

function createWindow() {
  const window = new BrowserWindow({
    width: 1440,
    height: 960,
    minWidth: 920,
    minHeight: 680,
    title: "NEXUS",
    backgroundColor: "#0b1017",
    autoHideMenuBar: true,
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });

  window.webContents.setWindowOpenHandler(() => ({ action: "deny" }));
  window.webContents.on("will-navigate", (event, url) => {
    if (new URL(url).origin !== origin) event.preventDefault();
  });
  window.loadURL(origin);
}

app.setAppUserModelId("com.nexus.desktop");
const hasSingleInstance = app.requestSingleInstanceLock();
if (!hasSingleInstance) {
  app.quit();
} else {
  app.on("second-instance", () => {
    const [window] = BrowserWindow.getAllWindows();
    if (window) {
      if (window.isMinimized()) window.restore();
      window.focus();
    }
  });

  app.whenReady().then(async () => {
    try {
      await startBackend();
      createWindow();
    } catch (error) {
      if (backend && !backend.killed) backend.kill();
      await dialog.showMessageBox({
        type: "error",
        title: "NEXUS could not start",
        message: error.message,
        detail: "Check that the desktop app is installed correctly and that port 8080 is available.",
      });
      app.quit();
    }
  });
}

app.on("before-quit", () => {
  if (backend && !backend.killed) backend.kill();
});

app.on("window-all-closed", () => app.quit());
