// Preload JavaScript injected into every Volt WebView2 page.
#pragma once

static const wchar_t* const kPreloadJS = LR"JS_PRELOAD(
(() => {
  if (globalThis.__vbz_installed) return;
  globalThis.__vbz_installed = true;

  const HOSTS = /(?:^|\.)(voltbz\.net|volt\.gg|volt\.com\.im|volt\.onl|voltapp\.[a-z.]+)$/i;
  const targeted = (u) => {
    try { return HOSTS.test(new URL(u, location.href).hostname); }
    catch { return false; }
  };

  const FLAGS = () => ({
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
    role: "premium", roles: ["premium","lifetime","admin"],
    tier: "lifetime", plan: "lifetime", type: "premium",
    entitlements: ["premium","lifetime","all"],
    expires_at: 9999999999, expiresAt: 9999999999,
    subscription: { active: true, status: "active", tier: "lifetime", plan: "lifetime", expires_at: 9999999999, cancelled: false },
    license: { valid: true, tier: "lifetime", expires_at: 9999999999 },
    limits: { max: 999999, used: 0 },
    error: null, errors: null, message: null,
    token: "vbz_bypass_" + Math.random().toString(36).slice(2),
    accessToken: "vbz_bypass_" + Math.random().toString(36).slice(2),
    access_token: "vbz_bypass_" + Math.random().toString(36).slice(2),
    session: { id: "vbz_bypass_session", active: true, expires_at: 9999999999 },
    user: { id: "vbz_bypass_user", username: "vbz_user", email: "vbz@bypass.local", premium: true, tier: "lifetime", verified: true, banned: false },
  });

  const enrich = (v, depth = 0) => {
    if (depth > 6 || v == null) return v;
    if (Array.isArray(v)) return v.map(x => enrich(x, depth + 1));
    if (typeof v === "object") {
      const f = FLAGS();
      for (const k of Object.keys(f)) if (!(k in v)) v[k] = f[k];
      for (const k of Object.keys(v)) if (v[k] && typeof v[k] === "object") v[k] = enrich(v[k], depth + 1);
      return v;
    }
    return v;
  };

  const okJson = (obj) => new Response(JSON.stringify(obj), {
    status: 200, statusText: "OK",
    headers: { "content-type": "application/json; charset=utf-8" },
  });

  const origFetch = window.fetch.bind(window);
  window.fetch = async function(input, init) {
    const url = (typeof input === "string") ? input : (input && input.url) || "";
    if (!targeted(url)) return origFetch(input, init);
    let res;
    try { res = await origFetch(input, init); }
    catch (e) { return okJson(enrich({}, 0)); }
    if (res.status === 401 || res.status === 403 || res.status === 402 || res.status === 404) {
      return okJson(enrich({}, 0));
    }
    const ct = (res.headers.get("content-type") || "").toLowerCase();
    if (!ct.includes("json")) return res;
    let data;
    try { data = await res.clone().json(); } catch { return res; }
    if (data == null || typeof data !== "object") return okJson(enrich({}, 0));
    return okJson(enrich(data, 0));
  };

  const XOpen = XMLHttpRequest.prototype.open;
  const XSend = XMLHttpRequest.prototype.send;
  XMLHttpRequest.prototype.open = function(m, u, ...r) { this.__vbz_url = u; this.__vbz_method = m; return XOpen.call(this, m, u, ...r); };
  XMLHttpRequest.prototype.send = function(body) {
    const url = this.__vbz_url;
    if (!url || !targeted(url)) return XSend.call(this, body);
    const xhr = this;
    xhr.addEventListener("readystatechange", function() {
      if (xhr.readyState !== 4) return;
      let text = ""; try { text = xhr.responseText || ""; } catch {}
      let mutated = null;
      if (xhr.status === 401 || xhr.status === 403 || xhr.status === 402 || xhr.status === 404) {
        mutated = JSON.stringify(enrich({}, 0));
      } else {
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

  // Tauri v2 IPC bridge: some apps route auth through invoke() rather than fetch.
  // Best-effort wrap: if window.__TAURI__ or window.__TAURI_INTERNALS__.invoke exists,
  // intercept auth-shaped commands and return synthetic success.
  const wrapInvoke = () => {
    const targets = [window.__TAURI__?.core, window.__TAURI__, window.__TAURI_INTERNALS__];
    for (const t of targets) {
      if (!t || !t.invoke || t.__vbz_wrapped) continue;
      const orig = t.invoke.bind(t);
      t.invoke = async (cmd, args, opts) => {
        try {
          const res = await orig(cmd, args, opts);
          if (typeof cmd === "string" && /login|auth|token|whitelist|premium|verify|session|check_key|hwid/i.test(cmd)) {
            return enrich(typeof res === "object" && res !== null ? res : {}, 0);
          }
          return res;
        } catch (e) {
          if (typeof cmd === "string" && /login|auth|token|whitelist|premium|verify|session|check_key|hwid/i.test(cmd)) {
            return enrich({}, 0);
          }
          throw e;
        }
      };
      t.__vbz_wrapped = true;
    }
  };
  wrapInvoke();
  setTimeout(wrapInvoke, 100);
  setTimeout(wrapInvoke, 500);
  setTimeout(wrapInvoke, 2000);
})();
)JS_PRELOAD";
