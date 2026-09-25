# 프로젝트 현재 상태

기준일: 2026-09-25. 아래 확인 결과는 사용자가 제공한 상태이며 이번 저장소 준비 작업에서 하드웨어나 클라우드를 다시 검증한 결과는 아니다.

## 아키텍처

```text
BME688
  -> nRF52840
  -> BLE Nordic UART Service
  -> ESP32-S3
  -> Wi-Fi / smartphone hotspot
  -> Google Apps Script
  -> Google Sheets / CSV
  -> Colab / ML
```

## 현재 작업 범위

Git 초기화 전 baseline을 준비한 상태다. 기존 원본 TXT 파일은 수정·삭제하지 않고 보존하며 Git 추적 대상에서 제외한다. 아래 세 파일은 원본 내용을 바꾸지 않고 위치와 확장자만 맞춰 복사했다. 리팩터링이나 새 기능 구현은 하지 않았다.

| 보존 원본 | Baseline 복사본 |
| --- | --- |
| `이것이 nrf쪽 개선 코드입니다..txt` | `firmware/nrf52840/bme688_node.ino` |
| `이것이 esp32 와이파이 연결가능 코드입니다..txt` | `firmware/esp32s3/gateway.ino` |
| `구글 시트 웹페이지용 코드입니다..txt` | `cloud/apps_script/Code.gs` |

세 복사본의 SHA-256을 원본과 비교하여 바이트 단위 동일성을 확인했다. 컴파일·하드웨어·클라우드 실행 검증은 이번 작업에서 수행하지 않았다. `git init`, commit, push는 아직 수행하지 않았다.

## Baseline 기술 부채

현재 코드 상태 보존을 위해 ESP32 설정용 AP 비밀번호와 Apps Script URL은 이번 단계에서 변경하지 않았다. 값 자체는 문서에 재기록하지 않는다. `gateway.ino`는 Git 포함 예정 파일이므로 원본 TXT를 제외하더라도 복사본 안의 credential과 endpoint는 남는다.

- ESP32 설정용 AP credential을 추후 별도 config로 분리한다. AP 이름에도 비밀번호 값이 포함된 점을 함께 처리한다.
- Serial에 AP password를 출력하지 않도록 변경한다.
- Apps Script endpoint를 config로 분리한다.
- 현재 ESP32 firmware의 Apps Script endpoint와 최신 배포 endpoint가 서로 다를 가능성이 있으므로 실제 하드웨어 테스트 전에 확인한다. 파일 검사에서 firmware URL과 별도 웹페이지 URL의 배포 ID는 서로 달랐으며, 실제 최신 배포 및 연결된 저장소는 확인하지 않았다.
- `client.setInsecure()`는 prototype 상태이며 추후 TLS 검증 방식을 검토한다.

## 현재 동작 및 제약

| 항목 | 현재 기준 |
| --- | --- |
| BLE 프로토콜 | Nordic UART Service(NUS) |
| 샘플 형식 | `T=xx.xx,H=xx.xx,P=xxxx.xx,G=xxx.xx\n` (`\n`은 실제 줄바꿈) |
| BLE framing | 20-byte fragmentation에 대응하는 chunk + newline framing |
| nRF 센서 측정 주기 | 약 2초 |
| ESP32 Google 업로드 주기 | 30초 |
| 업로드 대상 | 30초 사이 수신한 샘플 중 가장 최신 샘플 하나 |
| BME688 기본 테스트 API | Adafruit_BME680 호환 API |
| heater 설정 | 320°C / 150ms, 기본 센서 동작 테스트용 |
| I2C 및 Wire 기본 핀 | 실제 보드 매핑 확인 전 추측 금지 |
| 측정 데이터 단위 | 임의 변경 금지. 제공된 정보만으로 개별 필드의 단위는 확정하지 않음 |

## 확인된 사항

- nRF52840 dummy BLE 송신 동작 확인
- ESP32-S3 BLE NUS 수신 동작 확인
- BLE 20-byte fragmentation 문제를 chunk + newline framing으로 해결
- ESP32 Wi-Fi 동작 확인
- WiFiManager 기반 설정 코드 컴파일 확인
- ESP32 -> Google Apps Script 업로드를 과거에 실제 확인
- Apps Script Web UI 동작 확인
- Google 다중 계정 `/u/1` 접근 문제 해결

컴파일 확인과 과거 업로드 성공은 최신 통합 코드의 현장 실행 또는 현재 전체 파이프라인의 연속 동작 검증을 의미하지 않는다.

## 아직 실제 하드웨어 및 전체 파이프라인에서 확인되지 않은 사항

- BME688와 nRF52840 실제 I2C 연결
- nRF52840 Wire 기본 SDA/SCL 핀
- 실제 BME688 측정값의 BLE 전달
- 최신 통합 ESP32 코드의 현장 실행
- Google Sheets 연속 저장
- CSV 다운로드 파이프라인
- BME AI-Studio raw-data 수집

## ESP32-S3 Arduino 설정

| 설정 | 값 |
| --- | --- |
| Board | ESP32S3 Dev Module |
| Flash Size | 4MB |
| Partition Scheme | Huge APP (3MB No OTA / 1MB SPIFFS) |
| PSRAM | Disabled |
| USB CDC On Boot | Enabled |
| USB Mode | Hardware CDC and JTAG |

## 개발 및 ML 기준

1. 동작이 확인된 코드는 필요 없이 리팩터링하지 않는다.
2. 변경은 최소 단위로 하고 각 계층을 독립적으로 테스트한다.
3. 습도 임계값을 음식 부패 정답(label)로 사용하지 않는다.
4. ML train/test는 experiment/session 단위로 분리하고 인접한 시계열 샘플을 랜덤 분할하지 않는다.
5. 단위를 임의로 변경하지 않는다.

## 이후 단계와 선행 과제

- ML 데이터 수집 전에 최신 샘플 하나만 업로드하는 방식을 batch/ring-buffer 방식으로 개선해야 한다. 현재는 변경하지 않는다.
- BME AI-Studio/BSEC 및 heater-profile raw acquisition은 이후 단계다. 현재 heater 설정은 이 단계의 수집 프로파일로 확정된 설정이 아니다.
- 미검증 항목은 해당 계층에서 실제 확인한 후 상태를 갱신한다.
