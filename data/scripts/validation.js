


function validateDeviceName(input) {
  var deviceName = input.value.trim();
  var re = /^[a-zA-Z0-9_\-#@$]{4,20}$/;
  if (!re.test(deviceName)) {
    alert("Invalid Device Name. Please use alphabets, numbers and special characters Hiphen(-)/Underscore(_)/Hash(#)/At(@) only, No Space allowed. Min 4, Max 20 characters.");
    input.focus();
    return false;
  }
  return true;
}
function validateDevicePin(input) {
  var dp = input.value.trim();
  var re = /^[0-9]{4}$/;
  if (!re.test(dp)) {
    alert("Invalid Device Pin. Please enter a 4 digit number.");
    input.focus();
    return false;
  }
  return true;
}

function validateAPPass(input) {
  var dp = input.value.trim();
  var re = /^[a-zA-Z0-9!@#$%^&*()_+-=]{8,10}$/;
  if (!re.test(dp)) {
    alert("Invalid AP Password. Please enter password of length 8 to 10 characters.");
    input.focus();
    return false;
  }
  return true;
}

function validateEmail(input) {
  var email = input.value.trim();
  var re = /^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$/;
  if (!re.test(email)) {
    alert("Invalid Email Address. Please enter a valid email address.");
    input.focus();
    return false;
  }
  return true;
}

function validateSchedule(scheduleCard, action, isSaveAction=false) {
  console.log(action);
  var startTime = scheduleCard.querySelector("#start-time") ? scheduleCard.querySelector("#start-time").value : "";
  var endTime = scheduleCard.querySelector("#end-time") ? scheduleCard.querySelector("#end-time").value : "";
  var typeSelect = scheduleCard.querySelector("#sch-type") ? scheduleCard.querySelector("#sch-type").value : "";
  var days = scheduleCard.querySelectorAll(".day-check:checked").length;
  var errorBox = scheduleCard.querySelector(".er-box");

  if (!startTime || !endTime || typeSelect === "" || (typeSelect === "weekly" && days < 1)) {
      errorBox.innerHTML = "⚠ Please fill in all required fields " + action + ".";
      errorBox.style.display = "block";
      //scheduleCard.style.border = "2px solid red";
      return false;
  } else if (!isSaveAction && scheduleCard.classList.contains("sch-changed")) {
        errorBox.innerHTML = "⚠ Please save or cancel the changes.";
        errorBox.style.display = "block";
        return false;
  }
  
  var sp = startTime.split(":");
  var ep = endTime.split(":");
  var startTotal = (parseInt(sp[0], 10) || 0) * 60 + (parseInt(sp[1], 10) || 0);
  var endTotal = (parseInt(ep[0], 10) || 0) * 60 + (parseInt(ep[1], 10) || 0);
  var gap = endTotal - startTotal;
  if (gap <= 0) gap += 24 * 60; // handle overnight

  var modeSelect = scheduleCard.querySelector('#sch-mode');
  if (modeSelect && modeSelect.value === 'duration') {
    // Duration Based: the entered duration cannot exceed the start-end gap
    var schDurTotal = (parseInt(scheduleCard.querySelector('#schDurHrs').value, 10) || 0) * 60 + (parseInt(scheduleCard.querySelector('#schDurMins').value, 10) || 0);
    if (schDurTotal === 0) {
      errorBox.innerHTML = "⚠ Please enter a valid duration.";
      errorBox.style.display = "block";
      return false;
    }
    if (schDurTotal > gap) {
      errorBox.innerHTML = "⚠ Duration cannot exceed the gap between start and end time (" + Math.floor(gap / 60) + "h " + (gap % 60) + "m).";
      errorBox.style.display = "block";
      return false;
    }
  } else {
    // Cyclic: On/Off durations cannot exceed the gap between start and end time
    var isCyclicBox = scheduleCard.querySelector('#isCyclic');
    if (isCyclicBox && isCyclicBox.checked) {
      var onTotal = (parseInt(scheduleCard.querySelector('#onDurHrs').value, 10) || 0) * 60 + (parseInt(scheduleCard.querySelector('#onDurMins').value, 10) || 0);
      var offTotal = (parseInt(scheduleCard.querySelector('#offDurHrs').value, 10) || 0) * 60 + (parseInt(scheduleCard.querySelector('#offDurMins').value, 10) || 0);
      if (onTotal > gap) {
        errorBox.innerHTML = "⚠ On duration cannot exceed the gap between start and end time (" + Math.floor(gap / 60) + "h " + (gap % 60) + "m).";
        errorBox.style.display = "block";
        return false;
      }
      if (offTotal > gap) {
        errorBox.innerHTML = "⚠ Off duration cannot exceed the gap between start and end time (" + Math.floor(gap / 60) + "h " + (gap % 60) + "m).";
        errorBox.style.display = "block";
        return false;
      }
    }
  }



  return true;
  
}
var WEEK_DAYS = ["sun","mon","tue","wed","thu","fri","sat"];

// An overnight schedule (end <= start) is split into two segments:
// start->midnight on its own day, and midnight->end on the following day.
function scheduleSegments(sch, days) {
  let startMin = parseInt(sch.startHour) * 60 + parseInt(sch.startMinute);
  let endMin = parseInt(sch.endHour) * 60 + parseInt(sch.endMinute);
  let dayIdx = days.map(day => WEEK_DAYS.indexOf(day));
  if (endMin <= startMin) {
    return [
      { start: startMin, end: 24 * 60, days: dayIdx },
      { start: 0, end: endMin, days: dayIdx.map(d => (d + 1) % 7) }
    ];
  }
  return [{ start: startMin, end: endMin, days: dayIdx }];
}

function doIntervalsOverlap(sch1, sch2, days1, days2) {
  let segs1 = scheduleSegments(sch1, days1);
  let segs2 = scheduleSegments(sch2, days2);
  for (let a = 0; a < segs1.length; a++) {
    for (let b = 0; b < segs2.length; b++) {
      let commonDay = segs1[a].days.some(day => segs2[b].days.includes(day));
      if (commonDay && Math.max(segs1[a].start, segs2[b].start) < Math.min(segs1[a].end, segs2[b].end)) {
        return true;
      }
    }
  }
  return false;
}

function validateSchedulesForClash(schedules) {
  for (let i = 0; i < schedules.length; i++) {
      for (let j = i + 1; j < schedules.length; j++) {
          let days1 = schedules[i].days.length ? schedules[i].days : ["sun","mon","tue","wed","thu","fri","sat"];
          let days2 = schedules[j].days.length ? schedules[j].days : ["sun","mon","tue","wed","thu","fri","sat"];

          if (doIntervalsOverlap(schedules[i], schedules[j], days1, days2)) {
              return false; // Overlap detected
          }
      }
  }
  return true; // No overlaps found
}

function viewPassword(elementId){
  var x = document.getElementById(elementId);
  const iconElement = x.nextElementSibling.querySelector('.icon'); // Get the next sibling and find the icon

  if (x.type === "password") {
    x.type = "text";
    iconElement.classList.add('open');
  } else {
    x.type = "password";
    iconElement.classList.remove('open');
  }
}
// function fetchWT(url, options = {}, timeoutSec = DEFAULT_TIMEOUT) {
//   return new Promise((resolve, reject) => {
//     const timer = setTimeout(() => {
//       reject(new Error("Request timed out"));
//     }, timeoutSec);

//     fetch(url, options)
//       .then((response) => {
//         clearTimeout(timer);
//         resolve(response);
//       })
//       .catch((error) => {
//         clearTimeout(timer);
//         reject(error);
//       });
//   });
// }
// function displaySuccess( message){
//   var successSymbol = '<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><path d="M20.28 2l-11.28 11.53-5.29-5.05-3.28 3.07 9 8.93 14.3-13.65z"/></svg>';
//   //el("popupText").innerHTML = successSymbol + " " + message;
//   el("popupText").innerHTML = successSymbol + "✅ " + message;
//   el("popupText").textContent = message;
//   el("popup").style.display = "block";
//   setTimeout(function () {
//     el("popup").style.display = "none";
//   }, 4000);
// }

// function displayFailure( message){
//   var failureSymbol = '<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24"><path d="M12 2C6.48 2 2 6.48 2 12C2 17.52 6.48 22 12 22C17.52 22 22 17.52 22 12C22 6.48 17.52 2 12 2ZM8.59 16.97L10.17 15.39L11.75 16.97L13.33 15.39L15.82 16.97L13.33 18.59L11.75 16.97L10.17 18.59L8.59 16.97Z"/></svg>';
//   el("popupText").innerHTML = failureSymbol + " " + message;
//    el("popupText").textContent = message;
//   el("popup").style.display = "block";
//   setTimeout(function () {
//     el("popup").style.display = "none";
//   }, 4000);
// }

//el('uploadForm').onsubmit = function(e) {
function firmwareUpdate(e){
  e.preventDefault();

  // Guard against double submission (e.g. double-tap on mobile)
  if (firmwareUpdate._uploading) return false;

  const pin = prompt("Please enter device pin to update firmware");
  if (!pin) {
      alert("PIN is required!");
      return false;
  }

  const form = e.target;
  const firmwareFile = form["firmware"].value;
  if (!firmwareFile) {
      alert("Please select the firmware file to upload");
      return false;
  }

  const submitBtn = form.querySelector('input[type="submit"]');
  const overlay = document.getElementById('fw-upload-overlay');

  firmwareUpdate._uploading = true;
  if (submitBtn) { submitBtn.disabled = true; submitBtn.value = "Uploading..."; }
  if (overlay) { overlay.style.display = 'flex'; }

  const formData = new FormData(form);

  // A plain timeout only rejects the promise - the upload keeps streaming to
  // the device in the background. The user then retries and the second POST
  // collides with the first, which is how "Failed to Begin" used to appear.
  // AbortController makes the timeout actually close the socket, so the device
  // sees the transfer end and releases the OTA session.
  const abort = new AbortController();
  const uploadTimeout = setTimeout(() => abort.abort(), 240000);

  fetch(form.action, {
      method: 'POST',
      headers: { 'X-Upload-Pin': pin },
      body: formData,
      signal: abort.signal
    })
    .then(response => {
        clearTimeout(uploadTimeout);
        if (!response.ok) {
            // Server returns specific text for each failure (PIN required,
            // Wrong device Pin, Device is ON, Invalid file name, etc.).
            // Show that instead of a hard-coded message.
            return response.text().then((errorMessage) => {
                throw new Error('Upload failed: ' + errorMessage);
            });
        }
        return response.text();
    })
    .then(() => {
        if (overlay) { overlay.style.display = 'none'; }
        displaySuccess('Upload successful! System will restart now...  ');
        setTimeout(() => {
          location.reload();
        }, 5000);
    })
    .catch(error => {
        clearTimeout(uploadTimeout);
        if (overlay) { overlay.style.display = 'none'; }
        firmwareUpdate._uploading = false;
        if (submitBtn) { submitBtn.disabled = false; submitBtn.value = "Update Firmware"; }
        // A dropped connection is reported as a bare TypeError ("Failed to
        // fetch"), which tells the user nothing and - because the device may
        // well have flashed and restarted - is not even reliably a failure.
        if (error.name === 'AbortError') {
            alert('Upload timed out. Check the firmware version before retrying.');
        } else if (error instanceof TypeError) {
            alert('Connection to the device was lost during the upload. '
                + 'Reconnect and check the firmware version - the update may have completed. '
                + 'If it did not, wait 30 seconds before retrying.');
        } else {
            alert(error.message || 'Upload failed');
        }
    });

  return false;
};