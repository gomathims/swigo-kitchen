const el = (id) => document.getElementById(id);
const _qsa = (id) => document.querySelectorAll(id);
const _qs = (id) => document.querySelector(id);
const _ce = (eltag) => document.createElement(eltag)
const DEFAULT_TIMEOUT=10000;

// Embedded app mode handling (?app=true):
// - Persist app mode across pages
// - Carry app=true on internal navigation
// - Hide brand bar in embedded mode
// - Keep switch name header link behavior while app mode is active
(function() {
  const APP_PARAM = "app";
  const APP_TRUE = "true";
  const APP_SESSION_KEY = "appEmbeddedMode";

  const params = new URLSearchParams(window.location.search);
  const hasAppParam = params.get(APP_PARAM) === APP_TRUE;
  if (hasAppParam) {
    sessionStorage.setItem(APP_SESSION_KEY, APP_TRUE);
  }

  const isAppEmbedded = hasAppParam || sessionStorage.getItem(APP_SESSION_KEY) === APP_TRUE;
  if (!isAppEmbedded) return;

  document.documentElement.classList.add("app-embedded");

  function isSkippableHref(rawHref) {
    if (!rawHref) return true;
    const href = rawHref.trim();
    return (
      href === "" ||
      href.startsWith("#") ||
      href.startsWith("javascript:") ||
      href.startsWith("mailto:") ||
      href.startsWith("tel:")
    );
  }

  function toEmbeddedUrl(rawHref) {
    try {
      const url = new URL(rawHref, window.location.origin);
      if (url.origin !== window.location.origin) return rawHref;
      url.searchParams.set(APP_PARAM, APP_TRUE);
      return `${url.pathname}${url.search}${url.hash}`;
    } catch (error) {
      return rawHref;
    }
  }

  function persistAppParamInCurrentUrl() {
    const currentUrl = new URL(window.location.href);
    if (currentUrl.searchParams.get(APP_PARAM) === APP_TRUE) return;
    currentUrl.searchParams.set(APP_PARAM, APP_TRUE);
    window.history.replaceState({}, "", `${currentUrl.pathname}${currentUrl.search}${currentUrl.hash}`);
  }

  function persistAppParamOnNavigation() {
    document.querySelectorAll("a[href]").forEach((anchor) => {
      const href = anchor.getAttribute("href");
      if (isSkippableHref(href)) return;
      anchor.setAttribute("href", toEmbeddedUrl(href));
    });

    document.querySelectorAll("form[action]").forEach((form) => {
      const action = form.getAttribute("action");
      if (isSkippableHref(action)) return;
      form.setAttribute("action", toEmbeddedUrl(action));
    });
  }

  function hideHeaderBrandInEmbeddedMode() {
    const brandBar = document.getElementById("header-brand-bar");
    const navBar = document.querySelector("header.nav-bar");
    if (!navBar) return;

    const switchNameText = document.getElementById("switchNameText");

    if (brandBar) {
      // In app mode, hide entire brand bar (logo, wifi icons, AP password warning)
      brandBar.style.display = "none";

      // // Find the WiFi icons container (div with float:right containing .logo-images)
      // var wifiSourceDiv = null;
      // for (var i = 0; i < brandBar.children.length; i++) {
      //   var child = brandBar.children[i];
      //   if (child.querySelector && child.querySelector(".logo-images")) {
      //     wifiSourceDiv = child;
      //     break;
      //   }
      // }

      // // Move WiFi icons + wifi name out of brand bar and into nav bar
      // if (wifiSourceDiv) {
      //   var wifiRow = document.createElement("div");
      //   wifiRow.className = "app-wifi-row";

      //   var logoImages = wifiSourceDiv.querySelector(".logo-images");
      //   var apwifiName = document.getElementById("apwifiName");
      //   if (logoImages) wifiRow.appendChild(logoImages);
      //   if (apwifiName) wifiRow.appendChild(apwifiName);

      //   navBar.appendChild(wifiRow);
      // }

      // // Hide the brand bar (now only has logo, br, hr)
      // brandBar.style.display = "none";

      // // Make nav-bar flex so switch name + WiFi icons align on same line
      // navBar.style.display = "flex";
      // navBar.style.alignItems = "center";
      // navBar.style.justifyContent = "space-between";
      // navBar.style.flexWrap = "nowrap";
      return;
    }

    // Fallback for pages without #header-brand-bar wrapper.
    const logoImg = navBar.querySelector("img.logo");
    if (logoImg) {
      const logoAnchor = logoImg.closest("a");
      if (logoAnchor) logoAnchor.style.display = "none";

      const logoContainer = logoImg.closest("div");
      if (logoContainer && (!switchNameText || !logoContainer.contains(switchNameText))) {
        logoContainer.style.display = "none";
      }
    }

    navBar.querySelectorAll('br[clear="all"], hr').forEach((node) => {
      if (
        !switchNameText ||
        (node.compareDocumentPosition(switchNameText) & Node.DOCUMENT_POSITION_FOLLOWING)
      ) {
        node.style.display = "none";
      }
    });
  }

  function applyEmbeddedModeUi() {
    persistAppParamInCurrentUrl();
    persistAppParamOnNavigation();
    hideHeaderBrandInEmbeddedMode();
  }

  // Run immediately (deferred script executes after DOM parse) to avoid visible layout jerk.
  applyEmbeddedModeUi();

  // Keep a safety run after DOMContentLoaded in case late markup/scripts alter header/nav links.
  document.addEventListener("DOMContentLoaded", applyEmbeddedModeUi);
})();

// Dynamically adjust .page margin-top to sit below the fixed header + 8px gap
(function adjustPageMargin() {
  function setMargin() {
    var nav = document.querySelector("header.nav-bar");
    var page = document.querySelector(".page");
    if (nav && page) {
      page.style.setProperty("margin-top", (nav.offsetHeight ) + "px", "important");
    }
  }
  // Run on DOMContentLoaded, load, resize, and after fonts settle
  document.addEventListener("DOMContentLoaded", setMargin);
  window.addEventListener("load", setMargin);
  window.addEventListener("resize", setMargin);
  // Also observe header size changes (e.g. switch name update causing wrap)
  if (typeof ResizeObserver !== "undefined") {
    document.addEventListener("DOMContentLoaded", function() {
      var nav = document.querySelector("header.nav-bar");
      if (nav) new ResizeObserver(setMargin).observe(nav);
    });
  }
})();

// Centralized fetch wrapper with 401 handling
function fetchWithAuth(url, options = {}, timeoutSec = DEFAULT_TIMEOUT) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        reject(new Error("Request timed out"));
      }, timeoutSec);

      fetch(url, options)
        .then((response) => {
          clearTimeout(timer);

          // Handle 401 Unauthorized responses
          if (response.status === 401) {
            // Get current page path for return URL
            const currentPath = window.location.pathname + window.location.search;
            // Encode the return URL and redirect to login
            const returnUrl = encodeURIComponent(currentPath);
            // Preserve app=true parameter in redirect URL
            const appParam = sessionStorage.getItem("appEmbeddedMode") === "true" ? "&app=true" : "";
            window.location.href = `/login.html?returnUrl=${returnUrl}${appParam}`;
            return; // Don't resolve the promise
          }

          resolve(response);
        })
        .catch((error) => {
          clearTimeout(timer);
          reject(error);
        });
    });
  }

// Keep the original fetchWT for backward compatibility
function fetchWT(url, options = {}, timeoutSec = DEFAULT_TIMEOUT) {
    return fetchWithAuth(url, options, timeoutSec);
  }

  function displaySuccess( message){
    var successSymbol = '<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><path d="M20.28 2l-11.28 11.53-5.29-5.05-3.28 3.07 9 8.93 14.3-13.65z"/></svg>';
    //el("popupText").innerHTML = successSymbol + " " + message;
    el("popupText").innerHTML = successSymbol + "✅ " + message;
    el("popupText").textContent = message;
    el("popup").style.display = "block";
    setTimeout(function () {
      el("popup").style.display = "none";
    }, 4000);
  }
  
  function displayFailure( message){
    var failureSymbol = '<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><path d="M12 2C6.48 2 2 6.48 2 12C2 17.52 6.48 22 12 22C17.52 22 22 17.52 22 12C22 6.48 17.52 2 12 2ZM8.59 16.97L10.17 15.39L11.75 16.97L13.33 15.39L15.82 16.97L13.33 18.59L11.75 16.97L10.17 18.59L8.59 16.97Z"/></svg>';
    el("popupText").innerHTML = failureSymbol + " " + message;
     el("popupText").textContent = message;
    el("popup").style.display = "block";
    setTimeout(function () {
      el("popup").style.display = "none";
    }, 4000);
  }

function scanNetworks() {
  const networkSelect = document.getElementById("wifiNetwork");
  const scanBtn = document.getElementById("scanNetworksBtn");
  
  // Disable button and show scanning state
  if (scanBtn) {
    scanBtn.style.pointerEvents = "none";
    scanBtn.style.opacity = "0.6";
    scanBtn.innerHTML = "<u>Scanning...</u>";
  }
  if (networkSelect) {
    networkSelect.innerHTML = '<option value="">Scanning networks...</option>';
  }
  
  // The scan API is async on the device: it replies 202 {"status":"scanning"}
  // while the radio scan runs (so the AP stays responsive and the phone does
  // not drop off), then 200 with the network list. Poll until results arrive.
  const scanStartTime = Date.now();
  function pollScan() {
    return fetchWT("/api/scanNetworks", { cache: "no-store" }, 10000).then((response) => {
      if (!response.ok) {
        throw new Error("Failed to scan networks");
      }
      if (response.status === 202) {
        if (Date.now() - scanStartTime > 30000) {
          throw new Error("Scan timed out");
        }
        return new Promise((resolve) => setTimeout(resolve, 1200)).then(pollScan);
      }
      return response.json();
    });
  }

  pollScan()
    .then((networks) => {
      const customWiFiDiv = document.getElementById("customWiFiDiv");
      const wifiPasswordDiv = document.getElementById("wifiPasswordDiv");
      const customWiFiSSID = document.getElementById("customWiFiSSID");
      
      if (networkSelect) {
        if (networks.length === 0) {
          // No networks found - show message, disable select, and show custom SSID directly
          networkSelect.innerHTML = '<option value="">No networks found</option>';
          networkSelect.disabled = true;
          
          // Show custom SSID input and password field directly
          if (customWiFiDiv) customWiFiDiv.style.display = "block";
          if (wifiPasswordDiv) wifiPasswordDiv.style.display = "block";
          if (customWiFiSSID) {
            customWiFiSSID.focus();
          }
        } else {
          // Networks found - enable select and populate options
          networkSelect.disabled = false;
          networkSelect.innerHTML = '<option value="">Select a network (optional)</option>';
          networkSelect.value = ""; // Explicitly reset to empty selection
          
          networks.forEach((network) => {
            const option = document.createElement("option");
            option.value = network.ssid;
            let displayText = network.ssid;
            if (network.rssi) {
              // Add signal strength indicator with brackets
              const strength = network.rssi > -50 ? "[Strong]" : network.rssi > -70 ? "[Medium]" : "[Weak]";
              displayText += " " + strength;
            }
            if (network.encrypted) {
              displayText += " 🔒";
            }
            option.textContent = displayText;
            networkSelect.appendChild(option);
          });
          
          // Add "Other" option at the end (always available)
          const otherOption = document.createElement("option");
          otherOption.value = "__OTHER__";
          otherOption.textContent = "Other (Enter manually)";
          networkSelect.appendChild(otherOption);
          
          // Hide custom SSID div and password div when networks are found and no selection is made
          // They will be shown/hidden based on selection via handleWiFiNetworkChange
          if (customWiFiDiv) {
            customWiFiDiv.style.display = "none";
          }
          if (wifiPasswordDiv) {
            wifiPasswordDiv.style.display = "none";
          }
        }
      }
    })
    .catch((error) => {
      console.error("Error scanning networks:", error);
      const customWiFiDiv = document.getElementById("customWiFiDiv");
      const wifiPasswordDiv = document.getElementById("wifiPasswordDiv");
      const customWiFiSSID = document.getElementById("customWiFiSSID");
      
      if (networkSelect) {
        networkSelect.innerHTML = '<option value="">Error scanning networks</option>';
        networkSelect.disabled = true;
        
        // Show custom SSID input and password field directly on error
        if (customWiFiDiv) customWiFiDiv.style.display = "block";
        if (wifiPasswordDiv) wifiPasswordDiv.style.display = "block";
        if (customWiFiSSID) {
          customWiFiSSID.focus();
        }
      }
    })
    .finally(() => {
      if (scanBtn) {
        scanBtn.style.pointerEvents = "auto";
        scanBtn.style.opacity = "1";
        scanBtn.innerHTML = "<u>Scan</u>";
      }
    });
}

function handleWiFiNetworkChange() {
  const networkSelect = document.getElementById("wifiNetwork");
  const customWiFiDiv = document.getElementById("customWiFiDiv");
  const wifiPasswordDiv = document.getElementById("wifiPasswordDiv");
  const customWiFiSSID = document.getElementById("customWiFiSSID");
  
  if (!networkSelect) return;
  
  if (networkSelect.value === "__OTHER__") {
    // Show custom SSID input and password field
    if (customWiFiDiv) customWiFiDiv.style.display = "block";
    if (wifiPasswordDiv) wifiPasswordDiv.style.display = "block";
    if (customWiFiSSID) customWiFiSSID.focus();
  } else if (networkSelect.value !== "") {
    // Show password field for selected network
    if (customWiFiDiv) customWiFiDiv.style.display = "none";
    if (wifiPasswordDiv) wifiPasswordDiv.style.display = "block";
    if (customWiFiSSID) customWiFiSSID.value = "";
  } else {
    // Hide both fields when no network is selected
    if (customWiFiDiv) customWiFiDiv.style.display = "none";
    if (wifiPasswordDiv) wifiPasswordDiv.style.display = "none";
    if (customWiFiSSID) customWiFiSSID.value = "";
    const passInput = document.getElementById("pass");
    if (passInput) passInput.value = "";
  }
}