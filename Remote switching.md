# Swigo-Gen2 - Remote switching Configuration
SwiGO SuperSmart 

Same wifi - Remote switching 

REmote switch configuration in SEttings tab - 
    - When selected as none will not do anything
    - When selected as 'Master', Master will respond to 'RemoteSwitching' in addition to all other modes of switching the relay.
    - When selected as 'Remote', the inner relay does not have any local switching functionality

        -When both devices are connected on the same network or if the master has public ip/url, 
            
            - Local masters url with credential will be taken for configuration
            - When a button is pressed in the remote, the button number will be sent to Master
                - when the api parameters (button number and pin) are valid, a http post api call will be made from Remote
                - The master will receive the api call and if the parameter matches, it will process it.
                - The duration will be as per the button pressed. If button 1 is pressed on Remote switch, master's duration for number 1 will be the duration. turn on the Relay.
                - If the master is already ON, Target device status will be indicated, with blue led.
                - If the master is on some fault condition( like high/low voltage, dryrun, overload, flow mode is enabled and flow is not detected), it will be logged

                - When the stop button is pressed in Remote, master switch will be turned off and Blue LED indication will stop.
                - When Master device is ON indication is blinking, Once in 20Secs, Master status will be checked with a api call.
                    - If the api returns the target supersmart Status, the indicator will be stopped
                    - If api could not be called or target is not reachable for 3 times, indicator will stop.
            

        -When not on same wifi 
            when the device is connected to IOT.EVIQTECH, Remote to Master relation need to be set.
                - get the mac-id of the master
                - Open devices menu in IOT.EVIQTECH.COM
                - Click on the Remote device, Rightside pan will open
                - Go to relations tab
                - Click Inbound or "To" relations
                - Add a relation, by clicking +
                - Relation name as "Manages", entity type as Device, put the mac-id as the device name or select the device from list

            When a button is pressed, a mqtt message will be sent. 
                {method:"remoteSwitch", params:{"remoteSwitching":"ON", "buttonNo":1}}
            
            When the stop button is pressed, athe following mqtt message will be sent
                {method:"remoteSwitch", params:{"remoteSwitching":"OFF", "buttonNo":0}}

            When the master is ON and when it is configured as Master 
                Whenever the device is ON, the following message will be sent to Remote device
                {method:"notifyRemotes", params:{"targetStatus":"ON"}}
                - Hence, even if the device is not started by Remote switch, Target device's status will be indiated in Remote, if MQTT mode is enabled
                
        - when float mode is set to "Tankfull indication", 
            if the overflow is detected in remote , a stop message will be sent to master
            If any start button is pressed in remote and the overflow is already detected, start command will not be sent. but there will be a log.
            There will be a Tankfull LED indication, when the overflow is detected in remote. And if button press is sent to master, that time also there will be a LED indication.
            When the overflow is detected in remote, ff the master is started directly in Master, 20 secs once Stop command will be senot nt to Master

        - When float mode is set to "Flow Detection", 
            if there is a flow starting, a start message with button number 11 (which is 23H:59M) will be sent to master.
            if the flow is stopped, a stop message will be sent to master
            If any start button is pressed in remote and the flow is not detected, start command will not be sent. A log will be a logged. Tankfull LED combination will be indicated
            
        