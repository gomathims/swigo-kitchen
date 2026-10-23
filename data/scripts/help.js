// Portal help system.
//
// All help text comes from /help_en.json, served by this device itself. That
// keeps it working in AP mode with no internet, in a plain browser, and on a
// factory-fresh unit. The portal makes no cloud request of its own.
//
// An entry may carry an optional "Learn more" URL in `l`. It renders as a
// normal link; the app intercepts cross-origin navigation and opens it in the
// system browser.
//
// In the app's embedded webview it is always shown, because the system browser
// it opens in has the phone's own connectivity regardless of what network the
// webview is on. In a plain browser it is shown only when the device itself has
// an uplink - script.js feeds setHelpNetworkState() from the wMode field of the
// WebSocket status - because in AP mode the phone is joined to the device's own
// network with no route out and the link would just fail. The app can veto
// either way by setting window.__EVIQ_ONLINE__ to false.
//
// The EVIQtech app may still override entries by setting window.__EVIQ_HELP__
// and firing "eviq-help-ready" - that hook is the app-side half of a fixed
// contract and stays wired up, but nothing is fetched unless the app pushes it.
//
// Ids in data-help are a contract. Once shipped, do not rename or remove one.

(function () {
  var builtinHelp = null;      // from /help_en.json
  var panel = null;            // the one shared help panel
  var openId = null;           // id currently displayed, or null
  var openIcon = null;         // the icon it is anchored to
  var deviceOnline = false;    // device has a WiFi/Ethernet uplink (not AP mode)

  // ── Resolution ───────────────────────────────────────────────────────────
  function helpFor(id) {
    var injected = window.__EVIQ_HELP__ && window.__EVIQ_HELP__[id];
    return injected || (builtinHelp && builtinHelp[id]);
  }

  // `l` holds a path relative to the catalog's own `_base`, so the manual's
  // URL is configured in one place. An absolute URL is still honoured as an
  // escape hatch. The base comes from whichever catalog the entry came from,
  // so an app-injected one can point somewhere else entirely.
  function resolveLink(id, entry) {
    if (!entry || !entry.l) return null;
    if (/^https?:\/\//i.test(entry.l)) return entry.l;
    var source = (window.__EVIQ_HELP__ && window.__EVIQ_HELP__[id])
      ? window.__EVIQ_HELP__
      : builtinHelp;
    var base = source && source._base;
    if (!base) return null;          // relative path with no base: nothing safe to link to
    return base.replace(/\/+$/, "") + "/" + String(entry.l).replace(/^\/+/, "");
  }

  // Inside the app's webview the link never travels over the device's network:
  // the app intercepts cross-origin navigation and hands it to the system
  // browser, which uses the phone's own connectivity. So it works even while
  // the phone is joined to the device's AP and the device has no uplink of its
  // own. common.js stamps this class on <html> whenever ?app=true has been seen
  // in the session, and every page that loads help.js loads common.js first.
  function appEmbedded() {
    return document.documentElement.classList.contains("app-embedded");
  }

  // External links only make sense when something in the path can reach the
  // internet. Outside the app that has to be the device itself, and deviceOnline
  // starts false: assume no uplink until the device tells us otherwise.
  function externalLinksUsable() {
    if (window.__EVIQ_ONLINE__ === false) return false;
    return appEmbedded() || deviceOnline;
  }

  // Called by script.js on every status update from the device.
  function setHelpNetworkState(online) {
    online = !!online;
    if (online === deviceOnline) return;
    deviceOnline = online;
    refreshOpenHelpPanel();
  }

  // ── Rendering ────────────────────────────────────────────────────────────
  // Help bodies may contain <br> for paragraph breaks. Render them as text
  // nodes split on <br> rather than through innerHTML, so an app-injected
  // catalog cannot smuggle markup in.
  function appendBody(target, text) {
    var parts = String(text).split(/<br\s*\/?>/i);
    for (var i = 0; i < parts.length; i++) {
      if (i > 0) target.appendChild(_ce("br"));
      target.appendChild(document.createTextNode(parts[i].trim()));
    }
  }

  function ensurePanel() {
    if (panel) return panel;
    panel = _ce("div");
    panel.id = "eviq-help-panel";
    panel.setAttribute("role", "dialog");
    panel.style.display = "none";
    panel.addEventListener("click", function (event) {
      event.stopPropagation();     // clicks inside must not dismiss the panel
    });
    document.body.appendChild(panel);
    return panel;
  }

  function renderPanel(id) {
    var p = ensurePanel();
    var entry = helpFor(id);
    p.innerHTML = "";

    var title = _ce("div");
    title.className = "eviq-help-title";
    title.textContent = entry && entry.t ? entry.t : "Help";
    p.appendChild(title);

    var body = _ce("div");
    body.className = "eviq-help-body";
    if (entry && entry.s) {
      appendBody(body, entry.s);
    } else if (!builtinHelp) {
      body.textContent = "Loading help...";
    } else {
      body.textContent = "No help available for this item.";
    }
    p.appendChild(body);

    // "Learn more" link, built from the entry's `l` path and the catalog's
    // `_base`. Only rendered while the device has an uplink - see
    // externalLinksUsable().
    var href = externalLinksUsable() ? resolveLink(id, entry) : null;
    if (href) {
      var link = _ce("a");
      link.className = "eviq-help-more";
      link.href = href;
      link.target = "_blank";
      link.rel = "noopener";
      link.textContent = "Learn more";
      p.appendChild(link);
    }
  }

  // The panel is position:fixed, so these are viewport coordinates. That also
  // keeps it anchored correctly to icons inside the fixed page header.
  function positionPanel(icon) {
    var p = ensurePanel();
    var rect = icon.getBoundingClientRect();
    var margin = 8;

    p.style.display = "block";
    p.style.left = "0px";                 // measure at a known origin first
    p.style.top = "0px";

    var width = p.offsetWidth;
    var height = p.offsetHeight;

    // Prefer below and right-aligned to the icon, then clamp to the viewport.
    var left = rect.right - width;
    if (left + width > window.innerWidth - margin) {
      left = window.innerWidth - margin - width;
    }
    if (left < margin) left = margin;

    var top = rect.bottom + 6;
    if (top + height > window.innerHeight - margin) {
      var above = rect.top - height - 6;
      top = above > margin ? above : Math.max(margin, window.innerHeight - margin - height);
    }

    p.style.left = left + "px";
    p.style.top = top + "px";
  }

  function openHelp(icon) {
    var id = icon.getAttribute("data-help");
    if (!id) return;
    openId = id;
    openIcon = icon;
    renderPanel(id);
    positionPanel(icon);
  }

  function closeHelp() {
    openId = null;
    openIcon = null;
    if (panel) panel.style.display = "none";
  }

  // Called when the catalog loads, or when the app injects an override, after
  // the panel is already open - the text updates in place.
  function refreshOpenHelpPanel() {
    if (!openId || !panel || panel.style.display === "none") return;
    renderPanel(openId);
    if (openIcon) positionPanel(openIcon);
  }

  // ── Wiring ───────────────────────────────────────────────────────────────
  // Delegated, so help icons cloned at runtime (schedule accordions) work too.
  document.addEventListener("click", function (event) {
    var icon = event.target.closest && event.target.closest(".help-i");
    if (icon) {
      event.preventDefault();
      event.stopPropagation();
      // Compare the element, not the id: the same id appears on every cloned
      // schedule card, and tapping a different card should re-anchor the panel.
      if (openIcon === icon && panel && panel.style.display === "block") {
        closeHelp();
      } else {
        openHelp(icon);
      }
      return;
    }
    closeHelp();
  });

  document.addEventListener("keydown", function (event) {
    if (event.key === "Escape") closeHelp();
  });

  window.addEventListener("resize", closeHelp);
  window.addEventListener("scroll", closeHelp, { passive: true });

  // ── Catalog load ─────────────────────────────────────────────────────────
  function loadBuiltinHelp() {
    // ##VERSION## is replaced at build time. The catalog is served with a
    // one-year max-age, so the version in the URL is what brings a new one
    // down after an update.
    return fetch("/help_en.json?##VERSION##", { credentials: "same-origin" })
      .then(function (response) {
        if (!response.ok) throw new Error("help " + response.status);
        return response.json();
      })
      .then(function (data) {
        builtinHelp = data;
        refreshOpenHelpPanel();
      })
      .catch(function (error) {
        console.log("Built-in help unavailable:", error);
        builtinHelp = {};
      });
  }

  // The app may set window.__EVIQ_HELP__ before this script runs or shortly
  // after, so handle both: helpFor() reads the global on every lookup, and the
  // event repaints an already-open panel.
  window.addEventListener("eviq-help-ready", function () {
    refreshOpenHelpPanel();
  });

  document.addEventListener("DOMContentLoaded", function () {
    loadBuiltinHelp();
  });

  // Exposed for the cloud device-control widget and for debugging.
  window.helpFor = helpFor;
  window.refreshOpenHelpPanel = refreshOpenHelpPanel;
  window.setHelpNetworkState = setHelpNetworkState;
})();
