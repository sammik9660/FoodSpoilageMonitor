# Food Spoilage Monitor

BME688 기반 음식 상태 측정 프로젝트. 현재 저장소의 reliability v2 구현은 장기 수집을 위한 신뢰성 개선 버전이며, **컴파일·호스트 모의 테스트 통과와 실제 하드웨어 장기 검증은 구분한다.** 새 버전의 장치 업로드/Apps Script 배포는 사용자가 수행해야 한다.

```text
BME688 -> nRF52840 -> BLE Nordic UART Service -> ESP32-S3
       -> Wi-Fi / smartphone hotspot -> Google Apps Script
       -> Google Sheets / CSV -> Colab / ML
```

## 파일 구조

```text
firmware/nrf52840/bme688_node/bme688_node.ino
firmware/esp32s3/gateway/gateway.ino
firmware/esp32s3/gateway/Integrity.h
firmware/esp32s3/gateway/Spool.h
cloud/apps_script/Code.gs
cloud/apps_script/Dashboard.html
tests/                         # 실제 코드에 대한 호스트 테스트와 mock
analysis/                      # 향후 분석 코드
mltest.ipynb                    # 기존 수동 POST/CSV/그래프 탐색 예제
docs/RELIABILITY.md             # 프로토콜, 한계, 검증, 수동 배포 절차
data/                          # Git 제외 실험 데이터
```

기존 진단 스케치, 루트 원본 TXT와 과거 리뷰는 보존한다. 진단 스케치는 production firmware와 별개다. 빈 디렉터리는 Git이 추적하지 않는다.

## 현재 동작

- 검증된 I2C D21/D20, 0x76, heater 320°C/150ms와 약 2초 측정 목표를 유지한다.
- 실패한 BME688 측정은 누락으로 처리하며 stale frame/valid sequence를 만들지 않는다. 연속 실패 시 복구와 backoff를 수행한다.
- NUS chunk/newline framing에 BID/SEQ/MS를 추가했다. nRF와 ESP v2를 함께 배포해야 한다.
- ESP는 모든 수신 valid sample을 큐에 넣고 약 10초마다 배치 업로드한다. application ACK 이후에만 해제하며 outage spool은 제한된 임시 저장소다.
- recording 기본 STOPPED. Start/Stop과 capture-time session 구간을 사용하며 unknown-time 샘플은 Unassigned에 저장한다.
- 기존 SensorData 첫 5열과 과거 행은 보존한다. 세션/UID 등의 열과 보조 시트를 추가한다.
- CSV는 실제 배포 URL을 사용한다. 10,000행을 넘는 자료는 명시적인 offset/limit 페이지로 내려받는다.

## 먼저 읽을 문서

- [PROJECT_STATE.md](PROJECT_STATE.md): 사용자 검증 상태, 2026-09-28 실제 센서 장애, 현재 구현 및 남은 검증
- [docs/RELIABILITY.md](docs/RELIABILITY.md): 정확한 프로토콜·ACK·spool 용량/손실 한계, 빌드·테스트, 수동 배포 순서
- [AGENTS.md](AGENTS.md): hardware mapping과 Data Integrity Invariants
- [BASELINE_CODE_REVIEW_2026-09-25.md](BASELINE_CODE_REVIEW_2026-09-25.md): 수정하지 않는 과거 baseline 리뷰

측정값을 식품 섭취 안전 판정으로 취급하지 않는다. 습도 threshold로 부패 정답을 만들지 않으며 train/test는 experiment/session으로 분리한다. 기존 endpoint/AP 설정과 prototype TLS 정책은 보존되어 있으므로 외부 공개 전 보안 부채를 확인한다.
