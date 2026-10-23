!/bin/bash

# Configuration
DEVICE_IP="swigo-eth.local"  # Replace with your device IP
DEVICE_PIN="1234"           # Replace with your device PIN
COOKIE_FILE="cookies.txt"

# Get current time in ISO format
CURRENT_TIME=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

echo "Current directory: $(pwd)"
echo "Step 1: Logging in..."

LOGIN_RESPONSE=$(curl -s -X POST http://$DEVICE_IP/api/login \
  -H "Content-Type: application/json" \
  -d "{
    \"devicePin\": \"$DEVICE_PIN\",
    \"deviceTime\": \"$CURRENT_TIME\",
    \"timezoneOffset\": 330
  }" \
  -c $COOKIE_FILE)

echo "Login response: $LOGIN_RESPONSE"
echo "Test cookie"

# Check if cookie file was created
if [ -f "$COOKIE_FILE" ]; then
    echo "Cookie file created: $(pwd)/$COOKIE_FILE"
    echo "Cookie contents:"
    cat $COOKIE_FILE
else
    echo "ERROR: Cookie file was not created!"
    exit 1
fi

echo ""
echo "Step 2: fetching session id ..."
SESSION_ID=$(grep SESSIONID cookies.txt | cut -f7)
Echo "SESSION = $SESSION_ID"

# Try the corrected format
CLEAR_RESPONSE=$(curl -v -X POST http://$DEVICE_IP/api/clearLogsByType?type=debug \
  -H "Content-Type: application/json" \
  -H "Cookie: SESSIONID=$SESSION_ID" \
  -d "{
    \"enteredPin\": \"$DEVICE_PIN\"
  }" )

echo "Clear logs response: $CLEAR_RESPONSE"

# Clean up
rm -f $COOKIE_FILE

echo "Done!"
