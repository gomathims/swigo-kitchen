// Access widget context
var data = widgetContext.data; // Timeseries data
var timeWindow = widgetContext.timeWindow; // Time window for start/end time
var csvContent = "data:text/csv;charset=utf-8,";

// Function to format timestamp to local timezone in YYYY-MM-DD HH:mm:ss
function formatLocalDate(timestamp) {
    var date = new Date(timestamp);
    var year = date.getFullYear();
    var month = String(date.getMonth() + 1).padStart(2, '0');
    var day = String(date.getDate()).padStart(2, '0');
    var hours = String(date.getHours()).padStart(2, '0');
    var minutes = String(date.getMinutes()).padStart(2, '0');
    var seconds = String(date.getSeconds()).padStart(2, '0');
    return year + '-' + month + '-' + day + ' ' + hours + ':' + minutes + ':' + seconds;
}
// Add metadata (Device Name, Start Time, End Time)
var deviceNames = [...new Set(data.map(ds => ds.datasource.entityName))]; // Unique device names
var startTime = formatLocalDate(timeWindow.minTime);
var endTime = formatLocalDate(timeWindow.maxTime);
csvContent += "Device(s)," + deviceNames.join("; ") + "\n";
csvContent += "Start Time," + startTime + "\n";
csvContent += "End Time," + endTime + "\n";
csvContent += "\n"; // Empty line for separation

// Create CSV headers
var headers = ["Timestamp"];
data.forEach(function(ds) {
    headers.push(ds.datasource.entityName + " - " + ds.dataKey.label);
});
csvContent += headers.join(",") + "\n";

// Process timeseries data
var rows = {};
data.forEach(function(ds) {
    ds.data.forEach(function(point) {
        var timestamp = point[0];
        var value = point[1];
        if (!rows[timestamp]) {
            rows[timestamp] = { timestamp: formatLocalDate(timestamp), values: {} };
        }
        rows[timestamp].values[ds.datasource.entityName + " - " + ds.dataKey.label] = value;
    });
});

// Convert rows to CSV
Object.keys(rows).forEach(function(timestamp) {
    var row = [rows[timestamp].timestamp];
    headers.slice(1).forEach(function(header) {
        row.push(rows[timestamp].values[header] || "");
    });
    csvContent += row.join(",") + "\n";
});

// Trigger CSV download
var encodedUri = encodeURI(csvContent);
var link = document.createElement("a");
link.setAttribute("href", encodedUri);
link.setAttribute("download", deviceNames.join("; ") + "_operation_log_" + startTime.split(" ")[0] + "_" + endTime.split(" ")[0] +  ".csv");
document.body.appendChild(link);
link.click();
document.body.removeChild(link);