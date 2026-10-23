# Schedule MQTT Payload Size Analysis

## Current Configuration

- **MAX_PAYLOAD_LENGTH**: 1024 bytes (defined in `mqtt.h`)
- **PubSubClient Buffer**: 1280 bytes (`MAX_PAYLOAD_LENGTH + 256`)
- **PubSubClient Library**: knolleary/PubSubClient@^2.8
- **Max Schedules**: 10 (defined as `MAX_SCHEDULE_OBJECTS`)

## Payload Structure

The schedule is sent as a ThingsBoard attribute in this format:
```json
{"schedule":"{\"s\":[{...},{...}]}"}
```

**Important**: The inner schedule JSON is stored as a **string** and gets **escaped** during serialization, which roughly **doubles** its size.

## Size Calculation Per Schedule

### Worst Case (Weekly Schedule with All 7 Days, Cyclic Enabled)
```json
{"t":"W","e":1,"d":[0,1,2,3,4,5,6],"1":23,"2":59,"3":23,"4":59,"c":1,"5":23,"6":59,"7":23,"8":59}
```
**Size**: ~110-120 bytes per schedule

### Best Case (Daily Schedule, No Cyclic)
```json
{"t":"D","e":0,"1":0,"2":0,"3":23,"4":59,"c":0,"5":0,"6":0,"7":0,"8":0}
```
**Size**: ~60-70 bytes per schedule

### Average Case (Weekly with 3-4 Days, No Cyclic)
**Size**: ~80-90 bytes per schedule

## Total Payload Size Calculation

### For 10 Schedules (Worst Case):
1. **Raw Schedule JSON**: ~110 bytes × 10 = ~1,100 bytes
2. **Wrapper overhead**: `{"s":[...]}` = ~7 bytes
3. **Total raw JSON**: ~1,107 bytes
4. **When wrapped in attribute**: `{"schedule":"..."}` 
   - The inner JSON string gets escaped (quotes become `\"`, etc.)
   - Escaping roughly doubles the size: ~1,107 × 2 = ~2,214 bytes
   - Plus wrapper: ~2,214 + 15 = **~2,229 bytes**

### For 10 Schedules (Average Case):
1. **Raw Schedule JSON**: ~85 bytes × 10 = ~850 bytes
2. **Wrapper overhead**: ~7 bytes
3. **Total raw JSON**: ~857 bytes
4. **Escaped + wrapped**: ~857 × 2 + 15 = **~1,729 bytes**

### For 10 Schedules (Best Case):
1. **Raw Schedule JSON**: ~65 bytes × 10 = ~650 bytes
2. **Wrapper overhead**: ~7 bytes
3. **Total raw JSON**: ~657 bytes
4. **Escaped + wrapped**: ~657 × 2 + 15 = **~1,329 bytes**

## Conclusion

**❌ ISSUE CONFIRMED**: Sending 10 schedules will **exceed** the 1024-byte limit in most cases:
- **Worst case**: ~2,229 bytes (2.2× over limit)
- **Average case**: ~1,729 bytes (1.7× over limit)  
- **Best case**: ~1,329 bytes (1.3× over limit)

## Recommendations

### Option 1: Increase MAX_PAYLOAD_LENGTH (Recommended)
- Increase `MAX_PAYLOAD_LENGTH` to **2048** or **4096** bytes
- Update PubSubClient buffer: `mqttClient.setBufferSize(MAX_PAYLOAD_LENGTH + 256)`
- **Pros**: Simple, supports all 10 schedules
- **Cons**: Uses more RAM

### Option 2: Send Schedule as Raw JSON Object (Not String)
Instead of:
```cpp
attrDoc["schedule"] = scheduleJson;  // Stores as string
```

Use:
```cpp
JsonDocument scheduleDoc;
deserializeJson(scheduleDoc, scheduleJson);
attrDoc["schedule"] = scheduleDoc;  // Stores as object
```

This avoids escaping and reduces size by ~50%, but ThingsBoard might require string format.

### Option 3: Split into Multiple Messages
Send schedules in chunks (e.g., 5 schedules per message).

### Option 4: Use More Compact JSON Format
Reduce field names (already using "1"-"8", "t", "e", "c", "d") - already optimized.

## PubSubClient Limits

- **Default max packet size**: 128 bytes (not used, overridden)
- **Current buffer**: 1280 bytes (configurable)
- **Theoretical max**: ~65KB (uint16_t limit in PubSubClient)
- **Practical ESP32 limit**: ~16KB reported by users

## ThingsBoard Limits

### MQTT Payload Size Limits
- **Default maximum payload size**: **64 KB (65,536 bytes)** - configurable via `transport.mqtt.netty.max_payload_size`
- **Practical limit**: **~8 KB (8,192 bytes)** - Issues reported with payloads exceeding this size
  - Related to MqttDecoder max decoding size of 8,092 bytes
  - May cause disconnects if exceeded

### For Your Use Case
- **Your payload size**: ~1.3-2.2 KB (for 10 schedules)
- **Status**: ✅ **Well within both limits** (8KB practical and 64KB theoretical)
- **Conclusion**: **No ThingsBoard restrictions** for increasing payload size to 2-4 KB

## Recommendation

**✅ Safe to increase `MAX_PAYLOAD_LENGTH` to 2048 or 4096 bytes**

Your payload size (~1.3-2.2 KB) is well below ThingsBoard's practical limit of 8KB, so there are **no ThingsBoard restrictions** preventing you from increasing the payload size. The main consideration is ESP32 RAM usage, not ThingsBoard limits.

**Recommended action**: Increase `MAX_PAYLOAD_LENGTH` to **2048 bytes** to safely support 10 schedules with room for growth.
