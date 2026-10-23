// ***** Start - settings *************************************************
//  const MODE_OF_OPERATION = "T-S"; // Test & all success scenarios of APIs
// const MODE_OF_OPERATION = "T-F"; // Test & all success scenarios of APIs
const MODE_OF_OPERATION = "L"; // Live

const MAX_RUNTIME_HOURS = 24; // 7_WEB
const STD_ERROR =
  "There is a problem connecting with device. Please check device connection and/or refresh page.";

// ***** End - settings *************************************************
// const el = (id) => document.getElementById(id);

function updateWifiStrengthIndicator(rssi, wifiName) {
  var indicator = document.getElementById('wifiStrengthIndicator');
  if (!indicator) return;
  var rssiLabel = document.getElementById('wifiRssiLabel');
  var wifiNameLabel = document.getElementById('wifiNameLabel');
  var isAppMode = document.documentElement.classList.contains('app-embedded');

  if (!rssi) {
    indicator.style.display = 'none';
    return;
  }
  indicator.style.display = 'block';

  // Determine strength level
  var strength; // 'e' = excellent, 'g' = good, 'w' = weak
  var color;
  if (rssi > -50) {
    strength = 'e'; color = '#2e7d32';
  } else if (rssi > -70) {
    strength = 'g'; color = '#f57c00';
  } else {
    strength = 'w'; color = '#d32f2f';
  }

  var imgDiv = document.getElementById('wifiImgIndicator');

  if (isAppMode && imgDiv) {
    // App mode: show WiFi strength image + RSSI value
    imgDiv.style.display = 'block';
    var imgE = document.getElementById('wifi-e-cn');
    var imgG = document.getElementById('wifi-g-cn');
    var imgW = document.getElementById('wifi-w-cn');
    if (imgE) imgE.style.display = (strength === 'e') ? 'inline' : 'none';
    if (imgG) imgG.style.display = (strength === 'g') ? 'inline' : 'none';
    if (imgW) imgW.style.display = (strength === 'w') ? 'inline' : 'none';
    if (rssiLabel) {
      rssiLabel.textContent = rssi + ' dBm';
      rssiLabel.style.color = color;
    }
  } else {
    // Normal mode: show only RSSI value (WiFi icon already shown in header)
    if (imgDiv) imgDiv.style.display = 'none';
    if (rssiLabel) {
      rssiLabel.textContent = rssi + ' dBm';
      rssiLabel.style.color = color;
    }
  }

  // Show WiFi name with RSSI below the icon
  if (wifiNameLabel && wifiName) {
    wifiNameLabel.textContent = wifiName;
  } else if (wifiNameLabel) {
    wifiNameLabel.textContent = '';
  }
}

document.addEventListener("DOMContentLoaded", () => {
  // iWSoc();
  // iSch();
  // iOSet();
  // iPSet();
  fetchWT("/dn.htm").then((response) => {
    // Get the custom header value
    const v = response.headers.get("switchName");
    if (v) {
      // Set the custom header value to the div
      var e = _qs("#switchNameText");
      if (e) {
        e.innerText = v;
        document.title = v + " - SwiGO SuperSmart";
      }
    }
  });
  // Show the selected tab on page load
  const tab = localStorage.getItem("selectedTab");
  const menu = tab || "menu-1";

  showContent(menu);
  setInterval(ckCon, 10000);

  // Setting events
  const slaveControl = document.getElementsByName("slave-control-device");
  for (const e of slaveControl) {
    e.addEventListener("click", (event) => {
      if (event.target.value === "1") {
        el("thCur").disabled = false;
      } else {
        el("thCur").disabled = true;
        el("thCur").value = 0.0;
      }
    });
  }

  // Float enable/disable Logic
  // el('submitFloat').classList.add('button'); // Ensure the button class is applied
  // Disable the button by default
  const submitFloatButton = el('submitFloat');
  submitFloatButton.disabled = true;
  submitFloatButton.classList.add('button');
  _qsa('input[name="flSch"]').forEach(radio => {
    radio.addEventListener('change', function () {
      const ftCont = el('ftCont');
      const flowDelayCont = el('flowDelayCont');
      const enableFloatButton = el('submitFloat');
      if (this.value === "1" || this.value ==="2") {
        ftCont.style.display = "flex";
        // Show flow delay fields only for flow detection mode (2)
        if (this.value === "2") {
          flowDelayCont.style.display = "flex";
        } else {
          flowDelayCont.style.display = "none";
        }
        const requiredText = '220V is not connected to float switch';
        enableFloatButton.disabled = el('flTxt').value !== requiredText;
      } else {
        ftCont.style.display = "none";
        flowDelayCont.style.display = "none";
        enableFloatButton.disabled = false;
      }
  });
  });

  el('flTxt').addEventListener('input', function () {
    const enableFloatButton = el('submitFloat');
    const requiredText = '220V is not connected to float switch';
    enableFloatButton.disabled = this.value !== requiredText;
  });

  // Initialize Master-Slave settings
  initMasterSlaveEventListeners();
  loadMasterSlaveSettings();
  
  // Initialize collapsible sections in menu-4
  initCollapsibleSections();
});

function showContent(menuId) {
  // Prevent access to schedule (menu-2) and protection (menu-3) when in remote mode
  if (remoteModeActive && (menuId === 'menu-2' || menuId === 'menu-3')) {
    return;
  }
  
  // Hide all content divs
  _qsa(".content-div").forEach((div) => {
    div.classList.remove("active");
  });

  // Show the selected content div
  el(menuId).classList.add("active");
  const elements = _qsa('.main_menu');

  elements.forEach((element) => {
    element.classList.remove("active");
  });
  _qs('.' + menuId).classList.add("active");

  // Store the selected tab in local storage
  localStorage.setItem("selectedTab", menuId);
}

/***************************** WebSocket connection ************************ */
var gateway = "ws://" + window.location.hostname + "/ws";
let isSocketUpdate = false;
let isUserInteraction = 0;
let lastClickTime = 0;
let messageTimeout;
let pingTimeout;
let remoteModeActive = false; // Track remote mode state
// Float switch function as /api/getCur last reported it: 0 none, 1 tank full
// detection, 2 flow detection. getCur() keeps this current (page load, and
// again whenever the float settings are saved) - see isFlowMode().
var floatModeSetting = 0;
const messageInterval = 15000; // 15 seconds
const pingInterval = 5000; // 5 seconds

function startMessageTimer() {
  clearTimeout(messageTimeout);
  messageTimeout = setTimeout(() => {
    console.log(new Date().toLocaleTimeString());

    console.log("No message received in 15 seconds, sending ping...");
    sendPing();
  }, messageInterval);
}

// Function to send a ping message to the server
function sendPing() {
  if (socket.readyState === WebSocket.OPEN) {
    socket.send("ping");
    pingTimeout = setTimeout(() => {
      console.log("No pong response, connection might be lost");
      // Close only - handleClose() runs the backoff reconnect. Declaring the UI
      // dead on a single missed pong is what made one dropped frame look like a
      // permanent freeze.
      socket.close();
    }, pingInterval);
  }
}
function sendWsupdate(event) {
  let now = Date.now();
  if (now - lastClickTime < 300) {
    event.preventDefault();
    return;
  }

  lastClickTime = now;
  if (isSocketUpdate) {
    event.preventDefault();
    return;
  }
  isUserInteraction = 2;

  if (el("switch-main").checked) {
    const runHours = Number(el("runHours").value);
    const runMinutes = Number(el("runMinutes").value);
    // const restartOption = _qs('input[name="restart-input-1"]:checked').value;
    // console.log(runHours, runMinutes, restartOption);
    const hoursInMinutes = runHours * 60;
    if (isNaN(runHours) || isNaN(runMinutes) || runHours < 0 || runMinutes < 0 || runHours > 23 || runMinutes > 59) {
      alert("Please select the proper duration to run. Hours and minutes should be between 0 to 23 and 0 to 59.");
      el("switch-main").checked = false;
    } else if ((runMinutes + hoursInMinutes) > ((MAX_RUNTIME_HOURS * 60)-1)) {
      alert("Maximum run time allowed is 23 hours, 59 minutes. Please adjust the input values.");
      el("switch-main").checked = false;
    } else if ((runMinutes + hoursInMinutes) ==0) {
      alert("Please select the duration to run.");
      el("switch-main").checked = false;
    } else {
      try {
        socket.send(
          JSON.stringify({
            sControl: "ON",
            hours: runHours,
            minutes: runMinutes,
            // restartOption: restartOption
          })
        );
        setTimeout(() => {
          console.log("after web On - call fetchlogs");
           fetchLogsAndUpdateUI();
        }, 500);
        
      } catch (error) {}
    }
  } else {
    try {
      socket.send(JSON.stringify({ sControl: "OFF" }));
      setTimeout(() => {
        console.log("after web Off - call fetchlogs");
        fetchLogsAndUpdateUI();
      }, 500);
      console.log("Sent message: " + JSON.stringify({ sControl: "OFF" }));
    } catch (error) {}
  }

  setTimeout(() => (isUserInteraction = 0), 1000);
}

function updateCheckboxState(status) {
  isSocketUpdate = true;
  const checkbox = el("switch-main");
  checkbox.checked = status;
  isSocketUpdate = false;
}

let connectionAlive = false;
let connectionAttempts = 0;
let reconnectTimer = null;
// The device closes a client whose send queue backs up, so an otherwise healthy
// session can see occasional closes (backgrounded webview, WiFi roam, cloud
// reconnect). Retry with a backoff for a good while before declaring it dead.
const MAX_RECONNECT_ATTEMPTS = 10;
const RECONNECT_DELAYS = [500, 1000, 2000, 4000, 8000, 15000];

// Schedules a retry instead of reconnecting inline: onerror is normally followed
// by onclose, and retrying from both would spawn parallel sockets.
function scheduleReconnect() {
  if (reconnectTimer) return;
  if (socket && socket.readyState === WebSocket.OPEN) return;
  if (connectionAttempts >= MAX_RECONNECT_ATTEMPTS) {
    showErrorMessage();
    return;
  }
  const delay =
    RECONNECT_DELAYS[Math.min(connectionAttempts, RECONNECT_DELAYS.length - 1)];
  connectionAttempts++;
  console.log(
    "WebSocket closed. Reconnect attempt " +
      connectionAttempts +
      " in " +
      delay +
      "ms"
  );
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    ckCon();
  }, delay);
}

function ckCon() {
  // CONNECTING/OPEN means a socket is already in flight - only CLOSING and
  // CLOSED need a new one.
  if (
    socket &&
    (socket.readyState === WebSocket.CONNECTING ||
      socket.readyState === WebSocket.OPEN)
  ) {
    return;
  }
  try {
    socket = new WebSocket(gateway);
    socket.onopen = handleOpen;
    socket.onclose = handleClose;
    socket.onerror = handleError;
    socket.onmessage = handleMessage;
  } catch (error) {
    console.error(
      "WebSocket connection could not be established. Reason: " + error
    );
    scheduleReconnect();
  }
}
function handleOpen(event) {
  console.log("Websocket opened");
  // Reset here, otherwise the counter only ever grows and the third close in the
  // page's lifetime would permanently stop reconnecting.
  connectionAttempts = 0;
  startMessageTimer();
}

function handleClose(event) {
  console.log("Websocket closed");
  clearTimeout(messageTimeout);
  clearTimeout(pingTimeout);
  scheduleReconnect();
}
function handleError(error) {
  console.error("WebSocket error observed:", error);
  clearTimeout(messageTimeout);
  clearTimeout(pingTimeout);
  scheduleReconnect();
}

function showErrorMessage() {
  if (el("switch-main") && MODE_OF_OPERATION == "L") {
    const contentDiv = el("content");
    contentDiv.innerHTML =
      '<p style="padding:20px 10px; ">Connection to the device is lost. Please <a href="#" onclick="location.reload()">refresh</a> the page.</p>';
  }
}
let socket;
function iWSoc() {
  // Connect to the WebSocket
  ckCon();
  el("switch-main")
    .addEventListener("change", sendWsupdate);

  console.log("move to first page");
  fetchLogsAndUpdateUI();
  //fetchEnergyDataAndUpdateUI();
}

// Device alerts that raise the single header warning icon. Settings itself marks
// which section is at fault, so the icon only has to get the user there.
let hdrApPassWarn = false;
let hdrBattWarn = false;

function updateHeaderWarning() {
  const icon = el("headerWarning");
  if (!icon) return;
  if (!hdrApPassWarn && !hdrBattWarn) {
    icon.style.display = "none";
    return;
  }
  const alerts = [];
  if (hdrBattWarn) alerts.push("RTC backup battery is weak");
  if (hdrApPassWarn) alerts.push("AP password is not set");
  icon.title = alerts.join(" | ") + " - tap for Settings";
  icon.onclick = function () {
    showContent("menu-4");
  };
  icon.style.display = "inline";
}

// User asserts the RTC backup battery has been replaced. Posts to the device, which
// clears the alarm and persists it to flash (survives session reconnects and reboots).
function confirmBatteryReplaced() {
  if (!confirm("Confirm that you have replaced the RTC backup battery (Lithium CR1220, 3V)?")) {
    return;
  }
  fetch("/api/rtcBatteryReplaced", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    // Send the browser time so the device sets the RTC (which clears the hardware
    // oscillator-stop flag) even without internet - otherwise the warning returns on
    // the next boot.
    body: JSON.stringify({ deviceTime: new Date().toISOString() }),
  })
    .then((response) => {
      if (!response.ok) {
        return response.text().then((msg) => {
          throw new Error(msg || "HTTP Error: " + response.status);
        });
      }
      return response.text();
    })
    .then(() => {
      if (el("rtcBattError")) el("rtcBattError").style.display = "none";
      if (el("rtcBattReplacedBtn")) el("rtcBattReplacedBtn").style.display = "none";
      if (el("rtcBattWarningSettings")) el("rtcBattWarningSettings").style.display = "none";
      hdrBattWarn = false;
      updateHeaderWarning();
      if (el("deviceTimeTitle")) el("deviceTimeTitle").style.color = "";
      alert("RTC backup battery status cleared. If the warning returns after a power outage, the new cell may also need attention.");
    })
    .catch((error) => {
      alert("Could not update battery status: " + error.message);
    });
}

// A device update is a wide payload touching nearly every part of the home
// screen, so handleMessage only parses it and hands each area to its own
// renderer, in the order the screen reads.
function handleMessage(event) {
  //console.log(event.data);

  if (event.data === "pong") {
    clearTimeout(pingTimeout);
    startMessageTimer();
    return;
  }

  var data = JSON.parse(event.data);
  if (rejectTooManyClients(data)) return;

  // Check if log refresh is required
  if (data.logRefresh) {
    console.log("Log refresh requested, fetching logs...");
    fetchLogsAndUpdateUI();
  }

  applyRemoteMode(data);

  const incomingStatus = data.sstat === "ON";
  // Only the duration-completed case fills the current-schedule area, so clear
  // it on every update - otherwise it would linger into states that have
  // nothing to say there.
  var cSD = el("cur-sch-display");
  if (cSD) {
    cSD.style.display = "none";
    cSD.innerHTML = "";
  }

  updateSwitchAndTelemetry(data, incomingStatus);
  updateDeviceTime(data);
  updateNetworkIcons(data);
  updateDeviceWarnings(data);
  updateCloudStatus(data);
  checkFault(data);
  updateAlertSection(data);
  if (incomingStatus) {
    renderDeviceOnState(data);
  } else {
    renderDeviceOffState(data);
  }

  startMessageTimer();
}

// The device turns away clients past its limit; nothing on the screen can be
// trusted after that, so the rest of the update is skipped.
function rejectTooManyClients(data) {
  if (!data.error || data.error !== "ERROR: Too many clients connected") {
    return false;
  }
  //TODO - change this to use message popup routine
  var popup = el("popup");
  popup.style.display = "block";
  popup.innerHTML = "Connection closed: Too many clients connected";
  var switchMain = el("switch-main");
  switchMain.disabled = true;
  console.error("Connection closed: Too many clients connected");
  websocket.close();
  return true;
}

// Handle remote mode (master-slave mode 2): the relay follows the master, so
// the switch, the duration inputs and the schedule/protection pages are taken
// out of the user's hands and the master's state is shown in their place.
function applyRemoteMode(data) {
  var switchContainer = _qs(".switch-container");
  var masterStatusDisplay = el("master-status-display");
  var masterStatusText = el("master-status-text");
  var menu2 = _qs(".menu-2");
  var menu3 = _qs(".menu-3");
  var durationContainer = el("duration-input-container");
  var runHours = el("runHours");
  var runMinutes = el("runMinutes");

  if (data.remoteMode) {
    remoteModeActive = true;
    // Hide switch container and show master status
    if (switchContainer) {
      switchContainer.style.display = "none";
    }
    if (masterStatusDisplay) {
      masterStatusDisplay.style.display = "block";
    }
    if (masterStatusText && data.remoteStatus) {
      masterStatusText.textContent = data.remoteStatus;
      // Set color to green when ON, default/black when OFF
      if (data.remoteStatus === "ON") {
        masterStatusText.style.color = "#468a42"; // Green color matching the theme
      } else {
        masterStatusText.style.color = "";
      }
    }
    // Disable schedule and protection menu items
    if (menu2) {
      menu2.style.opacity = "0.5";
      menu2.style.pointerEvents = "none";
      menu2.style.cursor = "not-allowed";
    }
    if (menu3) {
      menu3.style.opacity = "0.5";
      menu3.style.pointerEvents = "none";
      menu3.style.cursor = "not-allowed";
    }
    // Disable duration input area
    if (durationContainer) {
      durationContainer.style.opacity = "0.5";
      durationContainer.style.pointerEvents = "none";
    }
    if (runHours) {
      runHours.disabled = true;
    }
    if (runMinutes) {
      runMinutes.disabled = true;
    }
    // If currently on schedule or protection page, switch to home
    const currentTab = localStorage.getItem("selectedTab");
    if (currentTab === 'menu-2' || currentTab === 'menu-3') {
      showContent('menu-1');
    }
  } else {
    remoteModeActive = false;
    // Show switch container and hide master status
    if (switchContainer) {
      switchContainer.style.display = "";
    }
    if (masterStatusDisplay) {
      masterStatusDisplay.style.display = "none";
    }
    // Re-enable schedule and protection menu items
    if (menu2) {
      menu2.style.opacity = "";
      menu2.style.pointerEvents = "";
      menu2.style.cursor = "";
    }
    if (menu3) {
      menu3.style.opacity = "";
      menu3.style.pointerEvents = "";
      menu3.style.cursor = "";
    }
    // Re-enable duration input area
    if (durationContainer) {
      durationContainer.style.opacity = "";
      durationContainer.style.pointerEvents = "";
    }
    if (runHours) {
      runHours.disabled = false;
    }
    if (runMinutes) {
      runMinutes.disabled = false;
    }
  }
}

// Telemetry and the switch itself only live on the home page, so everything
// here hangs off #switch-main being present.
function updateSwitchAndTelemetry(data, incomingStatus) {
  var switchMain = el("switch-main");
  if (!switchMain) return;

  console.log(new Date().toLocaleTimeString());

  console.log(data);
  console.log(switchMain.checked);

  syncSwitchState(switchMain, incomingStatus);
  updateTelemetryReadings(data);
  updateVsmrDisplay(data);
}

// The device has the last word on the relay state, except while the user's own
// click is still in flight (isUserInteraction).
function syncSwitchState(switchMain, incomingStatus) {
  if (incomingStatus === switchMain.checked) return;
  // console.log("isUserInteraction - ", isUserInteraction);
  if (!isUserInteraction) {
    updateCheckboxState(incomingStatus);
  }
  //switchMain.checked = incomingStatus;
  console.log(
    "after handle message - incomingStatus !== switchMain.checked"
  );
  fetchLogsAndUpdateUI();
  // if (data.sstat == 'ON')
  // { switchMain.checked = true } else { switchMain.checked = false }
}

function updateTelemetryReadings(data) {
  var cval = el("cval");
  var vval = el("vval");
  var power = el("power");
  //var pf = el('pf');
  var energy = el("energy");
  var cumEnergy = el("cumEnergy");
  if (cval) cval.innerHTML = data.cval ? data.cval : "N/A";
  if (vval) vval.innerHTML = data.vval ? data.vval : "N/A";
  if (power) power.innerHTML = data.power ? data.power : "N/A";
  //pf.innerHTML = data.pf ? data.pf : "N/A";
  if (energy) energy.innerHTML = data.energy ? data.energy : "N/A";
  if (cumEnergy) cumEnergy.innerHTML = data.cumEnergy ? data.cumEnergy : "N/A";
}

// The three-phase (VSMR) row, present only on devices wired to one.
function updateVsmrDisplay(data) {
  var vsmrR = el("vsmrR");
  if (!vsmrR) return;
  vsmrR.innerHTML = data.vsmrR !== undefined ? data.vsmrR : "N/A";
  el("vsmrY").innerHTML = data.vsmrY !== undefined ? data.vsmrY : "N/A";
  el("vsmrB").innerHTML = data.vsmrB !== undefined ? data.vsmrB : "N/A";

  // While VSMR data is flowing, replace Supersmart's own voltage
  // reading with the VSMR status (derived from flags + relay state).
  // When the ESP-NOW link is down (no vsmr keys in the payload), the
  // R/Y/B row stays visible but grayed out (vsmr-offline).
  var vval = el("vval");
  var vsmrStatus = el("vsmrStatus");
  var vsmrGrid = el("vsmr-voltages");
  if (data.vsmrR !== undefined) {
    vsmrGrid.classList.remove("vsmr-offline");
    var flags = data.vsmrFlags || 0;
    var statusText, statusClass;
    if (flags & 0x07) {
      statusText = "Phase Failure";
      statusClass = "vsmr-status-fault";
    } else if (flags & 0x08) {
      statusText = "Sequence Mismatch";
      statusClass = "vsmr-status-fault";
    } else if (data.vsmrRelay) {
      statusText = "3 Phase Voltages OK";
      statusClass = "vsmr-status-ok";
    } else {
      statusText = "VSMR Relay Off";
      statusClass = "vsmr-status-idle";
    }
    vsmrStatus.textContent = statusText;
    vsmrStatus.className = "vsmr-status " + statusClass;
    vsmrStatus.style.display = "inline";
    vval.style.display = "none";
    el("vvalUnit").style.display = "none";
  } else {
    vsmrGrid.classList.add("vsmr-offline");
    vsmrStatus.style.display = "none";
    vval.style.display = "";
    el("vvalUnit").style.display = "";
  }
}

// The device's own clock, as reported in the update.
function updateDeviceTime(data) {
  var timeDisplay = el("rtcTime");
  if (!timeDisplay) return;
  if (data.stime == null) {
    timeDisplay.innerHTML = "N/A";
    return;
  }
  const date = new Date(data.stime);
  if (isNaN(date.getTime())) {
    timeDisplay.innerHTML = "N/A";
    return;
  }
  const dateString =
    String(date.getDate()).padStart(2, "0") +
    "-" +
    date.toLocaleString("default", {
      month: "short",
    }) +
    "-" +
    date.getFullYear() +
    " " +
    String(date.getHours()).padStart(2, "0") +
    ":" +
    String(date.getMinutes()).padStart(2, "0") +
    ":" +
    String(date.getSeconds()).padStart(2, "0");

  timeDisplay.innerHTML = dateString;
}

// Wi-Fi / Ethernet / AP icons and the network name beside them.
function updateNetworkIcons(data) {
  // Whether the DEVICE has a route out of the local network; in AP mode the
  // phone is on the device's own network. help.js uses this to decide about
  // "Learn more" links in a plain browser - in the app's webview it shows
  // them regardless, since they open in the system browser.
  if (window.setHelpNetworkState) {
    window.setHelpNetworkState(data.wMode == "W" || data.wMode == "E");
  }
  if (data.wMode == "W") {

    el('apIcon').style.display = 'none';
    el('apIcon').alt = '';
    el('apIcon').title = '';
    el('ethIcon').style.display = 'none';

    //let wiFiStrengthString = data.wiFiStrength? ((data.wiFiStrength > -50 )?"(Excellent)": (data.wiFiStrength > -70 )?"(Good)":"(Week)"):"";
    updateWifiStrengthIndicator(data.wiFiStrength, data.wName);
    if (data.wiFiStrength && (data.wiFiStrength > -50)) {
      el('wifi-e').style.display = 'inline';
      el('wifi-g').style.display = 'none';
      el('wifi-w').style.display = 'none';
    } else if (data.wiFiStrength && (data.wiFiStrength > -70)) {
      el('wifi-e').style.display = 'none';
      el('wifi-g').style.display = 'inline';
      el('wifi-w').style.display = 'none';
    } else {
      el('wifi-e').style.display = 'none';
      el('wifi-g').style.display = 'none';
      el('wifi-w').style.display = 'inline';
    }

    // el('wifiIcon').alt = data.wName ? data.wName : "";
    // el('wifiIcon').title = data.wName ? data.wName+wiFiStrengthString : "";

  } else if (data.wMode == "E") {
    updateWifiStrengthIndicator(null);
    el('ethIcon').style.display = 'inline';
    // el('apIcon').alt = data.wName ? data.wName : "";
    // el('apIcon').title = data.wName ? data.wName : "";

    el('apIcon').style.display = 'none';
    el('wifi-e').style.display = 'none';
    el('wifi-g').style.display = 'none';
    el('wifi-w').style.display = 'none';

  } else {
    updateWifiStrengthIndicator(null);
    el('apIcon').style.display = 'inline';
    el('apIcon').alt = data.wName ? data.wName : "";
    el('apIcon').title = data.wName ? data.wName : "";

    el('ethIcon').style.display = 'none';
    el('wifi-e').style.display = 'none';
    el('wifi-g').style.display = 'none';
    el('wifi-w').style.display = 'none';
  }
  if (data.wName) {
    el("apwifiName").innerHTML = data.wName ? data.wName : "";
    el("apwifiName").style.display = "block";
  }
}

// Device alerts that are not about the run itself: AP password never set, and
// the RTC backup battery asking to be replaced.
function updateDeviceWarnings(data) {
  // One header warning icon covers every device alert; Settings then shows a
  // warning symbol against whichever section is at fault.
  hdrApPassWarn = (data.apPassSet === false);
  hdrBattWarn = !!data.rtcBattLow;
  updateHeaderWarning();
  // Same warning next to the AP Password accordion title in Settings, with
  // the detail text inside the accordion body (the icon itself cannot carry
  // a click handler - the accordion header toggle swallows it).
  if (el('apPassWarningSettings')) {
    el('apPassWarningSettings').style.display = (data.apPassSet === false) ? 'inline' : 'none';
  }
  if (el('apPassWarningText')) {
    el('apPassWarningText').style.display = (data.apPassSet === false) ? 'block' : 'none';
  }
  // RTC backup-battery replacement alarm -> warning symbol and detail text inside
  // Settings > Device time. On any such error, the accordion header turns orange.
  if (el('rtcBattWarningSettings')) {
    el('rtcBattWarningSettings').style.display = data.rtcBattLow ? 'inline' : 'none';
  }
  if (el('rtcBattError')) {
    el('rtcBattError').style.display = data.rtcBattLow ? 'block' : 'none';
  }
  if (el('rtcBattReplacedBtn')) {
    el('rtcBattReplacedBtn').style.display = data.rtcBattLow ? 'inline-block' : 'none';
  }
  if (el('deviceTimeTitle')) {
    el('deviceTimeTitle').style.color = data.rtcBattLow ? '#e65100' : '';
  }
}

// Cloud (MQTT) connection block in Settings: either the live status, or the
// sign-up form when no cloud is configured.
function updateCloudStatus(data) {
  if (data.mqttCon == 10 || data.mqttCon == 11) {
    el("cloudStatusDiv").style.display = "block";
    el("cloudEmailDiv").style.display = "none";
    if (data.mqttCon == 11) {
      el("cloudStatusText").innerHTML = "EVIQtech Cloud Connection Status: Connected ✅";
      el("cloudOk").style.display = "inline";
      el("cloudOff").style.display = "none";
    } else {
      el("cloudStatusText").innerHTML = "EVIQtech Cloud Connection Status: Configured but Not Connected❌";
      el("cloudOk").style.display = "none";
      el("cloudOff").style.display = "inline";
    }
    setCloudHelpId("settings.cloudStatus");

  } else {
    el("cloudEmailDiv").style.display = "block";
    setCloudHelpId("settings.cloud");
    el("cloudStatusDiv").style.display = "none";
    el("cloudOk").style.display = "none";
    el("cloudOff").style.display = "none";
  }
}

// How long a protection condition has been going on - the running commentary
// above the logs, as opposed to the latched fault icons from checkFault().
function updateAlertSection(data) {
  var alertDisplay = el("alert-text");
  if (!alertDisplay) return;
  var alertText = "";
  if (data.dryRunDuration) {
    alertText = "Dry run for about " + data.dryRunDuration + " Seconds<br/>";
  }
  if (data.overRunDuration) {
    alertText += "Overload for about " + data.overRunDuration + " Seconds<br/>";
  }
  if (data.lowVoltageDuration) {
    alertText += "Low voltage for about " + data.lowVoltageDuration + " Seconds<br/>";
  }
  if (data.highVoltageDuration) {
    alertText += "High voltage for about " + data.highVoltageDuration + " Seconds<br/>";
  }
  if (data.floatDuration) {
    alertText += "Tank is getting filled, float high for about " + data.floatDuration + " Seconds<br/>";
  }
  if (data.flowOffDuration) {
    alertText += "Flow is not detected for about " + data.flowOffDuration + " Seconds <br/>";
  }
  if (data.flowOnDuration) {
    alertText += "Flow is detected for about " + data.flowOnDuration + " Seconds <br/>"
  }
  if (alertText) {
    el("alert").style.display = "block";
    alertDisplay.innerHTML = alertText;
    var v9ac = el("v9-alert-container");
    if (v9ac) v9ac.style.display = "block";
    // Refresh logs when alert triggers (dryrun, overload, float, etc.)
    fetchLogsAndUpdateUI();
  } else {
    el("alert").style.display = "none";
    alertDisplay.innerHTML = "";
  }
}

// Float switch function 2 - flow detection. The relay follows the flow sensor,
// so a schedule window says nothing about what will actually happen: the
// next-schedule line is left out entirely, enabled schedule or not, and no
// scheduled end time is shown. floatModeSetting comes from getCur().
function isFlowMode() {
  return floatModeSetting == 2;
}

// Nothing to show in a schedule row - hidden and emptied, so an earlier frame's
// line cannot linger behind it.
function clearSchRow(box) {
  box.style.display = "none";
  box.innerHTML = "";
}

// The next-schedule line, computed client-side from the cached schedule. In
// flow mode - and whenever flow is actually running the relay - there is no
// schedule time to show, so the line is cleared instead.
function renderNextScheduleLine(nSD, data) {
  if (isFlowMode() || data.flowIncomingState) {
    clearSchRow(nSD);
    return;
  }
  renderScheduleWhileOn(nSD, data.runType);
}

// Relay ON: the countdown takes the place of the duration inputs, and the
// schedule area says what is running and what comes next.
function renderDeviceOnState(data) {
  var durationArea = _qs(".v9-duration-area");
  var countdownDisplay = el("count-down-timer");
  var nSD = el("next-sch-display");
  var cSD = el("cur-sch-display");

  // Relay ON: countdown replaces the hour/minute duration inputs
  if (durationArea) durationArea.style.display = "none";
  if (data.flowIncomingState) {
    // Flow is what keeps the relay on, so nothing is timing the run.
    if (countdownDisplay) countdownDisplay.style.display = 'none';
  } else if (countdownDisplay) {
    updateCountdown(countdownDisplay, data.offDuration);
  }

  if (nSD) {
    renderNextScheduleLine(nSD, data);
  }
  // What is running, under the switch: flow detection while flow drives the
  // relay, otherwise when a timed run ends. Nothing was shown here before, so
  // a schedule going along smoothly looked the same as no schedule at all.
  if (!cSD) return;
  if (data.flowIncomingState) {
    cSD.style.display = "block";
    cSD.innerHTML = schRowHtml("play", "FLOW DETECTED", "");
  } else if (!isFlowMode() && data.offTime && !isNaN(Date.parse(data.offTime))) {
    cSD.style.display = "block";
    cSD.innerHTML = schRowHtml("play", formatEndsAt(new Date(data.offTime)), "", "Ends at");
  }
}

// "HH:MM:SS" of remaining ON time into the three countdown digits; anything
// the device cannot express that way hides the countdown altogether.
function updateCountdown(countdownDisplay, offDuration) {
  const [hours, minutes, seconds] = offDuration?.split(":")?.map(part => part.padStart(2, "0"));
  if (hours && minutes && seconds && hours <= 23 && minutes <= 59 && seconds <= 59) {
    countdownDisplay.style.display = 'flex';
    el("count-down-hour").textContent = hours?hours:"00";
    el("count-down-minute").textContent = minutes?minutes:"00";
    el("count-down-second").textContent = seconds?seconds:"00";
  }
  else {
    countdownDisplay.style.display = 'none';
  }
}

// Relay OFF: the duration inputs come back, and the schedule area either shows
// the upcoming window or explains why the run that should be going on is not.
function renderDeviceOffState(data) {
  var durationArea = _qs(".v9-duration-area");
  var countdownDisplay = el("count-down-timer");
  var nSD = el("next-sch-display");
  var cSD = el("cur-sch-display");

  // Relay OFF: restore the duration inputs ("" falls back to the stylesheet)
  if (durationArea) durationArea.style.display = "";
  if (countdownDisplay) {
    countdownDisplay.style.display = "none";
    //countdownDisplay.innerHTML="";
  }
  /*if (alertDisplay) {
    el("alert").style.display = "none";
    alertDisplay.innerHTML = "";
  }*/
  if (!nSD) return;
  // In flow mode even a reported window is skipped - renderScheduleStatus()
  // clears the line and still shows an interruption message if one came.
  if (!isFlowMode() && data.upSchST && data.upSchET) {
    renderNextSchedule(data, nSD, cSD);
  } else {
    renderScheduleStatus(data, nSD, cSD);
  }
}

// The window the firmware reports as next (or, when its owed ON time is already
// spent, the window just finished).
function renderNextSchedule(data, nSD, cSD) {
  let schDate = isNaN(Date.parse(data.upSchST)) ? null : new Date(data.upSchST);
  let schETDate = isNaN(Date.parse(data.upSchET)) ? null : new Date(data.upSchET);

  if (!schDate) {
    clearSchRow(nSD);
    return;
  }

  // Duration Based schedule whose owed ON time has already run inside the
  // current window: the times below are this window's, not an upcoming start.
  let durationOver = data.upSchDurOver ? true : false;

  // The status shown beside the timing - its own element now, so no
  // brackets.
  let scheduleTypeString = "";
  if (data.upSchType === "Duration") {
    // The window is only where the run may fall, so the owed time is named
    // outright - and once it has been delivered, said to be completed.
    let durationText = formatSchDurationText(Math.round((parseInt(data.upSchDur, 10) || 0) / 60));
    scheduleTypeString = durationText
      ? "Duration-" + durationText + (durationOver ? "-completed" : "")
      : "Duration" + (durationOver ? "-completed" : "");
  } else if (data.upSchType && data.upSchType.trim() !== "") {
    scheduleTypeString = data.upSchType;
  }

  // The end is read against the start, so a window that stays on one day says
  // the day once: "Today, 09:00 AM - 11:00 AM".
  let timeText = formatSchTimeLabel(schDate) + " - " + formatSchTimeLabel(schETDate, schDate);

  if (durationOver && cSD) {
    // The owed time has been delivered, so this window describes what
    // just ran, not something to wait for: it goes under the switch,
    // and the next-schedule line keeps showing the upcoming one.
    cSD.style.display = "block";
    cSD.innerHTML = schRowHtml("pause", timeText, scheduleTypeString);
    // Same client-side computation used while the device is ON; it
    // hides the line by itself when the cache has nothing to show.
    renderNextScheduleLine(nSD, data);
  } else {
    nSD.style.display = "flex";
    nSD.innerHTML = schRowHtml("next", timeText, scheduleTypeString, "");
  }
}

// One end of a schedule window: "Today, 09:00 AM", "Tomorrow, 09:00 AM" or
// "Monday, 3rd 09:00 AM". Passing `referenceDate` (the window's start) drops
// the day whenever it matches - that end needs only a time.
function formatSchTimeLabel(date, referenceDate) {
  let time = date.toLocaleTimeString("en-US", {
    hour: "2-digit",
    minute: "2-digit",
    hour12: true,
  });
  let today = new Date();
  let tomorrow = new Date(today.getTime() + (24 * 60 * 60 * 1000));
  if (referenceDate) {
    if (date.toDateString() === referenceDate.toDateString()) return time;
  } else if (date.toDateString() === today.toDateString()) {
    return "Today, " + time;
  }
  if (date.toDateString() === tomorrow.toDateString()) return "Tomorrow, " + time;
  return date.toLocaleString("default", { weekday: "long" }) +
    ", " +
    date.getDate() +
    getOrdinalSuffix(date.getDate()) +
    " " +
    time;
}

// What became of the run that should be going on now - a manual stop, a fault,
// a float/flow stop. Several carry a quick link that clears the condition and
// resumes; ClearFaultConditionPrompt's argument picks which one.
const SCH_STATUS_MESSAGES = {
  SCHTIME_MANUAL_OFF_RESUME_YES_NOQUICKLINK:
    '<label for="nextSchST" > Device Operation Interrupted Manually<br><a href="#" onclick="ClearFaultConditionPrompt(1)">Resume again</a></label>',
  SCHTIME_FAULT_RESUME_YES:
    '<label for="nextSchST" >Device Stopped due to Fault Condition (Please check logs below)<br>Switching will be retried. <a href="#" onclick="ClearFaultConditionPrompt(2)">Reset Fault Conditions and start now.</a></label>',
  SCHTIME_FAULT_RESUME_YES_NOQUICKLINK:
    '<label for="nextSchST" >Device Stopped due to Fault Condition (Please check logs below)<br>Switching will be retried after Fault conditions are corrected</label>',
  SCHTIME_FAULT_RESUME_NO_NOQUICKLINK:
    '<label for="nextSchST" >Device Stopped due to Fault Condition (Please check logs below) </label>',
  SCHTIME_FAULT_RESUME_SOON:
    '<label for="nextSchST" >Device Stopped due to Fault Condition (Please check logs below)<br>Switching will be resumed soon. <a href="#" onclick="ClearFaultConditionPrompt(2)">Reset Fault Conditions and resume now.</a></label>',
  SCHTIME_FAULT_RESUME_NO:
    '<label for="nextSchST" >Device Stopped due to Fault Condition (Please check logs below)<br><a href="#" onclick="ClearFaultConditionPrompt(2)">Reset Fault Conditions and resume now.</a></label>',
  SCHTIME_FLOAT_OFF:
    '<label for="nextSchST" >Device Stopped as Tank full deducted.</label>',
  FLOW_TIME_MANUAL_INTERRUPTION:
    '<label for="nextSchST" >Flow detected but stopped manually.<br> <a href="#" onclick="ClearFaultConditionPrompt(4)">Reset Flow Function and resume now.</a></label>',
  SCHTIME_MAXTIME_BREAK:
    '<label for="nextSchST" >Device Stopped as Max run time reached, <br>Operation will be resumed after a break.</label>',
  SCHTIME_RESUME_YES:
    '<label for="nextSchST" >Device Operation Interrupted <br> Switching will be retried.</label>',
  SCHTIME_RESUME_NO:
    '<label for="nextSchST" >Device Operation Interrupted</label>',
};

// These belong in the current-schedule area under the switch. The firmware
// sends no upcoming event alongside them, so the next-schedule line is filled
// from the cached schedule and stays on screen regardless.
function renderScheduleStatus(data, nSD, cSD) {
  let statusHtml = (data.schStatus && Object.prototype.hasOwnProperty.call(SCH_STATUS_MESSAGES, data.schStatus))
    ? SCH_STATUS_MESSAGES[data.schStatus]
    : "";
  if (statusHtml) console.log(data.schStatus);

  // Without the current-schedule div (an older cached index.html), fall
  // back to the old behaviour rather than dropping the message.
  let statusBox = cSD || nSD;
  if (statusHtml) {
    statusBox.style.display = "block";
    // No timing to show - the message itself is the status.
    statusBox.innerHTML = schRowHtml("pause", "", statusHtml);
  }
  if (cSD) {
    // The status went under the switch, so the next-schedule line is
    // free to keep showing the upcoming window (hidden by the call
    // itself when the cached schedule has nothing to show, or in flow mode).
    renderNextScheduleLine(nSD, data);
  } else if (!statusHtml) {
    clearSchRow(nSD);
  }
}
function checkFault(data){
  // Hide alert icon when device is OFF
      var alertIcon = el("alert-icon");
      var flowAlertIcon = el("flow-alert-icon");
      var alertText2 = el("alert-text2");
      var flowAlertText = el("flow-alert-text");
      var message ="";
      if (alertIcon) {
        // Display voltage alert
        var hasVoltageAlert = 0;
        var hasFloatingAlert = 0;
        if (data.phaseFailureAlertState) {
            message += "Phase failure alert<br/>";
            hasVoltageAlert = 1;
        } else if (data.phaseSequenceReverseAlertState) {
            message += "Phase sequence reverse alert<br/>";
            hasVoltageAlert = 1;
        } else if (data.lowVoltageAlertState) {
            message += "Low voltage alert<br/>";
            hasVoltageAlert = 1;
        } else if (data.highVoltageAlertState) {
            message += "High voltage alert<br/>";
            hasVoltageAlert = 1;
        } else if (data.leakageAlertState) {
            message += "Current leakage detected<br/>";
            hasVoltageAlert = 1;
        } else {
          hasVoltageAlert = 0;
        }
        if (hasVoltageAlert > 0) {
          alertIcon.style.display = "block";
          alertText2.innerHTML = message;
        } else {
          alertIcon.style.display = "none";
          alertText2.innerHTML = "";
        }

        // Display floating alert
        message = "";
        if (data.floatingAlertState) {
          message += "Tank Full Detected <br/>";
          hasFloatingAlert = 1;
        } else if (data.flowIncomingState && !data.flowOffDuration) {
          message += "Flow detected<br/>";
          hasFloatingAlert = 1;
        } else {
          hasFloatingAlert = 0;
        }
        if (hasFloatingAlert > 0) {
          flowAlertIcon.style.display = "block";
          flowAlertText.innerHTML = message;
        } 
        else {
          flowAlertIcon.style.display = "none";
          flowAlertText.innerHTML = "";
        }
        
        // Show/hide v9 alert container
        var v9AlertContainer = el("v9-alert-container");
        if (v9AlertContainer) {
          v9AlertContainer.style.display = (hasVoltageAlert || hasFloatingAlert) ? "block" : "none";
        }
        return hasVoltageAlert || hasFloatingAlert;
      }
}

// One schedule line: the icon on the left says what it is - play (running now),
// pause (stopped) or next (upcoming) - then the timing and its status
// right-aligned. The icon replaces the old "Current Schedule :" / "Next
// Schedule :" labels, which cost a whole line each. The symbols live once in
// index.html (#sch-play / #sch-pause / #sch-next); the cloud widget carries the
// same three in its own template - keep them in sync.
function schRowHtml(state, timeText, statusText, prefixText) {
  var title = state === "play" ? "Running now" : (state === "pause" ? "Stopped" : "Next schedule");
  var html = '<div class="v9-sch-row v9-sch-' + state + '"><span class="v9-sch-icon" title="' + title + '">'
    + '<svg class="v9-sch-ico"><use href="#sch-' + state + '"></use></svg>'
    + '</span><span class="v9-sch-text">';
  if (prefixText) html += '<span class="v9-sch-status">' + prefixText + "</span>";
  if (timeText) html += '<span class="v9-sch-time">' + timeText + "</span>";
  // The note (duration, cyclic, or an interruption message) always folds onto
  // its own line - see .v9-sch-note.
  if (statusText) html += '<span class="v9-sch-note">' + statusText + "</span>";
  return html + "</span></div>";
}

// "10:22am", "Tomorrow 6am", "Wed 6am" - minutes are dropped on the hour, and
// the day is named only when the end is not today.
function formatEndsAt(endDate) {
  var now = new Date();
  var h = endDate.getHours(), m = endDate.getMinutes();
  var ampm = h >= 12 ? "pm" : "am";
  var h12 = h % 12; if (h12 === 0) h12 = 12;
  var t = h12 + (m ? ":" + String(m).padStart(2, "0") : "") + ampm;
  var endDay = new Date(endDate.getFullYear(), endDate.getMonth(), endDate.getDate());
  var today = new Date(now.getFullYear(), now.getMonth(), now.getDate());
  var dayDiff = Math.round((endDay - today) / 86400000);
  if (dayDiff === 0) return t;
  if (dayDiff === 1) return "Tomorrow " + t;
  return ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"][endDate.getDay()] + " " + t;
}

// "10mins", "1hr", "1hr 30mins" - the owed ON time of a Duration Based schedule,
// written compactly so it fits inside the next-schedule line. Empty for 0.
function formatSchDurationText(totalMins) {
  var mins = parseInt(totalMins, 10) || 0;
  if (mins <= 0) return "";
  var hrs = Math.floor(mins / 60);
  mins = mins % 60;
  var parts = [];
  if (hrs > 0) parts.push(hrs + (hrs === 1 ? "hr" : "hrs"));
  if (mins > 0) parts.push(mins + (mins === 1 ? "min" : "mins"));
  return parts.join(" ");
}

function getOrdinalSuffix(day) {
  if (day >= 11 && day <= 13) {
    return "th";
  }
  switch (day % 10) {
    case 1:
      return "st";
    case 2:
      return "nd";
    case 3:
      return "rd";
    default:
      return "th";
  }
}

// function openTab(evt, tabName) {
//   var i, tabcontent, tabbuttons;
//   tabcontent = document.getElementsByClassName("tab-content");
//   for (i = 0; i < tabcontent.length; i++) {
//     tabcontent[i].style.display = "none";
//   }
//   tabbuttons = document.getElementsByClassName("tab-button");
//   for (i = 0; i < tabbuttons.length; i++) {
//     tabbuttons[i].className = tabbuttons[i].className.replace(" active", "");
//   }
//   el(tabName).style.display = "block";
//   evt.currentTarget.className += " active";
// }

function fetchLogsAndUpdateUI() {
  console.log("Getting Logs...");
  fetchWT("/api/getLogs")
    .then((response) => {
      if (!response.ok) {
        return response.text().then((errorMessage) => {
            throw new Error(errorMessage || "HTTP Error: " + response.status);
        });
      }
      return response.json();
    })
    .then((data) => {
      //console.log("Fetched data:", result);
      populateLogs(data);
    })
    .catch((error) => {
      console.error("Error during fetch:", error.message);
      displayFailure(STD_ERROR);
    });

  // result = {"logArray":[{"ts":"2026-02-16 12:57:37","md":10,"duration":0},{"ts":"2026-02-16 12:40:17","md":0,"duration":60},{"ts":"2026-02-16 11:48:35","md":7,"duration":0},{"ts":"2026-02-16 11:43:35","md":0,"duration":5},{"ts":"2026-02-16 10:26:31","md":20,"duration":0},{"ts":"2026-02-15 23:13:28","md":20,"duration":0},{"ts":"2026-02-15 22:04:53","md":20,"duration":0},{"ts":"2026-02-15 21:23:37","md":20,"duration":0},{"ts":"2026-02-15 21:10:00","md":9,"duration":0},{"ts":"2026-02-15 21:00:00","md":27,"duration":10},{"ts":"2026-02-15 17:04:27","md":20,"duration":0},{"ts":"2026-02-15 02:23:00","md":9,"duration":0},{"ts":"2026-02-15 02:13:00","md":1,"duration":10},{"ts":"2026-02-15 02:12:00","md":9,"duration":0},{"ts":"2026-02-15 02:01:00","md":1,"duration":11},{"ts":"2026-02-15 01:55:00","md":9,"duration":0},{"ts":"2026-02-15 01:50:00","md":1,"duration":5},{"ts":"2026-02-14 22:38:53","md":20,"duration":0},{"ts":"2026-02-14 21:07:29","md":10,"duration":0},{"ts":"2026-02-14 21:00:00","md":27,"duration":10},{"ts":"2026-02-14 20:18:39","md":16,"duration":0},{"ts":"2026-02-14 20:18:03","md":0,"duration":5},{"ts":"2026-02-14 20:17:45","md":7,"duration":0},{"ts":"2026-02-14 20:12:45","md":0,"duration":5},{"ts":"2026-02-14 20:09:30","md":20,"duration":0},{"ts":"2026-02-14 17:13:12","md":20,"duration":0},{"ts":"2026-02-14 13:20:39","md":20,"duration":0},{"ts":"2026-02-14 13:00:46","md":20,"duration":0},{"ts":"2026-02-14 12:37:02","md":20,"duration":0},{"ts":"2026-02-14 11:44:48","md":20,"duration":0}]}
  // populateLogs(result);
}

function populateLogs(data) {
  if (data.logArray) {
    const logContentDiv = _qs(".log-content");
    const monthNames = ['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];
    let html = '';
    const maxLogs = Math.min(data.logArray.length, 10);
    for (let i = 0; i < maxLogs; i++) {
      const log = data.logArray[i];
      const timestamp = new Date(log.ts);

      const datePart = String(timestamp.getDate()).padStart(2, '0') + '/' + monthNames[timestamp.getMonth()];
      const h = timestamp.getHours() % 12 || 12;
      const timePart = h + ':' + String(timestamp.getMinutes()).padStart(2, '0') + ':' + String(timestamp.getSeconds()).padStart(2, '0') + (timestamp.getHours() >= 12 ? 'pm' : 'am');

      // Determine message based on mode
      let message = modeMessages[log.md] || '';

      // Add duration for start events
      if (log.duration && log.duration > 0) {
        const isStartEvent = log.md == MoA.START_WEB ||
                           log.md == MoA.START_SCHEDULED ||
                           log.md == MoA.START_POWER_RESUME ||
                           log.md == MoA.START_MANUAL_1 ||
                           log.md == MoA.START_MANUAL_2 ||
                           log.md == MoA.START_MANUAL_3 ||
                           log.md == MoA.START_MANUAL_4 ||
                           log.md == MoA.START_MQTT ||
                           log.md == MoA.START_WEBHOOK ||
                           log.md == MoA.START_CYCLIC;

        if (isStartEvent) {
          const hours = Math.floor(log.duration / 60);
          const minutes = log.duration % 60;
          let durationText = "";
          if (hours > 0 && minutes > 0) {
              durationText = hours + 'H ' + minutes + 'M';
          } else if (hours > 0) {
              durationText = hours + 'H';
          } else {
              durationText = minutes + 'M';
          }
          message += ' - ' + durationText;
        }
      }
      if (log.current || log.voltage) {
        let curValue = Math.round(log.current * 1000) / 1000;
        let volValue = Math.round(log.voltage * 100) / 100;
        message += ' (' + volValue + 'V, ' + curValue + 'A)';
      }

      html += '<div class="v9-log-entry">' +
        '<span class="v9-log-date">' + datePart + '</span>' +
        '<span class="v9-log-time">' + timePart + '</span>' +
        '<span class="v9-log-message">' + message + '</span>' +
        '</div>';
    }
    logContentDiv.innerHTML = html || '<div style="text-align:center; color:#999; font-style:italic; padding:8px;">No recent logs</div>';
  }
}

function iSch() {
  el("add-schedule")
    .addEventListener("click", addScheduleCard);
  // document
  //   .getElementById("submit-schedule")
  //   .addEventListener("click", submitSchedule);
  getSchedule();
  console.log("Schedule initialized");
}

function iPSet() {
  getCur();
}

function toggleWifiSettings() {
  const wifiDiv = el('set-wifi-div');
  const toggleLink = el('toggleWifiSettings');
  if (wifiDiv.style.display === 'none' || wifiDiv.style.display === '') {
  wifiDiv.style.display = 'block';
  toggleLink.textContent = 'IP Settings (Optional)▲';
  } else {
  wifiDiv.style.display = 'none';
  toggleLink.textContent = 'IP Settings (Optional)▼';
  }
}
function showWiFiDetails() {
  const wiFiURL = "/api/getWiFiStatus";

  fetchWT(wiFiURL)
    .then((response) => {
      if (!response.ok) {
        throw new Error(
          "There is a problem with WiFi details fetch operation: " +
            response.statusText
        );
      }
      return response.json();
    })
    .then(function (result) {
      console.log(result);
      
      // Display current connection details
      let connectionDetailsHTML = "<strong>Current Connection Details</strong><br>";
      connectionDetailsHTML += "Mode: " + (result.currMode === "WIFI" ? "WiFi" : result.currMode === "Ethernet" ? "Ethernet" : "AP Mode (Hotspot)") + "<br>"; 
      connectionDetailsHTML += "SSID: " + (result.currSSID || "") + "<br>";
      connectionDetailsHTML += "IP allotted: " + (result.currIP || "Not available");
      if (result.currIP && result.currIP !== "Not available") {
        connectionDetailsHTML += '<a href="#" onclick="copyIPLink(event, \'' + result.currIP + '\'); return false;" style="color: #007bff; text-decoration: underline; cursor: pointer; font-size: 0.9em; margin-top: 5px; display: inline-block;">📋 Copy</a><br>';
      } else {
        connectionDetailsHTML += '<br>';
      }
      connectionDetailsHTML += "MAC ID: " + (result.macID || "Not available") + "<br>";
      
      
      // Display set parameters
      let wiFiDetailsHTML = "<strong>WiFi Configuration</strong><br>";
      wiFiDetailsHTML += "WiFi: " + (result.wiFiName ? result.wiFiName : "Not set") + "<br>";
      // wiFiDetailsHTML += "Preferred-IP: " + (result.prefIP ? result.prefIP : "Not set") + "<br>";
      // wiFiDetailsHTML += "Gateway: " + (result.gwIP ? result.gwIP : "Not set") + "<br>";
      // wiFiDetailsHTML += "Subnet: " + (result.sbMask ? result.sbMask : "Not set") + "<br>";

      // Set the values of input boxes inside set-wifi-div
      el("preferredIP").value = result.prefIP || "";
      el("gwIP").value = result.gwIP || "";
      el("sbMask").value = result.sbMask || "";
      el("pDNS").value = result.pDNS || "";
      el("sDNS").value = result.sDNS || "";
      
      // Set WiFi network dropdown or custom SSID if WiFi is configured
      const networkSelect = document.getElementById("wifiNetwork");
      const customWiFiSSID = document.getElementById("customWiFiSSID");
      const wifiPasswordDiv = document.getElementById("wifiPasswordDiv");
      
      if (result.wiFiName && result.wiFiName.trim() !== "") {
        // Try to select from dropdown first
        if (networkSelect) {
          // Wait a bit for network scan to complete, then try to select
          setTimeout(() => {
            const option = Array.from(networkSelect.options).find(
              opt => opt.value === result.wiFiName
            );
            if (option) {
              networkSelect.value = result.wiFiName;
              handleWiFiNetworkChange();
            } else {
              // Network not in dropdown, use custom input
              networkSelect.value = "__OTHER__";
              if (customWiFiSSID) customWiFiSSID.value = result.wiFiName;
              handleWiFiNetworkChange();
            }
          }, 500);
        } else {
          // Fallback to old ssid input if it exists
          const oldSsidInput = document.getElementById("ssid");
          if (oldSsidInput) oldSsidInput.value = result.wiFiName;
        }
      }
      
      // "Remove WiFi" only makes sense when something is actually saved
      const removeBtn = el("removeWiFi");
      if (removeBtn) {
        removeBtn.style.display = result.wiFiStatus === "Enabled" ? "" : "none";
      }

      // Update the HTML elements
      el("connectionDetails").innerHTML = connectionDetailsHTML;
      el("wiFiDetails").innerHTML = wiFiDetailsHTML;
    })
    .catch((error) => {
      console.error(error);
      el("connectionDetails").innerHTML = "<strong>Current Connection Details</strong><br>Failed to load connection details";
      el("wiFiDetails").innerHTML = "<strong>WiFi Configuration</strong><br>Failed to load WiFi settings";
    });
}

// Forgets the saved WiFi credentials. The device clears them and comes back up in
// AP mode, so the portal is reachable again over the <device>_AP hotspot.
function removeWiFiDetails() {
  if (!confirm("Remove the saved WiFi configuration?\n\nThe device will disconnect from the WiFi and switch to Access Point (hotspot) mode. You will have to connect to the device's own \"_AP\" network to reach this portal again.")) {
    return;
  }
  const removeBtn = el("removeWiFi");
  if (removeBtn) removeBtn.disabled = true;

  fetchWT("/api/removeWiFi", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
    },
  })
    .then((response) => {
      if (!response.ok) {
        return response.text().then((errorMessage) => {
          throw new Error(errorMessage || "HTTP Error: " + response.status);
        });
      }
      return response.text();
    })
    .then((result) => {
      console.log(result);
      if (removeBtn) removeBtn.style.display = "none";

      const ulTag = _ce("ul");
      var liTag = _ce("li");
      liTag.textContent = "WiFi configuration removed.";
      ulTag.appendChild(liTag);
      liTag = _ce("li");
      liTag.textContent =
        "The device is switching to Access Point mode. This page will lose connection.";
      ulTag.appendChild(liTag);
      liTag = _ce("li");
      liTag.textContent =
        "Connect your phone or laptop to the device hotspot (<device name>_AP) and open the portal again to set a new WiFi.";
      ulTag.appendChild(liTag);

      el("popupText").innerHTML = "";
      el("popupText").appendChild(ulTag);
      el("popup").style.display = "block";
      // The popup has no close button anywhere in this portal - hide it on a timer
      setTimeout(function () {
        el("popup").style.display = "none";
      }, 20000);
    })
    .catch((error) => {
      console.log(error);
      if (removeBtn) removeBtn.disabled = false;
      displayFailure(error.message || STD_ERROR);
    });
}

function isValidIPv4(ip) {
  // Regular expression for IPv4 address validation
  const ipv4Regex = /^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$/;
  
  return ipv4Regex.test(ip);
}

function iOSet() {
  showFirmwareVersion();
  showWiFiDetails();
  el("submitWiFi").addEventListener("click", function () {
    function saveWiFiDetails() {
      // Get SSID from either dropdown or custom input
      const networkSelect = document.getElementById("wifiNetwork");
      const customWiFiSSID = document.getElementById("customWiFiSSID");
      const customWiFiDiv = document.getElementById("customWiFiDiv");
      let ssid = "";
      
      // Check if custom WiFi div is visible (handles scan error case and "__OTHER__" selection)
      const isCustomWiFiVisible = customWiFiDiv && customWiFiDiv.style.display !== "none";
      if (isCustomWiFiVisible && customWiFiSSID && customWiFiSSID.value.trim()) {
        ssid = customWiFiSSID.value.trim();
      } else if (networkSelect && networkSelect.value === "__OTHER__") {
        // Handle "__OTHER__" selection even if custom div might not be visible
        ssid = customWiFiSSID ? customWiFiSSID.value.trim() : "";
      } else if (networkSelect && networkSelect.value !== "") {
        ssid = networkSelect.value;
      } 
      // else {
      //   // Fallback to old ssid input if it exists (for backward compatibility)
      //   const oldSsidInput = document.getElementById("ssid");
      //   ssid = oldSsidInput ? oldSsidInput.value.trim() : "";
      // }
      
      var pass = el("pass").value.trim();

      if (!ssid || ssid.length < 1) {
        displayFailure("SSID is mandatory. Please \"Scan\" select the wifi from the list or choose Others option and enter SSID directly");
        return;
      }
      if (ssid.length > 20) {
        displayFailure("SSID must be between 1 and 20 characters.");
        return;
      }

      if (pass && ( pass.length > 20)) {
        displayFailure("Password must be less than 20 characters, or leave it empty for open networks.");
        return;
      }
      var prefIP = el("preferredIP").value;
      var gwIP = el("gwIP").value;
      var sbMask = el("sbMask").value;
      var pDNS = el("pDNS").value;
      var sDNS = el("sDNS").value;
      if (prefIP && prefIP.trim() !== "") {
        if (!isValidIPv4(prefIP)) {
          displayFailure("Invalid IP address format. Please enter a valid IPv4 address.");
          return;
        }
   
        if (!isValidIPv4(gwIP)) {
          displayFailure("Invalid Gateway IP address format. Please enter a valid IPv4 address.");
          return;
        }

        // Only validate subnet mask if provided (it's optional)
        if (sbMask && sbMask.trim() !== "" && !isValidIPv4(sbMask)) {
          displayFailure("Invalid Subnet Mask format. Please enter a valid IPv4 address.");
          return;
        }
      }
      if (pDNS && pDNS.trim() !== "") {
        if (!isValidIPv4(pDNS)) {
          displayFailure("Invalid primary DNS server IP. Please enter a valid IPv4 address.");
          return;
        }
   
        if (!isValidIPv4(sDNS)) {
          displayFailure("Invalid secondary DNS server IP. Please enter a valid IPv4 address.");
          return;
        }

      }

      var data = JSON.stringify({ ssid: ssid, pass: pass, prefIP: prefIP, gwIP: gwIP, sbMask: sbMask, pDNS: pDNS, sDNS: sDNS });

      fetchWT("/api/setWiFi", {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: data,
      })
        .then((response) => {
          if (!response.ok) {
            return response.text().then((errorMessage) => {
                throw new Error(errorMessage || "HTTP Error: " + response.status);
            });
          }
          return response.text();
        })
        .then((result) => {
          console.log(result);
          const ulTag = _ce("ul");
          var liTag = _ce("li");
          liTag.textContent = "WiFi details saved successfully.";
          ulTag.appendChild(liTag);
          liTag = _ce("li");
          liTag.textContent =
            "You will be connected to the given WiFi in about two minutes";
          ulTag.appendChild(liTag);
          liTag = _ce("li");
          liTag.textContent =
            "If the given wifi could not be connected, it will be retried after a while";
          ulTag.appendChild(liTag);

          el("popupText").innerHTML = "";
          el("popupText").appendChild(ulTag);
          el("popup").style.display = "block";

          // displaySuccess("Wi-Fi details saved");
          setTimeout(function () {
            el("popup").style.display = "none";
          }, 15000);
        })
        .catch((error) => {
          console.log(error);
          displayFailure(STD_ERROR);
        });
    }

    saveWiFiDetails();
  });

  if (el("removeWiFi")) {
    el("removeWiFi").addEventListener("click", removeWiFiDetails);
  }

  el("clearLogs").addEventListener("click", clearLogs);
  el("resetCumEnergy").addEventListener("click", resetCumEnergy);
  el("logoutBtn").addEventListener("click", logout);
}

_qsa(".hi").forEach((icon) => {
  icon.addEventListener("click", (event) => {
    const helpText = event.target.nextElementSibling; // Select the associated ht
    if (helpText.style.display === "block") {
      helpText.style.display = "none";
    } else {
      helpText.style.display = "block";
    }

    // Prevent click from hiding the text immediately
    event.stopPropagation();
  });
});

document.addEventListener("click", () => {
  _qsa(".ht").forEach((text) => {
    text.style.display = "none";
  });
});

// Cloud settings help differs before and after the device is registered.
function setCloudHelpId(helpId) {
  const icon = el("cloudHelpIcon");
  if (icon) icon.setAttribute("data-help", helpId);
}

function initiatePage(){
  iWSoc();
  iSch();
  iOSet();
  iPSet();
}

// Shared with the cloud device-control widget - keep both copies in sync.
// Returns {current, next}: the active ON window (cyclic: the running cycle)
// and the nearest upcoming one across all enabled schedules.
function computeScheduleInfo(arr) {
    var now = new Date();
    // Schedules store days as Mon=0..Sun=6 (DAY_NAMES in script_sch.js, and the
    // firmware's own bit order), while Date.getDay() is Sun=0..Sat=6. Without
    // this shift every weekly schedule was matched - and labelled - one day late.
    var nowDay = (now.getDay() + 6) % 7;
    var nowAbs = now.getHours() * 60 + now.getMinutes();
    var dayNames = ['Mon','Tue','Wed','Thu','Fri','Sat','Sun'];
    var current = null;
    var nextText = null;
    var nextMinsAway = Infinity;

    function p2(n) { return String(n).padStart(2, '0'); }
    function to12(mins) {
        mins = ((mins % 1440) + 1440) % 1440;
        var h = Math.floor(mins / 60), m = mins % 60;
        var ampm = h >= 12 ? 'PM' : 'AM';
        var h12 = h % 12; if (h12 === 0) h12 = 12;
        return p2(h12) + ':' + p2(m) + ' ' + ampm;
    }
    function labelFor(dd) {
        if (dd === 0) return 'Today';
        if (dd === 1) return 'Tomorrow';
        if (dd === -1) return 'Yesterday';
        return dayNames[((nowDay + dd) % 7 + 7) % 7];
    }
    // "10mins", "1hr", "1hr 30mins" - for the Duration Based note.
    function durText(mins) {
        var h = Math.floor(mins / 60), m = mins % 60, parts = [];
        if (h > 0) parts.push(h + (h === 1 ? 'hr' : 'hrs'));
        if (m > 0) parts.push(m + (m === 1 ? 'min' : 'mins'));
        return parts.length ? parts.join(' ') : '0mins';
    }
    // A window is labelled by the day it STARTS (startAbs can be negative for a
    // window that began yesterday and wraps past midnight into now).
    // suffix marks the mode: " (Cyclic)", or the Duration Based note - a duration
    // schedule's ON time can fall anywhere inside the window, so the window is
    // shown with the owed duration spelled out rather than as the run length.
    function seg(startAbs, endAbs, suffix) {
        return labelFor(Math.floor(startAbs / 1440)) + ', ' + to12(startAbs) + ' - ' + to12(endAbs) + (suffix || '');
    }
    function candidate(minsAway, text) {
        if (minsAway < nextMinsAway) { nextMinsAway = minsAway; nextText = text; }
    }

    for (var i = 0; i < arr.length; i++) {
        var s = arr[i];
        if (!(s.e === true || s.e === 1 || s.e === '1' || s.e === 'true')) continue;

        var startMins = (parseInt(s['1'],10)||0) * 60 + (parseInt(s['2'],10)||0);
        var endMins = (parseInt(s['3'],10)||0) * 60 + (parseInt(s['4'],10)||0);
        var isCyclic = (s.c === 1 || s.c === true || s.c === '1');
        // m:"D" marks a Duration Based schedule - "5"/"6" then carry the plain
        // duration instead of a cyclic on-duration (see getScheduleJsonString).
        var isDurationBased = (s.m === 'D' || s.m === 'd');
        var isDaily = (s.t === 'D');
        var isWeekly = (s.t === 'W');
        var days = s.d || [];
        var onDur = (parseInt(s['5'],10)||0) * 60 + (parseInt(s['6'],10)||0);
        var offDur = (parseInt(s['7'],10)||0) * 60 + (parseInt(s['8'],10)||0);
        var cycleDur = onDur + offDur;
        var modeSuffix = isCyclic ? ' (Cyclic)'
                       : (isDurationBased ? ' (Duration-' + durText(onDur) + ')' : '');
        // Overnight windows (end <= start, e.g. 15:11 -> 00:00) wrap past midnight.
        var winLen = endMins > startMins ? (endMins - startMins) : (endMins + 1440 - startMins);

        // dd = -1 catches a window that started yesterday and is still running now.
        for (var dd = -1; dd <= 7; dd++) {
            var checkDay = ((nowDay + dd) % 7 + 7) % 7;
            if (!(isDaily || (isWeekly && days.indexOf(checkDay) >= 0))) continue;
            var winStart = dd * 1440 + startMins;
            var winEnd = winStart + winLen;

            if (nowAbs >= winStart && nowAbs < winEnd) {
                /* Inside this window */
                if (isCyclic && cycleDur > 0) {
                    var pos = (nowAbs - winStart) % cycleDur;
                    if (pos < onDur) {
                        /* ON period: this cycle is current, next cycle is next */
                        var onStart = nowAbs - pos;
                        current = seg(onStart, Math.min(onStart + onDur, winEnd), modeSuffix);
                        var nextOn = onStart + cycleDur;
                        if (nextOn < winEnd) candidate(nextOn - nowAbs, seg(nextOn, Math.min(nextOn + onDur, winEnd), modeSuffix));
                    } else {
                        /* OFF period: next cycle is the next event */
                        var nextOn2 = nowAbs + (cycleDur - pos);
                        if (nextOn2 < winEnd) candidate(nextOn2 - nowAbs, seg(nextOn2, Math.min(nextOn2 + onDur, winEnd), modeSuffix));
                    }
                } else if (!isCyclic) {
                    current = seg(winStart, winEnd, modeSuffix);
                }
            } else if (winStart > nowAbs) {
                // A duration schedule's ON time can fall anywhere inside the window,
                // so show the whole window (with the duration noted), not an onDur slice.
                var firstEnd = (isCyclic && cycleDur > 0) ? Math.min(winStart + onDur, winEnd) : winEnd;
                candidate(winStart - nowAbs, seg(winStart, firstEnd, modeSuffix));
            }
        }
    }
    return { current: current, next: nextText };
}

// While the device is ON, show the running schedule window and the upcoming
// one (computed client-side from the cached schedule JSON), matching the
// cloud widget's display.
function renderScheduleWhileOn(nSD, runType) {
  var info = null;
  var arr = null;
  var stored = null;
  try {
    stored = localStorage.getItem("schedule");
    var sch = stored ? JSON.parse(stored) : null;
    if (sch) {
      // "{}" is what /api/getSchedule answers when no schedule is set - a valid
      // "there are none", not a broken cache.
      arr = Array.isArray(sch) ? sch : (Array.isArray(sch.s) ? sch.s : []);
    }
    if (arr) info = computeScheduleInfo(arr);
  } catch (e) {}
    var html = "";
   if (info && info.next) {
    // computeScheduleInfo() returns one string with the mode note appended -
    // "Today, 10:00 AM - 11:00 AM (Duration-5mins)". Split the note off so
    // the timing and the status can sit side by side in the row.
    var nextParts = info.next.match(/^(.*?)\s*\(([^()]*)\)\s*$/);
    html += schRowHtml("next",
                       nextParts ? nextParts[1] : info.next,
                       nextParts ? nextParts[2] : "");
  }
  if (html) {
    nSD.style.display = "block";
    nSD.innerHTML = html;
  } else {
    nSD.style.display = "none";
  }
}

// Initialize collapsible sections in menu-4
function initCollapsibleSections() {
  // Get all collapsible sections in menu-4
  const menu4 = document.getElementById('menu-4');
  if (!menu4) return;
  
  const sections = menu4.querySelectorAll('.collapsible-section');
  
  // Load saved state from localStorage
  const savedState = localStorage.getItem('menu4CollapsibleState');
  const expandedSections = savedState ? JSON.parse(savedState) : {};
  
  sections.forEach(section => {
    const sectionId = section.getAttribute('data-section-id');
    const header = section.querySelector('.collapsible-header');
    
    if (!header) return;
    
    // Restore saved state
    if (expandedSections[sectionId]) {
      section.classList.remove('collapsed');
      section.classList.add('expanded');
      
      // // Trigger network scan if WiFi settings section is already expanded
      // if (sectionId === 'wifi-settings') {
      //   scanNetworks();
      // }
    }
    
    // Add click handler to header
    header.addEventListener('click', function(e) {
      // Don't toggle if clicking on the help icon
      if (e.target.classList.contains('hi') || e.target.closest('.hi') || e.target.closest('.help-i')) {
        return;
      }
      
      // Toggle collapsed/expanded state
      const isCollapsed = section.classList.contains('collapsed');
      
      if (isCollapsed) {
        section.classList.remove('collapsed');
        section.classList.add('expanded');
        expandedSections[sectionId] = true;
        
        // Trigger network scan when WiFi settings section is expanded
        // if (sectionId === 'wifi-settings') {
        //   scanNetworks();
        // }
      } else {
        section.classList.remove('expanded');
        section.classList.add('collapsed');
        expandedSections[sectionId] = false;
      }
      
      // Save state to localStorage
      localStorage.setItem('menu4CollapsibleState', JSON.stringify(expandedSections));
    });
  });
}

/**
 * Copy the device IP address link to clipboard
 * Copies the URL (http:// + IP address) to the user's clipboard
 * @param {Event} event - The click event
 * @param {string} ipAddress - The IP address to copy
 */
function copyIPLink(event, ipAddress) {
  if (event) {
    event.preventDefault();
  }
  
  if (!ipAddress || ipAddress === "Not available") {
    alert("IP address is not available. Cannot copy link.");
    return;
  }
  
  // Construct the URL with http:// protocol
  const deviceUrl = "http://" + ipAddress;
  
  // Try modern Clipboard API first (works in most modern browsers)
  if (navigator.clipboard && navigator.clipboard.writeText) {
    navigator.clipboard.writeText(deviceUrl).then(function() {
      showCopySuccess(deviceUrl);
    }).catch(function(err) {
      // Fallback to older method if clipboard API fails
      fallbackCopyToClipboard(deviceUrl);
    });
  } else {
    // Fallback for older browsers
    fallbackCopyToClipboard(deviceUrl);
  }
}

/**
 * Fallback method to copy text to clipboard for older browsers
 * @param {string} text - The text to copy
 */
function fallbackCopyToClipboard(text) {
  const textArea = document.createElement("textarea");
  textArea.value = text;
  textArea.style.position = "fixed";
  textArea.style.left = "-999999px";
  textArea.style.top = "-999999px";
  document.body.appendChild(textArea);
  textArea.focus();
  textArea.select();
  
  try {
    const successful = document.execCommand('copy');
    if (successful) {
      showCopySuccess(text);
    } else {
      alert("Failed to copy. Please copy manually: " + text);
    }
  } catch (err) {
    alert("Failed to copy. Please copy manually: " + text);
  } finally {
    document.body.removeChild(textArea);
  }
}

/**
 * Show success message when link is copied
 * @param {string} url - The URL that was copied
 */
function showCopySuccess(url) {
  // Try to show a temporary message (you can customize this)
  const message = "Link copied to clipboard: " + url;
  
  // Option 1: Simple alert (works everywhere)
  // alert(message);
  
  // Option 2: Create a temporary notification element (better UX)
  const notification = document.createElement("div");
  notification.textContent = "✓ Link copied: " + url;
  notification.style.cssText = "position: fixed; top: 20px; right: 20px; background: #4CAF50; color: white; padding: 12px 20px; border-radius: 4px; box-shadow: 0 2px 8px rgba(0,0,0,0.2); z-index: 10000; font-size: 14px; max-width: 300px;";
  document.body.appendChild(notification);
  
  // Remove notification after 3 seconds
  setTimeout(function() {
    if (notification.parentNode) {
      notification.parentNode.removeChild(notification);
    }
  }, 3000);
}

window.onload=initiatePage;