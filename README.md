# Food Spoilage Monitor

BME688 기반 음식 상태 측정 프로젝트. 현재 작업 트리는 히터 OFF 환경 측정(기본 2초)과 HP-354 가스 스캔(기본 10분)을 분리한다. **컴파일·호스트 모의 테스트와 실제 하드웨어/클라우드 검증은 구분한다.** 장치/클라우드 확인 결과는 `PROJECT_STATE.md`에 기록한다.

## 핵심 기능

- nRF52840의 BME688 환경 측정, HP-354 10-step 가스 스캔, OLED와 VDDH 기반 배터리 추정
- BLE NUS 20-byte framing과 ESP32-S3 gateway의 재연결·device pairing
- CRC SPIFFS spool, 안정적인 sample UID, application ACK 이후 해제와 reboot replay
- Google Apps Script/Sheets ingest, 장치별 실험 session, dashboard와 paginated CSV
- 서명·anti-rollback을 적용한 ESP32 Wi-Fi OTA와 nRF52840 Legacy DFU relay
- 다중 장치 식별·session/UI 분리와 HP-354 학습 데이터 준비 도구

구현 범위와 검증 상태는 [기능 목록](docs/FEATURES.md), 전체 data/update 흐름은 [시스템 아키텍처](docs/ARCHITECTURE.md)에서 확인한다. 실제 부패 판정 AI는 아직 구현·검증되지 않았다.

ESP32-S3 현재 빌드 설정은 **Flash Size 16MB / sketch custom OTA partition / OPI PSRAM / USB CDC On Boot Enabled / Hardware CDC and JTAG / Erase All Flash Disabled**다. custom table은 기존 SPIFFS `0x310000`/`0xE0000`과 NVS를 유지한다. 파티션 선택을 바꾸면 미전송 spool 접근성이 달라질 수 있으므로 그대로 유지한다.

```text
BME688 -> nRF52840 -> BLE Nordic UART Service -> ESP32-S3
       -> Wi-Fi / smartphone hotspot -> Google Apps Script
       -> Google Sheets / CSV -> Colab / ML
```

## 파일 구조

```text
firmware/nrf52840/bme688_node/bme688_node.ino
firmware/nrf52840/bme688_node/Battery.h
firmware/nrf52840/bme688_node/Scan.h
firmware/esp32s3/gateway/gateway.ino
firmware/esp32s3/gateway/Integrity.h
firmware/esp32s3/gateway/Spool.h
cloud/apps_script/Code.gs
cloud/apps_script/Dashboard.html
tests/                         # 실제 코드에 대한 호스트 테스트와 mock
analysis/fingerprints.py        # CSV complete/incomplete scan 판정 (모델 아님)
mltest.ipynb                    # 기존 수동 POST/CSV/그래프 탐색 예제
docs/RELIABILITY.md             # 프로토콜, 한계, 검증, 수동 배포 절차
docs/GAS_SCAN_V3.md              # Bosch 근거, v3 프로토콜/마이그레이션/검증 절차
docs/ENVIRONMENT_AND_GAS.md       # 환경 측정/저빈도 스캔, schema 4 및 남은 현장 검증
docs/NRF52840_BLE_DFU.md           # 기존 Adafruit 부트로더를 이용한 application-only BLE DFU
docs/BATTERY_ESTIMATION.md          # VDDH 기반 추정, 제한 보정, 저장 및 실기 검증
docs/PSRAM_AND_TECH_DEBT.md         # ESP 내부 RAM/PSRAM 할당 근거와 남은 기술 부채
docs/FEATURES.md                      # 현재 구현 기능, 사용법과 검증 상태
docs/ARCHITECTURE.md                  # 시스템 구조, data/protocol/update 흐름
firmware/nrf52840/bme688_node/src/Bosch-BME68x-Library/ # 스케치 내 Bosch 공식 드라이버 고정본
data/                          # Git 제외 실험 데이터
```

기존 진단 스케치, 루트 원본 TXT와 과거 리뷰는 보존한다. 진단 스케치는 production firmware와 별개다. 빈 디렉터리는 Git이 추적하지 않는다.

## 현재 구현 목표

- 검증된 I2C D21/D20, 0x76 및 OLED는 유지한다. 평상시 Bosch forced-mode T/H/P 측정은 히터 OFF, 10분마다 HP-354 parallel mode를 사용한다. 과거 320°C/150ms·약 2초 고정 히터 측정은 v2 방식이다.
- 실패한 BME688 step은 누락으로 처리하며 stale frame/valid sequence를 만들지 않는다. 연속 실패 시 복구와 backoff를 수행한다.
- NUS 20-byte chunk/newline framing을 유지한다. 환경 프레임은 `ENV` T/H/P만 보내며, v3는 한 heater step당 BID/SEQ/SCAN/STEP/GI/MI/ST/PID/T/H/P/G 프레임 하나다.
- ESP는 v2/v3 수신과 v2 미전송 스풀 replay를 지원한다. 데이터 배치를 우선하며, 빈 상태에서는 약 5초 health heartbeat로 최신 환경·배터리 상태만 갱신한다. heartbeat는 이력 행이나 spool을 만들지 않고 데이터 sample은 application ACK 이후에만 해제한다.
- recording 기본 STOPPED. Start/Stop과 capture-time session 구간을 사용하며 unknown-time 샘플은 Unassigned에 저장한다.
- 기존 SensorData 첫 12열과 과거 행을 보존한다. 환경 이력은 5분마다 별도 `EnvironmentHistory` 시트에 저장한다.
- CSV는 실제 배포 URL을 사용한다. 10,000행을 넘는 자료는 명시적인 offset/limit 페이지로 내려받는다.

## 먼저 읽을 문서

- [docs/FEATURES.md](docs/FEATURES.md): 실제 구현 기능, 관련 source, 사용 방법과 검증 상태
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): sensor→BLE→gateway→cloud 구조, protocol과 OTA/DFU flow
- [PROJECT_STATE.md](PROJECT_STATE.md): 사용자 검증 상태, 2026-09-28 실제 센서 장애, 현재 구현 및 남은 검증
- [docs/GAS_SCAN_V3.md](docs/GAS_SCAN_V3.md): Bosch 근거, v3 프로토콜·스풀/Sheets 전환·수동 검증 순서
- [docs/ENVIRONMENT_AND_GAS.md](docs/ENVIRONMENT_AND_GAS.md): 현재 환경/가스 모드, 실시간 표시·이력 분리, 남은 현장 검증
- [docs/NRF52840_BLE_DFU.md](docs/NRF52840_BLE_DFU.md): nRF application-only BLE DFU 패키지, 스마트폰 절차와 복구 한계
- [docs/BATTERY_ESTIMATION.md](docs/BATTERY_ESTIMATION.md): 배터리 추정의 근거·제한, 학습 상태, 저장 정책과 실기 절차
- [docs/PSRAM_AND_TECH_DEBT.md](docs/PSRAM_AND_TECH_DEBT.md): PSRAM 할당 범위, 메모리 진단과 남은 통신·보안 검증
- [docs/RELIABILITY.md](docs/RELIABILITY.md): reliability v2 설계와 당시 검증 기록
- [AGENTS.md](AGENTS.md): hardware mapping과 Data Integrity Invariants
- [BASELINE_CODE_REVIEW_2026-09-25.md](BASELINE_CODE_REVIEW_2026-09-25.md): 수정하지 않는 과거 baseline 리뷰

측정값을 식품 섭취 안전 판정으로 취급하지 않는다. 습도 threshold로 부패 정답을 만들지 않으며 train/test는 experiment/session으로 분리한다. 기존 endpoint/AP 설정과 prototype TLS 정책은 보존되어 있으므로 외부 공개 전 보안 부채를 확인한다.
