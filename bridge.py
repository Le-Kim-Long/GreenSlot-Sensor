import os
import sys
import json
import time
import requests
import serial
import serial.tools.list_ports

# ==========================================
# 1. CẤU HÌNH API VÀ THIẾT BỊ
# ==========================================
BACKEND_LOGIN_URL = 'http://localhost:8080/api/auth/login' 
BACKEND_SENSOR_URL = 'http://localhost:8080/api/iot/sensors/data'
BACKEND_PUMP_URL = 'http://localhost:8080/api/iot/pump/status' 

API_KEY = 'default_iot_key_for_dev_only' 
DEVICE_ID = 'arduino-greenhouse-01'
BAUD_RATE = 9600

# TÀI KHOẢN ĐĂNG NHẬP
IOT_USERNAME = 'garden_staff' 
IOT_PASSWORD = 'Staff@123'

HEADERS = {
    'Content-Type': 'application/json',
    'X-IoT-Api-Key': API_KEY,
    'Authorization': '' 
}

# ==========================================
# 2. CÁC HÀM HỖ TRỢ
# ==========================================
def get_jwt_token():
    print(f"[AUTH] Đang gửi yêu cầu đăng nhập lên hệ thống...")
    payload = {
        "username": IOT_USERNAME,
        "password": IOT_PASSWORD
    }
    try:
        response = requests.post(BACKEND_LOGIN_URL, json=payload, timeout=5)
        if response.status_code == 200:
            data = response.json()
            token = data.get("token") or data.get("accessToken")
            if token:
                print("✅ [AUTH] Đã lấy JWT Token tự động thành công!")
                return token
            else:
                print("❌ [LỖI AUTH] Không tìm thấy trường token trong phản hồi.")
                return None
        else:
            print(f"❌ [LỖI AUTH] Đăng nhập thất bại: {response.status_code} - {response.text}")
            return None
    except Exception as e:
        print(f"❌ [LỖI MẠNG] Không thể kết nối API Đăng nhập: {e}")
        return None

def find_arduino():
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        if 'usb' in (p.description or '').lower() or 'arduino' in (p.description or '').lower():
            return p.device
    return None

# ==========================================
# 3. CHƯƠNG TRÌNH CHÍNH (MAIN LOOP)
# ==========================================
def main():
    print("======================================================")
    print("      HỆ THỐNG ĐỒNG BỘ IOT (BƠM + CẢM BIẾN)           ")
    print("======================================================")
    
    # BƯỚC 1: LẤY TOKEN TRƯỚC KHI LÀM VIỆC
    global HEADERS
    token = get_jwt_token()
    if not token:
        print("🛑 Dừng chương trình vì không lấy được Token xác thực!")
        sys.exit(1)
        
    HEADERS['Authorization'] = f'Bearer {token}'
    
    # BƯỚC 2: KẾT NỐI ARDUINO
    port = find_arduino()
    if not port:
        print("[LỖI] Không tìm thấy Arduino. Hãy cắm cáp USB vào mạch Master!")
        return

    print(f"[INFO] Đang kết nối Master Arduino ở cổng {port}...")
    try:
        arduino = serial.Serial(port, BAUD_RATE, timeout=0.1)
        time.sleep(2) # Chờ Arduino reset
        print(f"[INFO] Đã kết nối thành công. Đang lắng nghe dữ liệu...\n")
    except serial.SerialException:
        print("[LỖI] Cổng COM đang bị chiếm! Hãy tắt Serial Monitor trên PlatformIO.")
        return

    last_pump_check = 0
    last_sent_status = None 

    while True:
        # ----------------------------------------------------
        # LUỒNG 1: ĐỌC DỮ LIỆU TỪ ARDUINO (CẢM BIẾN VÀ BƠM)
        # ----------------------------------------------------
        while arduino.in_waiting > 0:
            raw_line = arduino.readline().decode('utf-8', errors='ignore').strip()
            if raw_line:
                print(f"[DEBUG COM] {raw_line}") 
                try:
                    data = json.loads(raw_line)
                    
                    # 1A. XỬ LÝ TRẠNG THÁI BƠM
                    if 'pump_status' in data:
                        status = data['pump_status']
                        if status == 'ON':
                            print("💧 [ARDUINO] Máy bơm đã được BẬT")
                        elif status == 'OFF':
                            print("🛑 [ARDUINO] Máy bơm đã TẮT")
                        elif status == 'TIMEOUT_OFF':
                            print("⏱️ [ARDUINO] Hết 5 giây -> Tự động TẮT BƠM")
                            last_sent_status = "OFF"
                            
                            # Báo cho Backend biết bơm đã tắt do timeout
                            try:
                                update_payload = {"deviceId": DEVICE_ID, "status": "OFF"}
                                resp = requests.post(BACKEND_PUMP_URL, json=update_payload, headers=HEADERS, timeout=2)
                                if resp.status_code in (200, 201):
                                    print("✅ [API BƠM] Đã cập nhật trạng thái TẮT lên Backend")
                            except Exception as e:
                                print(f"⚠️ [LỖI MẠNG] Không thể báo Backend tắt bơm: {e}")

                    # 1B. XỬ LÝ DỮ LIỆU CẢM BIẾN (GOM VÀO MẢNG READINGS)
                    if 'soil' in data or 'light' in data or 'ph' in data:
                        readings = []
                        if 'soil' in data:
                            readings.append({"sensorType": "SOIL_MOISTURE", "value": float(data['soil']), "unit": "%"})
                        if 'light' in data:
                            readings.append({"sensorType": "LIGHT_INTENSITY", "value": float(data['light']), "unit": "Lux"})
                        if 'ph' in data:
                            readings.append({"sensorType": "PH", "value": float(data['ph']), "unit": "pH"})

                        if len(readings) > 0:
                            payload = {"deviceId": DEVICE_ID, "readings": readings}
                            try:
                                response = requests.post(BACKEND_SENSOR_URL, json=payload, headers=HEADERS, timeout=2)
                                if response.status_code in [200, 201]:
                                    print(f"🌱 [API CẢM BIẾN] Đã đẩy {len(readings)} thông số lên Backend thành công!")
                                else:
                                    print(f"❌ [LỖI API CẢM BIẾN] Code {response.status_code}: {response.text}")
                            except Exception as e:
                                print(f"❌ [LỖI MẠNG] Không thể đẩy dữ liệu cảm biến: {e}")
                                
                except json.JSONDecodeError:
                    pass 

        # ----------------------------------------------------
        # LUỒNG 2: LẤY LỆNH BƠM TỪ BACKEND XUỐNG ARDUINO
        # ----------------------------------------------------
        current_time = time.time()
        if current_time - last_pump_check > 1.0: 
            try:
                pump_resp = requests.get(BACKEND_PUMP_URL, headers=HEADERS, timeout=2)
                if pump_resp.status_code == 200:
                    backend_status = pump_resp.json().get("status")
                    
                    if backend_status == "ON" and last_sent_status != "ON":
                        arduino.write(b"ON\n")
                        arduino.flush()
                        print("👉 [LỆNH WEB] Gửi lệnh ON xuống mạch")
                        last_sent_status = "ON" 
                        
                    elif backend_status == "OFF" and last_sent_status != "OFF":
                        arduino.write(b"OFF\n")
                        arduino.flush()
                        print("👉 [LỆNH WEB] Gửi lệnh OFF xuống mạch")
                        last_sent_status = "OFF"
            except Exception as e:
                pass # Bỏ qua lỗi kết nối nhẹ để vòng lặp không bị chết
                
            last_pump_check = current_time
            
        time.sleep(0.05)

if __name__ == '__main__':
    main()