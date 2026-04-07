const http = require("http");
const fs = require("fs");
const path = require("path");

const PORT = process.env.PORT || 3000;
const PUBLIC_DIR = path.join(__dirname, "public");
const GCODE_DIR = path.join(__dirname, "..", "gcode");
const CONFIG_ADV_PATH = path.join(__dirname, "..", "Marlin", "marlin-2.1.2.6", "Marlin", "Configuration_adv.h");

const MIME_TYPES = {
  ".css": "text/css; charset=utf-8",
  ".html": "text/html; charset=utf-8",
  ".js": "application/javascript; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".png": "image/png",
  ".svg": "image/svg+xml; charset=utf-8",
  ".txt": "text/plain; charset=utf-8",
  ".webmanifest": "application/manifest+json; charset=utf-8",
};

function sendJson(res, statusCode, payload) {
  res.writeHead(statusCode, { "Content-Type": "application/json; charset=utf-8" });
  res.end(JSON.stringify(payload));
}

function sendFile(res, filePath) {
  const ext = path.extname(filePath).toLowerCase();
  const type = MIME_TYPES[ext] || "application/octet-stream";

  fs.readFile(filePath, (error, data) => {
    if (error) {
      res.writeHead(error.code === "ENOENT" ? 404 : 500, {
        "Content-Type": "text/plain; charset=utf-8",
      });
      res.end(error.code === "ENOENT" ? "Not found" : "Server error");
      return;
    }

    res.writeHead(200, { "Content-Type": type });
    res.end(data);
  });
}

function readSpiderProgramMap() {
  const configText = fs.readFileSync(CONFIG_ADV_PATH, "utf8");
  const pattern = /SPIDER_SD_FILE_CODE\((\d+),\s*"([^"]+)"\)/g;
  const programs = [];
  let match;

  while ((match = pattern.exec(configText)) !== null) {
    const code = Number(match[1]);
    const marlinPath = match[2];
    const relativePath = marlinPath.replace(/^\/+gcodes\/+/i, "");
    const filePath = path.join(GCODE_DIR, relativePath);
    let content = null;
    let exists = false;

    try {
      content = fs.readFileSync(filePath, "utf8");
      exists = true;
    } catch (error) {
      if (error.code !== "ENOENT") {
        throw error;
      }
    }

    programs.push({
      code,
      name: path.basename(relativePath),
      marlinPath,
      filePath,
      exists,
      content,
    });
  }

  return programs.sort((left, right) => left.code - right.code);
}

http
  .createServer((req, res) => {
    const requestUrl = new URL(req.url || "/", "http://127.0.0.1");

    if (requestUrl.pathname === "/api/spider-files") {
      try {
        sendJson(res, 200, { programs: readSpiderProgramMap() });
      } catch (error) {
        sendJson(res, 500, { error: "spider_file_map_failed", message: error.message });
      }
      return;
    }

    const requestPath = requestUrl.pathname === "/" ? "index.html" : requestUrl.pathname.replace(/^[/\\]+/, "");
    const safePath = path.normalize(requestPath).replace(/^(\.\.[/\\])+/, "");
    const filePath = path.join(PUBLIC_DIR, safePath);

    if (!filePath.startsWith(PUBLIC_DIR)) {
      res.writeHead(403, { "Content-Type": "text/plain; charset=utf-8" });
      res.end("Forbidden");
      return;
    }

    sendFile(res, filePath);
  })
  .listen(PORT, () => {
    console.log(`WebApp running at http://localhost:${PORT}`);
  });
