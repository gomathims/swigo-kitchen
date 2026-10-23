let LOG_SIZE_LIMIT = 6 * 1024; // 6 Kilo Bytes

// New multi-file debug log parsing functions
async function getAllDebugLogs() {
    try {
        // Get list of debug log files
        const response = await fetchWithAuth('/api/getDebugLogFiles');
        if (!response.ok) {
            throw new Error('Failed to get debug log files');
        }
        
        const files = await response.json();
        console.log('Debug log files:', files);
        
        if (!files || files.length === 0) {
            console.log('No debug log files found');
            return [];
        }
        
        const allRecords = [];
        let totalEntriesRead = 0;
        const maxEntries = 1000; // Limit total entries to display
        
        // Read from newest to oldest files
        for (const filename of files) {
            if (totalEntriesRead >= maxEntries) break;
            
            try {
                const records = await parseDebugLogFile(filename);
                if (records && records.length > 0) {
                    allRecords.push(...records);
                    totalEntriesRead += records.length;
                }
            } catch (error) {
                console.error(`Error parsing debug file ${filename}:`, error);
            }
        }
        
        // Sort by timestamp (newest first) and limit to max entries
        allRecords.sort((a, b) => b.date.getTime() - a.date.getTime());
        return allRecords.slice(0, maxEntries);
        
    } catch (error) {
        console.error('Error getting debug logs:', error);
        return [];
    }
}

// Parse a single debug log file with header
async function parseDebugLogFile(filename) {
    try {
        const response = await fetchWithAuth(`/logs/${filename}`);
        if (!response.ok) {
            throw new Error(`Failed to fetch ${filename}`);
        }
        
        const arrayBuffer = await response.arrayBuffer();
        return parseDebugLogFileData(arrayBuffer);
        
    } catch (error) {
        console.error(`Error parsing debug file ${filename}:`, error);
        return [];
    }
}

function parseDebugLogFileData(arrayBuffer) {
    const recordSize = 208; // DebugEntry: timestamp(4) + action(1) + message(200) - no padding
    
    const dataView = new DataView(arrayBuffer);
    const records = [];
    
    // Calculate entry count from file size (no header)
    const entryCount = Math.floor(arrayBuffer.byteLength / recordSize);
    
    console.log(`Debug file: ${entryCount} entries, file size: ${arrayBuffer.byteLength} bytes`);
    
    // Read entries (in reverse order for newest first)
    for (let i = entryCount - 1; i >= 0; i--) {
        try {
            const offset = i * recordSize;
            
            // Read timestamp (4 bytes)
            const entryTimestamp = dataView.getUint32(offset, true);
            
            // Read action (1 byte)
            const action = dataView.getUint8(offset + 4);
            
            // Read message (200 bytes)
            const messageBytes = new Uint8Array(dataView.buffer, offset + 5, 200);
            // Clean the message by removing null bytes and invalid characters
            let message = new TextDecoder('utf-8').decode(messageBytes);
            message = message.replace(/\0/g, '').replace(/[^\x20-\x7E]/g, '').trim();
            
            // Only add entry if message is not empty and timestamp is valid
            if (message.length > 0) {
                // Convert timestamp to JavaScript Date object
                const date = new Date(entryTimestamp * 1000);
                records.push({ date, md: action, message: message });
            }
            
        } catch (error) {
            console.error(`Error parsing debug entry at index ${i}:`, error);
            break;
        }
    }
    
    return records;
}

// Legacy function for old circular queue format (fallback)
// async function getRecordsFromBinaryFile(dataUrl, pointerUrl) {
//     try {
//         const arrayBuffer = await downloadBinaryFile(dataUrl);
//         const pointerBuffer = await downloadBinaryFile(pointerUrl);
//         return parseBinaryData(arrayBuffer, pointerBuffer);
//     } catch (error) {
//         console.error('Error fetching or parsing binary file:', error);
//         return [];
//     }
// }

// async function downloadBinaryFile(url) {
//     const response = await fetch(url);
//     if (!response.ok) {
//         throw new Error('Network response was not ok');
//     }
//     const arrayBuffer = await response.arrayBuffer();
//     return arrayBuffer;
// }

// function parseBinaryData(arrayBuffer, pointerBuffer) {
//     const recordSize = 205; // 4 bytes (time_t) + 1 byte (uint8_t) + 200 bytes Char array - no padding
//     const pointerView = new DataView(pointerBuffer);
//     const dataView = new DataView(arrayBuffer);
//     const records = [];
//     const viewSize = dataView.byteLength;

//     // Read pointers values
//     const start = pointerView.getUint32(0, true);
//     const end = pointerView.getUint32(4, true);
//     const count = pointerView.getUint32(8, true);

//     const maxEntries = Math.floor(viewSize / recordSize);
//     let current = (end - recordSize);
//     if (current < 0) {
//         current = (maxEntries - 1) * recordSize;
//     }

//     const countToUI = count <= maxEntries ? count : maxEntries;

//     for (let i = 0; i < countToUI; i++) {
//         try {
//             // Read timestamp (4 bytes)
//             const timestamp = dataView.getUint32(current, true);
            
//             // Validate timestamp (must be reasonable)
//             if (timestamp === 0 || timestamp < 1577836800 || timestamp > 2000000000) {
//                 console.warn(`Skipping legacy entry with invalid timestamp: ${timestamp}`);
//                 current -= recordSize;
//                 if (current < 0) {
//                     current = (maxEntries - 1) * recordSize;
//                 }
//                 continue;
//             }

//             // Read mode (1 byte)
//             const mode = dataView.getUint8(current + 4);

//             // Read message (200 bytes)
//             const messageBytes = new Uint8Array(dataView.buffer, current + 5, 200);
//             // Clean the message by removing null bytes and invalid characters
//             let message = new TextDecoder('utf-8').decode(messageBytes);
//             message = message.replace(/\0/g, '').replace(/[^\x20-\x7E]/g, '').trim();
            
//             // Only add entry if message is not empty
//             if (message.length > 0) {
//                 // Convert timestamp to JavaScript Date object
//                 const date = new Date(timestamp * 1000);
//                 records.push({ date, md: mode, message: message });
//             }

//             current -= recordSize;
//             if (current < 0) {
//                 current = (maxEntries - 1) * recordSize;
//             }
//         } catch (error) {
//             console.error('Error parsing binary log at index', i, ':', error);
//             return records; // return the collected records
//         }
//     }
//     return records;
// }

// Main function to load debug logs
async function loadDebugLogs() {
    console.log('Loading debug logs...');
    
    // Show loading indicator
    const loadingIndicator = el('loading-indicator');
    const logContentDiv = _qs(".log-content");
    
    if (loadingIndicator) loadingIndicator.style.display = 'block';
    if (logContentDiv) logContentDiv.innerHTML = '';
    
    try {
        // Try new multi-file format first
        let records = await getAllDebugLogs();
        
        // If no records found, try legacy format as fallback
        if (!records || records.length === 0) {
            console.log('No multi-file debug logs found, trying legacy format...');
            records = await getRecordsFromBinaryFile('/logs/gen2-debug.bin', '/logs/debug-pointers.bin');
        }
        
        if (records && records.length > 0) {
            console.log('Debug logs loaded:', records.length, 'entries');
            populateLogs(records);
        } else {
            console.log('No debug logs found');
            if (logContentDiv) {
                logContentDiv.innerHTML = '<p style="text-align: center; color: #666;">No debug logs available</p>';
            }
        }
    } catch (error) {
        console.error('Error loading debug logs:', error);
        if (logContentDiv) {
            logContentDiv.innerHTML = '<p style="text-align: center; color: rgb(210, 30, 30);">Error loading debug logs</p>';
        }
    } finally {
        // Hide loading indicator
        if (loadingIndicator) loadingIndicator.style.display = 'none';
    }
}

function populateLogs(data) {
    if (data) {
        const logContentDiv = _qs(".log-content");
        if (!logContentDiv) return;
        
        const table = _ce("table");
        data.forEach((log, index) => {
            const row = _ce("tr");
            const timestampCell = _ce("td");
            timestampCell.style.width = '30%';
            const messageCell = _ce("td");
            messageCell.style.width = '70%';
            
            // Format timestamp - no need to adjust for timezone as the timestamp is already in local time
            let offset = new Date().getTimezoneOffset();
            console.log(offset);
            const timestamp = new Date(log.date.getTime() + (offset * 60 * 1000)); // add the offset hours value to the timestamp
            
            // Format timestamp in a clean, readable format
            const formattedTimestamp = timestamp.toLocaleString('en-US', { 
                month: 'short',
                day: 'numeric',
                hour: 'numeric',
                minute: '2-digit',
                second: '2-digit',
                hour12: true
            });
                    
            timestampCell.innerHTML = formattedTimestamp;

            // Determine message based on mode
            let message = DebugMessages[log.md] || `Unknown Action (${log.md})`;
            let messageText = log.message;
            
            messageCell.innerHTML = message + ' <br /> -' + messageText;

            // Append cells to row, and row to table
            row.appendChild(timestampCell);
            row.appendChild(messageCell);
            table.appendChild(row);
        });
        
        // Clear existing content and append the new table
        logContentDiv.innerHTML = "";
        logContentDiv.appendChild(table);
    }
}

// Initialize when DOM is loaded
document.addEventListener("DOMContentLoaded", () => {
    loadDebugLogs();
});

