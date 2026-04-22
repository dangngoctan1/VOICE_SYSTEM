# 🛠️ BUILD PLAN: Hệ Thống Điều Khiển LED RGB Bằng Giọng Nói + Android

> **Dự án:** ESP32-S3 + INMP441 + HW-479 + Android App  
> **Môi trường:** Ubuntu 24.04 | ESP-IDF v6.0 | VS Code + EIM | Port: `/dev/ttyACM0`  
> **Phần cứng yêu cầu:** ESP32-S3 **N16R8** (16MB Flash + 8MB Octal PSRAM — bắt buộc để chạy WakeNet/MultiNet)

---

## 📐 Kiến Trúc Tổng Thể

```
┌─────────────────────────────────────────────────────┐
│                    ESP32-S3 (N16R8)                  │
│                                                       │
│  Core 0 (PRO_CPU)        Core 1 (APP_CPU)            │
│  ┌─────────────────┐    ┌──────────────────────┐     │
│  │ WiFi Stack      │    │ I2S DMA Task (HIGH)  │     │
│  │ WebSocket Server│◄───│ AFE → WakeNet        │     │
│  │ JSON Parser     │    │ MultiNet → Command   │     │
│  │ LED Sync Push   │    │ LED Control Task     │     │
│  └─────────────────┘    └──────────────────────┘     │
│         ▲  FreeRTOS Queue (EV_UPDATE_LED)  │          │
│         └──────────────────────────────────┘          │
└──────────┬──────────────────────┬────────────────────┘
           │                      │
     WiFi (ws://IP:81)       GPIO PWM
           │                      │
    ┌──────▼──────┐        ┌──────▼──────┐
    │ Android App │        │ HW-479 RGB  │
    │ (OkHttp WS) │        │ R/G/B LEDs  │
    └─────────────┘        └─────────────┘
```

---

## 🗂️ Sơ Đồ Kết Nối Phần Cứng

### INMP441 Microphone → ESP32-S3

| Chân INMP441 | GPIO ESP32-S3 | Ghi chú |
|---|---|---|
| VDD | 3.3V | **TUYỆT ĐỐI KHÔNG dùng 5V** — sẽ phá hủy mic |
| GND | GND | Nối đất chung |
| L/R | GND | Kéo xuống GND → kênh trái (Left Channel) |
| WS (LRCK) | GPIO 6 | Word Select / tần số lấy mẫu |
| SCK (BCLK) | GPIO 5 | Bit Clock |
| SD (DOUT) | GPIO 4 | Serial Data (dữ liệu PCM ra) |

### HW-479 RGB Module → ESP32-S3

| Chân HW-479 | GPIO ESP32-S3 | Ghi chú |
|---|---|---|
| GND (-) | GND | Cực âm chung |
| R (Red) | GPIO 7 | PWM kênh đỏ |
| G (Green) | GPIO 8 | PWM kênh xanh lá |
| B (Blue) | GPIO 9 | PWM kênh xanh dương |

> **Lý do chọn GPIO 7/8/9 cho LED:** Tránh xung đột với I2S đang dùng GPIO 4/5/6.

---

## 📋 Quy Tắc Chung Cho Toàn Bộ Dự Án

- **Không bao giờ bỏ qua bước Test** của một giai đoạn để sang giai đoạn tiếp theo.
- Mỗi giai đoạn build trên nền **code đã chạy được** của giai đoạn trước — không viết lại từ đầu.
- Commit code (git) sau mỗi giai đoạn vượt qua Test thành công.
- Nếu một Test thất bại, **debug ngay tại giai đoạn đó** — không tiếp tục.

---

## ✅ GIAI ĐOẠN 0: Chuẩn Bị Môi Trường — **HOÀN THÀNH**

> **Mục tiêu:** Đảm bảo toàn bộ toolchain hoạt động trước khi chạm vào phần cứng thật.  
> **Workflow chuẩn của dự án này:** Dùng **icon Build/Flash/Monitor trên VS Code** + **terminal bên trong VS Code** (`Ctrl + `` ` ``). Không dùng terminal Linux bên ngoài cho các lệnh ESP-IDF.

### 0.1 Quyền cổng serial

Chạy trong **terminal Linux bên ngoài** (chỉ cần làm một lần):

```bash
sudo usermod -a -G dialout $USER
# Tạo udev rule để tự động gán quyền mỗi khi cắm USB
echo 'KERNEL=="ttyACM[0-9]*", MODE="0666"' | sudo tee /etc/udev/rules.d/40-usbtty.rules
sudo udevadm control --reload-rules
# Đăng xuất và đăng nhập lại để group có hiệu lực
```

### 0.2 Cài đặt các gói phụ thuộc hệ thống

Chạy trong **terminal Linux bên ngoài** (chỉ cần làm một lần):

```bash
sudo apt-get install git wget flex bison gperf python3 python3-pip \
  python3-venv cmake ninja-build ccache libffi-dev libssl-dev \
  dfu-util libusb-1.0-0
```

### 0.3 Cài ESP-IDF qua EIM trong VS Code

1. Mở VS Code → Cài extension **ESP-IDF** từ Marketplace.
2. Mở Command Palette (`Ctrl+Shift+P`) → **ESP-IDF: Configure ESP-IDF Extension**.
3. Chọn **EXPRESS** → chọn phiên bản **v6.0** → target **ESP32-S3** → nhấn Install.
4. EIM tự tải toolchain, tạo Python venv cách ly vào `~/.espressif/` — không cần làm gì thêm.

> ⚠️ **Không git clone ESP-IDF thủ công.** Nếu đã clone vào `~/esp-idf`, hãy xóa đi và gỡ khỏi `~/.bashrc` để tránh xung đột toolchain với EIM.

### 0.4 Tạo project Hello World

1. `Ctrl+Shift+P` → **ESP-IDF: Show Examples Projects** → chọn **hello_world** → **Create project**.
2. Lưu vào `~/esp/hello_world`.
3. `Ctrl+Shift+P` → **ESP-IDF: Set Espressif Device Target** → chọn **esp32s3**.

### 0.5 Build, Flash, Monitor bằng icon VS Code

Thanh status bar dưới cùng VS Code có các icon theo thứ tự:

```
🔧 Build   →   ⚡ Flash   →   🖥️ Monitor
```

- **🔧 Build:** Biên dịch firmware.
- **⚡ Flash:** Nạp firmware lên ESP32 qua `/dev/ttyACM0`.
- **🖥️ Monitor:** Mở Serial Monitor xem log — tương đương `idf.py monitor`.
- **🔥 (Build + Flash + Monitor):** Làm cả 3 bước liên tiếp.

> 💡 **Dùng terminal bên trong VS Code** (`Ctrl + `` ` ``) nếu cần gõ lệnh như `idf.py menuconfig`. Terminal này được EIM inject sẵn đúng `IDF_PATH` và toolchain — gõ `idf.py --version` sẽ in ra `ESP-IDF v6.0.x`.

### ✅ TEST GIAI ĐOẠN 0

| # | Kiểm tra | Cách thực hiện | Kết quả mong đợi | Trạng thái |
|---|---|---|---|---|
| T0.1 | Quyền cổng serial | `ls -la /dev/ttyACM0` trong terminal Linux | File tồn tại, mode `crw-rw-rw-` | ✅ |
| T0.2 | EIM cài thành công | Terminal **trong VS Code**: `idf.py --version` | In ra `ESP-IDF v6.0.x` | ✅ |
| T0.3 | Build Hello World | Click icon **🔧 Build** | Build thành công, không có lỗi đỏ | ✅ |
| T0.4 | Flash Hello World | Click icon **⚡ Flash** | Nạp xong, không có `Failed to connect` | ✅ |
| T0.5 | Monitor hoạt động | Click icon **🖥️ Monitor** | Serial in `Hello World!` liên tục | ✅ |

> 💡 **`Failed to connect to ESP32`:** Kiểm tra đúng port `/dev/ttyACM0` trong status bar. Nhấn giữ nút BOOT trên board khi bắt đầu flash.  
> 💡 **`idf.py: command not found` trong terminal Linux bên ngoài:** Đây là bình thường — EIM chỉ inject vào terminal bên trong VS Code. Không cần lo.

---

## ✅ GIAI ĐOẠN 1: Thu Âm I2S — Đọc Dữ Liệu Microphone — **HOÀN THÀNH**

> **Mục tiêu:** ESP32-S3 đọc được luồng PCM số từ INMP441 qua I2S + DMA, in ra Serial Monitor.  
> **Output:** Thấy nhãn `SILENT / AMBIENT / VOICE` thay đổi khi nói vào mic.

### 1.1 Code `main/hello_world.c` (đã hoàn thành)

> **Lưu ý thực tế:** File hiện tại tên là `hello_world.c` (từ project mẫu). Khi chuyển sang Giai đoạn 2, đổi tên thành `main.c` cho khớp với cấu trúc thư mục đề xuất — nhớ cập nhật `CMakeLists.txt` tương ứng.

Code đã implement đầy đủ:

```c
#include <stdio.h>
#include <stdlib.h>
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "MIC";
i2s_chan_handle_t rx_handle;

void init_i2s(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),   // 16kHz — bắt buộc cho ESP-SR
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_32BIT,
                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = GPIO_NUM_5,        // SCK
            .ws   = GPIO_NUM_6,        // WS/LRCK
            .dout = I2S_GPIO_UNUSED,
            .din  = GPIO_NUM_4,        // SD (data từ mic)
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
    ESP_LOGI(TAG, "I2S initialized OK — 16kHz | Mono | 32-bit slot");
}

void mic_read_task(void *arg) {
    const int SAMPLE_COUNT = 256;
    int32_t *raw = malloc(SAMPLE_COUNT * sizeof(int32_t));
    if (!raw) { ESP_LOGE(TAG, "malloc failed"); vTaskDelete(NULL); return; }

    size_t bytes_read = 0;

    while (1) {
        esp_err_t ret = i2s_channel_read(rx_handle, raw,
            SAMPLE_COUNT * sizeof(int32_t), &bytes_read, portMAX_DELAY);
        if (ret != ESP_OK) { ESP_LOGE(TAG, "I2S read error: %s", esp_err_to_name(ret)); continue; }

        int samples_read = bytes_read / sizeof(int32_t);

        // Phân tích peak + RMS
        int32_t peak = 0;
        int64_t sum_sq = 0;
        for (int i = 0; i < samples_read; i++) {
            int32_t val = raw[i] >> 8;   // >> 8: lấy 24-bit có nghĩa của INMP441
            int32_t abs_val = val < 0 ? -val : val;
            if (abs_val > peak) peak = abs_val;
            sum_sq += (int64_t)val * val;
        }
        int32_t rms = (int32_t)(sum_sq / samples_read);

        ESP_LOGI(TAG, "samples=%d | peak=%-8ld | rms=%-8ld | noise_floor=%s",
            samples_read, peak, rms,
            peak < 5000 ? "SILENT" : peak < 50000 ? "AMBIENT" : "VOICE/LOUD");
    }
    free(raw);
    vTaskDelete(NULL);
}

void app_main(void) {
    init_i2s();
    xTaskCreatePinnedToCore(mic_read_task, "mic_read", 4096, NULL, 5, NULL, 1);
}
```

### 1.2 CMakeLists.txt của main/ (đã cập nhật)

```cmake
idf_component_register(
    SRCS "hello_world.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_i2s
)
```

> **Lưu ý:** `esp_driver_i2s` được khai báo tường minh — đúng với ESP-IDF v6.0, tránh phụ thuộc ngầm.

### ✅ TEST GIAI ĐOẠN 1

| # | Kiểm tra | Cách thực hiện | Kết quả mong đợi | Trạng thái |
|---|---|---|---|---|
| T1.1 | Build thành công | Click **🔧 Build** | Không có lỗi đỏ, in `Build complete` | ✅ |
| T1.2 | Flash và Monitor | Click **🔥** | Monitor mở, không có `I2S driver install failed` | ✅ |
| T1.3 | **Im lặng** — xem log | Để yên, nhìn Monitor | Log in `SILENT` liên tục | ✅ |
| T1.4 | **Nói to vào mic** | Nói bình thường cách 20cm | Log chuyển sang `VOICE/LOUD`, peak > 50,000 | ✅ |
| T1.5 | **Vỗ tay gần mic** | Vỗ 1 cái | Peak đột biến lớn, rõ ràng khác `SILENT` | ✅ |
| T1.6 | Chạy liên tục 2 phút | Để board chạy | Không Reboot, không Watchdog reset | ✅ |

> 💡 **Debug nếu số toàn 0:** Kiểm tra chân L/R đã nối GND chưa. Kiểm tra nguồn 3.3V.  
> 💡 **Debug nếu số không đổi khi nói:** Hoán đổi SCK và WS thử xem.

---

## 🔄 GIAI ĐOẠN 2: Tích Hợp ESP-SR — Wake Word + Command — **ĐANG THỰC HIỆN**

> **Mục tiêu:** ESP32-S3 nhận diện "Hi ESP" và các lệnh màu sắc bằng tiếng Anh.  
> **Build từ:** Code Giai đoạn 1 (giữ nguyên I2S, thêm lớp AI phía trên).

### 2.1 Thêm ESP-SR qua Component Manager ✅ (đã làm)

Tạo file `main/idf_component.yml`:

```yaml
dependencies:
  idf: ">=5.0"
  espressif/esp-sr:
    version: ">=1.0.0"
```

> **Khác với plan gốc:** Plan ban đầu dùng `set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/../esp-sr)` — cách đó yêu cầu clone esp-sr thủ công. Cách dùng **Component Manager qua `idf_component.yml`** là phương pháp chuẩn và được khuyến nghị cho ESP-IDF v5.0+ — tự động tải, quản lý version, không cần thao tác thủ công.

### 2.2 Cập nhật CMakeLists.txt ✅ (đã làm)

```cmake
idf_component_register(
    SRCS "hello_world.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_i2s
)
```

> **Lưu ý:** ESP-SR được kéo vào qua `idf_component.yml`, không cần thêm vào `REQUIRES` trong `idf_component_register`. Khi bật PSRAM và thêm code ESP-SR, sẽ thêm các thư viện liên quan (esp_afe_sr, esp_mn_models) vào `REQUIRES` ở bước 2.4.

### 2.3 Bật PSRAM và Chọn Model trong menuconfig ✅ (đã làm)

Đã thực hiện qua **ESP-IDF: SDK Configuration Editor** trong VS Code (không dùng terminal):

```
Component config → ESP32S3-specific
  └─ [x] Support for external, SPI-connected RAM (PSRAM)
        └─ SPI RAM config → Mode: Octal Mode PSRAM   ← bắt buộc cho N16R8

Component config → ESP-SR
  └─ Wake Word Engine: WakeNet → wn9_hiesp           ← đã chọn
  └─ Speech Command Recognition: MultiNet → mn6_en   ← đã chọn
```

> ✅ **Model đã xác nhận:** `wn9_hiesp` (WakeNet9 — "Hi ESP") + `mn6_en` (MultiNet6 English). Đây là phiên bản model mới nhất tương thích với ESP-IDF v6.0 và PSRAM 8MB.

> ⚠️ **Bắt buộc phải bật PSRAM Octal Mode trước khi build với ESP-SR.** Thiếu bước này → `LoadProhibited` exception khi khởi tạo WakeNet.

> 💡 **Nếu chưa thành công:** Kiểm tra lại:
> - Đã bật PSRAM trong menuconfig chưa?
> - Đã chọn đúng model `wn9_hiesp` và `mn6_en` chưa?
> - Đã save & sync sau khi thay đổi chưa?

### 2.4 Cấu hình AFE (Audio Front-End) — 🔄 Cần làm tiếp

Cập nhật `CMakeLists.txt` — thêm ESP-SR vào REQUIRES:

```cmake
idf_component_register(
    SRCS "hello_world.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_i2s esp_afe_sr_models esp_mn_models
)
```

Thêm vào code (giữ nguyên phần I2S đã có):

```c
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "model_path.h"

// AFE xử lý: Noise Suppression + VAD + chuẩn hóa cho AI
const esp_afe_sr_iface_t *afe_handle = &ESP_AFE_SR_HANDLE;
afe_config_t afe_config = AFE_CONFIG_DEFAULT();
afe_config.aec_init = false;        // Không có loa phản hồi → tắt AEC
afe_config.se_init = true;          // Bật Noise Suppression
afe_config.vad_init = true;         // Bật Voice Activity Detection
afe_config.wakenet_init = true;     // Bật WakeNet (wn9_hiesp)
afe_config.voice_communication_init = false;

esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(&afe_config);
```

### 2.5 Feed dữ liệu I2S vào AFE pipeline ✅ (đã làm)

```c
// Task chạy trên Core 1 — thay thế mic_read_task cũ
void audio_feed_task(void *arg) {
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
    int16_t *audio_buffer = malloc(audio_chunksize * sizeof(int16_t));

    while (1) {
        size_t bytes_read;
        int32_t raw[audio_chunksize];
        i2s_channel_read(rx_handle, raw, audio_chunksize * 4, &bytes_read, portMAX_DELAY);

        // Convert 32-bit → 16-bit (ESP-SR yêu cầu int16)
        for (int i = 0; i < audio_chunksize; i++) {
            audio_buffer[i] = (int16_t)(raw[i] >> 14);
        }

        afe_handle->feed(afe_data, audio_buffer);
    }
}
```

### 2.6 Nhận kết quả từ AFE + WakeNet + MultiNet ✅ (đã làm)

```c
void audio_detect_task(void *arg) {
    while (1) {
        afe_fetch_result_t *res = afe_handle->fetch(afe_data);
        if (!res) continue;

        if (res->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI("AI", ">>> Wake word detected! Listening for command...");
        }

        if (res->command_id >= 0) {
            ESP_LOGI("AI", ">>> Command ID: %d", res->command_id);
            // command_id 0 = "turn on red", 1 = "turn on green", v.v.
        }
    }
}
```

### 2.7 Định nghĩa Command List — cần làm tiếp

Tạo file `main/commands_en.txt`:

```
turn on red
make it red
turn on green
make it green
turn on blue
make it blue
turn off
lights off
```

Khai báo trong code qua `multinet_handle` sau khi wake word được phát hiện.

### 2.8 Cập nhật app_main — cần làm tiếp

```c
void app_main(void) {
    init_i2s();
    // Tạo AFE data (afe_data phải được init trước khi tạo các task)

    // Core 1: Audio pipeline
    xTaskCreatePinnedToCore(audio_feed_task,   "feed_task",   4096, NULL, 8, NULL, 1);
    xTaskCreatePinnedToCore(audio_detect_task, "detect_task", 8192, NULL, 7, NULL, 1);
}
```

### ✅ TEST GIAI ĐOẠN 2

| # | Kiểm tra | Cách thực hiện | Kết quả mong đợi | Trạng thái |
|---|---|---|---|---|
| T2.1 | Build thành công (với ESP-SR) | Click **🔧 Build** | Không có lỗi `Failed to allocate tensors` | 🔄 Cần build lại sau khi thêm code AFE |
| T2.2 | Khởi động board | Click **🔥** | Monitor in `AFE initialized`, `WakeNet ready` | ⏳ |
| T2.3 | Wake word | Nói **"Hi ESP"** | Monitor in `Wake word detected!` trong ≤ 1 giây | ⏳ |
| T2.4 | Lệnh đỏ | Sau wake word, nói **"turn on red"** | Monitor in `Command ID: 0` | ⏳ |
| T2.5 | Lệnh xanh | Sau wake word, nói **"turn on blue"** | Monitor in đúng Command ID tương ứng | ⏳ |
| T2.6 | Không có false positive | Im lặng 30 giây | Không có random wake, không có random command | ⏳ |
| T2.7 | Kiểm tra heap | Xem Monitor sau 5 phút | `Free heap` không giảm tuyến tính | ⏳ |

**→ Chỉ tiếp tục khi T2.3 và T2.4/T2.5 đều xanh.**

> 💡 **Debug `LoadProhibited` khi khởi động:** PSRAM chưa được bật đúng trong menuconfig.  
> 💡 **Wake word không nhận:** Thử nói chậm rõ hơn, kiểm tra VAD threshold trong `afe_config`.  
> 💡 **Heap giảm liên tục:** Có memory leak — kiểm tra xem `afe_fetch_result_t` có được free sau khi dùng không.  
> 💡 **Build lỗi thiếu model:** Đảm bảo đã chọn đúng `wn9_hiesp` và `mn6_en` trong menuconfig trước khi build.

---

## ⏳ GIAI ĐOẠN 3: Điều Khiển LED RGB — LEDC PWM

> **Mục tiêu:** Gắn LED HW-479, điều khiển bằng kết quả Command từ Giai đoạn 2.  
> **Build từ:** Code Giai đoạn 2 (thêm LEDC driver + kết nối logic với AI output).

### 3.1 Cấu hình LEDC (LED PWM Controller)

```c
#include "driver/ledc.h"

#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_FREQ_HZ    10000    // 10kHz — tránh xung đột với I2S interrupt
#define LEDC_RESOLUTION LEDC_TIMER_8_BIT  // 0-255

void init_ledc(void) {
    // Cấu hình timer
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_RESOLUTION,
        .freq_hz         = LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    // Cấu hình 3 kênh RGB
    ledc_channel_config_t ch_cfg = {
        .speed_mode = LEDC_MODE,
        .timer_sel  = LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .duty       = 0,
        .hpoint     = 0,
    };

    ch_cfg.channel = LEDC_CHANNEL_0; ch_cfg.gpio_num = GPIO_NUM_7; // Red
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
    ch_cfg.channel = LEDC_CHANNEL_1; ch_cfg.gpio_num = GPIO_NUM_8; // Green
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
    ch_cfg.channel = LEDC_CHANNEL_2; ch_cfg.gpio_num = GPIO_NUM_9; // Blue
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
}
```

### 3.2 Hàm set màu và LED Task

```c
// Struct sự kiện LED — dùng chung cho FreeRTOS Queue
typedef struct { uint8_t r; uint8_t g; uint8_t b; } led_event_t;
QueueHandle_t led_queue;

void set_led_color(uint8_t r, uint8_t g, uint8_t b) {
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_0, r);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_0);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_1, g);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_1);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_2, b);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_2);
}

void led_control_task(void *arg) {
    led_event_t ev;
    while (1) {
        if (xQueueReceive(led_queue, &ev, portMAX_DELAY)) {
            set_led_color(ev.r, ev.g, ev.b);
            ESP_LOGI("LED", "Color set → R:%d G:%d B:%d", ev.r, ev.g, ev.b);
        }
    }
}
```

### 3.3 Map Command ID → màu LED

```c
// Trong audio_detect_task, khi nhận được command:
static const led_event_t CMD_COLORS[] = {
    {255, 0,   0  },  // 0: "turn on red"
    {255, 0,   0  },  // 1: "make it red"
    {0,   255, 0  },  // 2: "turn on green"
    {0,   255, 0  },  // 3: "make it green"
    {0,   0,   255},  // 4: "turn on blue"
    {0,   0,   255},  // 5: "make it blue"
    {0,   0,   0  },  // 6: "turn off"
    {0,   0,   0  },  // 7: "lights off"
};

if (res->command_id >= 0 && res->command_id < 8) {
    xQueueSend(led_queue, &CMD_COLORS[res->command_id], 0);
}
```

### ✅ TEST GIAI ĐOẠN 3

| # | Kiểm tra | Cách thực hiện | Kết quả mong đợi | Trạng thái |
|---|---|---|---|---|
| T3.1 | Build thành công | Click **🔧 Build** | Không có lỗi LEDC | ⏳ |
| T3.2 | LED bật khi khởi động | Quan sát LED sau flash | LED sáng màu mặc định | ⏳ |
| T3.3 | Wake + "turn on red" | Nói "Hi ESP" → "turn on red" | LED đổi sang **đỏ** | ⏳ |
| T3.4 | Wake + "turn on green" | Nói "Hi ESP" → "turn on green" | LED đổi sang **xanh lá** | ⏳ |
| T3.5 | Wake + "turn on blue" | Nói "Hi ESP" → "turn on blue" | LED đổi sang **xanh dương** | ⏳ |
| T3.6 | Wake + "turn off" | Nói "Hi ESP" → "turn off" | LED tắt hoàn toàn | ⏳ |
| T3.7 | Không nhấp nháy | Quan sát LED khi mic hoạt động | LED sáng đều, không chớp tắt do I2S | ⏳ |
| T3.8 | Bền vững 5 phút | Để chạy | Không reboot, không I2S error | ⏳ |

**→ Chỉ tiếp tục khi T3.3, T3.4, T3.5 đều xanh.**

> 💡 **LED nhấp nháy khi mic hoạt động:** I2S interrupt xung đột PWM → tăng `LEDC_FREQ_HZ` lên 10kHz (đã set sẵn trong code trên).

---

## ⏳ GIAI ĐOẠN 4: WiFi + WebSocket + Android App

> **Mục tiêu:** Android App điều khiển LED và đồng bộ trạng thái 2 chiều với ESP32.  
> **Build từ:** Code Giai đoạn 3.

### 4.1 Khởi tạo WiFi Station

```c
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"

#define WIFI_SSID "your_ssid"
#define WIFI_PASS "your_password"

void wifi_init_sta(void) {
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_cfg = {
        .sta = { .ssid = WIFI_SSID, .password = WIFI_PASS },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    esp_wifi_start();
    esp_wifi_connect();
}
```

### 4.2 WebSocket Server (nhận màu từ Android)

```c
#include "esp_http_server.h"

esp_err_t ws_handler(httpd_req_t *req) {
    uint8_t buf[128] = {0};
    httpd_ws_frame_t ws_pkt = { .payload = buf, .type = HTTPD_WS_TYPE_TEXT };
    httpd_ws_recv_frame(req, &ws_pkt, sizeof(buf));

    // Parse JSON
    cJSON *json = cJSON_Parse((char*)ws_pkt.payload);
    uint8_t r = cJSON_GetObjectItem(json, "r")->valueint;
    uint8_t g = cJSON_GetObjectItem(json, "g")->valueint;
    uint8_t b = cJSON_GetObjectItem(json, "b")->valueint;
    cJSON_Delete(json);

    // Gửi vào LED queue (thread-safe)
    led_event_t ev = {r, g, b};
    xQueueSend(led_queue, &ev, 0);
    return ESP_OK;
}
```

### 4.3 Push trạng thái từ AI về Android (Từ Core 1 → Core 0 → WebSocket)

```c
// Khi AI detect command thành công trên Core 1:
// → Gửi vào ws_push_queue (Core 0 sẽ đọc và push WS frame)

// Trong WiFi task (Core 0):
led_event_t ev;
if (xQueueReceive(ws_push_queue, &ev, 0)) {
    char json_buf[64];
    snprintf(json_buf, sizeof(json_buf),
             "{\"r\":%d,\"g\":%d,\"b\":%d}", ev.r, ev.g, ev.b);
    // Push WebSocket frame đến tất cả clients
    httpd_ws_send_frame_async(ws_handle, ...);
}
```

### 4.4 Android App (Kotlin + OkHttp)

```kotlin
// Kết nối WebSocket
val client = OkHttpClient()
val request = Request.Builder().url("ws://<IP_ESP32>:81/").build()
val wsListener = object : WebSocketListener() {
    override fun onMessage(webSocket: WebSocket, text: String) {
        val json = JSONObject(text)
        val r = json.getInt("r"); val g = json.getInt("g"); val b = json.getInt("b")
        runOnUiThread { updateColorPicker(r, g, b) }
    }
}
val ws = client.newWebSocket(request, wsListener)

// Gửi màu từ Color Picker
fun sendColor(r: Int, g: Int, b: Int) {
    ws.send("""{"r":$r,"g":$g,"b":$b}""")
}
```

### ✅ TEST GIAI ĐOẠN 4

| # | Kiểm tra | Cách thực hiện | Kết quả mong đợi | Trạng thái |
|---|---|---|---|---|
| T4.1 | ESP32 nhận IP | Click **🔥**, xem Monitor | Monitor in địa chỉ IP (ví dụ: `192.168.1.x`) | ⏳ |
| T4.2 | Ping từ Android | Ping IP trên cùng mạng WiFi | Ping thành công | ⏳ |
| T4.3 | Android kết nối WS | Mở App, nhập IP, nhấn Connect | Log `onOpen` được gọi, không lỗi | ⏳ |
| T4.4 | Điều khiển từ App | Kéo thanh màu → chọn đỏ | LED đổi sang **đỏ** ngay lập tức (< 100ms) | ⏳ |
| T4.5 | Đồng bộ từ giọng nói | Nói "Hi ESP" → "turn on green" | LED xanh **VÀ** UI Android cũng cập nhật xanh | ⏳ |
| T4.6 | Reconnect | Tắt/bật WiFi router | ESP32 tự kết nối lại, App reconnect | ⏳ |
| T4.7 | Song song | Vừa nói vừa kéo thanh màu | Cả hai kênh hoạt động, không deadlock | ⏳ |
| T4.8 | Bền vững | Để chạy 15 phút | Không reboot, heap ổn định | ⏳ |

**→ Chỉ tiếp tục khi T4.4, T4.5 và T4.7 đều xanh — đây là điểm hội tụ của toàn hệ thống.**

> 💡 **ESP32 không nhận IP:** Kiểm tra SSID/password. Đảm bảo router ở băng tần 2.4GHz.  
> 💡 **Giọng nói bị ảnh hưởng sau khi bật WiFi:** Đây là xung đột interrupt — xem Giai đoạn 5.

---

## ⏳ GIAI ĐOẠN 5: Tối Ưu FreeRTOS — Ghim Task Vào Core

> **Mục tiêu:** Hệ thống chạy bền vững, không giật lag, không reboot, phản hồi < 500ms.  
> **Build từ:** Code Giai đoạn 4 (tái tổ chức task pinning).

### 5.1 Phân chia task theo Core

```c
void app_main(void) {
    // Khởi tạo hardware
    init_i2s();
    init_ledc();
    wifi_init_sta();

    // Tạo queues
    led_queue     = xQueueCreate(10, sizeof(led_event_t));
    ws_push_queue = xQueueCreate(10, sizeof(led_event_t));

    // Core 0: Mọi thứ liên quan Network
    xTaskCreatePinnedToCore(wifi_websocket_task, "ws_task",    8192, NULL, 5, NULL, 0);

    // Core 1: Mọi thứ liên quan Audio + AI + LED
    xTaskCreatePinnedToCore(audio_feed_task,     "feed_task",  4096, NULL, 8, NULL, 1);
    xTaskCreatePinnedToCore(audio_detect_task,   "detect_task",8192, NULL, 7, NULL, 1);
    xTaskCreatePinnedToCore(led_control_task,    "led_task",   2048, NULL, 6, NULL, 1);
}
```

> **Nguyên tắc ưu tiên (priority):** Audio feed > AI detect > LED control > WebSocket task

### 5.2 Monitor heap memory trong runtime

```c
void monitor_task(void *arg) {
    while (1) {
        ESP_LOGI("MEM", "Free heap: %lu bytes", esp_get_free_heap_size());
        ESP_LOGI("MEM", "Min free heap: %lu bytes", esp_get_minimum_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(10000)); // Log mỗi 10 giây
    }
}
xTaskCreatePinnedToCore(monitor_task, "monitor", 2048, NULL, 1, NULL, 0);
```

### 5.3 Cấu hình Watchdog Timer

```c
#include "esp_task_wdt.h"
esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 5000,
    .idle_core_mask = 0,
    .trigger_panic = true,
};
esp_task_wdt_reconfigure(&wdt_config);
```

### ✅ TEST GIAI ĐOẠN 5 — BỘ TEST CUỐI CÙNG

| # | Kiểm tra | Cách thực hiện | Kết quả mong đợi | Trạng thái |
|---|---|---|---|---|
| T5.1 | Core assignment | Xem Monitor khi boot | `feed_task` và `detect_task` trên Core 1; `ws_task` trên Core 0 | ⏳ |
| T5.2 | Stress giọng nói | Nói 10 lệnh liên tiếp | Tất cả nhận đúng, < 500ms latency | ⏳ |
| T5.3 | Stress App | Kéo thanh màu liên tục 30 giây | LED mượt, không giật, không freeze | ⏳ |
| T5.4 | Stress song song | Vừa nói vừa kéo thanh | Cả hai hoạt động đồng thời, không deadlock | ⏳ |
| T5.5 | Heap sau 30 phút | Xem log `MEM` trong Monitor | `Free heap` dao động nhỏ, không giảm tuyến tính | ⏳ |
| T5.6 | Min heap an toàn | Xem log `Min free heap` | > 20KB | ⏳ |
| T5.7 | Bền vững 1 tiếng | Để board chạy không tương tác | Không Reboot, không Watchdog trigger | ⏳ |
| T5.8 | Latency thực tế | Bấm đồng hồ từ lúc nói đến LED sáng | Giọng nói → LED: < 500ms / App → LED: < 100ms | ⏳ |

**→ Hệ thống hoàn tất khi toàn bộ 8 test trên đều xanh.**

---

## 📊 Bảng Tóm Tắt Toàn Bộ Lộ Trình

```
Giai đoạn 0 ──► Giai đoạn 1 ──► Giai đoạn 2 ──► Giai đoạn 3 ──► Giai đoạn 4 ──► Giai đoạn 5
  Môi trường       I2S RAW          AI/ESP-SR         LED PWM        WiFi + WS       FreeRTOS
  (5 test)         (6 test)         (7 test)           (8 test)       (8 test)        (8 test)
      ↓                ↓                ↓                  ↓               ↓               ↓
  ✅ DONE          ✅ DONE          🔄 ĐANG LÀM       ⏳ Chờ          ⏳ Chờ           ⏳ Chờ
```

Mỗi giai đoạn **kế thừa code** từ giai đoạn trước — không có bước nào viết lại từ đầu.  
Mỗi bộ test **verify đầu ra** của giai đoạn đó **trước khi** code được mang vào giai đoạn tiếp theo.

---

## 🐛 Bảng Lỗi Thường Gặp Và Cách Xử Lý

| Lỗi | Giai đoạn | Nguyên nhân | Fix |
|---|---|---|---|
| `Permission denied /dev/ttyACM0` | 0 | Chưa thêm user vào group dialout | `sudo usermod -a -G dialout $USER` + logout/login |
| `externally-managed-environment` | 0 | Ubuntu 24.04 chặn pip global | Dùng EIM → tự tạo venv |
| Số in ra toàn 0 | 1 | Chân L/R không nối GND | Kiểm tra và nối L/R xuống GND |
| `LoadProhibited` exception | 2 | PSRAM chưa bật | Bật PSRAM Octal trong menuconfig |
| `Failed to allocate tensors` | 2 | Không đủ RAM cho model | Cần ESP32-S3 N16R8 (8MB PSRAM) |
| Wake word không nhận | 2 | VAD threshold quá cao | Giảm `vad_init` sensitivity hoặc nói gần mic hơn |
| LED nhấp nháy khi mic hoạt động | 3 | I2S interrupt xung đột PWM | Tăng LEDC frequency lên 10kHz |
| Heap giảm liên tục | 2/4 | Memory leak | Kiểm tra `cJSON_Delete()` và `afe_fetch_result` free |
| Giọng nói lag sau khi bật WiFi | 4 | Task chạy cùng core | Ghim WiFi task vào Core 0, AI task vào Core 1 |
| WebSocket disconnect ngẫu nhiên | 4 | Timeout mặc định | Gửi ping frame mỗi 30 giây từ Android |

---

## 📁 Cấu Trúc Thư Mục Đề Xuất

```
led_voice_control/
├── CMakeLists.txt
├── partitions.csv           ← Phân vùng flash tùy chỉnh cho AI models
├── sdkconfig                ← Cấu hình menuconfig (commit file này!)
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml    ← Component Manager: esp-sr (đã tạo ✅)
│   ├── hello_world.c        ← Giai đoạn 1: I2S (hiện tại) → đổi tên main.c ở GĐ2
│   ├── ai_engine.c/h        ← Giai đoạn 2: AFE + WakeNet + MultiNet
│   ├── led_control.c/h      ← Giai đoạn 3: LEDC PWM
│   ├── wifi_ws.c/h          ← Giai đoạn 4: WiFi + WebSocket
│   └── commands_en.txt      ← Danh sách lệnh tiếng Anh
└── android_app/             ← Android Studio project
    └── app/src/main/
        └── MainActivity.kt
```

> **Lưu ý file hiện tại:** `components/esp-sr/` không cần tạo thủ công — Component Manager tự tải khi build lần đầu.

---

## 📝 Nhật Ký Thay Đổi So Với Plan Gốc

| Mục | Plan gốc | Thực tế đã làm | Lý do |
|---|---|---|---|
| Cài ESP-SR | `set(EXTRA_COMPONENT_DIRS)` + clone thủ công | `idf_component.yml` + Component Manager | Chuẩn hơn cho IDF v5+, không cần clone thủ công |
| REQUIRES I2S | `REQUIRES driver` | `REQUIRES driver esp_driver_i2s` | Khai báo tường minh, tránh phụ thuộc ngầm |
| Model WakeNet | Ghi chung "Hi ESP" | `wn9_hiesp` (WakeNet9) | Version cụ thể, xác nhận qua menuconfig |
| Model MultiNet | Ghi chung "English" | `mn6_en` (MultiNet6 English) | Version cụ thể, xác nhận qua menuconfig |
| Tên file source | `main.c` | `hello_world.c` (từ template) | Cần đổi tên khi bắt đầu Giai đoạn 2 |

---

*Build plan cập nhật theo tiến trình thực tế.*  
*Môi trường: Ubuntu 24.04 | ESP-IDF v6.0 qua EIM | VS Code Build/Flash/Monitor icons | ESP32-S3 N16R8*  
*Cập nhật lần cuối: Sau khi hoàn thành setup ESP-SR (idf_component.yml + menuconfig wn9_hiesp/mn6_en + build)*