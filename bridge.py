import os
import sys
import json
import time
import requests
import serial
import serial.tools.list_ports

# ==============================================================================
# CONFIGURATION: ENVIRONMENT VARIABLES OR MANUAL CONFIG
# ==============================================================================
# IF RUNNING ON A REMOTE LAPTOP/PI, REPLACE 'localhost' WITH THE SERVER IPv4/DOMAIN
BACKEND_URL = os.getenv('BACKEND_URL', 'http://localhost:8080/api/iot/device/data')
API_KEY = os.getenv('IOT_API_KEY', 'default_iot_key_for_dev_only')
DEVICE_ID = os.getenv('DEVICE_ID', 'arduino-greenhouse-01')
BAUD_RATE = int(os.getenv('BAUD_RATE', 9600))

HEADERS = {
    'Content-Type': 'application/json',
    'X-IoT-Api-Key': API_KEY
}
# ==============================================================================


def find_arduino_port():
    """Scan available serial ports for common Arduino hardware identifiers."""
    identifiers = ['arduino', 'ch340', 'cp210', 'uart', 'ftdi', 'usb serial']
    ports = list(serial.tools.list_ports.comports())

    if not ports:
        return None

    # Priority 1: Match common Arduino/USB-Serial identifiers in description or hwid
    for port in ports:
        desc_lower = (port.description or '').lower()
        hwid_lower = (port.hwid or '').lower()
        if any(ident in desc_lower or ident in hwid_lower for ident in identifiers):
            print(f"[AUTO-DETECT] Found likely Arduino device on {port.device} ({port.description})")
            return port.device

    # Priority 2: Fallback to the first active USB COM port if available
    for port in ports:
        if 'usb' in (port.description or '').lower() or 'usb' in (port.hwid or '').lower():
            print(f"[AUTO-DETECT] Fallback to USB COM port {port.device} ({port.description})")
            return port.device

    return None


def create_serial_connection(port, baud_rate):
    """Attempt to connect to the specified serial port."""
    try:
        connection = serial.Serial(port, baud_rate, timeout=1)
        print(f"[INFO] Successfully connected to {port} at {baud_rate} baud.")
        return connection
    except serial.SerialException as e:
        print(f"[ERROR] Serial connection failed on {port}: {e}")
        return None


def forward_to_backend(payload):
    """POST transformed telemetry payload to Spring Boot backend."""
    try:
        response = requests.post(
            BACKEND_URL,
            json=payload,
            headers=HEADERS,
            timeout=5.0
        )
        if response.status_code == 200:
            print(f"[SUCCESS] POST {BACKEND_URL} -> Status: {response.status_code} | Response: {response.text}")
        else:
            print(f"[WARNING] Backend returned status {response.status_code}: {response.text}")
    except requests.exceptions.Timeout:
        print(f"[ERROR] Request timeout when connecting to {BACKEND_URL}")
    except requests.exceptions.ConnectionError:
        print(f"[ERROR] Connection failed. Is Spring Boot reachable at {BACKEND_URL}?")
    except requests.exceptions.RequestException as e:
        print(f"[ERROR] HTTP request error: {e}")


def main():
    print("======================================================================")
    print("            GreenSlot Plug-and-Play IoT Bridge Gateway                ")
    print("======================================================================")
    print(f"[CONFIG] Backend URL : {BACKEND_URL}")
    print(f"[CONFIG] Device ID   : {DEVICE_ID}")
    print(f"[CONFIG] Baud Rate   : {BAUD_RATE}")
    print("----------------------------------------------------------------------")

    active_port = find_arduino_port()
    if not active_port:
        print("[ERROR] No Arduino or USB serial device detected!")
        print("[ACTION REQUIRED] Please plug your Arduino board into a USB port and restart.")
        sys.exit(1)

    arduino = None

    while True:
        try:
            if arduino is None or not arduino.is_open:
                if not active_port:
                    active_port = find_arduino_port()
                if not active_port:
                    print("[RETRY] Waiting for Arduino device connection...")
                    time.sleep(5)
                    continue

                arduino = create_serial_connection(active_port, BAUD_RATE)
                if arduino is None:
                    print(f"[RETRY] Retrying connection to {active_port} in 5 seconds...")
                    time.sleep(5)
                    active_port = find_arduino_port()
                    continue

            if arduino.in_waiting > 0:
                raw_line = arduino.readline().decode('utf-8', errors='ignore').strip()
                if not raw_line:
                    continue

                try:
                    sensor_data = json.loads(raw_line)
                except json.JSONDecodeError:
                    print(f"[DEBUG] Ignored non-JSON string: {raw_line}")
                    continue

                if 'moisture' in sensor_data:
                    payload = {
                        "device_id": DEVICE_ID,
                        "sensor_type": "SOIL_MOISTURE",
                        "value": float(sensor_data['moisture']),
                        "unit": "%"
                    }
                    print(f"[TELEMETRY] Moisture: {sensor_data['moisture']}% -> Forwarding to backend...")
                    forward_to_backend(payload)

                if 'light' in sensor_data:
                    payload = {
                        "device_id": DEVICE_ID,
                        "sensor_type": "LIGHT_INTENSITY",
                        "value": float(sensor_data['light']),
                        "unit": "Lux"
                    }
                    print(f"[TELEMETRY] Light: {sensor_data['light']} Lux -> Forwarding to backend...")
                    forward_to_backend(payload)

        except (serial.SerialException, OSError) as e:
            print(f"[ERROR] Serial disconnection on {active_port}: {e}")
            if arduino and arduino.is_open:
                arduino.close()
            arduino = None
            active_port = None
            time.sleep(3)
        except KeyboardInterrupt:
            print("\n[INFO] Bridge script terminated cleanly by user.")
            if arduino and arduino.is_open:
                arduino.close()
            break
        except Exception as e:
            print(f"[ERROR] Unexpected error: {e}")
            time.sleep(1)


if __name__ == '__main__':
    main()