# 구현 기능과 검증 상태

이 문서는 현재 저장소에 실제로 구현된 기능을 한곳에서 찾기 위한 색인이다. 세부 설계와 운용 절차는 링크된 문서를 따르며, 최신 현장 검증 기록은 [`PROJECT_STATE.md`](../PROJECT_STATE.md)를 기준으로 한다.

## 상태 표기

| 표기 | 의미 |
|---|---|
| 구현 | 현재 소스에 기능이 존재한다. |
| 자동 검증 | 호스트 테스트 또는 컴파일로 확인했다. 실제 장치 동작을 뜻하지 않는다. |
| 실기 검증 | 사용자가 실제 장치 또는 운영 클라우드에서 확인했다. |
| 미검증 | 구현은 있으나 현재 소스 조합으로 현장 검증하지 않았다. |
| 미완성 | 준비 코드만 있거나 제품 기능으로 완성되지 않았다. |

## nRF52840 센서 노드

주요 소스: [`firmware/nrf52840/bme688_node/bme688_node.ino`](../firmware/nrf52840/bme688_node/bme688_node.ino), [`Scan.h`](../firmware/nrf52840/bme688_node/Scan.h), [`Battery.h`](../firmware/nrf52840/bme688_node/Battery.h)

| 기능 | 구현 내용 | 검증 상태 |
|---|---|---|
| 보드와 I²C | V1940 Pro Micro nRF52840/nice!nano V2 호환 보드를 Adafruit Feather nRF52840 Express variant로 빌드한다. `Wire.setPins(21, 20)`으로 D21/P0.31 SDA와 D20/P0.29 SCL을 사용한다. | 배선·BME688·OLED 실기 검증 |
| BME688 환경 측정 | 주소 `0x76`, T/H/P oversampling 8X/2X/4X, IIR 3, 히터 OFF forced mode로 약 120초마다 측정한다. 실패는 5초 간격으로 재시도한다. | 이전 2초 주기는 실기 검증. 120초 주기는 자동 검증, 실기 미검증 |
| HP-354 가스 스캔 | Bosch BME68x 고정본과 10단계 parallel heater profile을 사용한다. 현재 소스의 시도 주기는 10분이며 직전 forced T/H/P 성공 후 시작한다. 140 ms polling, 60초 timeout/cooling을 적용한다. | 다중 gas step 수집·클라우드 저장 실기 검증. 현재 주기 조합의 장기 시험은 별도 |
| 무효 측정 처리 | `GASM_VALID=0` dummy 변환은 대기로 처리하고 HEAT_STAB, 유한값, 양수 gas resistance를 만족한 실제 step만 전송한다. 성공한 측정만 sequence를 증가시키며 연속 실패 시 복구/backoff를 수행한다. | 소스·호스트 테스트 검증, 장애 복구는 과거 실기 기록 존재 |
| OLED | 128×32 OLED에 온도·습도·기압, 동작 상태와 배터리 정보를 표시한다. OLED가 없어도 측정과 BLE는 계속한다. | 기본 표시 실기 검증. 최신 통합 배터리 표시는 현장 재검증 필요 |
| 배터리 전압 | 내부 VDDH/5 ADC를 14-bit로 읽어 mV 정수로 관리한다. USB 전원 상태에서는 배터리 전압으로 단정하지 않으며 마지막 runtime 배터리 단독 추정치만 `last`로 표시할 수 있다. | 독립 진단에서 배터리 단독 VDDH와 멀티미터 일치 실기 검증. USB 중 실제 충전율 측정은 미지원 |
| 배터리 잔량 추정 | 4.20–3.50 V 비선형 곡선, 5-sample median과 제한된 자가 보정 상태를 사용한다. `/battery-a.bin`, `/battery-b.bin` 교대 저장에 version, generation, CRC를 둔다. 실제 mAh 측정이나 정밀 SOC가 아니다. | 자동 테스트 통과. 최신 OLED→BLE→클라우드 통합은 실기 미검증 |
| System ON 저전력 | Adafruit core의 FreeRTOS tickless idle/SoftDevice event wait를 재사용하고 variable-latency low-power mode를 명시한다. loop idle은 10 ms이며 ENV 120초/HP-354 10분 주기를 사용한다. | 소스·호스트 검증. 실제 전류/수명 실측 전 |
| Loop liveness 복구 | core Wire의 무제한 TWIM wait 등으로 loop 진행이 120초 멈추면 상위 FreeRTOS software timer가 application reset을 수행한다. DFU bootloader에는 지속되지 않는다. | 소스·호스트 정책 검증. 4,000초 현상의 정확한 원인 및 실기 복구는 미검증 |
| BLE NUS | Nordic UART Service로 한 줄 프레임을 최대 20-byte chunk로 나누고 newline으로 종료한다. 연결 해제 후 재광고·재연결한다. | 실기 검증 |
| 펌웨어 식별 | 부팅 로그와 Device Information Service/진단 상태에서 센서 펌웨어 버전을 확인할 수 있다. | 구현 및 DFU 후 버전 확인 경로 존재 |

배터리 계산의 한계와 보정 상태는 [`BATTERY_ESTIMATION.md`](BATTERY_ESTIMATION.md), 환경/가스 상태 머신은 [`ENVIRONMENT_AND_GAS.md`](ENVIRONMENT_AND_GAS.md)와 [`GAS_SCAN_V3.md`](GAS_SCAN_V3.md)를 참고한다.

## BLE 데이터 형식

- 환경 프레임: `ENV,BID=...,SEQ=...,MS=...,T=...,H=...,P=...`
- 가스 v3 프레임: `V=3,BID=...,SEQ=...,SCAN=...,STEP=...,GI=...,MI=...,MS=...,ST=...,PID=354,T=...,H=...,P=...,G=...`
- 진단 `STAT` 프레임: 센서 상태와 `BV`, `BOK`, `USB`, `BP`, `BS`, 보정 범위, 선택적 측정 uptime `BM`, USB 중 선택적 마지막 추정치 `BLP`를 전달한다.

환경 프레임에 가짜 `G=0`이나 `STEP`을 추가하지 않는다. ESP32 parser는 배터리 확장 필드가 없는 구버전 프레임도 허용한다. BLE framing과 v2/v3 호환 규칙은 [`ARCHITECTURE.md`](ARCHITECTURE.md)에 정리되어 있다.

## ESP32-S3 게이트웨이

주요 소스: [`firmware/esp32s3/gateway/gateway.ino`](../firmware/esp32s3/gateway/gateway.ino), [`Integrity.h`](../firmware/esp32s3/gateway/Integrity.h), [`Spool.h`](../firmware/esp32s3/gateway/Spool.h), [`GatewayUi.h`](../firmware/esp32s3/gateway/GatewayUi.h)

| 기능 | 구현 내용 | 검증 상태 |
|---|---|---|
| BLE 수신 | NUS scan/connect/subscribe, newline 재조립, v2/v3/ENV/STAT parsing, 재연결을 수행한다. 명시적 sensor ID pairing으로 다른 센서 연결을 막을 수 있다. | 단일 장치 실기 검증 |
| Wi-Fi 자동 복구 | 최대 네 저장 슬롯, 마지막 성공 슬롯 우선, event 기반 연결 종료 후 failover, 제한된 retry/backoff를 사용한다. 연결된 SSID를 불필요하게 끊지 않으며 IP와 `foodmonitor.local`을 제공한다. | 자동 테스트 통과. 실제 네트워크 조건별 장기 검증 일부 미완료 |
| 최신 상태 heartbeat | 실제 sample이 없을 때 약 5초마다 최신 environment/battery health를 전송한다. data upload를 우선하며 heartbeat는 spool이나 환경·가스 sample을 만들지 않는다. battery health만 장치별 최대 5분 간격으로 별도 이력화한다. | 호스트·클라우드 테스트 통과. 운영 배포 버전 일치 여부 확인 필요 |
| HTTPS 업로드 | JSON POST, redirect GET, application ACK parsing과 제한된 retry를 수행한다. UID가 ACK되기 전 sample을 제거하지 않는다. | 운영 ACK·재전송 실기 검증 |
| PSRAM 메모리 관리 | 직렬화된 큰 HTTP JSON을 exact-size PSRAM buffer로 옮기고 내부 임시 buffer를 TLS 전에 해제한다. 실패 시 내부 RAM fallback을 사용하며 내부/PSRAM heap 진단을 남긴다. BLE queue, RTOS 객체와 TLS 필수 객체는 내부 RAM에 둔다. | 자동 테스트 통과. 현재 최종 빌드의 장시간 TLS 실기 검증 필요 |
| 로컬 표시 | LCD와 GPIO 48 NeoPixel을 사용한다. green은 정상, yellow는 backlog/실제 data transfer 대기, red는 BLE 단절, purple은 Wi-Fi 단절, blue는 boot/update, rainbow는 검증된 OTA 부팅 완료를 뜻한다. health-only POST는 경고 점멸을 만들지 않는다. | 기본 LED 동작 실기 기록 존재. 최신 정책 재검증 필요 |
| 진단 | boot/build ID, Wi-Fi 전이, queue/spool, ACK, TLS heap, OTA/DFU 상태를 시리얼과 로컬 관리 페이지에 노출한다. 비밀 token은 일반 상태 응답에 출력하지 않는다. | 구현·테스트 |

## 데이터 보존과 무결성

주요 소스: [`Spool.h`](../firmware/esp32s3/gateway/Spool.h), [`Integrity.h`](../firmware/esp32s3/gateway/Integrity.h), [`docs/RELIABILITY.md`](RELIABILITY.md)

- RAM queue는 최대 128 samples, cloud batch는 최대 32 samples다.
- SPIFFS는 자동 format 없이 마운트한다. 최대 128 spool files를 FIFO로 관리하고 `.tmp` write/readback/CRC 검증 후 rename한다.
- legacy v2 spool record는 88 bytes, 현재 v3 record는 120 bytes다. v2를 v3로 재해석하거나 ACK 전에 삭제하지 않는다.
- sample UID는 gateway boot ID와 receive sequence를 포함하며 retry와 reboot replay에서 유지된다.
- 정확한 application ACK를 받은 sample만 queue/spool에서 해제한다. cloud는 UID dedup과 gateway boot high-water를 함께 사용한다.
- queue/spool overflow, filesystem fault와 불완전 scan은 조용히 덮어쓰지 않고 오류 또는 missing data로 남긴다.
- 환경 최신값과 `EnvironmentHistory`는 분리된다. 120초 ENV 전체를 Sheets 행이나 spool record로 만들지 않으며 기존 5분 gate를 지난 첫 프레임만 저장한다.

이 보존 경로는 실제 SPIFFS 재부팅 복원, backlog drain, cloud ACK로 실기 검증된 기록이 있다. 현재 소스의 모든 failure 조합을 장시간 검증한 것은 아니다.

## Google Apps Script, Sheets와 대시보드

주요 소스: [`cloud/apps_script/Code.gs`](../cloud/apps_script/Code.gs), [`Dashboard.html`](../cloud/apps_script/Dashboard.html)

| 기능 | 구현 내용 | 검증 상태 |
|---|---|---|
| ingest API | schema 2 legacy, schema 3 gas step, schema 4 environment와 health payload를 처리한다. | 자동 테스트 및 운영 저장 실기 검증 |
| Sheets 저장 | `SensorData`, `EnvironmentHistory`, `BatteryHistory`, `Unassigned`, `Sessions`, `IngestState`, `DeviceRegistry`를 사용한다. 기존 SensorData 첫 12열과 과거 행을 보존한다. BatteryHistory는 장치별 최대 5분 간격이다. | 기존 저장은 운영 실기 검증. BatteryHistory는 mock Sheets 검증, 운영 미배포 |
| transaction/ACK | ScriptLock과 write-ahead journal recovery 후에 ACK한다. duplicate UID, high-water와 재전송을 처리한다. | 동시성 mock 테스트 통과, 운영 ACK 실기 검증 |
| 빠른 health 경로 | 빈 `samples` heartbeat에서 무거운 spreadsheet/journal 작업을 피하면서 per-device `LIVE_V2`를 갱신한다. dashboard lock 충돌 시 recording 상태를 추측하지 않는다. | 자동 테스트 통과. 최신 Apps Script 배포 여부는 별도 확인 필요 |
| 대시보드 | 5초 polling, 장치별 카드, 환경/가스/배터리/연결/recording 상태, 배터리 24시간·7일·전체 그래프, 로컬 OTA/DFU 링크를 제공한다. stale gas와 storage fault/overflow/실제 queue loss를 구분한다. | 단일 장치 운영 실기 검증. 최신 battery/integrity UI는 mock 검증, 운영 미배포 |
| 실험 세션 | 장치별 START/STOP과 capture-time `[start, stop)` 경계로 sample을 배정한다. 시각이 불명확하면 `Unassigned`에 보존한다. | 운영 실기 검증 |
| CSV | 최근/현재 session과 장치 전체 데이터를 UTF-8 CSV로 내려받는다. Unicode 실험명은 본문에 유지하고 파일명은 안전한 ASCII fallback을 사용한다. 10,000행 limit과 offset pagination을 유지한다. | 자동 회귀 테스트 통과. 최신 코드 배포 필요 |

## ESP32 Wi-Fi OTA

주요 소스/문서: [`gateway.ino`](../firmware/esp32s3/gateway/gateway.ino), [`firmware/esp32s3/gateway/partitions.csv`](../firmware/esp32s3/gateway/partitions.csv), [`ESP32_WIFI_OTA_STAGE2.md`](ESP32_WIFI_OTA_STAGE2.md)

- 16 MB flash에서 `otadata`, 3 MiB `app0`, 3 MiB `app1`, 기존 NVS와 SPIFFS 주소를 유지한다.
- 로컬 port 80 관리 페이지, Basic authentication, Host/Origin 검사와 명시적 사용자 업로드를 사용한다.
- manifest의 target/version/build, image size/SHA-256과 ECDSA P-256 signature를 검증하고 anti-rollback을 적용한다.
- `NORMAL → OTA_PREPARE → OTA_ACTIVE`로 전환한다. recording STOPPED, 기존 queue/spool drain과 HTTP 종료를 확인하고 새 수집을 제어한다.
- 수신 byte 수, hash와 `Update.write()`를 모두 확인한 뒤 `Update.end(true)`를 호출한다.
- 새 image boot self-check와 rollback 해제 후에만 성공으로 확정하고 약 3초 rainbow를 표시한다.

서명된 Wi-Fi OTA는 사용자 실기 검증을 통과했다. 현재 최종 소스와 새 binary의 설치 성공은 binary별로 다시 확인해야 한다. signing key와 token은 저장소 문서나 로그에 기록하지 않는다.

## nRF52840 업데이트

문서: [`NRF52840_BLE_DFU.md`](NRF52840_BLE_DFU.md)

두 업데이트 경로는 application-only Legacy DFU 0.5를 사용하며 bootloader/SoftDevice를 바꾸지 않는다.

1. **스마트폰 직접 DFU:** Nordic DFU 앱에서 검증된 ZIP을 사용한다. 구형 bootloader 호환을 위해 MTU 23과 낮은 PRN 설정을 사용한 실기 성공 기록이 있다.
2. **ESP32 DFU relay:** 인증된 `/nrf-dfu` 페이지에서 ZIP을 임시 경로로 streaming upload하고 CRC, ZIP 구조, device type `0x0052`, SoftDevice requirement `0x00B6`를 검증한 뒤 `/nrf-dfu.zip`으로 교체한다. 명시적으로 paired sensor만 대상으로 하며 upload만으로 시작하지 않는다. 20-byte payload와 PRN 8을 유지하고 bootloader scan, control/packet transfer, activation, NUS 재연결과 firmware version 확인을 수행한다.

ESP32→nRF relay는 사용자 실기 성공이 보고되었다. 전원 차단, 모든 BLE 간섭 조건과 반복 업데이트에 대한 장기 검증은 남아 있다. 실패 시 USB bootloader와 Arduino IDE upload가 복구 경로다.

## 다중 기기 지원 범위

문서: [`MULTI_DEVICE_STAGE1.md`](MULTI_DEVICE_STAGE1.md)

- gateway는 eFuse MAC 기반의 재부팅 후 유지되는 `device_id`를 사용하고 boot별 `gatewayBoot`은 ACK identity에 별도로 유지한다.
- nRF `sensor_id`와 BLE address를 구분하며 NVS pairing으로 각 gateway의 대상을 고정할 수 있다.
- Apps Script recording, LIVE cache, registry, sessions, dashboard card와 CSV filter는 device별로 분리된다.
- ID가 없던 과거 행은 임의의 새 device에 귀속하지 않고 `legacy`로 취급한다.
- 서로 다른 두 가상 device ID에 대한 서버/UI 분리 테스트는 존재한다.

2호기 실물이 없으므로 두 ESP32와 두 nRF의 동시 BLE/Wi-Fi/Sheets 운용은 **실기 미검증**이다. `MULTI_DEVICE_STAGE1.md`의 OTA/DFU 제외 설명은 당시 단계 기록이며 현재 기능 상태는 이 문서와 `PROJECT_STATE.md`를 따른다.

## 자동 테스트와 진단 자산

- Cloud API/session/CSV/concurrency: `tests/cloud.test.cjs`
- battery curve/persistence/calibration: `tests/battery_test.cpp`
- BLE source/protocol/integrity/spool/UI/Wi-Fi recovery: `tests/*`
- nRF Legacy DFU package/protocol/upload/web: `tests/*nrf*dfu*`
- ESP OTA partition/signing/package: `tests/*ota*`
- gas scan completeness: [`analysis/fingerprints.py`](../analysis/fingerprints.py)와 `tests/fingerprint_test.py`
- 독립 VDDH 진단 스케치와 절차: [`V1940_BATTERY_ADC_DIAGNOSTIC.md`](V1940_BATTERY_ADC_DIAGNOSTIC.md)
- System ON/OFF, BLE, heater, DC/DC와 PPK2 절차: [`NRF52840_LOW_POWER.md`](NRF52840_LOW_POWER.md)

호스트 테스트와 compile 성공은 실제 BLE radio, Wi-Fi AP, TLS heap, flash wear, battery discharge 및 cloud deployment 검증을 대신하지 않는다.

## AI 학습 준비와 미완성 범위

[`analysis/fingerprints.py`](../analysis/fingerprints.py)는 `(session, nrf_boot, scan_seq, profile_id)`로 0..9 gas step의 중복·누락·순서를 확인하고 complete scan만 특징 생성 대상으로 분류한다. CSV/session export와 experiment label metadata가 있어 데이터셋 준비는 가능하다.

현재 저장소에는 검증된 부패 판정 모델, 배포된 inference, 안전/부패 LED 판정이 없다. [`mltest.ipynb`](../mltest.ipynb)는 역사적 탐색 예제다. `experiment_label`은 정답 label이 아니며 습도 threshold로 정답을 만들지 않는다. 실제 학습에서는 인접 행 random split 대신 experiment/session 단위 train/test 분리를 사용해야 한다.

## 사용 전 확인 사항

1. build/flash/partition 설정은 [`PROJECT_STATE.md`](../PROJECT_STATE.md)와 OTA 문서의 현재 값을 확인한다.
2. Apps Script 변경은 새 deployment version으로 반영해야 하며 local source 변경만으로 운영 dashboard가 바뀌지 않는다.
3. OTA/DFU 전에 recording STOPPED, queue/spool/HTTP 상태와 대상 device/sensor pairing을 확인한다.
4. 측정값과 추정 배터리 잔량을 식품 섭취 안전이나 정밀 배터리 용량 판정으로 사용하지 않는다.
