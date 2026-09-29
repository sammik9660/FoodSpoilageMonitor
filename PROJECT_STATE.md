# 프로젝트 현재 상태

갱신일: 2026-09-29. reliability v2 구현 및 자동 검증을 완료했으며, 장치/클라우드 배포와 장기 운전 검증은 아직 수행하지 않았다. 기존 baseline 및 과거 리뷰 기록은 보존했다.

## 사용자 보고로 확인된 실제 하드웨어 상태

- BME688 -> nRF52840 -> BLE NUS -> ESP32-S3 -> Wi-Fi(phone hotspot) -> Apps Script -> Google Sheets end-to-end 성공.
- V1940 Pro Micro nRF52840 / nice!nano V2 compatible clone, `Adafruit Feather nRF52840 Express` variant.
- `Wire.setPins(21,20)`: D21 -> P0.31 SDA, D20 -> P0.29 SCL. I2C 0x76 ACK 및 register 0xD0 chip ID 0x61 확인.
- T/H/P oversampling 8X/2X/4X, IIR 3, heater 320°C/150ms. 약 2초 측정 목표.
- ESP32-S3: ESP32S3 Dev Module / 4MB / Huge APP (3MB No OTA / 1MB SPIFFS) / PSRAM Disabled / USB CDC On Boot Enabled / Hardware CDC and JTAG.
- 과거 dummy BLE, fragmentation 해결, WiFiManager compile, Web UI와 Google 다중계정 접근 검증 기록은 historical reviews를 참조한다.

## 2026-09-28 BME688 incident — historical note

정상 측정은 대략 25.91°C / 50.93% / 1004.04hPa / 71.63kΩ였다. 이후 T=31.61, H=100.00, P=735.46, G=0.00으로 급변했다. `performReading()`이 반복 실패했지만 구 펌웨어는 메모리에 남은 값을 계속 전송하여 invalid rows가 Google Sheets에 도달했다.

사용자 하드웨어 진단 결과:

- I2C address 0x76 ACK: OK
- BME68x chip ID 0x61: OK
- reset/reinitialization 후 T/H/P-only 측정: 10회 정상
- gas heater 320°C / 150ms 측정: 27회 이상 연속 정상
- sensor hardware 손상은 확인되지 않았다. reset/reinitialization으로 동작이 복구되었다.

입증된 것은 관찰된 실패 모드와 복구 성공이다. 정확한 root cause(전원, 접촉, 센서 내부 상태 등)는 입증되지 않았다. 따라서 failed reading을 거부하고 자동 recovery/backoff를 유지해야 한다.

사용자 보고상 2026-09-28 21:34:59 이후 반복된 31.61 / 100 / 735.46 / 0 구간은 stale-data bug의 invalid data다. 이번 작업은 기존 Google Sheets 행을 삭제하거나 수정하지 않는다. 구간 확인·정리는 사용자에게 맡긴다.

## 현재 구현 변경

- nRF: 올바른 Adafruit API, 검증된 핀/주소/측정 설정 유지, 실패 시 frame/sequence 생성 금지, 연속 3회 실패 검출, soft reset+재초기화, 5~60초 backoff, 성공 측정으로 recovery 확인.
- BLE v2: `BID=XXXXXXXX,SEQ=n,MS=n,T=...,H=...,P=...,G=...` 및 독립 STAT 진단 프레임. NUS/chunk/newline 유지.
- ESP: strict parser/프레임 재동기화, boot UUID+receive sequence UID, 128개 RAM queue, 최대 32개 batch, 기본 10초 upload, 정확한 application ACK 이후 해제, 10~60초 retry.
- SPIFFS: 기존 partition 사용, CRC 검증 immutable chunk, 재부팅 FIFO replay, 최대 128 chunks/동적 byte budget, ACK 이후 파일 회수. 자동 format/미전송 데이터 overwrite 없음.
- 시간: ESP receive 시 UTC(NTP가 유효한 경우)와 uptime 보존. unknown time은 Unassigned에 저장하여 잘못된 session 배정 방지.
- Apps Script: 기존 SPREADSHEET_ID 필수, SensorData 비파괴 12열 확장, Sessions/IngestState/Unassigned 보조 시트, lock, bulk write, write-ahead journal, boot별 high-water dedup.
- recording 기본 STOPPED. Start/Stop [start,stop) 구간을 capture time으로 비교. legacy short/long single POST 지원(서버 시각이라는 제한 명시).
- Dashboard: 10초 polling, freshness/오류/recording/큐 진단, absolute deployed URL의 CSV와 session filter. Dashboard.html 추가.
- ESP 중요 로그는 USB CDC Serial과 UART Serial0에 전달. credential 출력 제거. 전력 관련 변경은 짧은 yield와 연결 LED 억제에 한정한다.

## 검증 및 한계

실제로 수행한 빌드/자동 테스트 결과와 정확한 capacity 계산, 수동 업로드 순서는 `docs/RELIABILITY.md`에 기록한다. 이번 개선 버전을 실제 보드에 업로드하거나 Apps Script에 배포해서 E2E/장기 시험한 것은 아니다.

- nRF Feather52840 빌드 성공: flash 144,764 / 815,104 bytes, static RAM 15,984 / 237,568 bytes.
- ESP32-S3 지정 설정 빌드 성공: flash 1,428,505 / 3,145,728 bytes, static RAM 55,884 / 327,680 bytes. 태스크/큐 등 runtime heap은 이 static RAM 수치에 포함되지 않는다.
- Code.gs/Dashboard 실제 코드에 대한 Node 모의 테스트 15개 통과. 중복, recording 경계, 저장 실패·journal 복구, legacy POST, CSV 및 UI JS 포함.
- C++17/MSVC 테스트 3개 실행 성공: parser/framer/sequence/ACK gate, 실제 nRF 소스의 실패·복구 주입, 실제 spool의 재부팅/CRC/용량/손상 처리. 실제 전기적 장애·SPIFFS power-cut 테스트는 아니다.

- 진단 스케치 2개, 원본 TXT, historical review, 기존 notebook은 수정하지 않았다.
- 기존 endpoint와 AP 설정 값은 보존했다. TLS `setInsecure()`와 서버 배포 권한/Start·Stop 접근 제어는 남은 보안 부채다.
- filesystem mount/손상 시 자동 복구로 데이터를 버리지 않고 fault 상태로 중지한다. virgin SPIFFS 최초 준비가 필요할 수 있다.
- 실제 flash filesystem capacity/쓰기 지연, 재부팅·전원차단, RAM overflow, BLE reconnect, NTP/session 경계, CSV 다운로드는 현장 검증 필요.
- 보드 전력과 heater 안정 상태, 정확한 실패 원인은 미검증이다. BSEC/AI-Studio raw acquisition은 이번 범위가 아니다.
- 기존 baseline commit 90e5cc78ead2af1474672beb6b20956ad87cc9b7 및 historical review는 재작성하지 않는다.
