# WebSocket Connection & Login Session Lifecycle

## Overview

The embedded portal uses two independent communication channels:
- **HTTP REST API** - For control commands, settings, login/logout (authenticated via session cookie)
- **WebSocket** - For real-time telemetry and state broadcasts (unauthenticated)

These are independent - a valid HTTP session is NOT required for WebSocket, and vice versa.

---

## 1. Session Lifecycle

### 1.1 Session Storage

Sessions are stored in **RTC memory** (persists across soft resets/deep sleep):

```
Max sessions:     3  (MAX_WEBSOCKET_CLIENTS)
Session timeout:  10 minutes (600,000 ms)
Token format:     16 random hex characters
Storage:          RTC_DATA_ATTR SessionData rtcSessionData
```

**Session struct** (`src/websocketsettings.h:19-28`):
```cpp
struct Session {
    char token[17];                  // 16 hex chars + null
    unsigned long lastActivityTime;  // millis() at last API call
    bool active;                     // slot in use
};
```

### 1.2 Boot Initialization

On device startup (`src/main.cpp:284` -> `src/websocketsettings.cpp:83-94`):
- Checks RTC magic number (`0xA5A5A5A5`)
- **First boot / RTC corrupted**: Clears all 3 session slots
- **Power-loss recovery**: Preserves existing sessions (can be reused if not timed out)

### 1.3 Login Flow

**Client** sends POST `/api/login` with `{ devicePin, deviceTime, timezoneOffset }`

**Server** (`src/api.cpp:857-934`):
1. Validates PIN via `strcmp(pin, devicePin)`
2. Syncs device RTC clock from `deviceTime` and `timezoneOffset`
3. Checks if client already has a valid session cookie
   - If valid existing session found -> reuses the token
   - If no session or invalid -> calls `startNewSession(token)`
4. Returns `Set-Cookie: SESSIONID=<token>; Path=/; HttpOnly`

**`startNewSession()`** (`src/websocketsettings.cpp:385-426`):
1. Finds first inactive session slot
2. If all slots active, replaces the oldest
3. Generates random 16-char hex token
4. Sets `active = true`, `lastActivityTime = millis()`

### 1.4 Session Validation (Every API Call)

**`verifyCookie()`** (`src/api.cpp:10-41`) is called by every protected endpoint:

```
1. Extract SESSIONID from Cookie header
2. Find session by token (findSessionByToken)
3. Check session->active == true
4. Check millis() - lastActivityTime < 600,000ms (10 min)
5. If valid: update lastActivityTime (reset idle timer), return true
6. If invalid/expired: return false -> endpoint returns 401
```

**Key point**: Every successful API call resets the 10-minute idle timer.

### 1.5 Session Timeout (10 Minutes Idle)

When no API calls are made for 10 minutes:

```
Timeline:
  T+0:00  Last API call (lastActivityTime updated)
  T+0:15  WebSocket ping/pong continues (does NOT reset session timer)
  T+5:00  WebSocket still broadcasting telemetry (no session impact)
  T+10:00 Session timeout threshold reached
  T+10:01 Next HTTP API call -> verifyCookie() fails -> 401 Unauthorized
  T+10:01 Browser receives 401 -> redirects to /login.html?returnUrl=...
  T+10:01 Page unload closes WebSocket connection
```

**Important**: WebSocket ping/pong does NOT reset the session timer. Only HTTP API calls (fetchWithAuth) reset it.

### 1.6 Logout Flow

**Client** sends DELETE `/api/logout`

**Server** (`src/api.cpp:70-88, 2021-2028`):
1. Finds session by cookie token
2. Sets `session->active = false`
3. Responds with `Set-Cookie: SESSIONID=; Path=/; Max-Age=0` (clears browser cookie)
4. Browser redirects to `login.html`
5. Page unload closes WebSocket connection naturally

### 1.7 Session Cleanup

**`cleanUpSessions()`** (`src/websocketsettings.cpp:354-361`):
- Iterates all slots, marks timed-out sessions as `active = false`
- **Note**: This function is defined but not actively called on a timer. Timed-out sessions still fail `verifyCookie()` validation, so the practical impact is limited to session slot reuse.

---

## 2. WebSocket Lifecycle

### 2.1 Server-Side Initialization

**`initWebSocket()`** (`src/websocketsettings.cpp:78-81`):
- Registers WebSocket handler on `/ws` endpoint
- No authentication required at connection time

**Event Handlers** (`src/websocketsettings.cpp:52-76`):

| Event | Action |
|-------|--------|
| `WS_EVT_CONNECT` | Calls `notifyClients()` to send current device state |
| `WS_EVT_DISCONNECT` | No special cleanup |
| `WS_EVT_DATA` | Handles "ping" -> responds "pong"; otherwise processes JSON commands |
| `WS_EVT_PONG` | No handler |
| `WS_EVT_ERROR` | No handler |

**`ws.cleanupClients()`** (`src/main.cpp:515`):
- Called in main loop
- Cleans up disconnected WebSocket client resources (frees RAM)
- Does NOT proactively disconnect idle clients

### 2.2 Client-Side Connection

**Initial Connection** (`data/scripts/script.js`):
- Opens WebSocket to `ws://<host>/ws` on page load
- Attaches handlers for open, close, error, message events

**Ping/Pong Keep-Alive** (`data/scripts/script.js:182-202`):
```
Every time a message is received:
  -> Reset 15-second timer

If no message for 15 seconds:
  -> Send "ping" to server
  -> Start 5-second pong timer

If "pong" received within 5 seconds:
  -> Connection healthy, reset timers

If no "pong" in 5 seconds:
  -> Close socket
  -> Show error message "Connection to the device is lost"
```

**Reconnection Logic** (`data/scripts/script.js:89, 273-295`):
```
Every 10 seconds (setInterval):
  -> Check if socket.readyState === CLOSED
  -> If closed, increment connectionAttempts
  -> If attempts > 2, show error message
  -> Create new WebSocket connection
  -> Re-attach all event handlers
```

### 2.3 Server-Side Broadcasting

**`notifyClients()`** (`src/websocketsettings.cpp:83+`):
- Broadcasts device state (relay status, telemetry, schedules) as JSON
- Called on WebSocket connect and whenever device state changes
- Uses `ws.textAll(output)` to send to ALL connected WebSocket clients

---

## 3. Complete Lifecycle Diagram

```
DEVICE BOOT
    |
    v
initializeSessions() -- Check RTC magic, init slots if needed
    |
    v
initWebSocket() -- Register /ws handler
    |
    v
setupAPIs() -- Register HTTP endpoints with verifyCookie()

========== User Opens Browser ==========

Browser -> GET /index.html
    |
    (No cookie) -> Redirect to /login.html
    |
    v
Browser -> POST /api/login { pin, deviceTime }
    |
    v
Server: Validate PIN -> startNewSession() -> Set-Cookie: SESSIONID=abc123
    |
    v
Browser -> Redirect to /index.html (with cookie)
    |
    v
Browser -> GET /index.html (cookie valid) -> Serve page
    |
    v
Browser -> Opens WebSocket to ws://<host>/ws (no auth needed)
    |
    v
Server: WS_EVT_CONNECT -> notifyClients() -> Sends full device state
    |
    v
    +------ Normal Operation Loop ------+
    |                                    |
    |  WebSocket: telemetry broadcasts   |
    |  every few seconds                 |
    |                                    |
    |  HTTP API: control commands        |
    |  (each resets 10-min timer)        |
    |                                    |
    |  Ping/Pong: every 15s if idle      |
    +------------------------------------+

========== 10 Minutes No API Calls ==========

Browser -> Any HTTP API call (e.g., /api/getStatus)
    |
    v
verifyCookie(): millis() - lastActivity > 600,000ms -> FAIL
    |
    v
Server -> 401 Unauthorized
    |
    v
fetchWithAuth() intercepts 401 -> Redirect to /login.html?returnUrl=...
    |
    v
Page unload -> WebSocket closes -> WS_EVT_DISCONNECT
    |
    v
ws.cleanupClients() in main loop -> Frees WebSocket RAM

========== User Logs Out ==========

Browser -> DELETE /api/logout
    |
    v
Server: session->active = false, Set-Cookie: Max-Age=0
    |
    v
Browser -> Redirect to /login.html
    |
    v
Page unload -> WebSocket closes -> RAM freed
```

---

## 4. Key File References

| File | Lines | Purpose |
|------|-------|---------|
| `src/websocketsettings.h` | 14-28 | Session constants and struct |
| `src/websocketsettings.cpp` | 4-6 | RTC storage and timeout constant |
| `src/websocketsettings.cpp` | 52-81 | WebSocket event handlers |
| `src/websocketsettings.cpp` | 83-94 | Session init on boot |
| `src/websocketsettings.cpp` | 354-361 | Session cleanup (defined, not called) |
| `src/websocketsettings.cpp` | 385-426 | Session creation |
| `src/api.cpp` | 10-41 | `verifyCookie()` - session validation |
| `src/api.cpp` | 70-88 | `logoutSession()` |
| `src/api.cpp` | 857-934 | Login endpoint |
| `src/api.cpp` | 2021-2028 | Logout endpoint |
| `src/main.cpp` | 284 | `initializeSessions()` call |
| `src/main.cpp` | 515 | `ws.cleanupClients()` in loop |
| `data/scripts/common.js` | 175-204 | 401 -> login redirect |
| `data/scripts/script.js` | 89 | 10s reconnect check interval |
| `data/scripts/script.js` | 182-202 | Ping/pong keep-alive |
| `data/scripts/script.js` | 273-312 | Reconnect and event handlers |
