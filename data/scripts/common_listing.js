// Common listing functions for date filtering and pagination
// This file provides reusable functions for handling lists with date filtering and pagination

// Configuration object for list management
let listConfig = {
    availableDates: new Set(),
    minDate: null,
    maxDate: null,
    allRecords: [],
    filteredRecords: [],
    displayedRecords: [],
    currentPage: 1,
    recordsPerPage: 100,
    dateField: 'startDate', // Field name for date filtering
    listType: 'generic' // Identifier for the type of list
};

// Initialize list configuration
function initializeList(config) {
    listConfig = { ...listConfig, ...config };
    listConfig.availableDates = new Set();
    listConfig.allRecords = [];
    listConfig.filteredRecords = [];
    listConfig.displayedRecords = [];
    listConfig.currentPage = 1;
}

// Extract available dates from records
function extractAvailableDates() {
    if (!listConfig.allRecords || listConfig.allRecords.length === 0) {
        return;
    }
    
    listConfig.availableDates.clear();
    listConfig.minDate = null;
    listConfig.maxDate = null;
    
    listConfig.allRecords.forEach(record => {
        const recordDate = new Date(record[listConfig.dateField]);
        const dateString = recordDate.toISOString().split('T')[0];
        listConfig.availableDates.add(dateString);
        
        if (!listConfig.minDate || recordDate < listConfig.minDate) {
            listConfig.minDate = new Date(recordDate);
        }
        if (!listConfig.maxDate || recordDate > listConfig.maxDate) {
            listConfig.maxDate = new Date(recordDate);
        }
    });
    
    // Update date input constraints
    updateDateInputConstraints();
}

// Update date input min/max attributes
function updateDateInputConstraints() {
    const iSD = el('startDate');
    const iED = el('endDate');
    
    if (listConfig.minDate && listConfig.maxDate) {
        const minDateString = listConfig.minDate.toISOString().split('T')[0];
        const maxDateString = listConfig.maxDate.toISOString().split('T')[0];
        
        if (iSD) iSD.min = minDateString;
        if (iED) iED.min = minDateString;
        if (iSD) iSD.max = maxDateString;
        if (iED) iED.max = maxDateString;
    }
}

// Validate date selection
function validateDates() {
    const iSD = el('startDate');
    const iED = el('endDate');
    const erSD = el('erSD');
    const erED = el('erED');
    const applyFilterBtn = el('applyFilterBtn');
    
    let hasError = false;
    
    // Clear previous errors
    if (erSD) erSD.style.display = 'none';
    if (erED) erED.style.display = 'none';
    if (erSD) erSD.textContent = '';
    if (erED) erED.textContent = '';
    
    const startDate = iSD ? iSD.value : '';
    const endDate = iED ? iED.value : '';
    
    if (startDate && endDate) {
        if (new Date(startDate) > new Date(endDate)) {
            if (erED) {
                erED.textContent = 'End date must be after start date';
                erED.style.display = 'block';
                hasError = true;
            }
        }
    }
    
    if (startDate && !listConfig.availableDates.has(startDate)) {
        if (erSD) {
            erSD.textContent = 'No data available for this date';
            erSD.style.display = 'block';
            hasError = true;
        }
    }
    
    if (endDate && !listConfig.availableDates.has(endDate)) {
        if (erED) {
            erED.textContent = 'No data available for this date';
            erED.style.display = 'block';
            hasError = true;
        }
    }
    
    // Disable apply button if there are errors
    if (applyFilterBtn) applyFilterBtn.disabled = hasError;
}

// Toggle filter expansion
function toggleFilter() {
    const filterContainer = el('dateFilterContainer');
    const filterToggle = el('filterToggle');
    
    if (filterContainer && filterToggle) {
        if (filterContainer.classList.contains('expanded')) {
            filterContainer.classList.remove('expanded');
            filterToggle.innerHTML = '<svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor"><path d="M10 18h4v-2h-4v2zM3 6v2h18V6H3zm3 7h12v-2H6v2z"/></svg>';
        } else {
            filterContainer.classList.add('expanded');
            filterToggle.innerHTML = '<svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor"><path d="M19 6.41L17.59 5 12 10.59 6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 12 13.41 17.59 19 19 17.59 13.41 12z"/></svg>';
        }
    }
}

// Filter records by date range with timezone handling
function filterRecordsByDate(startDate, endDate) {
    if (!startDate && !endDate) {
        listConfig.filteredRecords = [...listConfig.allRecords];
    } else {
        // Get local timezone offset in minutes
        const timezoneOffset = new Date().getTimezoneOffset();
        
        listConfig.filteredRecords = listConfig.allRecords.filter(record => {
            const recordDate = record[listConfig.dateField];
            let includeRecord = true;
            
            if (startDate) {
                // Create start date in local timezone
                const start = new Date(startDate + "T00:00:00");
                // Adjust for timezone offset to compare with UTC log dates
                const startUTC = new Date(start.getTime() - (timezoneOffset * 60 * 1000));
                if (recordDate < startUTC) {
                    includeRecord = false;
                }
            }
            
            if (endDate && includeRecord) {
                // Create end date in local timezone
                const end = new Date(endDate + "T23:59:59.999");
                // Adjust for timezone offset to compare with UTC log dates
                const endUTC = new Date(end.getTime() - (timezoneOffset * 60 * 1000));
                if (recordDate > endUTC) {
                    includeRecord = false;
                }
            }
            
            return includeRecord;
        });
    }
    
    // Reset to first page
    listConfig.currentPage = 1;
    
    // Update UI
    updateFilterDisplay();
    displayCurrentPage();
}

// Update filter display
function updateFilterDisplay() {
    const filterDateRange = el('filterDateRange');
    const filterCount = el('filterCount');
    const filterTotalEnergy = el('filterTotalEnergy');
    const filterTotalRuntime = el('filterTotalRuntime');
    
    if (filterCount) {
        filterCount.textContent = 'Total logs: ' + listConfig.filteredRecords.length;
    }
    
    if (filterTotalEnergy && listConfig.listType === 'energy') {
        const totalEnergy = listConfig.filteredRecords.reduce((sum, record) => sum + (record.energyValue || 0), 0);
        filterTotalEnergy.textContent = 'Total Energy: ' + totalEnergy.toFixed(3)+ ' kWh';
    }
    
    if (filterTotalRuntime && listConfig.listType === 'energy') {
        const totalRuntimeSeconds = listConfig.filteredRecords.reduce((sum, record) => {
            if (record.endDate && record.endDate.getFullYear() >= 2000) {
                return sum + Math.round((record.endDate - record.startDate) / 1000);
            }
            return sum;
        }, 0);
        const totalHours = Math.floor(totalRuntimeSeconds / 3600);
        const totalMinutes = Math.floor((totalRuntimeSeconds % 3600) / 60);
        filterTotalRuntime.textContent = 'Total Runtime: ' + totalHours +'h ' + totalMinutes +'m';
    }
    
    if (filterDateRange) {
        const startDate = el('startDate') ? el('startDate').value : '';
        const endDate = el('endDate') ? el('endDate').value : '';
        
        if (startDate && endDate) {
            filterDateRange.textContent = startDate + ' to ' + endDate;
        } else if (startDate) {
            filterDateRange.textContent = 'From ' + startDate;
        } else if (endDate) {
            filterDateRange.textContent = 'Until ' + endDate;
        } else {
            filterDateRange.textContent = 'All logs';
        }
    }
}

// Display current page of records
function displayCurrentPage() {
    if (!listConfig.filteredRecords || listConfig.filteredRecords.length === 0) {
        const logContentDiv = _qs ? _qs(".log-content") : document.querySelector('.log-content');
        if (logContentDiv) {
            logContentDiv.innerHTML = '<p style="text-align: center; color: #6c757d; padding: 20px;">No records found for the selected date range.</p>';
        }
        const pagination = el('paginationContainer');
        if (pagination) pagination.style.display = 'none';
        return;
    }
    
    const totalPages = Math.max(1, Math.ceil(listConfig.filteredRecords.length / listConfig.recordsPerPage));
    if (listConfig.currentPage > totalPages) listConfig.currentPage = totalPages;
    if (listConfig.currentPage < 1) listConfig.currentPage = 1;
    
    const startIndex = (listConfig.currentPage - 1) * listConfig.recordsPerPage;
    const endIndex = startIndex + listConfig.recordsPerPage;
    listConfig.displayedRecords = listConfig.filteredRecords.slice(startIndex, endIndex);
    
    populateRecords(listConfig.displayedRecords);
    updatePagination();
}

// Pagination helpers
function updatePagination() {
    const pagination = el('paginationContainer');
    const prevBtn = el('prevPageBtn');
    const nextBtn = el('nextPageBtn');
    const pageInfo = el('pageInfo');
    
    if (!pagination || !prevBtn || !nextBtn || !pageInfo) return;
    
    const totalPages = Math.max(1, Math.ceil(listConfig.filteredRecords.length / listConfig.recordsPerPage));
    pageInfo.textContent = `Page ${listConfig.currentPage} of ${totalPages}`;
    
    prevBtn.disabled = listConfig.currentPage <= 1;
    nextBtn.disabled = listConfig.currentPage >= totalPages;
    
    pagination.style.display = totalPages > 1 ? 'flex' : 'none';
}

function goToPreviousPage() {
    if (listConfig.currentPage > 1) {
        listConfig.currentPage -= 1;
        displayCurrentPage();
    }
}

function goToNextPage() {
    const totalPages = Math.max(1, Math.ceil(listConfig.filteredRecords.length / listConfig.recordsPerPage));
    if (listConfig.currentPage < totalPages) {
        listConfig.currentPage += 1;
        displayCurrentPage();
    }
}

// Apply date filter
function applyDateFilter() {
    const startDate = el('startDate') ? el('startDate').value : '';
    const endDate = el('endDate') ? el('endDate').value : '';
    const logLimitSelect = el('logLimit');
    const newRecordsPerPage = logLimitSelect ? parseInt(logLimitSelect.value) : listConfig.recordsPerPage;
    
    // Update records per page if changed
    if (newRecordsPerPage !== listConfig.recordsPerPage) {
        listConfig.recordsPerPage = newRecordsPerPage;
    }
    
    filterRecordsByDate(startDate, endDate);
    toggleFilter();
}

// Clear date filter
function clearDateFilter() {
    const iSD = el('startDate');
    const iED = el('endDate');
    const erSD = el('erSD');
    const erED = el('erED');
    const applyFilterBtn = el('applyFilterBtn');
    
    if (iSD) iSD.value = '';
    if (iED) iED.value = '';
    if (erSD) erSD.style.display = 'none';
    if (erED) erED.style.display = 'none';
    if (applyFilterBtn) applyFilterBtn.disabled = false;
    
    filterRecordsByDate('', ''); // Apply filter to show all records
}

// Refresh records (to be implemented by specific list type)
function refreshRecords() {
    const loadingIndicator = el('loading-indicator');
    const logContent = document.querySelector('.log-content');
    
    // Show loading indicator
    if (loadingIndicator) loadingIndicator.style.display = 'block';
    if (logContent) logContent.innerHTML = '';
    
    // Reset global variables
    listConfig.allRecords = [];
    listConfig.filteredRecords = [];
    listConfig.displayedRecords = [];
    listConfig.currentPage = 1;
    
    // Hide pagination during refresh
    const pagination = el('paginationContainer');
    if (pagination) pagination.style.display = 'none';
    
    // Call specific refresh function based on list type
    if (typeof window[`refresh${listConfig.listType.charAt(0).toUpperCase() + listConfig.listType.slice(1)}`] === 'function') {
        window[`refresh${listConfig.listType.charAt(0).toUpperCase() + listConfig.listType.slice(1)}`]();
    }
}

// Generic populate function (to be overridden by specific implementations)
function populateRecords(data) {
    console.log('populateRecords called with', data.length, 'records');
    // This should be overridden by specific list implementations
}

// Export functions for global use
window.CommonListing = {
    listConfig,
    initializeList,
    extractAvailableDates,
    updateDateInputConstraints,
    validateDates,
    toggleFilter,
    filterRecordsByDate,
    updateFilterDisplay,
    displayCurrentPage,
    updatePagination,
    goToPreviousPage,
    goToNextPage,
    applyDateFilter,
    clearDateFilter,
    refreshRecords,
    populateRecords
};
