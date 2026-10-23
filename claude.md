// claude.md (in project root)
// --------------------------------------------------

# Thingsboard  community version - New Dashboard

## Device List page - Mobile view
- Dashboard should list the devices of the user, blocks
- Each block will have device name, Online status(Online/Offline), Running status(On/Off), Voltage (eg., 230V). All these does not need a lable.
- There could be 2 columns of blocks, when viewed on mobile.
- When a block is clicked, the device control page should be shown.
  
## Device List page - Desktop view

- Device could be shown as list, as shown already in the dashboard.
- When a device is clicked, the device control page should be shown.

## Device Details page / State(Thingsboard state)
- New control should look like the index.html page in the data folder
- Need to have home tab,  schedule tab, Monitoring tab - 

### Home tab
- New custom control widget should be done  to control the device and display the telemetry data, as done in the existing device control widget.
- New control should look like the home tab in the index.html file. There will be Runnning duration, ON/OFF toggle, count down or Schedule display area, Alert, information and a few logs
- New custom control widget should be able to list a few logs which are published by the device, as listed in the index.html file.
  

### Schedule tab
- Use the existing schedule widget to list the schedules for the device.

### Monitoring tab
- Existing standard controls could be used to display the monitoring data for the device, voltage&current, power&energy, activity logs(Which is partly listed in home tab)
- There should be a vertical navigation bar at the left side of the screen, with the tabs -voltage & Current, Power & Energy, Activity Logs
- When a each tab is clicked, the corresponding data should be displayed as in the existing dashboard


### small Fixes required 
- Home /Schedule/ Monitoring tabs are shown at the top. It needs to be fixed at bottom in the mobile view and left on desktop view.
- First time, I am able to go to Home tab in device details, from List page. But If I move to schedules, not able to come back to Home.(Side note:  I also notice that even though I go to Schedules tab, Home is Highlighted.)
- The Font used, Borders used should be as in index.html. BAsically the background could be different in Cloud dashboard, but other styles should be as same as in embedded portal.
- Logs are not displayed in the monitoring third 

### Major Question
- More than anything I go for complete custom widget is to overcome the Thingsboard widget/ height restrictions. when in mobile view if the schedule list is going beyond the limit, it should show a scroll automatically. When in desktop view, the widget size should be adjustable. If it is a too much trouble to adjust in Thingsboard dashboard, Can we build the interface in app directly, instead of dashboard dashboard being shown in webview 

# Changes - Feb 25
Extend Device control widget, so that the tab navigation is part of device control widget itself
The widget will also show the DeviceName clientAttribute on top, along with Online status
Tab navigation needs to be positioned at the bottom, when user sees the dashboard, with mobile.
Schedule should also be part of the device control widget itself, otherwise managing the autogrow is a problem
Monitoring tab Each chart's height should be matching to device's height. In a mobile, the height should be fitting.


When the new dashboard opened in mobile, widget height increases and  the navigation bar goes down  below, beyond visibility. What can be done for it, with thingsboard widget size restrictions?. can it be floting wrt screen? or should the navigation be kept at left or top only?  

Monitoring widget height also goes so tall, could not see the x axis at all. Will it be rendered properly, if it is kept as a individual widget and having menu as a hamburger menu First level Home/Schedule/Monitoring and under monitoring second level as monitoring menu

# Changes - Feb 26
Device control widget shows the menu in horizontal way, should be shown vertically
Online needs to be indicated in a dot (Green dot/ Gray dot) Maximize button is overlapping with online, need to move it.
Size of on/off needs to be reduced to 3/4th
Icons before the Current/Voltage readings should be a bit dark
Units should also be a bit dark
Recent logs- when a new one comes, existing one changes to the same description
 Count down timer should be above the on/OFF button behind hours/minutes inputs. When Device is ON, only count down should be shown, when the device is off Hours/Minutes input should be shown. Background Div color is not required, Font size should be 2 points higher than this. and There should be a blinking effect for the colon. 
 colon should be removed in the Hours/Minutes label
 Alerts area or information area- should be hidden now. Will take care of it in next set

Monitoring state- Menu is shown in hamburger always even in wider display, Is it possible to make it uniform in both widgets?


Space above and below the Hours/Minutes area should be reduced
Online Green dot shoule be before the device name
Next Schedule - is not considering the other schedules. It shows the first schedule and shows. Need to consider cyclic schedule also.
{"s":[{"t":"D","e":1,"1":1,"2":0,"3":1,"4":3,"c":0,"5":0,"6":0,"7":0,"8":0},{"t":"W","e":1,"d":[0,1,2,3,4,5,6],"1":21,"2":0,"3":22,"4":0,"c":1,"5":0,"6":10,"7":0,"8":50}]} Here, "t":"D" represents daily, "W" represents weekly, "e":1 represents enabled, "d" gives the days enabled, "1":21,"2":0,"3":22,"4":0 is Start time hours/minutes, End time Hours/Minutes respectively.  "c": 1 indicates cyclic and "5":0,"6":10,"7":0,"8":50 indicates ON duration hours, ON duration minutes, OFF duration hours, OFF duration minutes, respectively. Hence should pick the enabled ones, and consider start time and End time within which, cyclic time will be considered.

Schedule tab style should also be changed to white background, Matching fonts as the Home tab. Icons need not have to be colored. PAdding should be reduced, inside each schedule. 

Monitoring nav widget- shows the menu horizontally as in picture 
From monitoring if schedule menu is clicked, device widget should open the schedule 


DeviceName, if not available from client attributes, show the device Lable, standard parameter.
In my local portal the next schedule is shown as the pic. Need to specify the time applicable for the schedule only. In case of cyclic schedule, need to specify the exacty cycle start time and end time
Alert section needs to be removed.

Still monitoring nav - shows the menu horizontally on the right most.

Menu - Monitoring should have a submenu- Voltage-Current, Energy, Operation Logs 
Each will be placed in different states, so that user need not have to scroll between various widgets, which becomes very difficult. Guide me the procedure for it.
There should be a more log button/icon, in the Recent logs area- which take me to monitoring-Logs state

Some times navigating to moniroting is not working, even if the data(voltage/current or energy data is present) 
[v7] Sidebar item pressed: Monitoring
VM16320:230 [v7] Navigate to state: device_monitoring entity: {"entityId":{"entityType":"DEVICE","id":"4f60b210-08a1-11f1-b22d-6da3d5c60efc"},"entityName":"98:A3:16:DE:EE:84"}
VM16320:263 [v7] Navigation failed for: device_monitoring
navigateToState @ VM16320:263
VM16320:185 [v7] Sidebar item pressed: Monitoring
VM16320:230 [v7] Navigate to state: device_monitoring entity: {"entityId":{"entityType":"DEVICE","id":"4f60b210-08a1-11f1-b22d-6da3d5c60efc"},"entityName":"98:A3:16:DE:EE:84"}
VM16320:263 [v7] Navigation failed for: device_monitoring
navigateToState @ VM16320:263

# changes Feb 28
Next Schedule - Should be shown as in the picture, Considering the current time, next schedule which will start and if it is cyclic, the exact on duration should be shown
{"s":[{"t":"D","e":1,"1":1,"2":0,"3":1,"4":3,"c":0,"5":0,"6":0,"7":0,"8":0},{"t":"W","e":1,"d":[0,1,2,3,4,5,6],"1":21,"2":0,"3":22,"4":0,"c":1,"5":0,"6":10,"7":0,"8":50}]} Here, "t":"D" represents daily, "W" represents weekly, "e":1 represents enabled, "d" gives the days enabled, "1":21,"2":0,"3":22,"4":0 is Start time hours/minutes, End time Hours/Minutes respectively.  "c": 1 indicates cyclic and "5":0,"6":10,"7":0,"8":50 indicates ON duration hours, ON duration minutes, OFF duration hours, OFF duration minutes, respectively. Hence should pick the enabled ones, and consider start time and End time within which, cyclic time will be considered.

Font of the Schedule displayed - should be like the other font used. BAckground colur lighter than gray (like silver)


#Changes Mar 1

Attached is a Thingsboard widget- Schedule widget of SuperSmart Dashboard
This widget shows various schedules as rows and when add or edit is clicked, the details are shown in a popup. The popups' size is very big and in mobile view all the inputs are not visible. The input boxes can be a bit small and font size could be adjusted. Need to make each Schedule line as accordions and editable view is shown when accordion is opened. 

Padding of each schedule line is big, need to reduce it a bit.
Delete button will be inside accordion, hence Enable button and down arrow will be shown when accordion is closed.
Minutes should be displayed in 2 digit format not as 19:3(Which is 3 minutes here, it should be 19:03)
In case of cyclic schedule, there should be a validation- On duration or Off duration cannot be more than the gap between start and end.


# Changes Mar 2
Reduce the width of the start/end time boxes and duration boxes, so that even in mobile view start,end time 4 boxes will take only a single line

In navigation widget, Highlighting of the selected menu is sometimes occurring and sometimes not. AS far as i noticed, when i navigate quickly by clicking fast, selected class is not applying. 
Last menu - Device_logs, should be taken out from menu, We have a link for that state, somewhere else. Instead BAck to device List Menu should be added.

When the schedule widget is loaded in the App, within a few minutes, it stops responding to clicks. I think web socket session is lost in webviews, session is not establishing again. We had the same issue in device_control widget and it was solved by you


Start time and end time should not be the same
Bring the weekdays below the check box instead of right, so that width will be reduced and height will increase and all seven days will be placed in a single line

In schedule widget some messages like "Start time and end time cannot be the same.", "Please enter valid On and Off durations for cyclic schedule." appears as popup. And the one added by you now "On duration cannot exceed the gap between start and end time (0h 3m) appears just below the box. Please make it uniform. Let us make it like the second one.

Already schedule name is shown like 'Daily Schedule' then another badge "Daily" or "weekly" is not required.
"Cyclic" badge is to be shown in some gray shade only

device_list_card_widget.json The device name should be taken from deviceName client attribute. If the attribute missing show the device label. Current should also be added, in the card, if the device is ON
When the device is offline, do not show the Voltage in the card. On and Off status need to be shown as a toggle switch, without lable. and it os not clickable in the card view.
When there are more devices, it is not possible to do any filter. Please add a filter which will search based on device name, label, client attributes, mac-id server attribute, customer name

# changes 4-Mar

If the device is offline, do not show the on-off status/voltage/current of the device, in device_list_widget.
Read device_list_card_widget.json
The toggle is already conditionally shown for voltage/current when offline. I just need to also hide the toggle switch when offline. Let me make that change:

Edit device_list_card_widget.json
Modified
Done. When the device is offline, the toggle switch (on/off), voltage, and current are all hidden. Only the device name and the gray offline dot are shown.

If the device is offline, do not show the on-off status/voltage/current of the device, in device_list_widget.
when the device is offline, disble the input hour/min boxes, show the voltage & other readings blurred out. - some grayed texts
If the device is 3phase- do not show the power, Energy and CumEnergy sections
 If the device is 3phase- do not show the Energy state in the monitoring nav_header 8


## changes Mar 18

# Device-control OTA - changes
As shown in the picture, please change the interface
- We need to segregate various parts in the interface as section. There should be a border  Each section will have background #FAFAFA and border: 1px solid rgba(0, 66, 37, 0.18); A light shadow will be there
- Countdown timer, duration inputs, device control (ON-OFF) checkbox and next schedule display will be in a section

- Telemetry section will be the next one
- Alert will be shown in the next section
- Recent logs will be shown like in picture, there will be 10 entries displayed.
- Date will be shown as 10/Feb, time as 10:50pm, shown in slightly different tones, Message will be shown after some gap.
- name of hte new control will be device-control-v9
- As shown in the pic, telemetric data reading box will not have a symbol belore, as the symbol is not accurate. And the units will be shown at the extream right of the box. Energy box reading will have the unit as Session kWh and cumulative energy will have Cum kWh "Session" and "Cum." will be above the kwh in smaller font.
- Again as given in pic, the second row of telemetric area will use entire width for 2 boxes. (First row will have 3 boxes)
REcent logs section's log entry's date time will take a fixed comfortable width, so that message will always start at a particular point. 
next schedule display time will use the green color of reading boxes (2e7d32)
Hide the alert section for now.
There should be a shadow below each section, space between 2 secions could be a bit less
-On-off check box to have ON color as 2e7d32
REduce the v9-section padding for mobile as 10px
.widget-type-tenant-device_control_v9 .duration-item input as 6px 4px
REduce the top spacing above first section

# Firmware - WebInterface changes

Similar to the above the interface of embedded portal UI should be changed for home menu.
Logo will be present with right side wifi icons and password set alert in normal mode. In app embedded mode this line wont be present.
Menu, which is appearing at the bottom for mobile mode, will be on top , to the right of switch name
- We need to segregate various parts in the interface as section. There should be a border  Each section will have background #FAFAFA and border: 1px solid rgba(0, 66, 37, 0.18); A light shadow will be there
- Countdown timer, duration inputs, device control (ON-OFF) checkbox and next schedule display will be in a section

- Telemetry section will be the next one
- Alert will be shown in the next section
- Recent logs will be shown like in picture, there will be 10 entries displayed.
- Date will be shown as 10/Feb, time as 10:50pm, shown in slightly different tones, Message will be shown after some gap.
- name of hte new control will be device-control-v9
- As shown in the pic, telemetric data reading box will not have a symbol belore, as the symbol is not accurate. And the units will be shown at the extream right of the box. Energy box reading will have the unit as Session kWh and cumulative energy will have Cum kWh "Session" and "Cum." will be above the kwh in smaller font.
- Again as given in pic, the second row of telemetric area will use entire width for 2 boxes. (First row will have 3 boxes)
REcent logs section's log entry's date time will take a fixed comfortable width, so that message will always start at a particular point. 
next schedule display time will use the green color of reading boxes (2e7d32)

Alert & Info section will be below the telemetric area, as another section. 

REcent logs section's  table like design will need to be changed as in pic.