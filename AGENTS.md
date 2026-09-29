# 개발 규칙

BME688 Food Spoilage Monitor의 단일 저장소다. 작업 전에 `PROJECT_STATE.md`와 `docs/RELIABILITY.md`를 읽고 사용자 보고의 하드웨어 검증, 자동 테스트, 아직 하지 않은 현장 검증을 구분한다.

## 변경 및 검증

- 동작하는 코드를 이유 없이 리팩터링하거나 요청하지 않은 기능을 구현하지 않는다. 변경과 검증은 계층별로 최소 단위로 수행한다.
- 기존 원본 TXT, 진단 스케치, 과거 리뷰는 사용자 요청 없이 수정·이동·삭제하지 않는다. 과거 리뷰를 현재 구현에 맞춰 고쳐 쓰지 않는다.
- 실행하지 않은 컴파일·하드웨어·클라우드 검증을 완료로 보고하지 않는다. 상태 변화는 `PROJECT_STATE.md`에 기록한다.
- 명시적 요청 없이 commit/push, 장치 업로드, Apps Script 배포, 실제 Sheets 데이터 수정·삭제를 하지 않는다.

## Hardware / Protocol

- 사용자 검증: V1940 Pro Micro nRF52840 / nice!nano V2 compatible clone, Arduino `Adafruit Feather nRF52840 Express` variant.
- 검증한 `Wire.setPins(21, 20)` 유지: Arduino D21 -> P0.31 SDA, D20 -> P0.29 SCL. Arduino pin 인자로 31/29 또는 031/029를 추측해서 사용하지 않는다.
- BME688 주소 0x76, chip ID 0x61. `Adafruit_BME680(&Wire)`와 `begin(address, bool initSettings)` API를 구분한다.
- T/H/P oversampling 8X/2X/4X, IIR 3, heater 320°C/150ms, 약 2초 측정 목표를 유지한다. BSEC/AI-Studio/heater-profile 전환은 별도 단계다.
- BLE NUS, 20-byte chunk + newline framing을 유지한다. v2 sample은 BID/SEQ/MS/T/H/P/G이며 STAT 프레임은 진단용이다. chunk를 완전한 샘플로 가정하지 않는다.
- ESP32-S3 4MB, Huge APP (3MB No OTA / 1MB SPIFFS), PSRAM disabled, CDC enabled, Hardware CDC/JTAG를 유지한다. 실제 SPIFFS partition은 현재 core에서 0xE0000(896KiB)다.
- 실제 endpoint 및 AP credential은 현재 값 보존 요청을 따른다. 민감한 값을 로그·문서·보고서에 재출력하지 않는다. `setInsecure()`는 아직 prototype 기술 부채다.

## Reliability / Data Integrity Invariants

이 규칙은 구현 취향이 아니다. 2026-09-28 실제 BME688 연속 읽기 실패와 reset/reinitialization 복구에서 나온 요구사항이다. 정확한 고장 원인은 입증되지 않았다.

1. `performReading()` 실패 후 메모리에 남은 T/H/P/G를 절대 전송·저장하지 않는다. 실패한 측정은 유효값이 아니라 missing sample이다.
2. valid sensor sequence는 성공한 측정에만 증가한다. 정상 운전이 잘 된다는 이유로 연속 실패 검출·recovery·backoff를 제거하지 않는다.
3. 유효한 application-level cloud ACK 전에 pending 샘플을 제거하지 않는다. retry/reboot replay는 같은 sample UID를 유지하며 cloud dedup과 함께 검증한다.
4. recording session은 capture time으로 배정한다. HTTP arrival time으로 지난 샘플을 새 실험에 넣지 않는다. 시각 미확정 샘플은 명시적으로 Unassigned에 보존한다.
5. 장치 재부팅·설정 누락 시 기존 SensorData/Spreadsheet를 조용히 초기화·재생성하지 않는다. migration은 과거 행을 보존해야 한다.
6. silent corruption보다 명시적 missing sample/error가 낫다. queue/spool overflow와 filesystem 손상을 숨기거나 미확인 데이터를 덮어쓰지 않는다. filesystem 자동 format을 켜지 않는다.

## 데이터 및 ML

- T/H/P/G 단위는 °C / %RH / hPa / kΩ다. 임의 변경하지 않는다.
- 습도 threshold로 부패 정답을 만들지 않는다. experiment_label은 실험 메타데이터이며 자동으로 검증된 부패 정답이 아니다.
- train/test는 experiment/session으로 분리한다. 인접 시계열 랜덤 분할을 금지한다. 기존 `mltest.ipynb`는 역사적 탐색 예제로, 검증된 학습 파이프라인으로 취급하지 않는다.
- 실험 데이터는 `data/`에 두고 Git에서 제외한다. credential/secret은 새 소스·문서에 추가하지 않는다.
- 빌드/테스트와 수동 배포 절차는 `docs/RELIABILITY.md`를 따른다. 단위/호스트 테스트를 실제 장치 장기 시험으로 표현하지 않는다.
