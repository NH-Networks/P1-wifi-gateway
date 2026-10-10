#!/bin/bash
#
# Script to fetch solar power data, weather, grid voltage and upload to PVOutput.org
# Optimized for minimal log file entries (one summary line per run).
#
# Optional strict mode for more robust error handling:
# set -euo pipefail

# --- Configuration ---
# PVOutput.org Settings
PVOUTPUT_SID="90883"
PVOUTPUT_KEY="c41722e303203bd9fa59b5f71e1d9e31bbebd15b"
PVOUTPUT_API_URL="http://pvoutput.org/service/r2/addstatus.jsp"

# ESPHome Device
ESPHOME_IP="10.10.32.2"
ESPHOME_ACTIVE_POWER_URL="http://${ESPHOME_IP}/sensor/Active%20Power"
ESPHOME_ENERGY_TODAY_URL="http://${ESPHOME_IP}/sensor/Energy%20today"

# Weather Settings
WEATHER_LOCATION_QUERY="Almelo"
WEERLIVE_API_KEY="ca4bbf5213"
WEERLIVE_API_URL="https://weerlive.nl/api/json-data-10min.php?key=${WEERLIVE_API_KEY}&locatie=${WEATHER_LOCATION_QUERY}"
BUIENRADAR_API_URL="https://data.buienradar.nl/2.0/feed/json"
BUIENRADAR_STATION_ID="6290"
DEFAULT_TEMP="99"

# P1 Meter for Grid Voltage
P1_METER_IP="10.10.33.1"
P1_METER_JSON_URL="http://${P1_METER_IP}/P1.json"
DEFAULT_GRID_VOLTAGE="1"

# Script Behavior
LOG_RETENTION_DAYS=1
NIGHT_QUIET_START="23:30"
NIGHT_QUIET_END="04:30"

# WirePusher Notifications
WIREPUSHER_ID="C4XkmpsZT"
WIREPUSHER_API_URL="https://wirepusher.com/send"

# --- Script Setup ---
SCRIPT_START_TIME=$(date +%s)
SCRIPT_FULL_PATH=$(realpath "$0")
SCRIPT_DIR=$(dirname "${SCRIPT_FULL_PATH}")

LOG_DIR="${SCRIPT_DIR}/logs"
ZERO_POWER_LOGGED_FILE="${SCRIPT_DIR}/.zeropower_logged_today"

mkdir -p "${LOG_DIR}"

TODAY_YMD=$(date '+%Y%m%d')
CURRENT_HM=$(date '+%H:%M') # Time for operational window check primarily
TODAY_LOG_FILE="${LOG_DIR}/${TODAY_YMD}-em3080topvoutput.log"

# Global variables for fetched data
ACTIVE_POWER=""
ENERGY_TODAY=""
TEMPERATURE="${DEFAULT_TEMP}"
GRID_VOLTAGE="${DEFAULT_GRID_VOLTAGE}"

# Global variable for the main log summary message
MAIN_LOG_SUMMARY=""
# Global variable to hold details from PVOutput submission for summary
PVOUTPUT_SUBMISSION_DETAILS=""


# --- Helper Functions ---

echo_console() {
    # Outputs message to console only, prefixed with a timestamp.
    # Does NOT write to the log file.
    local message="$1"
    echo "[$(date '+%H:%M:%S')] $message"
}

cleanup_and_exit() {
    # This function is the primary way the script should exit.
    # It logs ONE summary line to the log file and then exits.
    local exit_code=${1:-0}
    local script_end_time
    script_end_time=$(date +%s)
    local duration=$((script_end_time - SCRIPT_START_TIME))
    local log_time_for_entry
    log_time_for_entry=$(date '+%H:%M:%S') # Current time for the log entry itself

    if [ -z "${MAIN_LOG_SUMMARY}" ]; then
        if [ "$exit_code" -eq 0 ]; then
            MAIN_LOG_SUMMARY="Completed successfully (no specific action summary)."
        else
            MAIN_LOG_SUMMARY="Exited due to an issue (no specific summary provided)."
        fi
    fi
    
    local final_log_entry="${TODAY_YMD} ${log_time_for_entry} ${MAIN_LOG_SUMMARY} Runtime: ${duration}s. Exit: ${exit_code}."
    
    echo_console "Final Summary for Log: ${final_log_entry}" # Echo to console for clarity
    echo "${final_log_entry}" >> "${TODAY_LOG_FILE}"
    
    # shellcheck disable=SC2086
    exit $exit_code
}

send_notification() {
    local title="$1"
    local message="$2"
    local type="${3:-pverror}"
    echo_console "NOTIFICATION: Title: $title, Message: $message" # Console only
    # Actual curl call for notification
    curl -s -k --connect-timeout 10 \
        "${WIREPUSHER_API_URL}?id=${WIREPUSHER_ID}&title=${title// /%20}&message=${message// /%20}&type=${type}&message_id=pvoutput_$(date +%s)" \
        -o /dev/null
    # Optionally, append to MAIN_LOG_SUMMARY that a notification was sent
    MAIN_LOG_SUMMARY="${MAIN_LOG_SUMMARY} (Notification Sent: ${title})"
}

is_numeric() {
    [[ "$1" =~ ^-?[0-9]+(\.[0-9]+)?$ ]]
}

# --- Core Logic Functions ---

check_operational_window() {
    echo_console "Checking operational window (${NIGHT_QUIET_START} - ${NIGHT_QUIET_END}). Current: ${CURRENT_HM}."
    if [[ "${CURRENT_HM}" > "${NIGHT_QUIET_START}" || "${CURRENT_HM}" < "${NIGHT_QUIET_END}" ]]; then
        MAIN_LOG_SUMMARY="SKIPPED: Quiet Hours (Current: ${CURRENT_HM}, Quiet: ${NIGHT_QUIET_START}-${NIGHT_QUIET_END})."
        cleanup_and_exit 1
    fi
    echo_console "Operational window check passed."
}

check_esphome_online() {
    echo_console "Pinging ESPHome device at ${ESPHOME_IP}..."
    if ! ping -c 1 -W 5 "${ESPHOME_IP}" >/dev/null 2>&1; then
        MAIN_LOG_SUMMARY="ERROR: ESPHome Offline (IP: ${ESPHOME_IP})."
        send_notification "ESPHome Offline" "ESPHome device at ${ESPHOME_IP} is not responding."
        cleanup_and_exit 1
    fi
    echo_console "ESPHome device ${ESPHOME_IP} is ONLINE."
}

fetch_esphome_data() {
    echo_console "Fetching data from ESPHome device..."
    local success_flag=0 # 0 for success, 1 for any failure
    local curl_opts=(-H 'Cache-Control: no-cache, no-store' -s -k --connect-timeout 10 --retry 2 --retry-connrefused --retry-delay 2 --max-time 30)
    local ap_json et_json

    ap_json=$(curl "${curl_opts[@]}" "${ESPHOME_ACTIVE_POWER_URL}")
    if [[ $? -ne 0 || -z "$ap_json" ]]; then
        echo_console "ERROR: Failed to retrieve active_power from ESPHome."
        success_flag=1
    else
        ACTIVE_POWER=$(echo "$ap_json" | jq -r '.value')
        if ! is_numeric "${ACTIVE_POWER}"; then
            echo_console "ERROR: Invalid active_power value: '${ACTIVE_POWER}'"
            ACTIVE_POWER="" success_flag=1
        else
            echo_console "Active Power: ${ACTIVE_POWER} W"
        fi
    fi

    et_json=$(curl "${curl_opts[@]}" "${ESPHOME_ENERGY_TODAY_URL}")
    if [[ $? -ne 0 || -z "$et_json" ]]; then
        echo_console "ERROR: Failed to retrieve energy_today from ESPHome."
        success_flag=1
    else
        ENERGY_TODAY=$(echo "$et_json" | jq -r '.value')
        if ! is_numeric "${ENERGY_TODAY}"; then
            echo_console "ERROR: Invalid energy_today value: '${ENERGY_TODAY}'"
            ENERGY_TODAY="" success_flag=1
        else
            echo_console "Energy Today: ${ENERGY_TODAY} kWh"
        fi
    fi
    
    return $success_flag # 0 if both likely okay, 1 if any issue
}

fetch_weather_data() {
    echo_console "Fetching weather data..."
    local temp_val="" jsondata

    jsondata=$(curl -s -k --connect-timeout 10 "${WEERLIVE_API_URL}")
    if [[ $? -eq 0 && -n "$jsondata" ]]; then
        temp_val=$(echo "$jsondata" | jq -r '.liveweer[0] .temp')
        if is_numeric "$temp_val"; then
            echo_console "Temperature from Weerlive.nl: ${temp_val}°C"
            TEMPERATURE="$temp_val"
            return 0
        fi
        echo_console "WARN: Weerlive.nl invalid temp: '$temp_val'."
    else echo_console "WARN: Failed Weerlive.nl fetch."; fi

    jsondata=$(curl -s -k --connect-timeout 10 "${BUIENRADAR_API_URL}")
    if [[ $? -eq 0 && -n "$jsondata" ]]; then
        temp_val=$(echo "$jsondata" | jq -r ".actual.stationmeasurements[] | select(.stationid == ${BUIENRADAR_STATION_ID}) | .temperature")
        if is_numeric "$temp_val"; then
            echo_console "Temperature from Buienradar.nl: ${temp_val}°C"
            TEMPERATURE="$temp_val"
            return 0
        fi
        echo_console "WARN: Buienradar.nl invalid temp: '$temp_val'."
    else echo_console "WARN: Failed Buienradar.nl fetch."; fi
    
    echo_console "Using default temperature: ${DEFAULT_TEMP}°C"
    TEMPERATURE="${DEFAULT_TEMP}"
    return 1
}

fetch_grid_voltage() {
    echo_console "Fetching grid voltage from P1 meter (${P1_METER_JSON_URL})..."
    local p1_json voltage_val
    p1_json=$(curl -H 'Cache-Control: no-cache, no-store' -s -k --connect-timeout 10 --retry 1 --max-time 20 "${P1_METER_JSON_URL}")
    
    if [[ $? -eq 0 && -n "$p1_json" ]]; then
        # Parse voltage from P1 JSON (supports jq, with fallback to grep regex)
        if command -v jq >/dev/null 2>&1; then
            voltage_val=$(echo "$p1_json" | jq -r 'if (.P1.V.L1 // 0) > 0 then .P1.V.L1 elif (.P1.V.L2 // 0) > 0 then .P1.V.L2 elif (.P1.V.L3 // 0) > 0 then .P1.V.L3 else empty end' 2>/dev/null)
        fi

        # Fallback if jq is not present or failed to parse
        if [[ -z "$voltage_val" || ! "$voltage_val" =~ ^-?[0-9]+(\.[0-9]+)?$ ]]; then
            voltage_val=$(echo "$p1_json" | grep -oP '"L1"\s*:\s*\K[0-9]+(\.[0-9]+)?' | head -1)
        fi

        if is_numeric "$voltage_val"; then
            echo_console "P1 Grid Voltage: ${voltage_val} V"
            GRID_VOLTAGE="$voltage_val"
            return 0
        fi
        echo_console "WARN: Could not parse P1 voltage from JSON: '$voltage_val'."
    else
        echo_console "WARN: Failed P1 meter data retrieval."
    fi

    echo_console "Using default grid voltage: ${DEFAULT_GRID_VOLTAGE} V"
    GRID_VOLTAGE="${DEFAULT_GRID_VOLTAGE}"
    return 1
}

manage_old_logs() {
    echo_console "Checking for old logs (older than ${LOG_RETENTION_DAYS} days)..."
    # Use a subshell for find to prevent issues if no files match and set -e is active
    (find "${LOG_DIR}/" -type f -name "*-em3080topvoutput.log" -daystart -mtime "+${LOG_RETENTION_DAYS}" -print -delete)
    echo_console "Old log cleanup finished."
}

submit_to_pvoutput() {
    # This function attempts to submit data.
    # It sets PVOUTPUT_SUBMISSION_DETAILS for the main log summary.
    # Returns 0 for PVOutput OK, 1 for any failure.

    if ! is_numeric "${ACTIVE_POWER}" || ! is_numeric "${ENERGY_TODAY}"; then
        PVOUTPUT_SUBMISSION_DETAILS="SubmitERR: Invalid base data (AP: '${ACTIVE_POWER}', ET: '${ENERGY_TODAY}')"
        return 1
    fi
    
    local energy_wh
    energy_wh=$(echo "${ENERGY_TODAY} * 1000" | bc | cut -d. -f1)
    if ! is_numeric "$energy_wh"; then
        PVOUTPUT_SUBMISSION_DETAILS="SubmitERR: bc conversion failed for EnergyToday='${ENERGY_TODAY}'"
        energy_wh=0 # Fallback
    fi

    # Ensure aux data is numeric, use defaults if not (though fetch functions should handle this)
    is_numeric "${TEMPERATURE}" || TEMPERATURE=${DEFAULT_TEMP}
    is_numeric "${GRID_VOLTAGE}" || GRID_VOLTAGE=${DEFAULT_GRID_VOLTAGE}

    local current_submit_time # Use current time for PVOutput 't' parameter
    current_submit_time=$(date '+%H:%M')
    local payload_data="d=${TODAY_YMD}&t=${current_submit_time}&v1=${energy_wh}&v2=${ACTIVE_POWER}&v5=${TEMPERATURE}&v6=${GRID_VOLTAGE}"
    local full_payload_url="${PVOUTPUT_API_URL}?sid=${PVOUTPUT_SID}&key=${PVOUTPUT_KEY}&${payload_data}"
    local data_summary_for_log="v1=${energy_wh}Wh v2=${ACTIVE_POWER}W v5=${TEMPERATURE}C v6=${GRID_VOLTAGE}V"

    echo_console "Attempting PVOutput POST: ${data_summary_for_log}"
    
    local response curl_exit_code
    response=$(curl -s -k --connect-timeout 15 --max-time 30 "${full_payload_url}")
    curl_exit_code=$?

    if [[ $curl_exit_code -eq 0 ]]; then
        if [[ "$response" == OK* ]]; then
            PVOUTPUT_SUBMISSION_DETAILS="POST OK: ${data_summary_for_log}. PVResp: ${response}"
            echo_console "PVOutput Success: ${response}"
            return 0
        else
            PVOUTPUT_SUBMISSION_DETAILS="POST FAIL: ${data_summary_for_log}. PVResp: ${response}"
            echo_console "ERROR: PVOutput did not return OK. Response: ${response}"
            return 1
        fi
    else
        PVOUTPUT_SUBMISSION_DETAILS="POST FAIL: ${data_summary_for_log}. CurlERR: ${curl_exit_code}"
        echo_console "ERROR: curl command failed to post to PVOutput. Exit code: ${curl_exit_code}"
        return 1
    fi
}

# --- Main Script Logic ---

echo_console "--- Script execution started ---"

check_operational_window
manage_old_logs
check_esphome_online

if ! fetch_esphome_data; then # This means a fetch attempt had some issue
    # Check if CRITICAL data is actually missing/invalid
    if ! is_numeric "${ACTIVE_POWER}" || ! is_numeric "${ENERGY_TODAY}"; then
        MAIN_LOG_SUMMARY="ERROR: ESPHome Data CRITICAL (AP: '${ACTIVE_POWER}', ET: '${ENERGY_TODAY}')."
        send_notification "ESPHome PVOutput Error" "Critical data from ESPHome empty/invalid: AP='${ACTIVE_POWER}', ET='${ENERGY_TODAY}'"
        cleanup_and_exit 1
    else
        echo_console "WARN: ESPHome data fetch had minor issues, but critical values seem present. Continuing."
        # MAIN_LOG_SUMMARY could note this if desired, e.g. append "(ESPHome fetch issues)"
    fi
fi

is_active_power_zero=0
# Ensure ACTIVE_POWER is valid before bc comparison
if is_numeric "${ACTIVE_POWER}" && (( $(echo "${ACTIVE_POWER} == 0" | bc -l) )); then
    is_active_power_zero=1
fi

if [[ $is_active_power_zero -eq 1 ]]; then
    echo_console "Active power is zero."
    if [[ -f "${ZERO_POWER_LOGGED_FILE}" ]]; then
        MAIN_LOG_SUMMARY="SKIPPED: Zero Power (already logged ${ZERO_POWER_LOGGED_FILE} exists)."
        cleanup_and_exit 0
    else
        echo_console "First zero power reading for logging. Fetching aux data..."
        fetch_weather_data
        fetch_grid_voltage
        if submit_to_pvoutput; then
            MAIN_LOG_SUMMARY="PVOutput ${PVOUTPUT_SUBMISSION_DETAILS} (First Zero Logged)."
            touch "${ZERO_POWER_LOGGED_FILE}"
        else
            MAIN_LOG_SUMMARY="PVOutput ${PVOUTPUT_SUBMISSION_DETAILS} (First Zero Log - FAILED)."
            # Consider not creating zerocheck file so it retries the "first zero" post
        fi
        cleanup_and_exit 0 # Exit code 0 because script attempted its intended operation
    fi
else # Active power is NOT zero
    echo_console "Active power is ${ACTIVE_POWER} W (non-zero)."
    if [[ -f "${ZERO_POWER_LOGGED_FILE}" ]]; then
        echo_console "Power generation resumed. Removing ${ZERO_POWER_LOGGED_FILE}."
        rm -f "${ZERO_POWER_LOGGED_FILE}"
    fi
    fetch_weather_data
    fetch_grid_voltage
    if submit_to_pvoutput; then
         MAIN_LOG_SUMMARY="PVOutput ${PVOUTPUT_SUBMISSION_DETAILS}."
    else
         MAIN_LOG_SUMMARY="PVOutput ${PVOUTPUT_SUBMISSION_DETAILS} (Submission FAILED)."
    fi
    cleanup_and_exit 0 # Exit code 0 if post attempted, 1 if submit_to_pvoutput itself decides to cause non-zero exit
fi

# Fallback exit - should not be reached if logic above is complete
MAIN_LOG_SUMMARY="ERROR: Reached unexpected end of script."
cleanup_and_exit 1