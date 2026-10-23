const ccMsg = 
    'Your device data will be uploaded to cloud and you can access your device with internet.<br><br>' +
    'Have you read the privacy policy document ' +
    '<a href="https://shop.eviqtech.com/policies/terms-of-service" target="_blank" style="color:#007bff;text-decoration:underline;">https://shop.eviqtech.com/policies/terms-of-service</a>?<br><br>' +
    'Do you want to continue?';

const cdMsg =
    'Your device will be disconnected from cloud and device data will not be uploaded to cloud.<br><br>' +
    'Do you want to continue?'; 

function popupDisplay(popupType) {
  // Create the popup overlay
  const ppO = _ce("div");
  ppO.className = "popup-overlay";

  // Create the popup box
  const ppB = _ce("div");
  ppB.className = "popup-box";

  // Message with clickable link
  const msg = _ce("div");
  msg.className = "popup-message";
  
  if (popupType === "connectCloud") {
    msg.innerHTML = ccMsg;
  } else if (popupType === "disconnectCloud") {
    msg.innerHTML = cdMsg;
  }

  const btnYes = _ce("button");
  btnYes.textContent = "Yes";
  btnYes.className = "popup-btn-yes";

  const btnCancel = _ce("button");
  btnCancel.textContent = "Cancel";
  btnCancel.className = "popup-btn-cancel";

  btnYes.onclick = function () {
    document.body.removeChild(ppO);
    // Continue with the rest of the function
    if (popupType === "connectCloud") {
      proceedConnectCloud();
    } else if (popupType === "disconnectCloud") {
      proceedDisconnectCloud();
    }
  };
  
  btnCancel.onclick = function () {
    document.body.removeChild(ppO);
    // Stop further execution
  };

  ppB.appendChild(msg);
  ppB.appendChild(btnYes);
  ppB.appendChild(btnCancel);
  ppO.appendChild(ppB);
  document.body.appendChild(ppO);
}

function connectCloud() {
    const popupType = "connectCloud";  
    popupDisplay(popupType);
}
function proceedConnectCloud() {
  var emailObj = el("clEmail");
  //if (!validateEmail(emailObj)) return;
  var email = emailObj.value.trim();

  var data = JSON.stringify({ email: email });

  fetchWT("/api/connectCloud", {
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
      alert(result);
    })
    .catch((error) => {
      console.log(error.message);
      alert(error);
    });
}

function pauseCloud(operation) {
  if (operation == "disconnect" ) {
    const popupType = "disconnectCloud";  
    popupDisplay(popupType);
  }
}
function proceedDisconnectCloud() {
    
    var data = JSON.stringify({ operation: "disconnect" });

    fetchWT("/api/pauseCloud", {
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
      displaySuccess(
        result
      );
    })
    .catch((error) => {
      console.log(error.message);
      displayFailure(error);
    });
}

function enableFloat() {
  const floatMode = _qs('input[name="flSch"]:checked').value;
  if(floatMode === "1" || floatMode === "2")
  {
    el("ftCont").style.display = "flex";
    // Show flow delay fields only for flow detection mode (2)
    if(floatMode === "2") {
      el("flowDelayCont").style.display = "flex";
    } else {
      el("flowDelayCont").style.display = "none";
    }
  }
  else{
   el("ftCont").style.display= "none";
   el("flowDelayCont").style.display = "none";
  }
  if(floatMode === "1" || floatMode === "2" || floatMode==="0"){
  const flTxt = el("flTxt").value.trim();
  // Build request body - only include delay values if they are entered (including 0)
  const requestBody = { floatMode: floatMode, flTxt: flTxt };
  
  // Get flow delay values (in seconds) - only include if value is entered
  if (floatMode === "2") {
    const flowDetectDelayValue = el("flowDetectDelay").value.trim();
    const flowStopDelayValue = el("flowStopDelay").value.trim();
    
    // Only add to request body if value is entered (empty string means not entered)
    if (flowDetectDelayValue !== "") {
      requestBody.flowDetectDelay = parseInt(flowDetectDelayValue);
    }
    if (flowStopDelayValue !== "") {
      requestBody.flowStopDelay = parseInt(flowStopDelayValue);
    }
  }
  
  fetch("/api/enableFloat", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
    },
    body: JSON.stringify(requestBody),
  })
    .then((response) => {
      if (!response.ok) {
        return response.text().then((errorMessage) => {
          throw new Error(errorMessage || "HTTP Error: " + response.status);
        });
      }
      return response.text();
    })
    .then((data) => {
      alert("Float device support updated successfully.");
      getCur(); // Refresh the float status after enabling/disabling float device support
    })
    .catch((error) => {
      console.error("Error enabling float device support:", error.message);
      alert("Failed to update float device support: " + error.message);
    });
}
}

// // Float enable/disable Logic
// // el('submitFloat').classList.add('button'); // Ensure the button class is applied
// // Disable the button by default
// const submitFloatButton = el('submitFloat');
// submitFloatButton.disabled = true;
// submitFloatButton.classList.add('button');
// _qsa('input[name="flSch"]').forEach(radio => {
//   radio.addEventListener('change', function () {
//     const ftCont = el('ftCont');
//     const enableFloatButton = el('submitFloat');
//     if (this.value === "1") {
//       ftCont.style.display = "flex";
//       const requiredText = '220V is not connected to float switch';
//       enableFloatButton.disabled = this.value !== "1" || el('flTxt').value !== requiredText;
//     } else {
//       ftCont.style.display = "none";
//       enableFloatButton.disabled = false;
//     }
// });
// });

// el('flTxt').addEventListener('input', function () {
//   const enableFloatButton = el('submitFloat');
//   const requiredText = '220V is not connected to float switch';
//   enableFloatButton.disabled = this.value !== requiredText;
// });

function showFirmwareVersion(tableId, apiUrl) {
  const versionURL = "/api/getFirmwareVersion";

  fetchWT(versionURL)
    .then((response) => {
      if (!response.ok) {
        throw new Error(
          "There is a problem with firmware version fetch operation: " +
            response.statusText
        );
      }
      return response.text();
    })
    .then((data) => {
      el("firmwareVersion").innerHTML = data;
    })
    .catch((error) => {
      console.error(
        "There has been a problem with your fetch operation:",
        error
      );
      displayFailure(STD_ERROR);
    });
}


function saveDeviceName() {
  var nameObj = el("deviceName");
  if (!validateDeviceName(nameObj)) return;
  var name = nameObj.value.trim();

  var enteredPin = prompt(
    "Do you want to change the device name? Please enter your device pin to proceed..."
  );
  if (enteredPin === null) return;
  if (enteredPin.trim() === "") {
    displayFailure("Please enter the device pin.");
    return;
  }
  {
    var data = JSON.stringify({ deviceName: name, enteredPin: enteredPin });

    fetchWT("/api/setDeviceName", {
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
      const ulTag = _ce("ul");
      var liTag = _ce("li");
      liTag.textContent = "Device name is saved successfully.";
      ulTag.appendChild(liTag);
      liTag = _ce("li");
      liTag.textContent =
        "If you are connected to WiFi, You need to access the device with new URL("
         + name + ".local" + ").";
      ulTag.appendChild(liTag);
      liTag = _ce("li");
      liTag.textContent =
        "Else you need to connect to a new Wifi access point (" 
        + name + "_AP" +") and access the device with new URL(" 
        + name + ".local" + ").";
      ulTag.appendChild(liTag);
      liTag = _ce("li");
      liTag.textContent =
        "Connect to new AP now and after that Click the link below to refresh.";
      const link = _ce("a");
      link.textContent = "Refresh Page";
      link.style.color = "cyan";
      link.href = "#";
      link.addEventListener("click", () => {
        window.location.href = "http://" + name + ".local";
      });
      liTag.appendChild(_ce("br"));
      liTag.appendChild(link);
      ulTag.appendChild(liTag);

      el("popupText").innerHTML = "";
      el("popupText").appendChild(ulTag);
      el("popup").style.display = "block";
    })
    .catch((error) => {
      console.log(error.message);
      displayFailure("Setting device name failed: " + error.message);
    });
  }
}
function saveAPPass() {
  var passObj = el("APPass");
  if (!validateAPPass(passObj)) return;
  var pass = passObj.value.trim();

  var enteredPin = prompt(
    "Do you want to change the device AP Password? Please enter your device pin to proceed..."
  );
  if (enteredPin === null) return;
  if (enteredPin.trim() === "") {
    displayFailure("Please enter the device pin.");
    return;
  }
  {
    var data = JSON.stringify({ APPass: pass, enteredPin: enteredPin });

    fetchWT("/api/setAPPass", {
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
      displaySuccess(
        "AP password is changed successfully. After a device restart, you will need to connect to the device's AP with newly set Password."
      );
    })
    .catch((error) => {
      console.log(error.message);
      displayFailure("Setting AP Password failed: " + error.message);
    });
  }
}

function saveDevicePin() {
  var pinObj = el("devicePin");
  if (!validateDevicePin(pinObj)) 
    return;
  var newPin = pinObj.value.trim();

  var enteredPin = prompt(
    "Do you want to change the device pin? Please enter your current device pin to proceed..."
  );
  if (enteredPin === null) return;
  if (enteredPin.trim() === "") {
    displayFailure("Please enter the device pin.");
    return;
  }
  {
    var data = JSON.stringify({ enteredPin: enteredPin, newPin: newPin });

    fetchWT("/api/setDevicePin", {
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
      const message = "Device pin is changed successfully";
      displaySuccess(message);
    })
    .catch((error) => {
      console.error("Error setting pin:", error.message);
      displayFailure("Setting device pin failed: " + error.message);
    });
  }
}
function restartDevicePrompt() {
  var pin = prompt("Please enter device pin to restart the device");
  if (pin === null) return;
  if (pin.trim() === "") {
    displayFailure("Please enter the device pin.");
    return;
  }
  {
    var data = JSON.stringify({ enteredPin: pin });

    fetchWT("/api/restartDevice", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: data,
    }) // Set a 5-second timeout
      .then((response) => {
        if (!response.ok) {
          return response.text().then((errorMessage) => {
            throw new Error(errorMessage);
          });
        }
        return response.text();
      })
      .then((result) => {
        displaySuccess("Device restarting...");
        setTimeout(() => {
          location.reload();
        }, 5000);
      })
      .catch((error) => {
        console.error(error.message);
        displayFailure(error.message);
      });
  }
}


function clearLogs() {
  var pin = prompt(
    "Do you want to clear all the operation logs of the device? Please enter the device pin to clear the logs."
  );
  if (pin === null) return;
  if (pin.trim() === "") {
    displayFailure("Please enter the device pin.");
    return;
  }
  {
    var data = JSON.stringify({ enteredPin: pin });

    //fetchWT("/api/clearLogs", { method: "DELETE" })
    fetchWT("/api/clearLogs", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: data,
    })
      .then((response) => {
        if (!response.ok) {
          return response.text().then((errorMessage) => {
            throw new Error(errorMessage || "HTTP Error: ");
          });
        }
        return response.text();
      })
      .then((data) => {
        console.log("Logs cleared:", data);
        displaySuccess("Switch operation logs cleared.");
        const logContentDiv = _qs(".log-content");
        logContentDiv.innerHTML = "";
      })
      .catch((error) => {
        console.error("Error during fetch:", error.message);
        displayFailure("Error clearing logs: " + error.message);
        // Handle timeout or other errors
      });
  }
}

function resetCumEnergy() {
  var pin = prompt(
    "Do you want to reset the cumulative energy counter? Please enter device pin to reset."
  );
  if (pin === null) return;
  if (pin.trim() === "") {
    displayFailure("Please enter the device pin.");
    return;
  }
  {
    var data = JSON.stringify({ enteredPin: pin });

    //fetchWT("/api/resetCumulativeEnergy", { method: "DELETE" })
    fetchWT("/api/resetCumulativeEnergy", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: data,
    })
      .then((response) => {
        if (!response.ok) {
          return response.text().then((errorMessage) => {
            throw new Error(errorMessage);
          });
        }
        return response.text();
      })
      .then((data) => {
        console.log("Cumulative energy is reset:", data);
        displaySuccess("Cumulative energy is reset.");
      })
      .catch((error) => {
        console.error("Error resetting cumulative energy:", error.message);
        displayFailure("Error resetting cumulative energy: " + error.message);
        // Handle timeout or other errors
      });
  }
}
function logout() {
  fetchWT("/api/logout", { method: "DELETE" })
    .then((response) => {
      if (!response.ok) {
        return response.text().then((errorMessage) => {
            throw new Error(errorMessage || "HTTP Error: " + response.status);

        });
      }
      return response.text();
    })
    .then((data) => {
      console.log("logged out", data);
      window.location.href = "login.html";
    })
    .catch((error) => {
      console.error("Error logging out:", error.message);
      window.location.href = "login.html";
    });
}

function ClearFaultConditionPrompt(interruptionType) {
  if (interruptionType == 1) {
    var message = "Device operation was interrupted Manually, Do you want to continue the schedule? Enter the pin to restart the device.";
  } else if (interruptionType == 2) {
    var message = "Are you sure the Fault conditions like low voltage, high voltage, dry-run, over-current are externally fixed?  If you are sure, Enter the pin to reset the fault condition and restart the device.";
  } else if (interruptionType == 3) {
    var message = "There was a power interruption, Do you want to continue the schedule? Enter the pin to restart the device.";
  } else if (interruptionType == 4) {
    var message = "Device operation was interrupted Manually, Do you want to continue the Flow function? Enter the pin to restart the device";
  }else {
    var message = "Are you sure the Fault conditions like low voltage, high voltage, dry-run, over-current are externally fixed? By resetting, Manual stop will override and any scheduled Run will continue the schedule immediately. If you are sure, Enter the pin to reset the fault condition.";
  }
  var pin = prompt(message);
  if (pin === null) return;
  if (pin.trim() === "") {
    displayFailure("Please enter the device pin.");
    return;
  }
  {
    var data = JSON.stringify({ enteredPin: pin });

    fetchWT("/api/clearFaultConditions", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: data,
    }) // Set a 5-second timeout
      .then((response) => {
        if (!response.ok) {
          return response.text().then((errorMessage) => {
            throw new Error(errorMessage);
          });
        }
        return response.text();
      })
      .then((result) => {
        displaySuccess("Fault conditions are reset/cleared");
      })
      .catch((error) => {
        console.error(error.message);
        displayFailure(error.message);
      });
  }
}

// Master-Slave Configuration Functions
function loadMasterSlaveSettings() {
  fetchWT('/api/getMasterSlaveSettings')
    .then(response => {
      if (!response.ok) {
        throw new Error('Failed to load Master-Slave settings');
      }
      return response.json();
    })
    .then(data => {
      // Load master-slave configuration
      if (data.masterSlaveMode !== undefined) {
        const radio = _qs('input[name="master-slave-mode"][value="' + data.masterSlaveMode + '"]');
        if (radio) {
          radio.checked = true;
        }
      } else {
        const defaultRadio = _qs('input[name="master-slave-mode"][value="0"]');
        if (defaultRadio) {
          defaultRadio.checked = true;
        }
      }

      if (data.masterDeviceUrl) {
        const urlInput = el('mstr_url');
        if (urlInput) {
          urlInput.value = data.masterDeviceUrl;
        }
      } else {
        const urlInput = el('mstr_url');
        if (urlInput) {
          urlInput.value = '';
        }
      }

      if (data.mPin) {
        const pinInput = el('mstr_pin');
        if (pinInput) {
          pinInput.value = data.mPin;
        }
      } else {
        const pinInput = el('mstr_pin');
        if (pinInput) {
          pinInput.value = '';
        }
      }

      // Show/hide master device URL field based on mode
      const mstrUrlD = el('mstrUrlD');
      if (mstrUrlD && data.masterSlaveMode === 2) {
        mstrUrlD.style.display = 'block';
        // Load remote switching method
        let rmtMethods = data.rmtMethods || null;
        if (rmtMethods) {
          // rmtMethods is stored as comma-separated string (e.g., 'local,mqtt' or 'local' or 'mqtt')
          // Handle both string and array formats for flexibility
          let methods = [];
          if (typeof rmtMethods === 'string') {
            // Split comma-separated values
            methods = rmtMethods.split(',').map(m => m.trim()).filter(m => m);
          } else if (Array.isArray(rmtMethods)) {
            methods = rmtMethods;
          }
          
          // Check the corresponding checkboxes for each method
          methods.forEach(method => {
            const methodCheckbox = _qs('input[name="remoteMth"][value="' + method + '"]');
            if (methodCheckbox) {
              methodCheckbox.checked = true;
            }
          });
        }
        // Show/hide local WiFi fields based on checkbox state
        toggleRemote();
      } else if (mstrUrlD) {
        mstrUrlD.style.display = 'none';
      }
    })
    .catch(error => {
      console.error('Error loading master-slave settings:', error.message);
    });
}

function toggleRemote() {
  const locWifiD = el('locWifiD');
  const localWifiCheckbox = _qs('input[name="remoteMth"][value="local"]');
  if (locWifiD && localWifiCheckbox) {
    if (localWifiCheckbox.checked) {
      locWifiD.style.display = 'flex';
      // Make fields required
      const urlInput = el('mstr_url');
      const pinInput = el('mstr_pin');
      if (urlInput) urlInput.required = true;
      if (pinInput) pinInput.required = true;
    } else {
      locWifiD.style.display = 'none';
      // Make fields not required
      const urlInput = el('mstr_url');
      const pinInput = el('mstr_pin');
      if (urlInput) urlInput.required = false;
      if (pinInput) pinInput.required = false;
    }
  }
}

function initMasterSlaveEventListeners() {
  // Add event listeners for master-slave mode radio buttons
  const masterSlaveModeRadios = document.querySelectorAll('.master-slave-mode');
  const mstrUrlD = el('mstrUrlD');

  if (masterSlaveModeRadios && mstrUrlD) {
    masterSlaveModeRadios.forEach(radio => {
      radio.addEventListener('change', function() {
        if (this.value === '2') { // Remote switch device selected
          mstrUrlD.style.display = 'block';
          // Check current checkbox state and toggle accordingly
          toggleRemote();
        } else {
          mstrUrlD.style.display = 'none';
        }
      });
    });
  }

  // Add event listeners for remote switching method checkboxes
  const rmtMethods = document.querySelectorAll('.remoteMth');
  if (rmtMethods) {
    rmtMethods.forEach(checkbox => {
      checkbox.addEventListener('change', function() {
        // Only toggle if Local WiFi checkbox is being changed
        if (this.value === 'local') {
          toggleRemote();
        }
      });
    });
  }
}

function submitMasterSlaveSettings() {
  // Get the master-slave values
  const masterSlaveMode = _qs('input[name="master-slave-mode"]:checked').value;
  const masterDeviceUrl = el('mstr_url').value.trim();
  const mPin = el('mstr_pin').value.trim();
  
  // Get remote switching methods (only relevant for remote switch device)
  let rmtMethods = null;
  const localWifiChecked = _qs('input[name="remoteMth"][value="local"]')?.checked;
  if (masterSlaveMode === '2') {
    const checkedMethods = Array.from(document.querySelectorAll('input[name="remoteMth"]:checked'))
      .map(cb => cb.value);
    if (checkedMethods.length > 0) {
      rmtMethods = checkedMethods.join(','); // Store as comma-separated string or handle as needed
    }
  }

  // Validate master device URL and PIN only if remote switch device is selected AND Local WiFi checkbox is checked
  if (masterSlaveMode === '2' && localWifiChecked) {
    if (!masterDeviceUrl) {
      alert('Please enter Master Device URL for remote switch device');
      return;
    }
    if (!mPin) {
      alert('Please enter Master Device PIN for remote switch device');
      return;
    }
    if (mPin.length !== 4 || !/^\d+$/.test(mPin)) {
      alert('Master Device PIN must be exactly 4 digits');
      return;
    }
  }

  const pin = prompt("Please enter device pin to save Master-Remote settings");
  if (pin === null) return;
  if (pin.trim() === "") {
    displayFailure("Please enter the device pin.");
    return;
  }
  {
    // Prepare data with master-slave settings
    const sData = {
      masterSlaveMode: masterSlaveMode,
      masterDeviceUrl: masterDeviceUrl,
      mPin: mPin,
      rmtMethods: rmtMethods,
      enteredPin: pin
    };

    const data = JSON.stringify(sData);
    console.log('Submitting master-slave settings:', data);

    fetchWT('/api/saveMasterSlaveSettings', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: data
    })
    .then(response => {
      if (!response.ok) {
        return response.text().then(errorMessage => { 
          throw new Error(errorMessage || 'Failed to save settings'); 
        });
      }
      return response.text();
    })
    .then(data => {
      console.log('Success:', data);
      displaySuccess('Master-Remote settings saved successfully!');
    })
    .catch(error => {
      console.error('Error:', error.message);
      displayFailure('Error saving Master-Slave settings: ' + error.message);
    });
  }
}