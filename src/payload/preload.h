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

  // Auth-shaped endpoints get a synthetic, fully-populated response instead of
  // a merged one — the client tends to read deep paths (data.session.accessToken)
  // that a shallow merge cannot satisfy.
  const isAuthUrl = (u) => /(?:login|signin|sign_in|auth|token|session|whoami|me\b|refresh|check.?key|hwid|verify|entitle|subscri|premium|whitelist|user|profile)/i.test(u);

  // voltbz.net is entirely an internal API (auth + entitlements + script hub).
  // Everything we hit against it gets the same synthetic answer.
  const origFetch = window.fetch.bind(window);
  window.fetch = async function(input, init) {
    const url = (typeof input === "string") ? input : (input && input.url) || "";
    if (!targeted(url)) return origFetch(input, init);
    console.warn("[vbz] fetch intercept ->", url);
    return okJson(buildFakeResponse());
  };

  const XOpen = XMLHttpRequest.prototype.open;
  const XSend = XMLHttpRequest.prototype.send;
  XMLHttpRequest.prototype.open = function(m, u, ...r) { this.__vbz_url = u; this.__vbz_method = m; return XOpen.call(this, m, u, ...r); };
  XMLHttpRequest.prototype.send = function(body) {
    const url = this.__vbz_url;
    if (!url || !targeted(url)) return XSend.call(this, body);
    const xhr = this;
    console.warn("[vbz] xhr intercept ->", url);
    xhr.addEventListener("readystatechange", function() {
      if (xhr.readyState !== 4) return;
      const mutated = JSON.stringify(buildFakeResponse());
      try { Object.defineProperty(xhr, "responseText", { get: () => mutated, configurable: true }); } catch {}
      try { Object.defineProperty(xhr, "response",     { get: () => mutated, configurable: true }); } catch {}
      try { Object.defineProperty(xhr, "status",       { get: () => 200,     configurable: true }); } catch {}
      try { Object.defineProperty(xhr, "statusText",   { get: () => "OK",    configurable: true }); } catch {}
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
        // Tauri http plugin -> check URL inside args
        if (/plugin:http/i.test(cmdS)) {
          const url = args?.url || args?.request?.url || "";
          if (typeof url === "string" && targeted(url)) {
            console.warn("[vbz] invoke http intercept ->", cmdS, url);
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
          console.warn("[vbz] invoke auth intercept ->", cmdS);
          try {
            const res = await orig(cmd, args, opts);
            // Even if backend responded, replace with our synthetic answer.
            return buildFakeResponse();
          } catch (e) {
            return buildFakeResponse();
          }
        }
        return orig(cmd, args, opts);
      };
      t.__vbz_wrapped = true;
      console.warn("[vbz] wrapped invoke on", t);
    }
  };
  wrapInvoke();
  for (const d of [50, 100, 200, 500, 1000, 2000, 5000]) setTimeout(wrapInvoke, d);
})();
)JS_PRELOAD";
