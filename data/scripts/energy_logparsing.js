let LOG_SIZE_LIMIT = 2 * 1024;

// Global variables for pagination and filtering
let allEnergyRecords = [];
let currentEnergyPage = 1;
let energyLogsPerPage = 100;
let filteredEnergyRecords = [];
let displayedEnergyRecords = [];

// New multi-file energy log parsing functions
async function getAllEnergyLogs() {
    try {
        // Get list of energy log files
        const response = await fetchWithAuth('/api/getEnergyLogFiles');
        if (!response.ok) {
            throw new Error('Failed to get energy log files');
        }
        
        const files = await response.json();
        console.log('Energy log files:', files);
        
        if (!files || files.length === 0) {
            console.log('No energy log files found');
            return [];
        }
        
        const allRecords = [];
        
        // Read from newest to oldest files
        for (const filename of files) {
            try {
                const records = await parseEnergyLogFile(filename);
                if (records && records.length > 0) {
                    allRecords.push(...records);
                }
            } catch (error) {
                console.error(`Error parsing energy file ${filename}:`, error);
            }
        }
        
        // Sort by timestamp (newest first)
        allRecords.sort((a, b) => b.startDate.getTime() - a.startDate.getTime());
        
        // Store all records globally and initialize filtering
        allEnergyRecords = [...allRecords];
        filteredEnergyRecords = [...allRecords];
        
        // Extract available dates for calendar constraints
        if (typeof extractAvailableDates === 'function') {
            extractAvailableDates();
        }
        // Set default date range to filters
        setDefaultDateRange();
        
        // Initialize UI
        updateEnergyFilterDisplay();
        
        // Display first page by default
        displayCurrentEnergyPage();
        
        return allRecords;
        
    } catch (error) {
        console.error('Error getting energy logs:', error);
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
    filterEnergyLogsByDate(iSD.value, iED.value);

}

// Parse a single energy log file with header
async function parseEnergyLogFile(filename) {
    try {
        const response = await fetchWithAuth(`/logs/${filename}`);
        if (!response.ok) {
            throw new Error(`Failed to fetch ${filename}`);
        }
        
        const arrayBuffer = await response.arrayBuffer();
        return parseEnergyLogFileData(arrayBuffer);
        
    } catch (error) {
        console.error(`Error parsing energy file ${filename}:`, error);
        return [];
    }
}

function parseEnergyLogFileData(arrayBuffer) {
    const recordSize = 12; // EnergyEntry: startTS(4) + endTS(4) + energy(4) - same size as old format
    
    const dataView = new DataView(arrayBuffer);
    const records = [];
    
    // Calculate entry count from file size (no header)
    const entryCount = Math.floor(arrayBuffer.byteLength / recordSize);
    
    console.log(`Energy file: ${entryCount} entries, file size: ${arrayBuffer.byteLength} bytes`);
    
    // Read entries (in reverse order for newest first)
    for (let i = entryCount - 1; i >= 0; i--) {
        try {
            const offset = i * recordSize;
            
            // Read start timestamp (4 bytes)
            const startTS = dataView.getUint32(offset, true);
            
            // Read end timestamp (4 bytes)
            const endTS = dataView.getUint32(offset + 4, true);
            
            // Read energy value (4 bytes)
            const energyValue = Math.round((dataView.getFloat32(offset + 8, true)) * 1000) / 1000;
            
            // Convert timestamps to JavaScript Date objects
            const startDate = new Date(startTS * 1000);
            const endDate = new Date(endTS * 1000);
            
            records.push({ startDate, endDate, energyValue });
            
        } catch (error) {
            console.error(`Error parsing energy entry at index ${i}:`, error);
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
//         const records = parseBinaryData(arrayBuffer, pointerBuffer);
        
//         // Store legacy records and initialize filtering
//         allEnergyRecords = [...records];
//         filteredEnergyRecords = [...records];
        
//         // Extract available dates for calendar constraints
//         if (typeof extractAvailableDates === 'function') {
//             extractAvailableDates();
//         }
        
//         // Initialize UI
//         updateEnergyFilterDisplay();
        
//         // Display first page by default
//         displayCurrentEnergyPage();
        
//         return records;
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
//     const recordSize = 12; // 4 bytes (time_t) + 4 bytes (time_t) + 4 bytes (float)
//     const pointerView = new DataView(pointerBuffer);
//     const dataView = new DataView(arrayBuffer);
//     const records = [];
//     const viewSize = dataView.byteLength;

//     // Read pointers values
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
//             const startTS = dataView.getUint32(current, true);
//             const endTS = dataView.getUint32(current + 4, true);

//             // Read energy value (4 bytes)
//             const energyValue = Math.round((dataView.getFloat32(current + 8, true)) * 1000) / 1000;

//             // Convert timestamp to JavaScript Date object
//             const startDate = new Date(startTS * 1000);
//             const endDate = new Date(endTS * 1000);
//             records.push({ startDate: startDate, endDate: endDate, energyValue: energyValue });

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

// Main function to load energy logs
async function loadEnergyLogs() {
    console.log('Loading energy logs...');
    
    // Show loading indicator
    const loadingIndicator = el('loading-indicator');
    const logContentDiv = _qs(".log-content");
    
    if (loadingIndicator) loadingIndicator.style.display = 'block';
    if (logContentDiv) logContentDiv.innerHTML = '';
    
    try {
        // Try new multi-file format first
        let records = await getAllEnergyLogs();
        
        // If no records found, try legacy format as fallback
        if (!records || records.length === 0) {
            console.log('No multi-file energy logs found, trying legacy format...');
            //records = await getRecordsFromBinaryFile('/logs/gen2-energy.bin', '/logs/energy-pointers.bin');
        }
        
        if (records && records.length > 0) {
            console.log('Energy logs loaded:', records.length, 'entries');
            // populateEnergyLogs is now called from displayCurrentEnergyPage()
        } else {
            console.log('No energy logs found');
            if (logContentDiv) {
                logContentDiv.innerHTML = '<p style="text-align: center; color: #666;">No energy logs available</p>';
            }
        }
    } catch (error) {
        console.error('Error loading energy logs:', error);
        if (logContentDiv) {
            logContentDiv.innerHTML = '<p style="text-align: center; color: rgb(210, 30, 30);">Error loading energy logs</p>';
        }
    } finally {
        // Hide loading indicator
        if (loadingIndicator) loadingIndicator.style.display = 'none';
    }
}

// Function to populate energy logs in the table (updated for pagination)
function populateEnergyLogs(data) {
    if (data) {
        const logContentDiv = _qs(".log-content");
        if (!logContentDiv) return;

        const table = _ce("table");

        // Header row
        const headerRow = _ce("tr");
        ['Start', 'End', 'Dur.', 'kWh'].forEach(text => {
            const th = _ce("th");
            th.textContent = text;
            th.style.color = '#555';
            th.style.fontWeight = '600';
            th.style.fontSize = '0.8rem';
            th.style.padding = '8px 4px';
            headerRow.appendChild(th);
        });
        table.appendChild(headerRow);

        data.forEach((log) => {
            const row = _ce("tr");

            // Format timestamp
            let offset = new Date().getTimezoneOffset();
            const start = new Date(log.startDate.getTime() + (offset * 60 * 1000));

            const startDateCell = _ce("td");
            const startDatePart = start.getDate() + '/' + start.toLocaleString('default', { month: 'short' });
            const startTimePart = (start.getHours() % 12 || 12) + ':' + String(start.getMinutes()).padStart(2, '0') + ':' + String(start.getSeconds()).padStart(2, '0') + (start.getHours() >= 12 ? 'pm' : 'am');
            startDateCell.innerHTML = '<span style="color:#888">' + startDatePart + '</span> <span style="color:#666;font-weight:500">' + startTimePart + '</span>';

            const endDateCell = _ce("td");
            const end = new Date(log.endDate.getTime() + (offset * 60 * 1000));
            let durationMessage = "";
            if (end.getFullYear() < 2000) {
                endDateCell.innerHTML = '<span style="color:#aaa">--</span>';
            } else {
                const endTimePart = (end.getHours() % 12 || 12) + ':' + String(end.getMinutes()).padStart(2, '0') + ':' + String(end.getSeconds()).padStart(2, '0') + (end.getHours() >= 12 ? 'pm' : 'am');
                endDateCell.innerHTML = '<span style="color:#666;font-weight:500">' + endTimePart + '</span>';
                const duration = Math.round((log.endDate - log.startDate) / 1000);
                const hours = Math.floor(duration / 3600);
                const minutes = Math.floor((duration % 3600) / 60);
                const seconds = duration % 60;
                // Seconds matter here: a Duration Based schedule's owed time is
                // accounted from these very entries, to the second.
                durationMessage = hours + "h " + minutes + "m " + seconds + "s";
            }

            const durationCell = _ce("td");
            durationCell.textContent = durationMessage;
            durationCell.style.color = '#555';

            const messageCell = _ce("td");
            messageCell.textContent = log.energyValue;
            messageCell.style.color = '#333';
            messageCell.style.fontWeight = '500';

            row.appendChild(startDateCell);
            row.appendChild(endDateCell);
            row.appendChild(durationCell);
            row.appendChild(messageCell);
            table.appendChild(row);
        });

        logContentDiv.innerHTML = "";
        logContentDiv.appendChild(table);
    }
}

// Initialize when DOM is loaded
document.addEventListener("DOMContentLoaded", () => {
    loadEnergyLogs();
});
