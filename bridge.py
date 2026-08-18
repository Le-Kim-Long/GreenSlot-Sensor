import os
import sys
import json
import time
import random
import argparse
import requests
import serial
import serial.tools.list_ports

# ==========================================
# 1. CẤU HÌNH MẶC ĐỊNH
# ==========================================
DEFAULT_BACKEND_URL = os.getenv('BACKEND_URL', 'http://localhost:8080')
DEFAULT_DEVICE_ID = os.getenv('DEVICE_ID', 'arduino-greenhouse-01')
DEFAULT_BAUD_RATE = int(os.getenv('BAUD_RATE', '9600'))

API_KEY = 'default_iot_key_for_dev_only'
IOT_USERNAME = os.getenv('IOT_USERNAME', 'garden_staff')
IOT_PASSWORD = os.getenv('IOT_PASSWORD', 'Staff@123')

# ==========================================
# 2. XÁC THỰC JWT
# ==========================================
def get_jwt_token(base_url):
    login_url = f"{base_url.rstrip('/')}/api/auth/login"
    print(f"[AUTH] Đang gửi yêu cầu đăng nhập tới: {login_url}")
    payload = {
        "username": IOT_USERNAME,
        "password": IOT_PASSWORD
    }
    try:
        response = requests.post(login_url, json=payload, timeout=5)
        if response.status_code == 200:
            data = response.json()
            token = data.get("token") or data.get("accessToken")
            if token:
                print(f"✅ [AUTH] Đã lấy JWT Token thành công cho '{IOT_USERNAME}'!")
                return token
            else:
                print("❌ [LỖI AUTH] Phản hồi không chứa trường token/accessToken.")
                return None
        else:
            print(f"❌ [LỖI AUTH] Đăng nhập thất bại ({response.status_code}): {response.text}")
            return None
    except Exception as e:
        print(f"❌ [LỖI MẠNG] Không thể kết nối tới Backend ({login_url}): {e}")
        return None

# ==========================================
# 3. TÌM KIẾM CỔNG COM / ARDUINO / ESP32
# ==========================================
def list_available_ports():
    ports = list(serial.tools.list_ports.comports())
    return ports

def auto_detect_serial_port():
    ports = list_available_ports()
    if not ports:
        return None

    # Ưu tiên các cổng chứa từ khóa mạch nạp phổ biến
    keywords = ['arduino', 'ch340', 'cp210', 'ftdi', 'usb-serial', 'silicon labs', 'usb serial', 'ch341', 'esp32']
    for p in ports:
        desc = (p.description or '').lower()
        hwid = (p.hwid or '').lower()
        for kw in keywords:
            if kw in desc or kw in hwid:
                return p.device

    # Nếu chỉ có duy nhất 1 cổng COM
    if len(ports) == 1:
        return ports[0].device

    return None

# ==========================================
# 4. CHẾ ĐỘ MÔ PHỎNG (SIMULATOR MODE)
# ==========================================
def run_simulation_loop(base_url, device_id, token):
    sensor_url = f"{base_url.rstrip('/')}/api/iot/sensors/data"
    pump_url = f"{base_url.rstrip('/')}/api/iot/pump/status"
    headers = {
        'Content-Type': 'application/json',
        'X-IoT-Api-Key': API_KEY,
        'Authorization': f'Bearer {token}'
    }

    print("\n======================================================")
    print("      🚀 ĐANG CHẠY CHẾ ĐỘ MÔ PHỎNG CẢM BIẾN (SIMULATOR) ")
    print(f"      Thiết bị: {device_id} | Backend: {base_url}")
    print("======================================================")
    print("💡 Chế độ này tự động sinh dữ liệu đo và phản hồi lệnh Bơm")
    print("👉 Nhấn Ctrl + C để dừng chương trình.\n")

    simulated_soil = 65.0
    simulated_light = 550.0
    simulated_ph = 6.2
    simulated_pump = "OFF"
    pump_start_time = 0
    last_sensor_sent = 0
    last_pump_poll = 0

    while True:
        current_time = time.time()

        # 1. Quản lý trạng thái máy bơm mô phỏng (tự tắt sau 5s nếu đang bật)
        if simulated_pump == "ON":
            simulated_soil = min(100.0, simulated_soil + random.uniform(0.8, 1.5))
            if current_time - pump_start_time >= 5.0:
                simulated_pump = "OFF"
                print("⏱️ [MÔ PHỎNG] Bơm đã chạy 5s -> Tự động TẮT")
                try:
                    update_payload = {"deviceId": device_id, "status": "OFF"}
                    requests.post(pump_url, json=update_payload, headers=headers, timeout=2)
                except Exception as e:
                    print(f"⚠️ [LỖI MẠNG] Không thể cập nhật trạng thái tắt bơm: {e}")
        else:
            # Độ ẩm giảm dần tự nhiên
            simulated_soil = max(20.0, simulated_soil - random.uniform(0.05, 0.2))

        # 2. Đẩy dữ liệu cảm biến mỗi 3 giây
        if current_time - last_sensor_sent >= 3.0:
            simulated_light = max(100.0, min(1200.0, simulated_light + random.uniform(-25.0, 25.0)))
            simulated_ph = max(5.0, min(7.8, simulated_ph + random.uniform(-0.05, 0.05)))

            readings = [
                {"sensorType": "SOIL_MOISTURE", "value": round(simulated_soil, 1), "unit": "%"},
                {"sensorType": "LIGHT_INTENSITY", "value": round(simulated_light, 1), "unit": "Lux"},
                {"sensorType": "PH", "value": round(simulated_ph, 2), "unit": "pH"},
                {"sensorType": "TEMPERATURE", "value": round(28.0 + random.uniform(-1.0, 1.0), 1), "unit": "°C"},
                {"sensorType": "HUMIDITY", "value": round(70.0 + random.uniform(-2.0, 2.0), 1), "unit": "%"}
            ]

            payload = {"deviceId": device_id, "readings": readings}
            try:
                resp = requests.post(sensor_url, json=payload, headers=headers, timeout=3)
                if resp.status_code in (200, 201):
                    print(f"🌱 [MÔ PHỎNG] Ẩm đất: {simulated_soil:.1f}% | Ánh sáng: {simulated_light:.1f} Lux | pH: {simulated_ph:.2f} -> Đã gửi Backend ✅")
                elif resp.status_code == 401:
                    print("⚠️ [AUTH] Token hết hạn, đang lấy lại token mới...")
                    new_token = get_jwt_token(base_url)
                    if new_token:
                        token = new_token
                        headers['Authorization'] = f'Bearer {token}'
                else:
                    print(f"❌ [LỖI API] Code {resp.status_code}: {resp.text}")
            except Exception as e:
                print(f"❌ [LỖI MẠNG] Không thể gửi dữ liệu cảm biến: {e}")

            last_sensor_sent = current_time

        # 3. Thăm dò lệnh điều khiển bơm từ Web mỗi 1.5 giây
        if current_time - last_pump_poll >= 1.5:
            try:
                pump_resp = requests.get(pump_url, headers=headers, timeout=2)
                if pump_resp.status_code == 200:
                    backend_status = pump_resp.json().get("status")
                    if backend_status == "ON" and simulated_pump != "ON":
                        simulated_pump = "ON"
                        pump_start_time = current_time
                        print("👉 [LỆNH WEB] Nhận lệnh BẬT MÁY BƠM 💧")
                    elif backend_status == "OFF" and simulated_pump != "OFF":
                        simulated_pump = "OFF"
                        print("👉 [LỆNH WEB] Nhận lệnh TẮT MÁY BƠM 🛑")
            except Exception:
                pass
            last_pump_poll = current_time

        time.sleep(0.1)

# ==========================================
# 5. CHẾ ĐỘ KẾT NỐI PHẦN CỨNG THẬT (HARDWARE MODE)
# ==========================================
def run_hardware_loop(base_url, device_id, port, baud_rate, token):
    sensor_url = f"{base_url.rstrip('/')}/api/iot/sensors/data"
    pump_url = f"{base_url.rstrip('/')}/api/iot/pump/status"
    headers = {
        'Content-Type': 'application/json',
        'X-IoT-Api-Key': API_KEY,
        'Authorization': f'Bearer {token}'
    }

    print("\n======================================================")
    print("      HỆ THỐNG ĐỒNG BỘ IOT (BƠM + CẢM BIẾN THẬT)      ")
    print(f"      Cổng: {port} | Baud: {baud_rate} | Thiết bị: {device_id}")
    print("======================================================")

    while True:
        print(f"\n[INFO] Đang mở kết nối nối tiếp tới cổng {port}...")
        try:
            ser = serial.Serial(port, baud_rate, timeout=0.1)
            time.sleep(2)  # Đợi mạch nạp reset
            print(f"✅ [INFO] Đã kết nối thành công với {port}. Đang lắng nghe dữ liệu...\n")
        except serial.SerialException as e:
            print(f"❌ [LỖI SERIAL] Không thể mở cổng {port}: {e}")
            print("👉 Vui lòng kiểm tra cáp USB hoặc đóng Serial Monitor trên Arduino IDE/PlatformIO.")
            print("⏳ Thử kết nối lại sau 5 giây...")
            time.sleep(5)
            continue

        last_pump_check = 0
        last_sent_status = None

        try:
            while True:
                # 1. Đọc dữ liệu từ Serial (Arduino / ESP32)
                while ser.in_waiting > 0:
                    raw_line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if raw_line:
                        print(f"[DEBUG COM] {raw_line}")
                        try:
                            data = json.loads(raw_line)

                            # Xử lý phản hồi trạng thái bơm từ mạch
                            if 'pump_status' in data:
                                status = data['pump_status']
                                if status == 'ON':
                                    print("💧 [ARDUINO] Máy bơm đã BẬT")
                                elif status == 'OFF':
                                    print("🛑 [ARDUINO] Máy bơm đã TẮT")
                                elif status == 'TIMEOUT_OFF':
                                    print("⏱️ [ARDUINO] Hết 5s -> Tự động TẮT BƠM")
                                    last_sent_status = "OFF"
                                    try:
                                        update_payload = {"deviceId": device_id, "status": "OFF"}
                                        requests.post(pump_url, json=update_payload, headers=headers, timeout=2)
                                        print("✅ [API BƠM] Đã báo trạng thái TẮT lên Backend")
                                    except Exception as e:
                                        print(f"⚠️ [LỖI MẠNG] Không thể báo Backend tắt bơm: {e}")

                            # Xử lý dữ liệu đo cảm biến
                            readings = []
                            if 'soil' in data:
                                readings.append({"sensorType": "SOIL_MOISTURE", "value": float(data['soil']), "unit": "%"})
                            if 'light' in data:
                                readings.append({"sensorType": "LIGHT_INTENSITY", "value": float(data['light']), "unit": "Lux"})
                            if 'ph' in data:
                                readings.append({"sensorType": "PH", "value": float(data['ph']), "unit": "pH"})
                            if 'temperature' in data or 'temp' in data:
                                temp_val = data.get('temperature', data.get('temp'))
                                readings.append({"sensorType": "TEMPERATURE", "value": float(temp_val), "unit": "°C"})
                            if 'humidity' in data or 'hum' in data:
                                hum_val = data.get('humidity', data.get('hum'))
                                readings.append({"sensorType": "HUMIDITY", "value": float(hum_val), "unit": "%"})

                            if readings:
                                payload = {"deviceId": device_id, "readings": readings}
                                try:
                                    response = requests.post(sensor_url, json=payload, headers=headers, timeout=2)
                                    if response.status_code in [200, 201]:
                                        print(f"🌱 [API CẢM BIẾN] Đã đẩy {len(readings)} thông số lên Backend thành công!")
                                    elif response.status_code == 401:
                                        print("⚠️ [AUTH] Token hết hạn, đang làm mới token...")
                                        new_tok = get_jwt_token(base_url)
                                        if new_tok:
                                            token = new_tok
                                            headers['Authorization'] = f'Bearer {token}'
                                    else:
                                        print(f"❌ [LỖI API CẢM BIẾN] Code {response.status_code}: {response.text}")
                                except Exception as e:
                                    print(f"❌ [LỖI MẠNG] Không thể đẩy dữ liệu cảm biến: {e}")

                        except json.JSONDecodeError:
                            pass

                # 2. Thăm dò lệnh điều khiển bơm từ Web
                current_time = time.time()
                if current_time - last_pump_check > 1.0:
                    try:
                        pump_resp = requests.get(pump_url, headers=headers, timeout=2)
                        if pump_resp.status_code == 200:
                            backend_status = pump_resp.json().get("status")
                            if backend_status == "ON" and last_sent_status != "ON":
                                ser.write(b"ON\n")
                                ser.flush()
                                print("👉 [LỆNH WEB] Gửi lệnh ON xuống mạch")
                                last_sent_status = "ON"
                            elif backend_status == "OFF" and last_sent_status != "OFF":
                                ser.write(b"OFF\n")
                                ser.flush()
                                print("👉 [LỆNH WEB] Gửi lệnh OFF xuống mạch")
                                last_sent_status = "OFF"
                    except Exception:
                        pass
                    last_pump_check = current_time

                time.sleep(0.05)

        except (serial.SerialException, OSError) as e:
            print(f"\n⚠️ [MẤT KẾT NỐI] Cổng serial bị ngắt: {e}")
            try:
                ser.close()
            except Exception:
                pass
            print("⏳ Đang thử kết nối lại sau 3 giây...")
            time.sleep(3)

# ==========================================
# 6. HÀM MAIN
# ==========================================
def main():
    parser = argparse.ArgumentParser(description="GreenSlot IoT Sensor & Pump Gateway Bridge")
    parser.add_argument('--port', type=str, default=None, help="Cổng COM (ví dụ: COM3, COM8, /dev/ttyUSB0)")
    parser.add_argument('--baud', type=int, default=DEFAULT_BAUD_RATE, help="Tốc độ Baud (mặc định: 9600)")
    parser.add_argument('--url', type=str, default=DEFAULT_BACKEND_URL, help="URL Backend (mặc định: http://localhost:8080)")
    parser.add_argument('--device-id', type=str, default=DEFAULT_DEVICE_ID, help="Mã thiết bị (mặc định: arduino-greenhouse-01)")
    parser.add_argument('--simulate', action='store_true', help="Chạy chế độ mô phỏng khi không có phần cứng vật lý")
    args = parser.parse_args()

    # Bước 1: Lấy Token xác thực
    token = get_jwt_token(args.url)
    if not token:
        print("🛑 Dừng chương trình vì không xác thực được với Backend!")
        print("👉 Hãy chắc chắn Backend đang chạy tại:", args.url)
        sys.exit(1)

    # Bước 2: Chế độ mô phỏng nếu có cờ --simulate
    if args.simulate:
        run_simulation_loop(args.url, args.device_id, token)
        return

    # Bước 3: Tìm cổng Serial
    port = args.port
    if not port:
        port = auto_detect_serial_port()

    if not port:
        available_ports = list_available_ports()
        if available_ports:
            print("\n🔍 Tìm thấy các cổng COM sau trên máy:")
            for idx, p in enumerate(available_ports):
                print(f"   [{idx + 1}] {p.device} - {p.description}")
            try:
                choice = input("\n👉 Nhập số thứ tự cổng COM muốn kết nối (hoặc nhấn 's' để chạy Mô phỏng): ").strip()
                if choice.lower() == 's':
                    run_simulation_loop(args.url, args.device_id, token)
                    return
                idx_choice = int(choice) - 1
                if 0 <= idx_choice < len(available_ports):
                    port = available_ports[idx_choice].device
            except Exception:
                pass

    if not port:
        print("\n⚠️ Không tìm thấy mạch Arduino/ESP32 nào cắm vào máy tính!")
        print("💡 Tự động chuyển sang chế độ MÔ PHỎNG (Simulator Mode) để bạn có thể test hệ thống...")
        time.sleep(1.5)
        run_simulation_loop(args.url, args.device_id, token)
        return

    run_hardware_loop(args.url, args.device_id, port, args.baud, token)

if __name__ == '__main__':
    main()