let LOG_SIZE_LIMIT = 2 * 1024 ;// 30 Kilo Bytes

// Global variables for pagination and filtering
let allLogRecords = [];
let currentPage = 1;
let logsPerPage = 100;
let filteredRecords = [];

async function downloadBinaryFile(url) {
    const response = await fetchWithAuth(url);
    if (!response.ok) {
        throw new Error('Network response was not ok');
    }
    const arrayBuffer = await response.arrayBuffer();
    console.log(arrayBuffer);
    return arrayBuffer;
}

// Parse a single log file with header
function parseLogFileData(arrayBuffer) {
    const recordSize = 16; // LogEntry: timestamp(4) + mode(1) + scheduleType(1) + duration(2) + current(4) + voltage(4)
    
    const dataView = new DataView(arrayBuffer);
    const records = [];
    
    // Calculate entry count from file size (no header)
    const entryCount = Math.floor(arrayBuffer.byteLength / recordSize);
    
    console.log(`Operation file: ${entryCount} entries, file size: ${arrayBuffer.byteLength} bytes`);
    
    // Read entries (in reverse order for newest first)
    for (let i = entryCount - 1; i >= 0; i--) {
        try {
            const offset = i * recordSize;
            
            // Read timestamp (4 bytes)
            const entryTimestamp = dataView.getUint32(offset, true);
            
            // Read mode (1 byte)
            const mode = dataView.getUint8(offset + 4);
            
            // Read schedule type (1 byte)
            const scheduleType = dataView.getUint8(offset + 5);
            
            // Read duration (2 bytes)
            const duration = dataView.getUint16(offset + 6, true);
            
            // Read current (4 bytes)
            const current = Math.round((dataView.getFloat32(offset + 8, true) ) * 100) / 100;
            
            // Read voltage (4 bytes)
            const voltage = Math.round((dataView.getFloat32(offset + 12, true) ) * 100) / 100;
            
            // Validate duration - skip entries with duration beyond 24*60 minutes (1440 minutes)
            const MAX_DURATION_MINUTES = 24 * 60; // 24 hours in minutes
            if (duration > MAX_DURATION_MINUTES) {
                console.log(`Skipping entry with invalid duration: ${duration} minutes`);
                continue;
            }
            
            // Convert timestamp to JavaScript Date object
            const date = new Date(entryTimestamp * 1000);
            records.push({ date, md: mode, duration, current, voltage });
            
        } catch (error) {
            console.error(`Error parsing operation entry at index ${i}:`, error);
            break;
        }
    }
    
    return records;
}

// Get list of all operation log files
async function getOperationLogFiles() {
    try {
        // First, try to get file list from API if available
        const response = await fetchWithAuth('/api/getOperationLogFiles');
        if (response.ok) {
            const fileList = await response.json();
            return fileList;
        }
    } catch (error) {
        console.log('API not available, will try directory listing');
    }
    
    // Fallback: try to get files from directory listing
    // This is a simplified approach - you might need to implement a proper directory listing API
    const response = await fetchWithAuth('/logs/');
    if (response.ok) {
        const text = await response.text();
        // Parse HTML directory listing to extract OP_*.bin files
        const files = [];
        const regex = /OP_\d+\.bin/g;
        let match;
        while ((match = regex.exec(text)) !== null) {
            files.push(match[0]);
        }
        return files;
    }
    
    return [];
}

// Download and parse all operation log files
async function getAllOperationLogs() {
    try {
        const files = await getOperationLogFiles();
        console.log('Found log files:', files);
        
        if (files.length === 0) {
            console.log('No operation log files found');
            return [];
        }
        
        // Sort files by timestamp (newest first)
        files.sort((a, b) => {
            const timestampA = parseInt(a.match(/OP_(\d+)\.bin/)[1]);
            const timestampB = parseInt(b.match(/OP_(\d+)\.bin/)[1]);
            return timestampB - timestampA; // Descending order
        });
        
        console.log('Sorted files:', files);
        
        let allRecords = [];
        
        // Download and parse each file
        for (const filename of files) {
            try {
                console.log(`Downloading ${filename}...`);
                const arrayBuffer = await downloadBinaryFile(`/logs/${filename}`);
                const records = parseLogFileData(arrayBuffer);
                console.log("after parseLogFile - record count - ", records.count);
                allRecords = allRecords.concat(records);
                console.log(`Parsed ${records.length} records from ${filename}`);
            } catch (error) {
                console.error(`Error processing file ${filename}:`, error);
            }
        }
        
        // Sort all records by timestamp (newest first)
        allRecords.sort((a, b) => b.date.getTime() - a.date.getTime());
        
        console.log(`Total records: ${allRecords.length}`);
        
        // Store all records globally and initialize filtering
        allLogRecords = [...allRecords];
        filteredRecords = [...allRecords];
        
        if (typeof extractAvailableDates === 'function') {
            extractAvailableDates();
        }
        // Set default date range to filters
        setDefaultDateRange();
        // Initialize UI
        updateFilterDisplay();
        
        // Display first page by default
        displayCurrentPage();
        
        return allRecords;
        
    } catch (error) {
        console.error('Error fetching or parsing log files:', error);
        return [];
    }
}
async function setDefaultDateRange(){
    // Initialize date pickers with default values (last 7 days)
    let edr = new Date();
    let sdr = new Date(edr.getTime() - (7 * 24 * 60 * 60 * 1000));
    
    const iSD = el('startDate');
    const iED = el('endDate');
    // Set default date range to Start &End date.
    // If the minDate and MaxDate are falling within the last 7 days, then set the default date range to the last 7 days.
    if (minDate && maxDate && minDate > sdr) {
        sdr = minDate;
    }
    if (iSD && iED) {
    iSD.value = sdr.toISOString().split('T')[0];
    iED.value = edr.toISOString().split('T')[0];
    }
    filterLogsByDate(iSD.value, iED.value);

}

// Legacy function for backward compatibility (single file approach)
async function getRecordsFromBinaryFile(dataUrl, pointerUrl) {
    try {
        const arrayBuffer = await downloadBinaryFile(dataUrl);
        const pointerBuffer = await downloadBinaryFile(pointerUrl);

        const records = parseBinaryData(arrayBuffer, pointerBuffer);
        return records;
    } catch (error) {
        console.error('Error fetching or parsing binary file:', error);
    }
}

// Legacy parsing function for single circular file
function parseBinaryData(arrayBuffer, pointerBuffer) {
    const recordSize = 16; // 4 bytes (time_t) + 1 byte (ModeOfAction) + 1 byte (ScheduleType) + 2 bytes (duration) + 4 bytes (float current) + 4 bytes (float voltage)
    const pointerView = new DataView(pointerBuffer);
    const dataView = new DataView(arrayBuffer);
    const records = [];
    const viewSize= dataView.byteLength;

    // Read pointers values
    const start = pointerView.getUint32(0, true);
    const end = pointerView.getUint32(4, true);
    const count = pointerView.getUint32(8, true);

    const maxEntries = Math.floor(viewSize / recordSize);
    let current = (end - recordSize);
    if (current < 0) {
        current = (maxEntries - 1) * recordSize;
    }

    const countToUI = count <= maxEntries ? count : maxEntries;

    for (let i = 0; i < countToUI; i++) {
        try {
        // Read timestamp (4 bytes)
            const timestamp = dataView.getUint32(current, true);

            // Read mode (1 byte)
            const mode = dataView.getUint8(current + 4);

            // Read schedule type (1 byte)
            const scheduleType = dataView.getUint8(current + 5);

            // Read duration (2 bytes)
            const duration = dataView.getUint16(current + 6, true);

            // Read current (4 bytes)
            const currentValue = Math.round((dataView.getFloat32(current + 8, true) ) * 1000) / 1000;
            
            // Read voltage (4 bytes)
            const voltageValue = Math.round((dataView.getFloat32(current + 12, true) ) * 100) / 100;
            
            // Validate duration - skip entries with duration beyond 24*60 minutes (1440 minutes)
            const MAX_DURATION_MINUTES = 24 * 60; // 24 hours in minutes
            if (duration > MAX_DURATION_MINUTES) {
                console.log(`Skipping legacy entry with invalid duration: ${duration} minutes`);
                current -= recordSize;
                if (current < 0) {
                    current = (maxEntries - 1) * recordSize;
                }
                continue;
            }

            // Convert timestamp to JavaScript Date object
            const date = new Date(timestamp * 1000);
            records.push({ date, md:mode, duration: duration, current: currentValue, voltage: voltageValue });

            current -= recordSize;
            if (current < 0) {
                current = (maxEntries - 1) * recordSize;
            }
        } catch (error) {
            console.error('Error parsing binary log at index', i, ':', error);
            return records; // return the collected records
        }
    }
    return records;
}

document.addEventListener("DOMContentLoaded", () => {
    // Try new multi-file approach first
    getAllOperationLogs()
        .then(records => {
            console.log('Multi-file logs:', records);
            if (records && records.length > 0) {
                // populateLogs is now called from displayCurrentPage()
                console.log('Logs loaded successfully');
            } else {
                // Fallback to legacy single file approach
                console.log('No multi-file logs found, trying legacy approach...');
                return getRecordsFromBinaryFile('/logs/gen2-log.bin', '/logs/pointers.bin');
            }
        })
        .then(records => {
            if (records) {
                console.log('Legacy logs:', records);
                // Store legacy records and initialize filtering
                allLogRecords = [...records];
                filteredRecords = [...records];
                updateFilterDisplay();
                displayCurrentPage();
            }
        })
        .catch(error => {
            console.error('Error loading logs:', error);
        });
});
