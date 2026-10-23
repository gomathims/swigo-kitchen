// Compact API: s=schedule, t=type(D/W), e=enabled(1/0), d=days(0-6), "1"-"4"=start/end H,M, c=isCyclic(1/0), "5"-"8"=cyclic durations
// m="D" marks a Duration Based schedule; in that case "5"/"6" are reused to carry the plain duration H,M
// instead of the cyclic on-duration (isCyclic and Duration Based are mutually exclusive per schedule).
var DAY_NAMES = ['mon', 'tue', 'wed', 'thu', 'fri', 'sat', 'sun'];
function dayNameToInt(day) { var i = DAY_NAMES.indexOf(day); return i >= 0 ? i : 0; }
function toCompactScheduleItem(sObj) {
  var item = {
    t: sObj.type === 'daily' ? 'D' : 'W',
    e: sObj.enabled ? 1 : 0,
    '1': parseInt(sObj.startHour, 10) || 0,
    '2': parseInt(sObj.startMinute, 10) || 0,
    '3': parseInt(sObj.endHour, 10) || 0,
    '4': parseInt(sObj.endMinute, 10) || 0
  };
  if (sObj.type === 'weekly' && sObj.days && sObj.days.length) item.d = sObj.days.map(dayNameToInt);
  if (sObj.isDurationBased) {
    item.m = 'D';
    item['5'] = sObj.schDurHrs || 0;
    item['6'] = sObj.schDurMins || 0;
  } else {
    item.c = sObj.isCyclic ? 1 : 0;
    item['5'] = sObj.onDurHrs || 0;
    item['6'] = sObj.onDurMins || 0;
    item['7'] = sObj.offDurHrs || 0;
    item['8'] = sObj.offDurMins || 0;
  }
  return item;
}
function fromCompactScheduleItem(obj) {
  var days = (obj.d || []).map(function (n) {
    return typeof n === 'number' ? DAY_NAMES[n] : n;
  });
  var isDurationBased = obj.m === 'D';
  return {
    type: obj.t === 'D' ? 'daily' : 'weekly',
    enabled: !!obj.e,
    days: days,
    startHour: obj['1'],
    startMinute: obj['2'],
    endHour: obj['3'],
    endMinute: obj['4'],
    isCyclic: !isDurationBased && !!obj.c,
    onDurHrs: isDurationBased ? 0 : (obj['5'] || 0),
    onDurMins: isDurationBased ? 0 : (obj['6'] || 0),
    offDurHrs: obj['7'] || 0,
    offDurMins: obj['8'] || 0,
    isDurationBased: isDurationBased,
    schDurHrs: isDurationBased ? (obj['5'] || 0) : 0,
    schDurMins: isDurationBased ? (obj['6'] || 0) : 0
  };
}

function closeAllSchedules(scheduleCardToKeepOpen) {
  var accordions = document.getElementsByClassName("accordion");
  for (var i = 1; i < accordions.length; i++) {
      if (scheduleCardToKeepOpen === undefined || accordions[i].querySelector(".schedule-card") !== scheduleCardToKeepOpen) {
          var scheduleCard = accordions[i].querySelector(".schedule-card");
          scheduleCard.style.display = "none";
          accordions[i].classList.remove("active");
      }
  }
}

function addScheduleCard(isExisting = false) {
  var accordionTemplate = el("accordion-template");
  var accordions = document.getElementsByClassName("accordion");
  var scheduleCardNo = accordions.length;
  if (isExisting !== true && scheduleCardNo > 10) {
    displayFailure("Maximum 10 schedules are allowed");
    return;
  }

  if (isExisting !== true && accordions.length > 1) {
    var lastSchedule = accordions[accordions.length - 1].querySelector(".schedule-card");
    if (!validateSchedule(lastSchedule, "before adding a new schedule")) {
        return;
    }
  }

  closeAllSchedules();

  var newAccordion = accordionTemplate.cloneNode(true);
  newAccordion.id = "accordion-" + scheduleCardNo.toString();
  newAccordion.style.display = "block";


  var scheduleCardContainer = el(
    "schedule-card-container"
  );
  var newScheduleCard = newAccordion.querySelectorAll(".schedule-card")[0];
  newScheduleCard.id = "schedule-card-" + scheduleCardNo.toString();
  console.log("new element id - ", newScheduleCard.id);

  // newScheduleCard.style.backgroundColor = "#fff";

  // newScheduleCard.style.position = "relative";
  // newScheduleCard.style.display = "none";
  // newScheduleCard.style.flexDirection = "column";
  // newScheduleCard.style.gap = "1rem";
  // newScheduleCard.style.padding = "1.25rem 1.5rem";

  console.log(isExisting);
  var title = newAccordion.querySelectorAll(".accordion-title")[0]
  if (isExisting !== true) {
    console.log("inside new accordion");
    newAccordion.querySelectorAll(".sch-name")[0].innerHTML = "Schedule <br> New";
    var schEnabled = newAccordion.querySelectorAll(".sch-enable-chk")[0];
    schEnabled.checked = true;
  }

  var errorBox = _ce("div");
  errorBox.className = "er-box";
  errorBox.style.display = "none";
  
  newScheduleCard.appendChild(errorBox);

  var btnDiv = _ce("div");
  btnDiv.className = "btn-div";

  var cancelButton = _ce("button");
  cancelButton.textContent = "Cancel";
  cancelButton.className = "act-btn";

  cancelButton.onclick = function () {
    const savedSchedule = localStorage.getItem('schedule');
    if (savedSchedule) {
      const schedule = JSON.parse(savedSchedule);
      const schCont = el("schedule-card-container");
      schCont.innerHTML = '';
      populateSchedule(schedule);
    } else {
      console.log("No schedule found in local storage.");
    }
  };

  var schEnabledBox = newAccordion.querySelectorAll(".sch-enable-chk")[0];
  schEnabledBox.onclick = function (event) {
    event.stopPropagation();
    // Immediately save all schedules when toggle is clicked —
    // no need to open the accordion and manually save.
    var acc = event.target.closest('.accordion');
    var titleEl = acc.querySelector('.sch-name');
    var origText = titleEl.innerHTML;
    titleEl.innerHTML = titleEl.innerHTML.replace(/<br\s*\/?>.*/s, '<br />Saving...');
    // Use a tiny delay so the UI updates before the fetch blocks
    setTimeout(function() {
      submitSchedule();
    }, 50);
  };

  var saveButton = _ce("button");
  saveButton.textContent = "Save";
  saveButton.className = "act-btn";

  saveButton.onclick = function () {
      if (validateSchedule(newScheduleCard, "to save this schedule", true)) {
          // newScheduleCard.style.border = "2px solid green";
          submitSchedule();
        
      }
  };
  // Delete sits at the bottom right of the card, well away from the help icon
  // at the top right.
  // Trash icon with a "Delete" label, as in the cloud schedule widget - the
  // icon alone read as decoration rather than an action.
  var removeButton = _ce("button");
  removeButton.type = "button";
  removeButton.className = "remove-schedule";
  removeButton.setAttribute("aria-label", "Delete schedule");
  removeButton.innerHTML = '<svg class="sch-ico del-ico"><use href="#sch-trash"></use></svg>Delete';
  removeButton.onclick = function () {
    removeScheduleCard(newScheduleCard);
  };

  btnDiv.appendChild(saveButton);
  btnDiv.appendChild(cancelButton);
  btnDiv.appendChild(removeButton);
  newScheduleCard.appendChild(btnDiv);

  title.addEventListener("click", function(event) {
    // Don't open/close the accordion when the help icon in the title is tapped.
    if (event.target.closest('.help-i')) return;

    var activeSchedule = _qs(".accordion.active");
    var activeScheduleCard = activeSchedule?.querySelector(".schedule-card");
    if (activeSchedule && activeSchedule !== this.closest('.accordion')) {
        console.log("inside if activeSchedule && activeSchedule !== newAccordion");
        if (!validateSchedule(activeScheduleCard, "before switching schedules")) {
            return;
        }
    } else if ( event.target.closest('.accordion').querySelector(".schedule-card").classList.contains("sch-changed")) {
      errorBox.innerHTML = "⚠ Please save or cancel the changes.";
      errorBox.style.display = "block";
      return false;
    }
    
    // Pass the schedule-card of the clicked accordion to closeAllSchedules
    closeAllSchedules(this.closest('.accordion').querySelector(".schedule-card"));

    const content = event.target.closest('.accordion').querySelector(".schedule-card");
    const button = event.target.closest('.accordion').querySelector(".accordion-button");
    if (content.style.display === "flex") {
        content.style.display = "none";
        button.classList.remove("open");
        this.closest('.accordion').classList.remove('active'); // Assuming 'this' refers to the title
    } else {
        content.style.display = "flex";
        button.classList.add("open");
        this.closest('.accordion').classList.add('active'); // Assuming 'this' refers to the title
    }
  });
  if (isExisting !== true) {
    newScheduleCard.style.display = "flex";
    newAccordion.classList.add("active");
    newAccordion.querySelector(".accordion-button").classList.add("open");
    // newAccordion.querySelector(".schedule-card").style.display = "flex";
  }
  attachClamp(newScheduleCard); 
  //scheduleCardContainer.insertBefore(newScheduleCard, scheduleCardContainer.firstChild);
  //newAccordion.appendChild(newScheduleCard);
  scheduleCardContainer.appendChild(newAccordion);

  return newAccordion;
}

function markC(e) {
  const accordionNode = e.closest(".accordion");
  //accordionNode.style.background = "#fce4e4";
  accordionNode.querySelector(".schedule-card").classList.add("sch-changed");
  accordionNode.querySelector(".sch-name").innerHTML = "Schedule<br> Modified";


}

function removeScheduleCard(card) {
  // console.log("Removing schedule card...");
    if (confirm("Are you sure you want to delete this schedule?")) {
      card.parentNode.remove();
      submitSchedule(true);
    }

}


function getSwitchOption(selectElement) {
  let output = selectElement.value;
  var valid = true;
  let weeklyOptions =
    selectElement.parentElement.parentElement.querySelector("#weekly-options");
  if (output == "weekly") {
    // An explicit value, not "": the stylesheet hides #weekly-options by id, so
    // clearing the inline style would hide the days again.
    weeklyOptions.style.display = "flex";
  } else {
    weeklyOptions.style.display = "none";
  }
}

function toggleCyclicFields(checkbox) {
  const scheduleCard = checkbox.closest('.schedule-card');
  const cyclicFields = scheduleCard.querySelector('.cyclic-fields');
  if (checkbox.checked) {
    cyclicFields.style.display = 'flex';
  } else {
    cyclicFields.style.display = 'none';
  }
}

// Clock Based / Duration Based, chosen from the #sch-mode select. Duration Based
// reveals the duration beside it and hides the cyclic option (clearing it);
// Clock Based does the reverse. Every lookup is guarded so a missing element can
// never abort the handler half way and leave the card inconsistent.
// A Duration Based schedule owes a fixed amount of ON time inside its window.
// If the power goes and restart-after-power-resumption is off, the device will
// not pick the run back up, so the owed time is simply lost. That setting lives
// on the Protection tab, which is the same document here, so its checkbox is
// read directly. Advisory only - the schedule still saves.
var DURATION_RESTART_WARNING =
  "⚠ Please select Restart option in Protection settings, to complete the duration even after a power interruption.";

function updateDurationRestartWarning(scheduleCard) {
  if (!scheduleCard) return;
  var warnBox = scheduleCard.querySelector('.sch-warn');
  var modeSelect = scheduleCard.querySelector('#sch-mode');
  if (!warnBox || !modeSelect) return;
  var restartBox = el("res-i-1");
  // Protection settings not loaded yet: say nothing rather than warn wrongly.
  var restartOff = restartBox ? !restartBox.checked : false;
  if (modeSelect.value === 'duration' && restartOff) {
    warnBox.innerHTML = DURATION_RESTART_WARNING;
    warnBox.style.display = 'block';
  } else {
    warnBox.style.display = 'none';
  }
}

// Called when the Protection tab finishes loading, so an open schedule card
// stops warning the moment the setting it complains about is turned on.
function refreshDurationRestartWarnings() {
  var cards = document.getElementsByClassName("schedule-card");
  for (var i = 0; i < cards.length; i++) {
    updateDurationRestartWarning(cards[i]);
  }
}

function toggleScheduleMode(modeSelect) {
  const scheduleCard = modeSelect.closest('.schedule-card');
  if (!scheduleCard) return;
  const durationFields = scheduleCard.querySelector('.duration-mode-fields');
  const cyclicOption = scheduleCard.querySelector('.cyclic-option');
  const cyclicFields = scheduleCard.querySelector('.cyclic-fields');
  const isCyclicBox = scheduleCard.querySelector('#isCyclic');
  const isDurationMode = modeSelect.value === 'duration';

  if (isDurationMode) {
    if (durationFields) durationFields.style.display = 'flex';
    if (cyclicOption) cyclicOption.style.display = 'none';
    if (cyclicFields) cyclicFields.style.display = 'none';
    if (isCyclicBox) isCyclicBox.checked = false;
  } else {
    if (durationFields) durationFields.style.display = 'none';
    if (cyclicOption) cyclicOption.style.display = 'flex';
  }
  updateDurationRestartWarning(scheduleCard);
}

// Show an inline error inside a specific schedule card (opens its accordion so it is visible).
function showScheduleCardError(scheduleCard, msg) {
  var accordion = scheduleCard.closest('.accordion');
  if (accordion) {
    var button = accordion.querySelector('.accordion-button');
    scheduleCard.style.display = 'flex';
    if (button) button.classList.add('open');
    accordion.classList.add('active');
  }
  var box = scheduleCard.querySelector('.er-box');
  if (box) {
    box.innerHTML = "⚠ " + msg;
    box.style.display = "block";
  }
}

function submitSchedule(isRemoveSchedule = false) {
  // console.log("Submitting schedule...");
  var scheduleCards = document.getElementsByClassName("schedule-card");
  var scheduleArray = [];
  var schGroupValid = true;

  // Clear any previous inline errors before re-validating.
  var prevBoxes = document.querySelectorAll(".schedule-card .er-box");
  for (var b = 0; b < prevBoxes.length; b++) { prevBoxes[b].style.display = "none"; }

  if (scheduleCards.length < 1) {
    displayFailure("Please add at least one schedule");
    return;
  }
  // First schedule card is a template card, hence 11 is the maximum allowed schedules
  if (scheduleCards.length > 11) {
    displayFailure("Maximum 10 schedules are allowed");
    return;
  }
  for (var i = 1; i < scheduleCards.length; i++) {
    // console.log("Checking schedule card...", i);
    var valid = true;
    var scheduleCard = scheduleCards[i];
    var sObj = {};
    var schEnabledBox = scheduleCard.parentElement.querySelectorAll('.sch-enable-chk')[0];
    var schEnabled=schEnabledBox.checked;
    var typeSelect = scheduleCard.querySelector("#sch-type");
    var type = typeSelect.value;

    var days = [];
    var dayCheckboxes = scheduleCard.querySelectorAll(".day-check");
    var startTime = scheduleCard.querySelector("#start-time");
    // var startMinuteInput = scheduleCard.querySelector('#start-minute-input');
    var endTime = scheduleCard.querySelector("#end-time");
    // var endMinuteInput = scheduleCard.querySelector('#end-minute-input');
    if (!startTime.value || !endTime.value) {
      valid = false;
    }

    for (var j = 0; j < dayCheckboxes.length; j++) {
      var checkbox = dayCheckboxes[j];
      if (checkbox.checked && (type == "weekly" || days.length > 0)) {
        days.push(checkbox.getAttribute("data-day"));
      }
    }

    if (type == "" || (type == "weekly" && days.length < 1)) {
      valid = false;
    }

    if (!valid) {
      showScheduleCardError(scheduleCard, "Please fill in all required fields.");
      return;
    }

    // Schedule mode: Clock Based (default, supports optional cyclic) or Duration Based
    var isDurationBased = scheduleCard.querySelector('#sch-mode').value === 'duration';
    sObj.isDurationBased = isDurationBased;

    if (isDurationBased) {
      sObj.isCyclic = false;
      sObj.schDurHrs = parseInt(scheduleCard.querySelector('#schDurHrs').value) || 0;
      sObj.schDurMins = parseInt(scheduleCard.querySelector('#schDurMins').value) || 0;
      if ((sObj.schDurHrs + sObj.schDurMins) === 0) {
        showScheduleCardError(scheduleCard, "Please enter a valid duration.");
        return;
      }
      var sp = (startTime.value || "").split(":");
      var ep = (endTime.value || "").split(":");
      var startTotal = (parseInt(sp[0], 10) || 0) * 60 + (parseInt(sp[1], 10) || 0);
      var endTotal = (parseInt(ep[0], 10) || 0) * 60 + (parseInt(ep[1], 10) || 0);
      var gap = endTotal - startTotal;
      if (gap <= 0) gap += 24 * 60; // handle overnight
      var durTotal = sObj.schDurHrs * 60 + sObj.schDurMins;
      if (durTotal > gap) {
        showScheduleCardError(scheduleCard, "Duration cannot exceed the gap between start and end time (" + Math.floor(gap / 60) + "h " + (gap % 60) + "m).");
        return;
      }
    } else {
      // Cyclic fields
      var isCyclic = scheduleCard.querySelector('#isCyclic').checked;
      sObj.isCyclic = isCyclic;
      if (isCyclic) {
        sObj.onDurHrs = parseInt(scheduleCard.querySelector('#onDurHrs').value) || 0;
        sObj.onDurMins = parseInt(scheduleCard.querySelector('#onDurMins').value) || 0;
        sObj.offDurHrs = parseInt(scheduleCard.querySelector('#offDurHrs').value) || 0;
        sObj.offDurMins = parseInt(scheduleCard.querySelector('#offDurMins').value) || 0;
        // Validation for cyclic fields
        if ((sObj.onDurHrs + sObj.onDurMins) === 0 || (sObj.offDurHrs + sObj.offDurMins) === 0) {
          showScheduleCardError(scheduleCard, "Please enter valid On/Off durations for cyclic schedule.");
          return;
        }
        // On/Off duration cannot exceed the gap between start and end time
        var sp = (startTime.value || "").split(":");
        var ep = (endTime.value || "").split(":");
        var startTotal = (parseInt(sp[0], 10) || 0) * 60 + (parseInt(sp[1], 10) || 0);
        var endTotal = (parseInt(ep[0], 10) || 0) * 60 + (parseInt(ep[1], 10) || 0);
        var gap = endTotal - startTotal;
        if (gap <= 0) gap += 24 * 60; // handle overnight
        var onTotal = sObj.onDurHrs * 60 + sObj.onDurMins;
        var offTotal = sObj.offDurHrs * 60 + sObj.offDurMins;
        if (onTotal > gap) {
          showScheduleCardError(scheduleCard, "On duration cannot exceed the gap between start and end time (" + Math.floor(gap / 60) + "h " + (gap % 60) + "m).");
          return;
        }
        if (offTotal > gap) {
          showScheduleCardError(scheduleCard, "Off duration cannot exceed the gap between start and end time (" + Math.floor(gap / 60) + "h " + (gap % 60) + "m).");
          return;
        }
      }
    }
    // else {
    //   scheduleCard.style.border = "2px solid black";
    // }

    // if (schGroupValid === false) {
    //   break;
    // }

    sObj.enabled=schEnabled;
    sObj.type = type;
    sObj.days = days;
    const startTimeValue = scheduleCard.querySelector("#start-time").value;
    const [startHour, startMinute] = startTimeValue.split(":");
    sObj.startHour = startHour;
    sObj.startMinute = startMinute;
    const endTimeValue = scheduleCard.querySelector("#end-time").value;
    const [endHour, endMinute] = endTimeValue.split(":");
    sObj.endHour = endHour;
    sObj.endMinute = endMinute;
    // const selectedRadio = scheduleCard.querySelector(".restart-input:checked");
    // sObj.restart = selectedRadio ? selectedRadio.value : "no";

    scheduleArray.push(sObj);
    // console.log(sObj);
  }

  // console.log("Schedule array:", scheduleArray);
  if (!validateSchedulesForClash(scheduleArray)) {
    // console.log("There is an overlap in the schedules.");
    displayFailure("There is an overlap in the schedules. Please correct it.");
    return;
  }

  var compactPayload = { s: scheduleArray.map(toCompactScheduleItem) };

  fetchWT("/api/setSchedule", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
    },
    body: JSON.stringify(compactPayload),
  })
    .then((response) => {
      if (!response.ok) {
        return response.text().then((errorMessage) => {
            throw new Error(errorMessage || "HTTP Error: " + response.status);
        });
      }
      return response.text();
    })
    .then(function (result) {
      // console.log(result);
      let message = isRemoveSchedule? "Schedule updated." : "Schedule updated.";
      displaySuccess(message);
      // Keep the local copy in step with what was just saved - compactPayload is
      // the same shape /api/getSchedule returns. The status tick reads this cache
      // to work out the next schedule, so it never has to fetch for itself.
      localStorage.setItem("schedule", JSON.stringify(compactPayload));
      const schCont = el("schedule-card-container");
      schCont.innerHTML = '';
      getSchedule();
    })
    .catch((error) => {
      // console.error("Error during fetch:", error.message);
      displayFailure(
        "Failed to save schedule... Please refresh and try again."
      );
      // Handle timeout or other errors
    });
}

// Only ever called deliberately - page load, after a save, and the error-state
// refresh link. It rebuilds #schedule-card-container from the device, so nothing
// opportunistic (a status tick, say) may call it: that would discard a card the
// user is still typing into.
function getSchedule() {
  // console.log("Getting schedule...");
  fetchWT("/api/getSchedule")
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
      localStorage.setItem("schedule", JSON.stringify(result));
      const schCont = el("schedule-card-container");
      schCont.innerHTML = '';
      populateSchedule(result);
      // remove changed class from all schedule cards
      const scheduleCards = _qsa(".schedule-card");
      scheduleCards.forEach((card) => {
        card.classList.remove("sch-changed");
      });
    })
    .catch((error) => {
      console.error("Error during fetch:", error.message);
      displayFailure(STD_ERROR);
      populateScheduleError();
      // Handle timeout or other errors
    });

  
//   result = {
//     "s": [
//         {
//             "t": "D",
//             "e": 1,
//             "1": 1,
//             "2": 0,
//             "3": 1,
//             "4": 3,
//             "c": 0,
//             "5": 0,
//             "6": 0,
//             "7": 0,
//             "8": 0
//         },
        
//         {
//             "t": "W",
//             "e": 1,
//             "d": [
//                 0,
//                 1,
//                 2,
//                 3,
//                 4,
//                 5,
//                 6
//             ],
//             "1": 21,
//             "2": 0,
//             "3": 22,
//             "4": 0,
//             "c": 1,
//             "5": 0,
//             "6": 10,
//             "7": 0,
//             "8": 50
//         }
//     ]
// }
//   populateSchedule(result);
}

function tog(element) {
  var checkbox = element.querySelector(".day-check");
  checkbox.checked = !checkbox.checked;

}
function populateSchedule(result) {
  var rawArray = result.s != null ? result.s : result.schedule;
  var scheduleArray = rawArray ? rawArray.map(function (item) { return fromCompactScheduleItem(item); }) : [];

  if (!scheduleArray || scheduleArray.length == 0) {
    console.log("No schedule found");
    addScheduleCard(false);
    return;
  }

  var scheduleCardNo = 0;
  scheduleArray.forEach((sObj) => {
    newAccordion = addScheduleCard(true);
    var scheduleCard = newAccordion.querySelectorAll(".schedule-card")[0];
    // There is a template with display none with same 'schedule-card' class. Hence -1 is not done
    //var scheduleCard = document.getElementsByClassName('schedule-card')[newCardNo];
    var schEnabled = newAccordion.querySelectorAll(".sch-enable-chk")[0];
    var typeSelect = scheduleCard.querySelector("#sch-type");
    var weeklyOptions = scheduleCard.querySelector("#weekly-options");
    var dayCheckboxes = scheduleCard.querySelectorAll(".day-check");
    var startTime = scheduleCard.querySelector("#start-time");
    var endTime = scheduleCard.querySelector("#end-time");
    //var nameInput = scheduleCard.querySelector('#switch-name-input');
    // console.log(sObj.type);
    typeSelect.value = sObj.type;

    if (sObj.type == "weekly") {
      weeklyOptions.style.display = "flex";

      if (sObj.days && sObj.days.length > 0) {
        sObj.days.forEach((day) => {
          // console.log(day);
          dayCheckboxes.forEach((checkbox) => {
            if (checkbox.getAttribute("data-day") === day) {
              // console.log(checkbox.id);
              checkbox.checked = true;
            }
          });
        });
      }
    } else {
      weeklyOptions.style.display = "none";
      dayCheckboxes.forEach((checkbox) => {
        checkbox.checked = false;
      });
    }
    const startHour = parseInt(sObj.startHour) || 0;
    const startMinute = parseInt(sObj.startMinute) || 0;
    const endHour = parseInt(sObj.endHour) || 0;
    const endMinute = parseInt(sObj.endMinute) || 0;

    // console.log(formatTimeinput(startHour, startMinute));
    startTime.value = formatTimeinput(startHour, startMinute);
    endTime.value = formatTimeinput(endHour, endMinute);

    // endMinuteInput.value = parseInt(sObj.endMinute) || sObj.endMinute;
    // if (sObj.restart) {
    //   scheduleCard.querySelector(
    //    '.restart-input[value="' + sObj.restart + '"]'
    //   ).checked = true;
    // }

    if (sObj.enabled) {
      schEnabled.checked = true;
    } else {
      // Visual indicator for disabled schedules
      newAccordion.classList.add('sch-disabled');
    }

    // Populate schedule mode: Duration Based or Clock Based (with optional cyclic)
    var modeSelect = scheduleCard.querySelector('#sch-mode');
    var durationFields = scheduleCard.querySelector('.duration-mode-fields');
    var cyclicOption = scheduleCard.querySelector('.cyclic-option');
    var isCyclicBox = scheduleCard.querySelector('#isCyclic');
    var cyclicFields = scheduleCard.querySelector('.cyclic-fields');

    if (sObj.isDurationBased) {
      modeSelect.value = 'duration';
      durationFields.style.display = 'flex';
      cyclicOption.style.display = 'none';
      cyclicFields.style.display = 'none';
      isCyclicBox.checked = false;
      scheduleCard.querySelector('#schDurHrs').value = sObj.schDurHrs || 0;
      scheduleCard.querySelector('#schDurMins').value = sObj.schDurMins || 0;
      // Shown as soon as a stored duration schedule is listed, so it survives
      // the reload that follows a save.
      updateDurationRestartWarning(scheduleCard);
    } else {
      modeSelect.value = 'clock';
      durationFields.style.display = 'none';
      cyclicOption.style.display = 'flex';
      if (sObj.isCyclic) {
        isCyclicBox.checked = true;
        cyclicFields.style.display = 'flex';
        scheduleCard.querySelector('#onDurHrs').value = sObj.onDurHrs || 0;
        scheduleCard.querySelector('#onDurMins').value = sObj.onDurMins || 0;
        scheduleCard.querySelector('#offDurHrs').value = sObj.offDurHrs || 0;
        scheduleCard.querySelector('#offDurMins').value = sObj.offDurMins || 0;
      } else {
        isCyclicBox.checked = false;
        cyclicFields.style.display = 'none';
      }
    }

    const startTimeStr = formatTimeinputAM(startHour, startMinute);
    const endTimeStr = formatTimeinputAM(endHour, endMinute);

    // Summary laid out like the cloud widget: a title with the mode badge, then
    // a detail line of days, timings and - for Duration Based - the owed time.
    const title = sObj.type === "daily" ? "Daily Schedule" : "Weekly Schedule";
    let badge = "";
    if (sObj.isDurationBased) badge = '<span class="sch-badge">Duration</span>';
    else if (sObj.isCyclic) badge = '<span class="sch-badge">Cyclic</span>';

    // Three-letter days - "Wed, Thu" reads unambiguously where "We, Th" does not.
    const daysText = (sObj.type === "weekly" && sObj.days && sObj.days.length)
      ? sObj.days.map((day) => day.charAt(0).toUpperCase() + day.slice(1, 3)).join(", ")
      : "Every day";

    // Each icon travels with its own text - the pairs are what wrap, never the
    // icon away from what it labels. A weekly day list takes a row to itself so
    // the timing and duration stay together on the next one.
    const daysClass = sObj.type === "weekly" ? "sch-det sch-det-row" : "sch-det";
    let detail = '<span class="' + daysClass + '">' + schIcon("sch-cal")
      + "<span>" + daysText + "</span></span>"
      + '<span class="sch-det">' + schIcon("sch-clock")
      + "<span>" + startTimeStr + " - " + endTimeStr + "</span></span>";
    if (sObj.isDurationBased) {
      detail += '<span class="sch-det">' + schIcon("sch-hourglass")
        + "<span>Runs: " + schDurLabel(sObj.schDurHrs, sObj.schDurMins) + "</span></span>";
    } else if (sObj.isCyclic) {
      detail += '<span class="sch-det">' + schIcon("sch-hourglass")
        + "<span>On: " + schDurLabel(sObj.onDurHrs, sObj.onDurMins)
        + ", Off: " + schDurLabel(sObj.offDurHrs, sObj.offDurMins) + "</span></span>";
    }

    newAccordion.querySelectorAll(".sch-name")[0].innerHTML =
      '<span class="sch-title">' + title + badge + "</span>"
      + '<span class="sch-details">' + detail + "</span>";
  });
}
function populateScheduleError(result) {
  var scheduleCardContainer = el("schedule-card-container");

  var refreshLink = _ce("a");
  refreshLink.href = "#";
  refreshLink.classList.add("cn");
  refreshLink.onclick = getSchedule;
  var refreshIcon = _ce("img");
  refreshIcon.src = "images/reload.png";
  refreshLink.appendChild(refreshIcon);
  scheduleCardContainer.appendChild(refreshLink);
  
   
}


function formatTimeinput(hour, minute) {
  // console.log(
  //   hour.toString().padStart(2, "0") + ":" + minute.toString().padStart(2, "0")
  // );
  return hour.toString().padStart(2, "0") + ":" + minute.toString().padStart(2, "0");
}

// One of the summary symbols defined once in index.html (#sch-cal, #sch-clock,
// #sch-hourglass), referenced rather than repeated per schedule.
function schIcon(id) {
  return '<svg class="sch-ico"><use href="#' + id + '"></use></svg>';
}

// "15m", "1h 02m" - the owed ON time of a Duration Based schedule, as shown in
// the accordion title. Minutes are padded only when hours are present.
function schDurLabel(hrs, mins) {
  var h = parseInt(hrs, 10) || 0;
  var m = parseInt(mins, 10) || 0;
  return h > 0 ? h + "h " + String(m).padStart(2, "0") + "m" : m + "m";
}

function formatTimeinputAM(hour, minute) {
  const ampm = hour < 12 ? "AM" : "PM";
  const h = hour % 12 || 12;
  return h + ":" + minute.toString().padStart(2, "0") + " " + ampm;
}

function clampToMinMax(e) {
  const min = parseInt(e.target.min, 10);
  const max = parseInt(e.target.max, 10);
  let val = parseInt(e.target.value, 10);
  if (isNaN(val)) val = min;
  if (val < min) e.target.value = min;
  if (val > max) e.target.value = max;
}
function attachClamp(card) {
  card.querySelectorAll('input[type="number"]').forEach(input => {
    if (input.id === 'onDurHrs' || input.id === 'onDurMins' || input.id === 'offDurHrs' || input.id === 'offDurMins' || input.id === 'schDurHrs' || input.id === 'schDurMins') {
      input.onblur = clampToMinMax;
    }
  });
}