// Range limits for every Protection Settings input - the single source of truth
// for the input min/max attributes, the on-change guard and the setCur() check,
// so each field behaves the same way: out of range means an alert and a revert
// to the last good value. Limits match what /api/setCur accepts (api.cpp).

const DC_MIN = 0.0;
const DCMAX = 30;
const OCMIN = 0.01;
const OCMAX = 50;
const LVMIN = 150;
const LVMAX = 250;
const OVMIN = 200;
const OVMAX = 290;

const RETRY_MINS_MAX = 240;  // firmware: drMin/orMin/lVolMin/hVolMin <= 240
const START_DELAY_MAX = 30;  // firmware: stDly <= 30
const RUN_MINS_MAX = 1440;   // firmware: mR/bT <= 1440

// blankOk fields are sent as 0 when left empty (see setCur), so a blank entry
// is accepted; the rest are required and a blank entry is reverted like any
// other invalid one.
var _pLimits = null;
function powerLimits() {
  if (!_pLimits) {
    _pLimits = {
      drCur:          { label: "Dry run current",          min: DC_MIN, max: DCMAX, unit: "Amps" , blankOk: true},
      thCur:          { label: "Dry run check threshold",  min: DC_MIN, max: DCMAX, unit: "Amps" },
      orCur:          { label: "Overload current",         min: OCMIN,  max: OCMAX, unit: "Amps" },
      lVol:           { label: "Low voltage limit",        min: LVMIN,  max: LVMAX, unit: "Volts" },
      hVol:           { label: "High voltage limit",       min: OVMIN,  max: OVMAX, unit: "Volts" },
      drMin:          { label: "Dry run retry time",       min: 0, max: RETRY_MINS_MAX,  unit: "Mins"},
      orMin:          { label: "Overload retry time",      min: 0, max: RETRY_MINS_MAX,  unit: "Mins"},
      lVolMin:        { label: "Low voltage retry time",   min: 0, max: RETRY_MINS_MAX,  unit: "Mins", blankOk: true },
      hVolMin:        { label: "High voltage retry time",  min: 0, max: RETRY_MINS_MAX,  unit: "Mins", blankOk: true },
      startDelayMins: { label: "Restart delay",            min: 0, max: START_DELAY_MAX, unit: "Mins", blankOk: true },
      maxRunTimeMins: { label: "Max run duration",         min: 0, max: RUN_MINS_MAX,    unit: "Mins", blankOk: true },
      breakTimeMins:  { label: "Break time",               min: 0, max: RUN_MINS_MAX,    unit: "Mins", blankOk: true },
    };
  }
  return _pLimits;
}

// Remembers the value to fall back to when the next entry is out of range.
function markPowerValue(input) {
  input.dataset.pv = input.value;
  input.style.borderColor = "";
}

function isPowerValueInRange(input, limit) {
  const raw = input.value.trim();
  if (raw === "") return !!limit.blankOk;  // blank is sent as 0 for these
  const value = parseFloat(raw);
  return !isNaN(value) && value >= limit.min && value <= limit.max;
}

// Alerts and restores the last good value when an entry is outside min/max.
// Returns false when the entry was rejected, so setCur() can stop as well.
function validatePowerInput(input) {
  const limit = powerLimits()[input.id];
  if (!limit) return true;

  if (isPowerValueInRange(input, limit)) {
    markPowerValue(input);
    return true;
  }
  alert(limit.label + " should be between " + limit.min + " and " + limit.max + " " + limit.unit);
  // dataset.pv is seeded from the device values in getCur(); fall back to
  // blank if the field was never loaded with a valid value.
  input.value = input.dataset.pv !== undefined ? input.dataset.pv : "";
  input.style.borderColor = "";
  input.focus();
  return false;
}

// Live feedback while editing: a bad entry is only marked, never repaired.
// Reverting here would defeat the submit check - clicking Set Values blurs the
// input first, so a field repaired on blur is back in range by the time
// setCur() looks at it, and the values would save right after the user was
// told the entry was wrong. setCur() is the only place that alerts and reverts.
document.addEventListener("change", function (ev) {
  const t = ev.target;
  if (!t || !t.id) return;
  const limit = powerLimits()[t.id];
  if (!limit) return;
  if (isPowerValueInRange(t, limit)) {
    markPowerValue(t);
  } else {
    t.style.borderColor = "rgba(255, 0, 0, 0.5)";
  }
});

// The inputs carry no min/max in the markup - the table is the only place the
// limits are written. Applying them here keeps the spinner and the arrow keys
// clamped to the same range the guard enforces.
document.addEventListener("DOMContentLoaded", function () {
  const limits = powerLimits();
  for (const id in limits) {
    const input = el(id);
    if (!input) continue;
    input.min = limits[id].min;
    input.max = limits[id].max;
  }
});

// Enables the retry-minutes box that belongs to this restart checkbox: the
// first .del-min after it inside the same card. It used to walk
// parentElement.nextElementSibling, which assumed each row was its own div -
// a card is now a single grid of flat children, so that walk landed on null.
function restartChange(e){
  var scope = e.closest(".form-grid") || e.parentElement;
  var t = null;
  var candidates = scope.querySelectorAll(".del-min");
  for (var i = 0; i < candidates.length; i++) {
    if (e.compareDocumentPosition(candidates[i]) & Node.DOCUMENT_POSITION_FOLLOWING) {
      t = candidates[i];
      break;
    }
  }
  if (!t) return;
  if (e.checked==true){
    t.disabled=false;
  } else {
    t.disabled=true;
    t.value=null;
  }

}

function getCur() {
  // console.log("Getting power settings...");
  fetchWT("/api/getCur")
    .then((response) => {
      if (!response.ok) {
        return response.text().then((errorMessage) => {
            throw new Error(errorMessage || "HTTP Error: " + response.status);

        });
      }
      return response.json();
    })
    .then(function (result) {
      // console.log(result);
      // min/max are applied once on DOMContentLoaded from the same table, so
      // only the value is set here. It is remembered as the fallback an
      // out-of-range entry reverts to.
      const updateValue = (elementId, value) => {
        const element = el(elementId);
        element.value = (value === null || value === undefined) ? "" : value;
        markPowerValue(element);
      };
      try {
        updateValue("orCur", result.orCur);
        updateValue("drCur", result.drCur);
        updateValue("thCur", result.thCur?result.thCur:null);
        updateValue("lVol", result.lVol);
        updateValue("hVol", result.hVol);
        updateValue("orMin", result.isRstOl==1?result.orMin:null);
        updateValue("drMin", result.isRstDr==1?result.drMin:null);
        updateValue("lVolMin", result.isRstLv==1? result.lVolMin:null);
        updateValue("hVolMin", result.isRstHv==1? result.hVolMin:null);
        updateValue("startDelayMins", result.isRst==1?result.stDly:null);
        updateValue("maxRunTimeMins", result.mR !== undefined ? result.mR : null);
        updateValue("breakTimeMins", result.bT !== undefined ? result.bT : null);
        
        el("res-i-1").checked=result.isRst==1? true:false;
        el("res-dr").checked=result.isRstDr==1? true:false;
        el("res-ol").checked=result.isRstOl==1? true:false;
        el("res-lv").checked=result.isRstLv==1? true:false;
        el("res-hv").checked=result.isRstHv==1? true:false;

        // The schedule cards warn when a duration schedule has no
        // restart-after-power-resumption behind it; refresh them now that the
        // real setting is known, so the notice clears as soon as it is enabled.
        if (typeof refreshDurationRestartWarnings === "function") {
          refreshDurationRestartWarnings();
        }

        el("startDelayMins").disabled=result.isRst==1? false:true;
        el("drMin").disabled=result.isRstDr==1? false:true;
        el("orMin").disabled=result.isRstOl==1? false:true;
        el("lVolMin").disabled=result.isRstLv==1? false:true;
        el("hVolMin").disabled=result.isRstHv==1? false:true;

        el("thCur").disabled=result.isSlave === 1?false:true;
        if (result.is3Ph === 0) {
            _qsa(".sglphonly").forEach(div => {
                div.style.display = "flex";
            });
        } else {
            _qsa(".sglphonly").forEach(div => {
                div.style.display = "none";
            });
        }
        if (result.is3Ph === 2) {
            _qsa(".vsmronly").forEach(div => {
                div.style.display = "grid";
            });
            if (el("main-telemetry-grid")) {
                el("main-telemetry-grid").classList.add("vsmr-mode");
            }
        } else {
            _qsa(".vsmronly").forEach(div => {
                div.style.display = "none";
            });
            if (el("main-telemetry-grid")) {
                el("main-telemetry-grid").classList.remove("vsmr-mode");
            }
        }

        //_qs(".three-phase-device[value='" + result.is3Ph + "']").checked = true;
        _qs(".slave-control-device[value='" + result.isSlave + "']").checked = true;
        if (result.is3Ph === 0) {
          _qsa(".sglphonly").forEach(div => {
              div.style.display = "flex";
          });
        } else {
            _qsa(".sglphonly").forEach(div => {
                div.style.display = "none";
            });
        }
        const frVal = result.floatRelayadded == 1 ? "1" : "0";
        // Remembered for the home screen, which leaves schedule timings out
        // while the relay follows the flow sensor (see isFlowMode()).
        floatModeSetting = frVal == "1" ? (parseInt(result.floatMode, 10) || 0) : 0;
        if (frVal == "1") {
          el("floatSwitchArea").style.display = "block";
          const fsVal = result.floatMode;
          _qs('input[name="flSch"][value="' + fsVal + '"]').checked = true;
          const ftCont = el('ftCont');
          const flowDelayCont = el('flowDelayCont');
          if (fsVal == "1" || fsVal == "2") {
            ftCont.style.display = "flex";
            // Show flow delay fields only for flow detection mode (2)
            if (fsVal == "2") {
              flowDelayCont.style.display = "flex";
              // Load flow delay values (convert from seconds to display)
              if (result.flowDetectDelay !== undefined) {
                el("flowDetectDelay").value = result.flowDetectDelay;
              }
              if (result.flowStopDelay !== undefined) {
                el("flowStopDelay").value = result.flowStopDelay;
              }
            } else {
              flowDelayCont.style.display = "none";
            }
          } else {
            ftCont.style.display = "none";
            flowDelayCont.style.display = "none";
          }

          el("submitFloat").disabled = true;
        }
        else {
          el("floatSwitchArea").style.display = "none";
        }
      } catch (error) {
        console.error("Error during getCur:");
      }
    })
    .catch((error) => {
      console.error("Error during fetch");
      displayFailure(STD_ERROR);
      // Handle timeout or other errors
    });
}

function setCur() {
  var drCur = el("drCur").value;
  var thCur = el("thCur").value;
  var orCur = el("orCur").value; // Overload current
  var lVol = el("lVol").value;
  var hVol = el("hVol").value;

  var drMin = parseFloat(el("drMin").value) || 0;
  var orMin = parseFloat(el("orMin").value) || 0;
  var lVolMin = parseFloat(el("lVolMin").value) || 0;
  var hVolMin = parseFloat(el("hVolMin").value) || 0;

  // let selectedRadio = _qs(".res-i-1:checked");
  var isRstRqd = el("res-i-1").checked ? "1" : "0";
  var isRstDr = el("res-dr").checked ? "1" : "0";
  var isRstOl = el("res-ol").checked ? "1" : "0";
  var isRstLv = el("res-lv").checked ? "1" : "0";
  var isRstHv = el("res-hv").checked ? "1" : "0";

  var startDelayMins = el("startDelayMins").value;
  var maxRunTimeMins = parseFloat(el("maxRunTimeMins").value) || 0;
  var breakTimeMins = parseFloat(el("breakTimeMins").value) || 0;
  // let selectedRadio = _qs(".three-phase-device:checked");
  // var isThreePhaseDevice = selectedRadio ? selectedRadio.value : "0";
  selectedRadio = _qs(".slave-control-device:checked");
  var isSlaveControlDevice = selectedRadio ? selectedRadio.value : "0";

  console.log(
 "DR: " + drCur + " OR: " + orCur + " LV: " + lVol + " HV: " + hVol + " : " + thCur + " : " + isSlaveControlDevice
  );
  // Same min/max rule for every field in the section: alert, put the last good
  // value back and stop. Disabled fields are not editable, so they are skipped.
  const limits = powerLimits();
  for (const id in limits) {
    const input = el(id);
    if (!input || input.disabled) continue;
    if (!validatePowerInput(input)) return false;
  }

  if ( isSlaveControlDevice === "1" && parseFloat(thCur) >= parseFloat(drCur) ) {
    alert("Dry run check threshold should be less than Dry run current");
    return false;
  }
  if (( isRstDr === "1" && (drMin == 0))
      || ( isRstOl === "1" && (orMin == 0))
      || ( isRstHv === "1" && (hVolMin ==0))
      || ( isRstLv === "1" && (lVolMin ==0)))
    
  {
    alert("Restart delay after fault should be specified and should be atleast 1 (minute) ");
    return false;
  }
  var xhr = new XMLHttpRequest();

  xhr.addEventListener("load", function () {
    if (xhr.status == 200) {
      displaySuccess("Values Set successfully!");

    //   if (isThreePhaseDevice === "0") {
    //     _qsa(".sglphonly").forEach(div => {
    //         div.style.display = "flex";
    //     });
    //   } else {
    //       _qsa(".sglphonly").forEach(div => {
    //           div.style.display = "none";
    //       });
    //   }

    } else {
      displayFailure("Failed to set values! Please refresh and try again.");
    }
  });

  xhr.addEventListener("timeout", function () {
    displayFailure(
      "Failed to set values! Please refresh and try again. Connection timed out."
    );
  });

  xhr.open("POST", "/api/setCur", true);
  xhr.timeout = DEFAULT_TIMEOUT;
  xhr.setRequestHeader("Content-Type", "application/json");
  xhr.send(
    JSON.stringify({
      drCur: drCur,
      thCur: thCur,
      orCur: orCur,
      lVol: lVol,
      hVol: hVol,
      drMin: drMin,
      orMin: orMin,
      lVolMin: lVolMin,
      hVolMin: hVolMin,
      isRst: isRstRqd,
      isRstDr:isRstDr,
      isRstOl:isRstOl,
      isRstHv:isRstHv,
      isRstLv:isRstLv,
      stDly: startDelayMins,
      mR: maxRunTimeMins,
      bT: breakTimeMins,
      // is3Ph: isThreePhaseDevice,
      isSlave: isSlaveControlDevice,
    })
  );
}