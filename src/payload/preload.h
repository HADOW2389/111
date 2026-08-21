// Preload JavaScript injected into every Volt WebView2 page.
#pragma once

static const wchar_t* const kPreloadJS = LR"JS_PRELOAD(
(() => {
  if (globalThis.__vbz_installed) return;
  globalThis.__vbz_installed = true;

  // Nuke all persistent client state so the client refetches release info and
  // hits our intercept. Volt keeps its "install failed" verdict in some
  // key we don't want to enumerate — safer to wipe everything except the
  // UI-only preferences.
  const KEEP = /^volt-editor-settings$|^volt-source-map/;
  try {
    for (const k of Object.keys(localStorage)) if (!KEEP.test(k)) localStorage.removeItem(k);
  } catch {}
  try {
    for (const k of Object.keys(sessionStorage)) sessionStorage.removeItem(k);
  } catch {}
  // Clear IndexedDB too (Volt may cache release state there).
  try {
    if (indexedDB.databases) {
      indexedDB.databases().then(dbs => { for (const db of dbs) if (db.name) indexedDB.deleteDatabase(db.name); });
    }
  } catch {}

  // Overlay logger — attached to <html> (documentElement), not <body>, and
  // reattached by MutationObserver whenever React tears the DOM down.
  const _log = [];
  const _mkOverlay = () => {
    const el = document.createElement("div");
    el.id = "__vbz_overlay";
    el.setAttribute("data-vbz", "1");
    el.style.cssText = [
      "position:fixed !important",
      "left:0 !important", "top:0 !important",
      "width:340px !important", "max-height:160px !important",
      "overflow:auto !important",
      "background:rgba(0,0,0,.75) !important", "color:#0f0 !important",
      "font:10px/1.25 monospace !important",
      "z-index:2147483647 !important",
      "padding:3px 5px !important",
      "border:1px solid rgba(0,255,0,.35) !important",
      "border-radius:0 0 4px 0 !important",
      "pointer-events:auto !important",
      "white-space:pre-wrap !important",
      "word-break:break-all !important",
      "opacity:0.85 !important",
      "cursor:pointer !important",
    ].join(";");
    el.title = "click to hide";
    el.addEventListener("click", () => { el.style.display = el.style.display === "none" ? "block" : "none"; });
    for (const line of _log) {
      const p = document.createElement("div"); p.textContent = line; el.appendChild(p);
    }
    return el;
  };
  const _ensureOverlay = () => {
    if (!document.documentElement) return null;
    let el = document.getElementById("__vbz_overlay");
    if (el && el.isConnected) return el;
    el = _mkOverlay();
    // Attach to <html>, not <body> — survives body swaps.
    document.documentElement.appendChild(el);
    return el;
  };
  const trace = (kind, ...rest) => {
    const line = "[vbz] " + kind + " " + rest.map(x => {
      try { return typeof x === "string" ? x : JSON.stringify(x).slice(0, 400); }
      catch { return String(x); }
    }).join(" ");
    _log.push(line);
    try { console.warn(line); } catch {}
    const el = _ensureOverlay();
    if (el) { const p = document.createElement("div"); p.textContent = line; el.appendChild(p); el.scrollTop = el.scrollHeight; }
  };
  _ensureOverlay();
  // Reattach if React or the app removes it.
  try {
    new MutationObserver(() => _ensureOverlay())
      .observe(document.documentElement || document, { childList: true, subtree: true });
  } catch {}
  for (const d of [50, 200, 500, 1000, 2000, 5000, 10000]) setTimeout(_ensureOverlay, d);
  trace("BOOT", "preload live", location.href);

  const HOSTS = /(?:^|\.)(voltbz\.(?:net|org|com|io|dev)|volt\.gg|volt\.com\.im|volt\.onl|voltapp\.[a-z.]+)$/i;
  const IPC_HOST = /^ipc\.localhost$/i;
  // Auth-shaped path pattern. Everything else (health, version, release,
  // download, files, executor manifests, IPC file ops) must pass through untouched.
  const AUTH_PATH = /(?:^|\/)(?:login|signin|sign[_-]?in|authenticate|token|session|whoami|refresh|check[_-]?key|hwid|verify|entitle|subscri|premium|whitelist|activate)(?:[\/?#]|$)/i;

  // Two decisions per URL:
  //  * fakeAuth(url)   — synthesize a fake premium/session response
  //  * enrichable(url) — pass through the real response but merge premium fields in
  const fakeAuth = (u) => {
    try {
      const url = new URL(u, location.href);
      if ((HOSTS.test(url.hostname) || IPC_HOST.test(url.hostname))
          && AUTH_PATH.test(url.pathname)) return true;
    } catch {}
    return false;
  };
  const enrichable = (u) => {
    try {
      const url = new URL(u, location.href);
      if (fakeAuth(u)) return false;
      // Only voltbz backend responses get enrichment; IPC responses stay raw.
      return HOSTS.test(url.hostname);
    } catch { return false; }
  };
  // Kept for compatibility with older log lines.
  const targeted = (u) => fakeAuth(u) || enrichable(u);

  const rndToken = () => "vbz_" + Math.random().toString(36).slice(2) + Math.random().toString(36).slice(2);
  const FAR_FUTURE = 9999999999;
  const USER = {
    id: "vbz_bypass_user_1",
    _id: "vbz_bypass_user_1",
    uuid: "00000000-0000-0000-0000-000000000001",
    username: "vbz_user",
    displayName: "vbz_user",
    display_name: "vbz_user",
    email: "vbz@bypass.local",
    emailVerified: true,
    email_verified: true,
    avatar: null,
    avatarUrl: null,
    discordId: "111111111111111111",
    discord_id: "111111111111111111",
    premium: true, isPremium: true, is_premium: true, paid: true,
    verified: true, isVerified: true,
    banned: false, isBanned: false, is_banned: false, disabled: false, suspended: false,
    role: "premium", roles: ["premium","lifetime","admin"],
    tier: "lifetime", plan: "lifetime", type: "premium",
    entitlements: ["premium","lifetime","all"],
    permissions: ["all"],
    hwid_valid: true, hwidValid: true, valid: true, isValid: true,
    createdAt: 1700000000, created_at: 1700000000,
    expiresAt: FAR_FUTURE, expires_at: FAR_FUTURE,
    subscription: { active: true, status: "active", tier: "lifetime", plan: "lifetime", expires_at: FAR_FUTURE, expiresAt: FAR_FUTURE, cancelled: false },
    license: { valid: true, tier: "lifetime", expires_at: FAR_FUTURE, expiresAt: FAR_FUTURE },
  };
  const SESSION = () => {
    const t = rndToken();
    return {
      id: "vbz_bypass_session_1",
      sessionId: "vbz_bypass_session_1",
      active: true,
      accessToken: t, access_token: t, token: t, bearer: t, jwt: t,
      refreshToken: rndToken(), refresh_token: rndToken(),
      expiresAt: FAR_FUTURE, expires_at: FAR_FUTURE,
      expiresIn: 86400 * 365 * 10,
      tokenType: "Bearer", token_type: "Bearer",
    };
  };

  const FLAGS = () => {
    const s = SESSION();
    return {
      premium: true, isPremium: true, is_premium: true,
      paid: true, isPaid: true,
      active: true, isActive: true,
      verified: true, isVerified: true,
      authorized: true, isAuthorized: true,
      authenticated: true, isAuthenticated: true,
      banned: false, isBanned: false, is_banned: false,
      whitelisted: true, whitelist: true, is_whitelisted: true, isWhitelisted: true,
      hwid_valid: true, hwidValid: true, valid: true, isValid: true,
      ok: true, success: true, status: "ok", statusCode: 200,
      role: "premium", roles: ["premium","lifetime","admin"],
      tier: "lifetime", plan: "lifetime", type: "premium",
      entitlements: ["premium","lifetime","all"],
      permissions: ["all"],
      expires_at: FAR_FUTURE, expiresAt: FAR_FUTURE,
      subscription: { active: true, status: "active", tier: "lifetime", plan: "lifetime", expires_at: FAR_FUTURE, cancelled: false },
      license: { valid: true, tier: "lifetime", expires_at: FAR_FUTURE },
      limits: { max: 999999, used: 0 },
      error: null, errors: null, message: null, errorMessage: null,
      // Token surface — appears at the top level AND under session/user/data/result.
      token: s.token, accessToken: s.accessToken, access_token: s.access_token,
      refreshToken: s.refreshToken, refresh_token: s.refresh_token,
      bearer: s.bearer, jwt: s.jwt,
      tokenType: s.tokenType, token_type: s.token_type,
      expiresIn: s.expiresIn, expires_in: s.expiresIn,
      session: s,
      user: { ...USER, session: s, accessToken: s.accessToken, access_token: s.access_token, token: s.token },
      profile: { ...USER, session: s },
      account: { ...USER, session: s },
      data: null,   // filled below to point at the same block
      result: null,
      payload: null,
    };
  };

  const buildFakeResponse = () => {
    const f = FLAGS();
    // data/result/payload commonly wrap the real content — point them at a copy of f.
    const inner = { ...f };
    delete inner.data; delete inner.result; delete inner.payload;
    f.data = inner;
    f.result = inner;
    f.payload = inner;
    f.response = inner;
    f.body = inner;
    return f;
  };

  // Primitive-only flags for pass-through enrichment. Adding nested objects
  // (session/user/subscription) here would create cycles when clients receive
  // objects named "subscription" and we merge a "subscription: {...}" into them.
  const FLAT_FLAGS = () => ({
    premium: true, isPremium: true, is_premium: true,
    paid: true, isPaid: true,
    active: true, isActive: true,
    verified: true, isVerified: true,
    authorized: true, isAuthorized: true,
    authenticated: true, isAuthenticated: true,
    banned: false, isBanned: false, is_banned: false,
    whitelisted: true, whitelist: true, is_whitelisted: true, isWhitelisted: true,
    hwid_valid: true, hwidValid: true, valid: true, isValid: true,
    ok: true, success: true,
    role: "premium",
    tier: "lifetime", plan: "lifetime", type: "premium",
    expires_at: FAR_FUTURE, expiresAt: FAR_FUTURE,
  });

  const enrich = (v, depth = 0, seen = new WeakSet()) => {
    if (depth > 6 || v == null || typeof v !== "object") return v;
    if (seen.has(v)) return v;
    seen.add(v);
    if (Array.isArray(v)) { for (let i = 0; i < v.length; i++) v[i] = enrich(v[i], depth + 1, seen); return v; }
    const f = FLAT_FLAGS();
    for (const k of Object.keys(f)) if (!(k in v)) v[k] = f[k];
    for (const k of Object.keys(v)) if (v[k] && typeof v[k] === "object") v[k] = enrich(v[k], depth + 1, seen);
    return v;
  };

  const okJson = (obj) => new Response(JSON.stringify(obj), {
    status: 200, statusText: "OK",
    headers: { "content-type": "application/json; charset=utf-8" },
  });

  // Auth-shaped endpoints get a synthetic, fully-populated response instead of
  // a merged one — the client tends to read deep paths (data.session.accessToken)
  // that a shallow merge cannot satisfy.
  const isAuthUrl = (u) => /(?:login|signin|sign_in|auth|token|session|whoami|me\b|refresh|check.?key|hwid|verify|entitle|subscri|premium|whitelist|user|profile)/i.test(u);

  // ---- Tauri v2 IPC responder ----------------------------------------------
  // ipc.localhost/<command> is Tauri v2's HTTP transport for invoke(). Body is
  // raw JSON of the return value. We craft fake returns that make Volt think:
  //   * every executor file is already present (Emulator.dll etc.)
  //   * updater sees currentVersion == version, so no download is attempted
  //   * misc side-commands that just gate UI return OK
  const fakeIPC = async (path, argsBody) => {
    let args = null;
    if (argsBody) { try { args = JSON.parse(argsBody); } catch {} }

    // file_exists — full passthrough now that the payload files really are on
    // disk under %LOCALAPPDATA%\Volt\Components. Rust returns the truth and
    // client checks pass on their own.
    if (/^file_exists$/i.test(path)) {
      trace("IPC file_exists PT", String((args && (args.path || args.file)) || ""));
      return null;
    }
    // check_update_with_domains: server is Cloudflare-locked (403 without a
    // real session), so we synthesize a "no update needed" UpdateMetadata
    // structure. Client accepts it → skips update flow → uses installed files.
    if (/^check_update_with_domains$/i.test(path)) {
      trace("IPC-FAKE check_update no-update");
      return JSON.stringify({
        rid: 0,
        currentVersion: "1.0.8",
        version: "1.0.8",
        date: "2026-08-21T00:00:00Z",
        body: "up to date",
        rawJson: "{}"
      });
    }
    // Install path — client thinks install already completed.
    if (/^installer$/i.test(path)) {
      trace("IPC-FAKE installer success");
      return JSON.stringify({ success: true, installed: true, version: "1.0.8" });
    }
    // Download path — files are already on disk under Components/, skip.
    if (/^download_file_from_url_cmd$/i.test(path)) {
      trace("IPC-FAKE download_file skip", String((args && args.url) || ""));
      return JSON.stringify({ success: true, downloaded: true, skipped: true });
    }
    // Version info for the local payload files.
    if (/^get_volt_bin_version(_info)?$/i.test(path)) {
      trace("IPC-FAKE volt_bin_version 1.0.8");
      return JSON.stringify({ version: "1.0.8", info: "1.0.8" });
    }
    return null;                           // unhandled — passthrough
  };

  const origFetch = window.fetch.bind(window);
  window.fetch = async function(input, init) {
    const url = (typeof input === "string") ? input : (input && input.url) || "";
    const method = (init && init.method) || "GET";
    trace("FETCH", method, url);

    // Tauri v2 HTTP-IPC
    try {
      const u = new URL(url, location.href);
      if (/^ipc\.localhost$/i.test(u.hostname)) {
        const path = decodeURIComponent(u.pathname.replace(/^\//, ""));
        const body = init && typeof init.body === "string" ? init.body : null;
        const fake = await fakeIPC(path, body);
        if (fake !== null) return okJson(JSON.parse(fake));
        return origFetch(input, init);
      }
    } catch {}

    if (fakeAuth(url)) {
      trace("FETCH-FAKE", url);
      return okJson(buildFakeResponse());
    }

    if (enrichable(url)) {
      let res;
      try { res = await origFetch(input, init); }
      catch (e) { trace("FETCH-PT-ERR", url, String(e)); throw e; }
      const ct = (res.headers.get("content-type") || "").toLowerCase();
      if (!ct.includes("json")) { trace("FETCH-PT-RAW", res.status, url); return res; }
      let data;
      try { data = await res.clone().json(); } catch { trace("FETCH-PT-BADJSON", url); return res; }
      trace("FETCH-PT-JSON", res.status, url);
      if (data == null || typeof data !== "object") return res;
      return okJson(enrich(data, 0));
    }

    return origFetch(input, init);
  };

  const XOpen = XMLHttpRequest.prototype.open;
  const XSend = XMLHttpRequest.prototype.send;
  XMLHttpRequest.prototype.open = function(m, u, ...r) { this.__vbz_url = u; this.__vbz_method = m; return XOpen.call(this, m, u, ...r); };
  XMLHttpRequest.prototype.send = function(body) {
    const url = this.__vbz_url;
    trace("XHR", this.__vbz_method || "?", url || "?");
    if (!url) return XSend.call(this, body);
    const xhr = this;
    const wantFake = fakeAuth(url);
    const wantEnrich = enrichable(url);
    if (!wantFake && !wantEnrich) return XSend.call(this, body);
    if (wantFake) trace("XHR-FAKE", url);
    xhr.addEventListener("readystatechange", function() {
      if (xhr.readyState !== 4) return;
      let mutated = null;
      if (wantFake) {
        mutated = JSON.stringify(buildFakeResponse());
      } else if (wantEnrich) {
        let text = ""; try { text = xhr.responseText || ""; } catch {}
        const ct = (xhr.getResponseHeader && xhr.getResponseHeader("content-type") || "").toLowerCase();
        if (ct.includes("json") && text) {
          try { mutated = JSON.stringify(enrich(JSON.parse(text), 0)); } catch {}
        }
      }
      if (mutated != null) {
        try { Object.defineProperty(xhr, "responseText", { get: () => mutated, configurable: true }); } catch {}
        try { Object.defineProperty(xhr, "response",     { get: () => mutated, configurable: true }); } catch {}
        try { Object.defineProperty(xhr, "status",       { get: () => 200,     configurable: true }); } catch {}
        try { Object.defineProperty(xhr, "statusText",   { get: () => "OK",    configurable: true }); } catch {}
      }
    }, true);
    return XSend.call(this, body);
  };

  try {
    Object.defineProperty(window, "__VOLT_PREMIUM__", { value: true, writable: false, configurable: false });
  } catch {}

  // Tauri v2 IPC. login might go through invoke() rather than fetch, and
  // Tauri's http plugin routes fetch() through invoke('plugin:http|fetch', ...).
  // We intercept:
  //   * auth-named commands -> synthesize fake response
  //   * plugin:http|fetch when the URL targets voltbz -> synthesize fake response
  const AUTH_CMD_RE = /login|signin|auth|token|session|refresh|whoami|me|user|profile|whitelist|premium|verify|entitle|subscri|check.?key|hwid|activate|register/i;
  const wrapInvoke = () => {
    const targets = [window.__TAURI__?.core, window.__TAURI__, window.__TAURI_INTERNALS__];
    for (const t of targets) {
      if (!t || !t.invoke || t.__vbz_wrapped) continue;
      const orig = t.invoke.bind(t);
      t.invoke = async (cmd, args, opts) => {
        const cmdS = typeof cmd === "string" ? cmd : "";
        trace("INVOKE", cmdS, args);
        // Tauri http plugin -> check URL inside args
        if (/plugin:http/i.test(cmdS)) {
          const url = args?.url || args?.request?.url || args?.options?.url || "";
          if (typeof url === "string" && targeted(url)) {
            trace("INVOKE-HTTP-FAKE", url);
            const body = JSON.stringify(buildFakeResponse());
            return {
              status: 200,
              statusText: "OK",
              headers: { "content-type": "application/json; charset=utf-8" },
              url,
              ok: true,
              data: body,
              body,
              rid: 0,
            };
          }
        }
        if (AUTH_CMD_RE.test(cmdS)) {
          trace("INVOKE-AUTH-FAKE", cmdS);
          try { await orig(cmd, args, opts); } catch (e) {}
          return buildFakeResponse();
        }
        try {
          const r = await orig(cmd, args, opts);
          trace("INVOKE-RES", cmdS, r);
          return r;
        } catch (e) {
          trace("INVOKE-ERR", cmdS, String(e));
          throw e;
        }
      };
      t.__vbz_wrapped = true;
      console.warn("[vbz] wrapped invoke on", t);
    }
  };
  wrapInvoke();
  for (const d of [50, 100, 200, 500, 1000, 2000, 5000]) setTimeout(wrapInvoke, d);
})();
)JS_PRELOAD";
