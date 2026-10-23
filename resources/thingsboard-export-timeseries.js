// Access widget data
var data = widgetContext.data; // Timeseries data from the widget
var csvContent = "data:text/csv;charset=utf-8,";

// Create CSV header
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
            rows[timestamp] = { timestamp: new Date(timestamp).toISOString(), values: {} };
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
link.setAttribute("download", "timeseries_data.csv");
document.body.appendChild(link);
link.click();
document.body.removeChild(link);