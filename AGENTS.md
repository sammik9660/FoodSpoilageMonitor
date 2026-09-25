# 개발 규칙

이 폴더는 BME688 기반 Food Spoilage Monitor의 단일 저장소다. 작업 전에 `PROJECT_STATE.md`를 읽고 확인된 동작과 미검증 항목을 구분한다.

## 변경 및 검증

- 동작이 확인된 코드는 필요 없이 리팩터링하지 않는다.
- 변경은 최소 단위로 하고 각 계층을 독립적으로 테스트한다.
- 요청하지 않은 기능을 임의로 구현하지 않는다. 현재 baseline은 기존 TXT에서 내용 변경 없이 복사한 소스다. 별도 요청 없이 리팩터링하거나 새 기능을 구현하지 않는다.
- 기존 참고 파일은 요청 없이 이동, 수정, 삭제하지 않는다.
- 컴파일 성공, 과거 동작 확인, 최신 코드의 현장 실행을 구분한다. 실행하지 않은 검증을 완료로 보고하지 않는다.
- 상태가 바뀌면 `PROJECT_STATE.md`에 검증 범위와 미확인 사항을 반영한다.

## 하드웨어 및 통신

- nRF52840 I2C 핀과 Wire 기본 SDA/SCL 핀은 실제 보드 매핑 확인 전에는 추측하지 않는다.
- 현재 BLE 프로토콜은 Nordic UART Service(NUS)다. 한 샘플 형식은 `T=xx.xx,H=xx.xx,P=xxxx.xx,G=xxx.xx\n`이다. `\n`은 실제 줄바꿈 문자다.
- BLE 20-byte fragmentation에 대응한 chunk + newline framing을 유지한다. 수신 chunk 하나를 완전한 샘플 하나로 가정하지 않는다.
- 현재 nRF 센서 측정 주기는 약 2초, ESP32 Google 업로드 주기는 30초다.
- 현재 ESP32는 30초 사이 수신한 모든 샘플이 아니라 가장 최신 샘플 하나만 업로드한다. ML 데이터 수집 전 batch/ring-buffer 방식으로 개선해야 하지만 지금 변경하지 않는다.
- 단위를 임의로 변경하지 않는다. 미확인 단위는 추측하지 말고 확인한다.
- BME688 기본 테스트는 Adafruit_BME680 호환 API를 사용한다.
- heater 설정 320°C / 150ms는 기본 센서 동작 테스트용이다.
- BME AI-Studio/BSEC 및 heater-profile raw acquisition은 이후 단계다.
- ESP32-S3 Arduino 설정은 `PROJECT_STATE.md`를 기준으로 하며 요청이나 검증 근거 없이 변경하지 않는다.

## 데이터 및 ML

- 습도 임계값을 음식 부패 정답(label)로 사용하지 않는다.
- ML train/test는 experiment/session 단위로 분리한다. 인접한 시계열 샘플을 랜덤 분할하지 않는다.
- 실제 실험 데이터는 `data/`에 보관하고 Git 추적에서 제외한다.
- 환경변수, Wi-Fi 자격 증명, 토큰 등 secret을 소스나 문서에 기록하지 않는다. 설정 예시는 placeholder를 사용한다.
- 이번 baseline의 ESP32 복사본은 사용자의 명시적 요청에 따라 기존 AP credential과 Apps Script URL을 그대로 보존한다. 이를 임의로 변경하지 않으며, 이후 분리 작업은 `PROJECT_STATE.md`의 기술 부채를 따른다. 민감한 값은 문서나 작업 보고에 재출력하지 않는다.

## 작업 환경

초기 개발 환경은 Windows/PowerShell이다. 디렉터리별 역할은 `README.md`를 따른다. 빌드·테스트 명령은 아직 정의되지 않았으며, 소스 및 설정이 정식 배치되면 실제 구성에 맞는 명령을 문서화한다.
