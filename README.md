# Food Spoilage Monitor

BME688 기반 음식 부패 모니터링 프로젝트의 단일 저장소다.

## 시스템 구성

```text
BME688 -> nRF52840 -> BLE Nordic UART Service -> ESP32-S3
       -> Wi-Fi / smartphone hotspot -> Google Apps Script
       -> Google Sheets / CSV -> Colab / ML
```

## 디렉터리 구조

```text
firmware/
  nrf52840/
    bme688_node.ino
  esp32s3/
    gateway.ino
cloud/
  apps_script/
    Code.gs
analysis/
docs/
data/
```

| 경로 | 용도 |
| --- | --- |
| `firmware/nrf52840/` | BME688 측정 및 BLE NUS 송신 펌웨어 |
| `firmware/esp32s3/` | BLE 수신 및 Wi-Fi/Google 업로드 펌웨어 |
| `cloud/apps_script/` | Google Apps Script 및 클라우드 연동 소스 |
| `analysis/` | Colab 노트북 및 ML 분석 |
| `docs/` | 보드 매핑, 연결, 설정, 검증 문서 |
| `data/` | 실제 실험 데이터 및 CSV; Git 추적 제외 |

## 프로젝트 문서

- [AGENTS.md](AGENTS.md): Codex가 지켜야 할 개발 규칙
- [PROJECT_STATE.md](PROJECT_STATE.md): 확인된 동작, 미검증 사항, 통신 형식, 측정·업로드 주기, Arduino 설정 및 이후 단계
- [.gitignore](.gitignore): 실험 데이터, 캐시, 임시 파일 및 secret 파일 제외 규칙

현재는 Git 초기화 전 baseline을 준비한 상태다. 세 코드 파일은 루트의 기존 TXT 원본에서 내용 변경 없이 복사했으며 SHA-256 일치를 확인했다. 루트 원본 TXT는 보존하고 Git 추적에서 제외한다. 새 기능 구현이나 리팩터링은 하지 않았다. 빌드·실행·테스트 절차는 개발 환경 검증 후 문서화한다.

ESP32 복사본의 AP credential과 Apps Script URL은 baseline 보존을 위해 유지했다. 추후 config 분리, 비밀번호 로그 제거, endpoint 확인 및 TLS 검증 검토는 `PROJECT_STATE.md`의 기술 부채에 기록했다. 이번 단계에서는 Git 초기화·커밋·푸시나 하드웨어 실행을 수행하지 않았다.

현재 ESP32는 30초마다 최신 샘플 하나만 업로드한다. ML 데이터 수집 전 batch/ring-buffer 개선이 필요하다. 습도 임계값은 부패 정답으로 사용하지 않으며, ML train/test는 experiment/session 단위로 분리한다.

빈 디렉터리는 Git이 추적하지 않는다. 현재 구조에는 별도의 placeholder 파일을 추가하지 않았다.
