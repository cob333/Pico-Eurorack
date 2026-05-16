/*
  Copyright 2026 Wenhao Yang

  Author: Wenhao Yang
  Contributor: Wenhao Yang

  Cloudflare Worker API gateway for the public Pico-Eurorack client.
*/

const JSON_HEADERS = {
  "Content-Type": "application/json; charset=utf-8"
};

export default {
  async fetch(request, env) {
    const corsHeaders = cors(request, env);
    if (request.method === "OPTIONS") {
      return new Response(null, { status: 204, headers: corsHeaders });
    }

    try {
      const url = new URL(request.url);
      const route = `${request.method} ${url.pathname}`;
      if (route === "GET /api/health") {
        return json({ ok: true }, corsHeaders);
      }
      if (route === "GET /api/manifest") {
        return json(await getManifest(env), corsHeaders);
      }
      if (route === "GET /api/app-size") {
        return json(await getAppSize(url, env), corsHeaders);
      }
      if (route === "POST /api/generate/start") {
        return json(await startGenerate(request, env), corsHeaders, 202);
      }
      if (route === "GET /api/generate/status") {
        return json(await getGenerateStatus(url, env), corsHeaders);
      }
      if (route === "GET /api/generate/download") {
        return await downloadGenerateOutput(url, env, corsHeaders);
      }
      if (route === "POST /api/generate/cancel") {
        return json({ ok: true, status: "running", message: "Cloud builds cannot be cancelled yet" }, corsHeaders);
      }
      if (route === "GET /api/samples") {
        return json(await getSamples(url, env), corsHeaders);
      }
      if (route === "POST /api/samples/upload") {
        return json(await uploadSamples(request, env), corsHeaders);
      }
      if (route === "POST /api/samples/delete-bank") {
        return json({ error: "cloud sample bank deletion is not enabled yet" }, corsHeaders, 501);
      }
      return json({ error: "not found" }, corsHeaders, 404);
    } catch (error) {
      const message = error && error.message ? error.message : "internal error";
      const status = error && Number.isInteger(error.status) ? error.status : 500;
      return json({ error: message }, corsHeaders, status);
    }
  }
};

function cors(request, env) {
  const origin = request.headers.get("Origin") || "";
  const allowed = env.ALLOWED_ORIGIN || "*";
  const allowOrigin = allowed === "*" || origin === allowed ? (origin || allowed) : allowed;
  return {
    "Access-Control-Allow-Origin": allowOrigin,
    "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type",
    "Access-Control-Expose-Headers": "Content-Disposition"
  };
}

function json(payload, corsHeaders, status = 200) {
  return new Response(JSON.stringify(payload), {
    status,
    headers: {
      ...JSON_HEADERS,
      ...corsHeaders
    }
  });
}

function httpError(status, message) {
  const error = new Error(message);
  error.status = status;
  return error;
}

async function getManifest(env) {
  if (!env.MANIFEST_URL) {
    throw httpError(500, "MANIFEST_URL is not configured");
  }
  const response = await fetch(env.MANIFEST_URL, { cf: { cacheTtl: 60, cacheEverything: true } });
  if (!response.ok) {
    throw httpError(502, `manifest HTTP ${response.status}`);
  }
  return await response.json();
}

async function getAppSize(url, env) {
  const appId = url.searchParams.get("app") || "";
  const manifest = await getManifest(env);
  const app = manifest.apps.find((item) => item.id === appId || item.name === appId);
  if (!app) {
    throw httpError(404, "unknown app");
  }
  return app;
}

async function startGenerate(request, env) {
  if (!env.GITHUB_TOKEN) {
    throw httpError(500, "GITHUB_TOKEN secret is not configured");
  }
  const payload = await readJson(request);
  const manifest = await getManifest(env);
  const normalized = validateBuildRequest(payload, manifest, env);
  const sampleKey = await prepareBuildSamples(payload, normalized, env);
  const id = crypto.randomUUID();
  const now = new Date().toISOString();
  const record = {
    id,
    status: "queued",
    progress: 1,
    message: "Queued",
    createdAt: now,
    updatedAt: now,
    sampleKey,
    ...normalized
  };
  await putJson(env, requestKey(id), record);
  await dispatchWorkflow(env, id, normalized, sampleKey);
  return { id, status: "queued", progress: 1, message: "Queued" };
}

async function readJson(request) {
  const contentType = request.headers.get("Content-Type") || "";
  if (!contentType.includes("application/json")) {
    throw httpError(415, "expected application/json");
  }
  try {
    return await request.json();
  } catch (_error) {
    throw httpError(400, "invalid JSON");
  }
}

function validateBuildRequest(payload, manifest, env) {
  const device = payload.device;
  const slots = Array.isArray(payload.slots) ? payload.slots.map((item) => String(item || "").trim()) : [];
  const active = Number.isInteger(payload.active) ? payload.active : Number.parseInt(payload.active || "0", 10);
  const maxSlots = Number.parseInt(env.MAX_SLOTS || "6", 10);
  if (device !== "pico" && device !== "picofx") {
    throw httpError(400, "invalid device");
  }
  if (!Number.isInteger(active) || active < 0 || active >= Math.max(slots.length, 1)) {
    throw httpError(400, "invalid active slot");
  }
  if (slots.length < 1 || slots.length > maxSlots) {
    throw httpError(400, `slots must contain 1 to ${maxSlots} entries`);
  }
  const appsById = new Map(manifest.apps.map((item) => [item.id, item]));
  const selected = [];
  for (const appId of slots) {
    if (!appId) {
      selected.push("");
      continue;
    }
    const app = appsById.get(appId);
    if (!app) {
      throw httpError(400, `unknown app id: ${appId}`);
    }
    if (app.device !== device) {
      throw httpError(400, `${appId} belongs to ${app.device}, not ${device}`);
    }
    selected.push(appId);
  }
  if (!selected.some(Boolean)) {
    throw httpError(400, "select at least one app");
  }
  return { device, slots: selected, active };
}

async function dispatchWorkflow(env, requestId, build, sampleKey = "") {
  const owner = env.GITHUB_OWNER;
  const repo = env.GITHUB_REPO;
  const workflow = env.GITHUB_WORKFLOW;
  const response = await fetch(
    `https://api.github.com/repos/${owner}/${repo}/actions/workflows/${workflow}/dispatches`,
    {
      method: "POST",
      headers: {
        "Accept": "application/vnd.github+json",
        "Authorization": `Bearer ${env.GITHUB_TOKEN}`,
        "Content-Type": "application/json",
        "User-Agent": "pico-eurorack-api",
        "X-GitHub-Api-Version": "2022-11-28"
      },
      body: JSON.stringify({
        ref: env.GITHUB_REF || "main",
        inputs: {
          request_id: requestId,
          device: build.device,
          slots: build.slots.join(","),
          active: String(build.active),
          sample_key: sampleKey
        }
      })
    }
  );
  if (response.status !== 204) {
    const text = await response.text();
    throw httpError(502, `workflow dispatch failed: ${response.status} ${text.slice(0, 240)}`);
  }
}

async function getSamples(url, env) {
  const app = url.searchParams.get("app") || "";
  if (app === "GridsSampler") {
    return {
      app,
      root: "Sketches/Pico/GridsSampler/Samples",
      banks: [],
      libraryFiles: [],
      wavs: [],
      bankless: true,
      cloud: true
    };
  }
  if (app === "OneshotSampler") {
    return {
      app,
      root: "Sketches/Pico/OneshotSampler/Samples",
      banks: [],
      bankless: false,
      cloud: true
    };
  }
  throw httpError(400, `sample app not supported: ${app}`);
}

async function uploadSamples(request, env) {
  const form = await request.formData();
  const app = String(form.get("app") || "").trim();
  const bank = safeSegment(String(form.get("bank") || "").trim() || "Custom");
  if (app !== "GridsSampler" && app !== "OneshotSampler") {
    throw httpError(400, `sample app not supported: ${app}`);
  }
  const files = form.getAll("files").filter((item) => isUploadFile(item));
  if (!files.length) {
    throw httpError(400, "no .wav files uploaded");
  }

  const uploadId = crypto.randomUUID();
  const saved = [];
  const uploadFiles = [];
  for (const file of files) {
    const filename = safeFilename(file.name);
    const key = `samples/uploads/${uploadId}/${app}/${app === "GridsSampler" ? "" : `${bank}/`}${filename}`;
    await env.BUILDS.put(key, file.stream(), {
      httpMetadata: {
        contentType: file.type || "audio/wav"
      }
    });
    saved.push(filename);
    uploadFiles.push({ name: filename, key, bytes: file.size });
  }

  const sampleKey = `samples/uploads/${uploadId}/manifest.json`;
  const manifest = {
    version: 1,
    id: uploadId,
    createdAt: new Date().toISOString(),
    uploads: [
      {
        app,
        bank: app === "GridsSampler" ? "" : bank,
        files: uploadFiles
      }
    ]
  };
  await putJson(env, sampleKey, manifest);

  if (app === "GridsSampler") {
    return {
      app,
      root: "Sketches/Pico/GridsSampler/Samples",
      banks: [],
      libraryFiles: uploadFiles.map((item) => ({ name: item.name, bytes: item.bytes })),
      wavs: uploadFiles.map((item) => ({ name: item.name, bytes: item.bytes })),
      bankless: true,
      cloud: true,
      sampleKey,
      saved,
      bank: ""
    };
  }

  return {
    app,
    root: "Sketches/Pico/OneshotSampler/Samples",
    banks: [
      {
        name: bank,
        path: `Sketches/Pico/OneshotSampler/Samples/${bank}`,
        count: uploadFiles.length,
        bytes: uploadFiles.reduce((total, item) => total + item.bytes, 0),
        samples: uploadFiles.map((item) => ({ name: item.name, bytes: item.bytes }))
      }
    ],
    bankless: false,
    cloud: true,
    sampleKey,
    saved,
    bank
  };
}

async function prepareBuildSamples(payload, build, env) {
  const sampleKeys = payload.sampleKeys && typeof payload.sampleKeys === "object" ? payload.sampleKeys : {};
  const selected = new Set(build.slots.filter(Boolean));
  const keys = [];
  for (const app of ["GridsSampler", "OneshotSampler"]) {
    const rawKeys = Array.isArray(sampleKeys[app]) ? sampleKeys[app] : [sampleKeys[app]];
    if (!selected.has(app)) {
      continue;
    }
    for (const rawKey of rawKeys) {
      const key = String(rawKey || "").trim();
      if (key) {
        keys.push(key);
      }
    }
  }
  if (!keys.length) {
    return "";
  }

  const uploads = [];
  for (const key of keys) {
    if (!key.startsWith("samples/uploads/") || !key.endsWith("/manifest.json")) {
      throw httpError(400, "invalid sample key");
    }
    const object = await env.BUILDS.get(key);
    if (!object) {
      throw httpError(404, "sample upload not found");
    }
    const manifest = await object.json();
    if (Array.isArray(manifest.uploads)) {
      uploads.push(...manifest.uploads);
    }
  }

  if (!uploads.length) {
    return "";
  }
  const buildKey = `samples/builds/${crypto.randomUUID()}/manifest.json`;
  await putJson(env, buildKey, {
    version: 1,
    createdAt: new Date().toISOString(),
    uploads
  });
  return buildKey;
}

async function getGenerateStatus(url, env) {
  const id = requiredId(url);
  const record = await getRequestRecord(env, id);
  const output = await findUf2Object(env, id);
  if (output) {
    return {
      id,
      status: "done",
      progress: 100,
      message: "Generated"
    };
  }

  const ageSeconds = (Date.now() - Date.parse(record.createdAt)) / 1000;
  const maxAge = Number.parseInt(env.MAX_REQUEST_AGE_SECONDS || "5400", 10);
  if (ageSeconds > maxAge) {
    return {
      id,
      status: "error",
      progress: 0,
      message: "Build timed out",
      error: "Build timed out before the UF2 appeared in R2"
    };
  }
  if (ageSeconds < 20) {
    return {
      id,
      status: "queued",
      progress: 5,
      message: "Queued in GitHub Actions"
    };
  }
  const progress = Math.min(95, 10 + Math.floor(ageSeconds / 12));
  return {
    id,
    status: "running",
    progress,
    message: "GitHub Actions build running"
  };
}

async function downloadGenerateOutput(url, env, corsHeaders) {
  const id = requiredId(url);
  await getRequestRecord(env, id);
  const object = await findUf2Object(env, id);
  if (!object) {
    throw httpError(404, "UF2 is not ready");
  }
  const filename = object.key.split("/").pop() || `Pico-Eurorack-${id}.uf2`;
  return new Response(object.body, {
    headers: {
      ...corsHeaders,
      "Content-Type": "application/octet-stream",
      "Content-Disposition": `attachment; filename="${filename}"`
    }
  });
}

function requiredId(url) {
  const id = url.searchParams.get("id") || "";
  if (!/^[0-9A-Za-z_.-]{8,80}$/.test(id)) {
    throw httpError(400, "invalid id");
  }
  return id;
}

async function getRequestRecord(env, id) {
  const object = await env.BUILDS.get(requestKey(id));
  if (!object) {
    throw httpError(404, "unknown build request");
  }
  return await object.json();
}

async function findUf2Object(env, id) {
  const prefix = `outputs/${id}/`;
  const listed = await env.BUILDS.list({ prefix, limit: 20 });
  const hit = listed.objects.find((item) => item.key.toLowerCase().endsWith(".uf2"));
  if (!hit) {
    return null;
  }
  return await env.BUILDS.get(hit.key);
}

async function putJson(env, key, value) {
  await env.BUILDS.put(key, JSON.stringify(value, null, 2), {
    httpMetadata: {
      contentType: "application/json; charset=utf-8"
    }
  });
}

function requestKey(id) {
  return `requests/${id}.json`;
}

function safeSegment(value) {
  return value.replace(/[^A-Za-z0-9_. -]+/g, "_").trim().replace(/^[ ._-]+|[ ._-]+$/g, "") || "Samples";
}

function safeFilename(value) {
  const name = value.split(/[\\/]/).pop() || "";
  const dot = name.lastIndexOf(".");
  const stem = safeSegment(dot >= 0 ? name.slice(0, dot) : name);
  const suffix = dot >= 0 ? name.slice(dot).toLowerCase() : "";
  if (suffix !== ".wav") {
    throw httpError(400, "only .wav files are supported");
  }
  return `${stem}.wav`;
}

function isUploadFile(value) {
  return (
    value &&
    typeof value === "object" &&
    typeof value.name === "string" &&
    typeof value.stream === "function"
  );
}
