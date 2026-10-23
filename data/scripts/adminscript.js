// Enable/disable custom inputs
const rBtns = _qsa('input[name="timing"]');
const inputs = _qsa('.timer-config input');
const selects = _qsa('.timer-config select');
const errMsg = el('errMsg');

rBtns.forEach(r => {
    r.addEventListener('change', function () {
        const isCustom = this.value === 'CUSTOM';
        inputs.forEach(i => {
            i.disabled = !isCustom;
            if (!isCustom) i.value = '0';
        });
        selects.forEach(s => s.disabled = !isCustom);
        if (!isCustom) errMsg.style.display = 'none';
    });
});


const threeP = _qsa('input[name="3ph-device"]');
const exTapOpt = _qsa('input[name="exTapOpt"]');
const tapDurInp = el('tap_secs');
const vsmrMacInp = el('vsmr_mac');

// The MAC only means anything for "3-Phase with VSMR" (option 2) - for the other
// two the box is disabled and cleared, so a stale pairing cannot be saved.
function applyVsmrMacState(option) {
    if (!vsmrMacInp) return;
    if (option === '2') {
        vsmrMacInp.disabled = false;
    } else {
        vsmrMacInp.disabled = true;
        vsmrMacInp.value = '';
    }
}

threeP.forEach(radio => {
    radio.addEventListener('change', function () {
        applyVsmrMacState(this.value);
    });
});

exTapOpt.forEach(radio => {
    radio.addEventListener('change', function () {
        const option = this.value;
        if (option === '2') {
            tapDurInp.disabled = false;
        } else {
            tapDurInp.disabled = true;
            tapDurInp.value = '';
        }
    });
});


document.addEventListener('DOMContentLoaded', () => {
    loadTimingConfig();
    //setup3PhaseSettings();
    load3PhaseSettings();
  });

function submitTimingConfig() {
  const selT = _qs('input[name="timing"]:checked').value;
  let tData;

  if (selT === 'CUSTOM') {
    const b1 = el('btn1').value;
    const b2 = el('btn2').value;
    const b3 = el('btn3').value;
    const b4 = el('btn4').value;

    if (!b1 || !b2 || !b3 || !b4) {
      errMsg.style.display = 'block';
      return;
    }

    errMsg.style.display = 'none';

    tData = {
      tConf: selT,
      b1: { value: b1, unit: el('unit1').value },
      b2: { value: b2, unit: el('unit2').value },
      b3: { value: b3, unit: el('unit3').value },
      b4: { value: b4, unit: el('unit4').value }
    };
  } else {
    tData = { tConf: selT };
  }

  var pin = prompt("Please enter admin pin to set timing");
  if (pin != null) {
    tData.pin = pin;
    var data = JSON.stringify(tData);
    console.log(data);
    fetchWT('/adminapi/setTimingConfig', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: data
    })
    .then((response) => {
      if (!response.ok) {
        return response.text().then((errMsg) => { throw new Error(errMsg); });
      }
      return response.text();
    })
    .then(data => {
      console.log('Success:', data);
      alert('Timing set successfully!');
    })
    .catch(error => {
      console.error('Error:', error.message);
      alert('Error submitting configuration');
    });
  }
}
function submit3PhaseSettings() {
  const is3P = _qs('input[name="3ph-device"]:checked').value;
  const tapOpt = _qs('input[name="exTapOpt"]:checked').value;
  const shuntOrCT = _qs('input[name="shuntOrCT"]:checked').value;
  const ledType = _qs('input[name="ledType"]:checked').value;

  const tapDur = el('tap_secs').value;
  const ethernetEnabled = _qs('input[name="ethernet-device"]:checked').value;
  const leakDetectionEnabled = _qs('input[name="leak-detection"]:checked').value;
  const inrushSecs = parseInt(el('inrush_secs').value, 10) || 0;

  if (inrushSecs < 0 || inrushSecs > 120) {
    alert('Inrush settle duration must be between 0 and 120 seconds');
    return;
  }

if (tapOpt === '2' && !tapDur) {
    alert('Please enter tapping duration');
    return;
}
if (tapOpt === '2' && tapDur > 60) {
    alert('Tapping duration cannot be more than 60 seconds');
    return;
}
if (tapOpt === '1' || tapOpt === '0') {
    el('tap_secs').value = '';
}

  const vsmrMac = (el('vsmr_mac').value || '').trim().toUpperCase();
  if (is3P === '2' && vsmrMac && !/^([0-9A-F]{2}:){5}[0-9A-F]{2}$/.test(vsmrMac)) {
    alert('Enter a valid VSMR MAC (AA:BB:CC:DD:EE:FF), or leave it blank to auto-learn while pairing.');
    return;
  }

  const sData = {
    "3p": is3P,
    tapR: tapOpt,
    tapD: tapDur,
    ethE: ethernetEnabled,
    leakE: leakDetectionEnabled,
    sOrC: shuntOrCT,
    ledT: ledType,
    inRush: inrushSecs,
    vsmrMac: vsmrMac,
  };

  const pin = prompt("Please enter admin pin to set 3-phase settings");
  if (pin != null) {
    sData.pin = pin;
    const data = JSON.stringify(sData);
    console.log(data);

    fetchWT('/adminapi/3phasesettings', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: data
    })
    .then(response => {
      if (!response.ok) {
        return response.text().then(errorMessage => { throw new Error(errorMessage); });
      }
      return response.text();
    })
    .then(data => {
      console.log('Success:', data);
      alert('3-phase settings saved successfully!');
    })
    .catch(error => {
      console.error('Error:', error.message);
      alert('Error submitting 3-phase settings');
    });
  }
}

function loadTimingConfig() {
  fetchWT('/adminapi/getTimingConfig')
    .then(response => {
      if (!response.ok) {
        throw new Error('Failed to fetch timing configuration');
      }
      return response.json();
    })
    .then(data => {
      const tConfig = data.tConf;
      _qs('input[name="timing"][value="' + tConfig + '"]').checked = true;

      if (tConfig === 'CUSTOM') {
        el('btn1').value = data.b1.value;
        el('unit1').value = data.b1.unit;
        el('btn2').value = data.b2.value;
        el('unit2').value = data.b2.unit;
        el('btn3').value = data.b3.value;
        el('unit3').value = data.b3.unit;
        el('btn4').value = data.b4.value;
        el('unit4').value = data.b4.unit;

        inputs.forEach(i => i.disabled = false);
        selects.forEach(s => s.disabled = false);
      } else {
        inputs.forEach(i => i.disabled = true);
        selects.forEach(s => s.disabled = true);
      }
    })
    .catch(error => {
      console.error('Error:', error.message);
      alert('Error loading timing configuration');
    });
}

function load3PhaseSettings() {
    fetchWT('/adminapi/get3PhaseSettings')
        .then(response => {
            if (!response.ok) {
                throw new Error('Failed to fetch 3-phase settings');
            }
            return response.json();
        })
        .then(data => {
            _qs('input[name="3ph-device"][value="' + data["3p"] + '"]').checked = true;
            _qs('input[name="exTapOpt"][value="' + data.tapR + '"]').checked = true;
            _qs('input[name="ethernet-device"][value="' + data.ethE + '"]').checked = true;
            _qs('input[name="leak-detection"][value="' + (data.leakE || "0") + '"]').checked = true;

            _qs('input[name="shuntOrCT"][value="' + data.sOrC + '"]').checked = true;
            _qs('input[name="ledType"][value="' + data.ledT + '"]').checked = true;

            el('macIdText').innerHTML = data.mac;
            if (vsmrMacInp) vsmrMacInp.value = data.vsmrMac || '';
            applyVsmrMacState(String(data["3p"]));
            el('tap_secs').value = data.tapD;
            el('inrush_secs').value = (data.inRush !== undefined && data.inRush !== null) ? data.inRush : 0;

            const option = data.tapR;

            if (option === 2 ) {
                tapDurInp.disabled = false;
            } else {
                tapDurInp.disabled = true;
                tapDurInp.value = '';
            }


        })
        .catch(error => {
            console.error('Error:', error.message);
            alert('Error loading 3-phase settings');
        });
}