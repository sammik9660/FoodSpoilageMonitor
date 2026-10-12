# 프로젝트 현재 상태

갱신일: 2026-10-12. 아래 reliability v2 기록은 역사적 검증 상태다. 현재 작업 트리는 HP-354 gas-scan v3와 히터 OFF 환경/저빈도 스캔 전환 코드를 포함한다. 아래의 초기 SPIFFS 조사는 읽기 전용이었고, 이어서 사용자가 기존 SPIFFS 데이터 폐기와 장치 업로드를 명시적으로 허용해 제한된 복구를 수행했다. 기존 baseline, 과거 리뷰 및 작업 시작 전의 ESP LCD/NeoPixel 미커밋 변경은 보존했다.

## 2026-10-12 정상 동작 기준선 보존

- **사용자 실기 검증 보고:** ESP32-S3 Arduino 설정은 Flash Size 16MB / Huge APP (3MB No OTA / 1MB SPIFFS) / PSRAM disabled / USB CDC enabled / Hardware CDC and JTAG이다. 이 설정에서 SPIFFS 마운트, nRF→ESP BLE, 다중 가스 저항 측정, 서버 저장, 웹 대시보드 및 실험 CSV 출력을 확인했다. 아래 4MB/No OTA 덤프·장애 분석은 변경 전 역사적 관찰이며 현재 정상 설정의 검증 결과로 혼동하지 않는다. 현재 세션에서는 장치 업로드, 플래시 조작, 웹 앱 배포나 실제 Sheets 수정을 하지 않았다.
- 설치된 ESP32 core 3.3.12의 `huge_app.csv`는 app0 `0x10000`/`0x300000`, SPIFFS `0x310000`/`0xE0000`(896 KiB)다. 메뉴의 “1MB SPIFFS”는 이 영역의 개략적인 이름이다. `SPIFFS.begin(false)`, 기존 스풀 레코드·ACK 게이트·BLE/HTTP schema는 유지했다. 실행 중 장치의 파티션 테이블과 현재 소스 바이너리가 동일한지는 이번 턴에 다시 읽지 않았으므로 사용자 실기 확인과 구분한다.
- 이번 기준선 점검에서 `node --test tests/cloud.test.cjs` 25개, `python tests/fingerprint_test.py` 5개, MSVC C++17 integrity/sensor/spool/gateway_ui 4개가 통과했다. nRF `adafruit:nrf52:feather52840` 컴파일 통과(Flash 159,128/815,104 bytes; RAM 16,308/237,568 bytes). ESP32 `esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=huge_app,PSRAM=disabled,CDCOnBoot=cdc,USBMode=hwcdc` 컴파일 통과(Flash 1,466,957/3,145,728 bytes; RAM 60,324/327,680 bytes); 빌드 파티션 바이너리는 app0 `0x10000`/3 MiB, SPIFFS `0x310000`/896 KiB로 해석됐다. LiquidCrystal I2C의 기존 AVR 아키텍처 경고는 있으나 빌드는 성공했다. 센서 호스트 테스트의 오래된 스캔 시각·measurement index 기대값만 현재 펌웨어 동작에 맞췄으며 펌웨어 로직은 바꾸지 않았다. 장기 운전, 전원 차단 중 보존, 현재 클라우드 배포와 저장소 소스의 바이트 단위 일치는 이번 점검에서 검증하지 않았다.

## 2026-10-12 가스 샘플/빈 세션 CSV 추적 (현장 전송 확인, 저장 검증 진행 중)

- 사용자 제공 `gatter.csv`는 22열 헤더만 있다. 제공된 시트 사본의 `SensorData`는 마지막 UTC 2026-10-10 15:42:55.952, schema 2 가스 행에서 끝나며 `gatter` 세션은 UTC 2026-10-12 02:00:34.616~02:02:52.776(약 2분 18초)이다. 새 `EnvironmentHistory` 행은 schema 4이고 gas가 비어 있다. schema 4 환경 이력은 가스 측정이 아니므로 SensorData/세션 CSV에 들어가지 않는다. 과거 `df` 중 BLE `RX seq`가 있었으나 당시 SPIFFS fault로 배치가 n=0이었고 이후 사용자가 기존 SPIFFS 데이터 폐기를 허용했다.
- 현재 nRF 소스는 첫 HP-354 스캔이 부팅 10초 후, 이후 30분 간격이었다. 짧은 세션에서 가스 행이 비기 쉬워 사용자 선택에 따라 이후 간격만 2분으로 변경했다. nRF `adafruit:nrf52:feather52840` 빌드 성공(Flash 159,128/815,104; RAM 16,308/237,568 bytes), COM6 DFU 업로드 성공. 실제 nRF 첫 스캔에서 `TX V=3` 가스 프레임 seq 1..6, gas 17.92~353.23 kΩ, status 176과 BLE notify 성공을 확인했다. step 1/5 등이 빠져 `SCAN incomplete`였지만 유효 원본 step은 그대로 전송된다.
- ESP COM9 연결 후 `RX seq=13` 이후와 `Batch upload start n=3`을 관찰했다. 서버 응답의 `recording.state=RECORDING`과 한 건의 `status=stored`, `stored=1`, `HTTP 200 application ACK=1 samples=1`을 확인했다. 이 한 건은 이후 CSV에 가스 행이 늘지 않아 환경 이력 또는 별도 기록일 수 있으며 가스 저장 증거로 취급하지 않는다. 이어지는 가스 배치는 SPIFFS에 보존되고 `n=3 spool=12`에서 재시도했으나 `HTTP POST error=connection refused`가 반복됐다. `/exec?download=csv`를 새 쿼리로 읽은 결과에도 아직 2026-10-10 마지막 SensorData 행까지만 있다. 따라서 START 상태의 새 schema 3 행 및 CSV 포함은 미확인이다.
- 배포된 HTML은 `No valid sample`을 포함하며 새 로컬 Dashboard의 환경/가스 표시 코드를 포함하지 않는다. 로컬 `Dashboard.html`에는 마지막 유효 가스값과 경과 시간을 별도로 표시하도록 최소 수정했다. 실제 배포 갱신은 인증/UI 접근이 없어 수행하지 못했다. `node --test tests/cloud.test.cjs` 25개 통과, `git diff --check` 통과. ESP SPIFFS/Wi-Fi/ACK 경로와 기존 데이터는 변경하지 않았다.

## 2026-10-12 서버 sample schema 4 거부 (배포 갱신 필요)

- 장치에서 관찰된 `samples=1` POST의 응답은 `{"success":false,"version":3,"error":"Unknown sample schema"}`였다. 현재 ESP 소스의 환경 이력 생성은 `schema=4`이고 `sampleJson()`은 `sample_schema:4`를 전송하며 gas/step 필드를 생략한다. spool은 2/3/4 이외의 schema를 유효한 기록으로 읽지 않는다. 반면 현재 로컬 `cloud/apps_script/Code.gs`는 schema 2/3/4를 검증하고 schema 4를 `EnvironmentHistory`에 저장하여 원래 UID의 ACK를 돌려준다. 따라서 실제 웹 앱 배포본이 로컬 코드보다 오래되었을 가능성이 높다. 실제 배포본 코드는 읽지 못해 정확한 버전·차이점은 미확인이다.
- `node --test tests/cloud.test.cjs`의 25개 호스트 테스트가 통과했다. schema 4 저장/ACK, 재시도 journal, 기존 SensorData 보존이 포함되지만 실제 Google 배포/Sheets 테스트는 아니다. 이 환경에는 `clasp` 명령과 인증 파일이 없고 브라우저 제어 도구가 시작되지 않아 웹 앱 배포를 갱신하지 못했다. 로컬 Code.gs, ESP 펌웨어, SPIFFS, BLE, Wi-Fi, ACK/재전송 코드는 이번 조사에서 수정하지 않았다. 현장 완료 조건인 `stored>0`와 실제 Sheets 행은 여전히 미검증이며 미승인 spool 샘플은 장치에 보존된다.

## 2026-10-12 SPIFFS 현장 복구 (장치 검증 완료, 클라우드 저장 미완료)

- 원인: 실제 16MB 플래시의 파티션 테이블은 4MB `no_ota`이며 SPIFFS가 `0x210000`/`0x1E0000`이다. 기존 유효한 SPIFFS 이미지는 과거 Huge APP 위치 `0x310000`/`0xE0000`에 남았고 현재 파티션 시작 1MiB는 미초기화 `0xFF`였다. 물리 플래시 용량 차이 자체가 mount 실패 원인은 아니다.
- 사용자가 기존 SPIFFS 데이터 폐기를 허용한 뒤 현행 No OTA FQBN(4MB, PSRAM disabled, CDC enabled, Hardware CDC/JTAG)으로 `spiffs-diag-20261012-r1` 펌웨어를 빌드했다(Flash 1,466,957/2,097,152; RAM 60,324/327,680 bytes). `mkspiffs` block 4096/page 256, size `0x1E0000`의 빈 이미지를 만들고 오프라인 mount를 확인했다. COM9 장치에는 앱 `0x10000`과 SPIFFS `0x210000`만 esptool로 기록했고 두 이미지의 플래시 검증이 통과했다. 전체 플래시, NVS, 파티션 테이블, coredump는 지우거나 기록하지 않았다. Apps Script는 배포하지 않았다.
- 실제 장치에서 `SPIFFS partition offset=0x210000 size=0x1e0000`, `Wi-Fi saved networks=2`, BLE 유효 프레임, `SPOOL saved in-flight 1` 및 `SPOOL saved queued 1`, `Batch upload start n=1 spool=1`을 관찰했다. 재부팅 후에도 저장된 샘플이 `n=1 spool=2`로 배치에 복원되고 UID를 유지하며 재시도됐다. 이는 마운트·쓰기·읽기/복원과 빈 배치 문제 해결의 실기 증거다. SPIFFS 자동 포맷은 여전히 비활성이다.
- 휴대폰 핫스팟 사용 후 저장된 Wi-Fi slot 1에 연결됐다(`Wi-Fi state=3`). 서버에는 `samples=1` 요청이 도달했지만 HTTP 200 본문은 `{"success":false,"version":3,"error":"Unknown sample schema"}`였고 `application ACK=0`으로 샘플이 spool에 남았다. 현재 로컬 `cloud/apps_script/Code.gs`는 schema 4를 허용하지만 실제 배포본과의 일치 여부는 확인하지 못했다. 따라서 신규 Google Sheets 행 저장, ACK 후 회수, 중복 방지는 현장 검증되지 않았다. HTTP 200 자체를 저장 성공으로 해석하지 않는다.

## 2026-10-12 SPIFFS 마운트 실패 원인 조사 (복구 전 읽기 전용 기록)

- 마지막 서버 저장은 사용자 CSV 기준 2026-10-10 15:42:55 UTC(10-11 00:42:55 KST)다. 이것은 SPIFFS 정상 마운트 증거가 아니다. Git commit/reflog의 마지막 항목은 10-01 00:35:39 KST이므로 그 이후 실제 펌웨어 변경·업로드 시각은 Git으로 추적되지 않는다. 저장소 문서/과거 리뷰에는 `SPOOL total>0`, `SPIFFS mount success`, spool replay 등 성공을 입증하는 실기 로그가 없다. 다만 아래 플래시 원본의 CRC-valid v2 spool 기록은 이전 레이아웃에서 과거 쓰기가 수행됐다는 별도 물리 증거다. 마지막으로 확인된 NTP capture time은 10-10 15:45:54 UTC이고 실제 쓰기 시각은 그 이후일 수 있다. 첫 확인 가능한 마운트 실패는 사용자가 제공한 10-11 부팅 로그의 `mount failed, -10025`이며 그 부팅의 벽시계 시각이 없어 정확한 최초 발생 시각은 불명이다.
- 09-29 `2284cfb`의 `Spool.h`는 `SPIFFS.begin(false)`였지만 09-30 `761412d`에는 `SPIFFS.begin(true)`가 들어갔다. 현재 작업 트리는 다시 `false`다. 설치된 ESP32 core 3.3.12의 `SPIFFSFS::begin`은 `true`일 때 mount 실패 후 `format()`을 시도한다. 이 과거 커밋이 어느 파티션 설정으로 실제 장치에 업로드됐는지, format이 실행됐는지 알 수 없다. 헤더 주석의 “No auto-format”만으로 과거 바이너리의 안전성을 판단하면 안 된다.
- 10-11 장치에서 읽기 전용으로 확보한 파티션 테이블은 4MB `no_ota`: app0 `0x10000`/`0x200000`, SPIFFS `0x210000`/`0x1E0000`이다. 과거 Huge APP는 SPIFFS `0x310000`/`0xE0000`이다. 10-12 연결된 장치의 ROM `flash-id` 읽기에서 물리 플래시 16MB를 직접 확인했다. 두 파티션 모두 첫 4MB 안에 있으므로 물리 16MB/빌드 4MB 차이만으로 mount 실패를 설명하지 못한다.
- 현재 소스의 `spool.mountFailedAtBoot()` + Wi-Fi 연결 + RAM queue>0 경로라면 다음 전송은 `n>0`이 되어야 한다. 사용자가 수정 펌웨어 업로드 후에도 보고한 `n=0`은 실제 실행 바이너리·같은 부팅의 연속 로그·mount fault 분류를 다시 확인해야 한다. 이번에는 부팅 로그에 고정 build ID, 컴파일 시각, ROM 플래시 ID 기반 크기, 실행 중 SPIFFS 파티션 주소/크기, mount 결과를 추가했다. 이것은 읽기 전용 진단이며 마운트나 데이터 복구 자체를 바꾸지 않는다.
- 10-12 ESP32-S3 native USB COM9 연결 후 사용자가 실험 중지와 RAM 손실 허용을 확인했다. ROM `--no-stub read-flash`로 `0x210000..0x3EFFFF` 1,966,080바이트 전체를 `data/diagnostics/esp32s3-spiffs-window-2026-10-12.bin`에 읽기 전용 백업했다(SHA-256 `A8F41D66CA747BFAABBCE1BA715F4B435566609FEB1A01333887DFE5D31EB507`). 원본은 수정하지 않았다. `0x210000..0x30FFFF` 첫 1MiB는 모두 `0xFF`이다. 설치된 `mkspiffs`(block 4096/page 256)로 현행 `0x1E0000` 크기 전체 이미지는 장치와 같은 `-10025`로 mount 실패하고, 옛 Huge APP의 `0x310000` 시작 `0xE0000` 크기 슬라이스는 mount 성공하며 현재 파일 목록은 비어 있다. 옛 레이아웃 FS가 남고 새 레이아웃 시작 구간은 초기화되지 않은 상태라는 직접 증거다. 옛 영역에서는 CRC가 유효한 v2 spool 헤더 343개와 그 직후의 CRC-valid 첫 샘플 343개(서로 다른 레코드)를 발견했다. NTP 품질이 표시된 262개 첫 샘플의 capture time은 UTC 2026-09-30 02:54:13.524부터 2026-10-10 15:45:54.102까지다. 마지막 기록 시각은 사용자 CSV의 마지막 서버 저장보다 약 3분 늦다. 이는 과거 정상 spool 쓰기를 강하게 뒷받침하지만, 삭제된 레코드의 ACK 여부·미전송 여부나 전체 파일 복원 가능성은 확인되지 않았다. 전환·포맷이 실제 언제, 어떤 업로드에서 일어났는지도 미상이다. 10-12 현재 파티션 테이블을 ROM `--no-stub read-flash 0x8000 0x1000`으로 다시 읽었고 SHA-256 `8F79A0532CE399FE065BBB7475B287FB33A40D1EB30932116157E44310F39408`로 10-11 덤프와 바이트 단위 동일하다. 따라서 현행 장치 테이블도 `no_ota`로 확인됐다. 기본 실행 환경에서 COM9 접근 거부를 포트 점유로 단정했던 설명은 잘못이었으며, 허용된 실행 환경에서 동일한 읽기가 성공했다. 어떤 플래시 쓰기·포맷·펌웨어 업로드도 하지 않았다.
- 이번 변경은 `gateway.ino`의 부팅 진단 로그뿐이다. 기존 RAM 직접 업로드 우회, Wi-Fi/NVS, UID/ACK/replay, spool 형식은 유지했다. 호스트 spool·integrity 테스트와 Node 클라우드 모의 테스트 25개가 통과했다. Arduino CLI ESP32 core 3.3.12, 4MB/No OTA/PSRAM off/CDC/HW CDC 빌드 성공(Flash 1,466,957 / 2,097,152 bytes, static RAM 60,324 / 327,680 bytes). 실제 수정 펌웨어 업로드, SPIFFS 복구, 신규 Sheets 행 생성은 검증하지 않았다.

## 2026-10-11 SPIFFS mount fault와 온라인 업로드 (작업 트리, 현장 미검증)

- 사용자 실제 로그: Wi-Fi/서버 연결 후 `HTTP 200 application ACK=1 samples=0`, `SPIFFS: mount failed, -10025`, `SPOOL fault=1`, 대시보드 `DATA INTEGRITY ERROR`, 최신 센서값 경과 시간 증가, 새 세션 CSV 행 없음. 같은 로그에서 BLE `RX seq=1..28` 동안 queue가 증가했지만 업로드는 계속 `Batch upload start n=0`이었다. 즉 이 구간에서는 유효 BLE 가스 프레임이 ESP RAM까지 도달했고 큐에서 업로드 배치로 빠지지 않았다. 로그의 Serial/Serial0 이중 출력은 중복 수신의 증거가 아니다. 오류 코드의 물리적 원인 및 마운트 불가 파티션에 기존 미전송 파일이 있는지는 확인되지 않았다. 앞서 읽은 실제 파티션 테이블은 No OTA와 일치하지만 파일시스템 무결성을 입증하지 않는다.
- 확인된 코드 경로: `SPIFFS.begin(false)` 실패 시 `spool.fault`가 켜지고, 기존 업로드 루프는 RAM queue를 `active`로 꺼내지 않아 빈 상태 heartbeat만 ACK 받았다. Apps Script는 빈 `samples`를 정상 ACK하지만 행을 추가하지 않는다. 대시보드의 integrity 경고는 `spool_error`에 따른 것이며 숨기지 않는다.
- 최소 수정: **부팅 시 mount 자체가 실패했고 Wi-Fi가 온라인인 경우에만** 현재 부팅 RAM 샘플을 FIFO 순서로 배치 업로드한다. 오프라인 임시 `staging`을 일반 RAM queue보다 먼저 보낸다. 정확한 application ACK 전에는 active 배치를 해제하지 않는다. 마운트된 스풀의 CRC/파일 오류는 계속 차단하며, 읽을 수 없는 기존 스풀 파일은 비어 있다고 가정하거나 포맷·삭제하지 않는다. 이 RAM-only 경로는 전원 차단/재부팅 시 미전송 RAM 데이터를 보존하지 못하므로 spool 오류가 해결된 상태와 동일하게 취급하지 않는다.
- 세션 CSV는 `SensorData`만 읽는다. 5분 간격 ENV schema 4 이력은 `EnvironmentHistory`에 별도 저장되므로, 가스 스캔 전의 세션 CSV가 비어 있는 것만으로 ENV 수신·저장 실패를 단정할 수 없다. 실제 새 데이터 도달 여부는 장치의 `RX`, `Batch upload start n>0`, ACK와 Sheets `EnvironmentHistory`/`SensorData`를 별도 확인해야 한다.
- 검증: MSVC `spool_test` 및 `integrity_test` 통과, Node `cloud.test.cjs` 25개 통과, `git diff --check` 통과. Arduino CLI ESP32 core 3.3.12, 4MB/No OTA/PSRAM off/CDC/HW CDC 빌드 완료(종료 코드 0). 기본 샌드박스의 Xtensa 실행은 `Illegal System DLL Relocation` 오류였으나 별도 권한 빌드는 통과했다. 실제 장치 업로드·전송, SPIFFS 복구, Sheets 새 행 저장은 검증하지 않았다.

## 2026-10-11 ESP32-S3 Wi-Fi 두 네트워크 전환 (작업 트리, 현장 미검증)

- 사용자 업로드 후 실제 ESP 로그에서 `saved networks=1` -> `connect timeout slot=0` -> `configuration portal start failed`가 반복됐다. 실패 로그 뒤 다음 연결 시도까지 약 180초의 포털 타임아웃이 더해진 약 200초 간격이므로 포털이 논리적으로 활성화된 정황도 있다. 같은 로그 행의 2회 출력은 `logEvent()`의 Serial/Serial0 이중 출력과 일치하며 포털 중복 실행의 증거가 아니다. 설치된 WiFiManager 2.0.17 소스상 비차단 `startConfigPortal()`은 정상적으로 `configPortalActive=true`를 설정하고 AP 초기화를 호출한 뒤에도 초기값 `false`를 반환한다. 따라서 기존 게이트웨이의 `if(startConfigPortal())` 분기는 정상 포털 시작을 실패로 오인했다. 반환값 대신 `getConfigPortalActive()`와 AP mode를 확인하도록 로그 판정을 수정했다. 최종 소스 4MB/No OTA/PSRAM off/CDC Arduino CLI 컴파일 성공(Flash 1,466,633 / 2,097,152 bytes, static RAM 60,324 / 327,680 bytes). 실제 AP beacon/접속 성공 여부와 원래 STA 연결 실패의 원인은 아직 장치에서 확인해야 한다. 첨부 nRF 로그에서는 약 2초 간격 ENV 송신이 이어지지만 BLE/센서 문제는 이번 범위에서 변경하지 않는다. 이 수정은 NVS 슬롯을 삭제하거나 Wi-Fi 자격 증명을 재설정하지 않는다.
- 설치된 WiFiManager 2.0.17의 `autoConnect()`는 ESP32 STA에 저장된 AP 한 개를 시도하고, 포털의 새 AP 저장은 그 단일 설정을 대체한다. 기존 게이트웨이의 `WiFi.reconnect()`도 다른 AP를 선택하지 않는다. 부팅 로그에서 휴대폰 AP만 시도된 것은 확인되지만, 집 AP 자격 증명이 언제 덮어써졌는지와 공유기 호환성은 장치 NVS를 확인하지 않아 확정하지 않는다.
- 게이트웨이는 WiFiManager 포털을 유지하면서 ESP32 Preferences의 별도 `fsm-wifi` 네임스페이스에 최대 두 개의 SSID/비밀번호를 저장한다. 최초 부팅 시 기존 WiFiManager STA 설정을 슬롯으로 복사한 뒤에만 연결 시도를 시작한다. NVS 쓰기 실패 시 기존 설정은 이번 부팅의 RAM에 보존하고 재부팅 지속성 실패를 로그에 표시한다. 포털에서 새 AP 연결이 성공하면 콜백으로 다른 슬롯에 저장한다. 두 슬롯이 찬 상태에서 새 AP를 등록하면 현재 연결된 슬롯이 아닌 쪽을 교체한다. 이미 이전 펌웨어에서 덮어써진 자격 증명은 복원할 수 없어 포털에서 다시 등록해야 한다.
- 연결이 끊기면 등록 슬롯을 각 20초까지 순서대로 시도한다. 모두 실패하면 기존 설정 AP를 최대 180초 동안 열고, 포털이 종료되면 등록 슬롯을 다시 순회한다(실패 라운드마다 최소 30초 재시도 간격). 포털 활성 중에는 별도 재연결을 하지 않고 정상 연결 중에는 전환하지 않는다. BOOT 길게 누르기는 기존 등록을 지우지 않고 등록 포털만 연다. WiFiManager 디버그 출력을 꺼 비밀번호가 로그에 나오지 않게 한다. NVS 저장 실패 시 이전 슬롯을 유지하고 오류를 기록한다.
- BLE task와 HTTP worker, 업로드 ACK/UID, SPIFFS spool, 센서/클라우드 형식은 이번 Wi-Fi 변경에서 수정하지 않았다. 포털에 새 설정을 제출할 때 WiFiManager의 동기 연결 대기(최대 20초)는 기존 라이브러리 동작으로 남는다. `malformed BLE frame`과 `SPOOL fault=1`은 별도 문제이며 이번 수정의 원인으로 확정하지 않는다. 최종 소스 Arduino CLI ESP32 core 3.3.12, 4MB/No OTA/PSRAM off/CDC 빌드 성공(Flash 1,466,485 / 2,097,152 bytes, static RAM 60,324 / 327,680 bytes). LiquidCrystal I2C의 기존 아키텍처 경고가 있었다. 실제 두 Wi-Fi 간 전환·재부팅 지속성은 장치에서 아직 검증하지 않았다.

## 2026-10-11 ESP32-S3 실제 파티션 테이블 읽기 전용 확인

- ESP32-S3 native USB COM9에서 ROM 부트로더 `read-flash 0x8000 0x1000`으로 파티션 테이블 4 KiB만 읽었다. flasher stub, flash write/erase/format, 펌웨어 업로드는 사용하지 않았다. 덤프 SHA-256: `8F79A0532CE399FE065BBB7475B287FB33A40D1EB30932116157E44310F39408`. 원본과 해석 CSV는 Git에서 제외되는 `data/diagnostics/`에 있다.
- 실제 테이블은 `no_ota`와 일치한다: app0 `0x10000`/`0x200000`(2 MiB), SPIFFS `0x210000`/`0x1E0000`(1,920 KiB), coredump `0x3F0000`/`0x10000`. 설치된 ESP32 core 3.3.12의 `no_ota.csv`에서 생성한 3,072-byte 파티션 바이너리와 덤프의 앞 3,072 bytes가 정확히 일치했다. 과거 Huge APP 기록의 SPIFFS 시작 `0x310000`과는 1 MiB 차이가 난다.
- 현재 IDE의 No OTA 선택은 실제 테이블과 일치한다. 단, 파티션 테이블 확인은 SPIFFS 파일의 무결성이나 기존 `SPOOL fault=1` 원인을 증명하지 않는다. `Erase All Flash: Disabled`도 업로드 시 파티션 테이블과 앱 쓰기를 막지 않으므로, 보존할 spool 데이터는 별도 확인/백업이 필요하다. 이 읽기 명령은 장치를 ROM 부트로더 상태로 남겼다.

## 2026-10-11 환경 실시간 측정 / 저빈도 가스 스캔 작업 트리

- nRF는 Bosch forced mode에서 히터 OFF T/H/P를 기본 2초 간격으로 측정하고, 첫 30분 및 이후 스캔 시작 기준 30분마다 기존 HP-354 parallel mode로 전환한다. 유효 환경값은 가스 저항/STEP 없는 ENV BLE 프레임으로 보낸다. 가스 스캔은 real step 9 또는 60초 제한에서 종료하며, 히터를 끈 뒤 임시 60초 냉각 구간을 둔다. 실제 온도 보정은 없다. 20초 gas watchdog은 가스 모드에만 적용한다. 불완전 스캔의 유효 원본 step은 기존 녹화 중 저장 정책에 따라 보존하고 완전한 fingerprint 분석에서는 제외한다.
- ESP는 ENV 최신 상태를 가스와 별도로 추적하고, 2초 프레임은 RAM 최신 상태에만 반영한다. 약 5분마다 한 건만 기존 ACK-gated queue/120-byte CRC spool에 schema 4로 넣는다. Apps Script는 기존 SensorData를 유지하고 `EnvironmentHistory`에 시각이 확인된 이력을 저장하며, unknown time은 Unassigned에 둔다. HTTP 환경 JSON에는 gas/STEP이 없다. 기존 v2/v3 수신·journal·dedup 경로를 유지한다.
- 웹 대시보드는 기존 접근 방식과 주요 UI를 유지하며 최신 T/H/P, 갱신 시각/경과 시간, 스캔·냉각·복구 상태를 표시한다. 기존 5초 gateway heartbeat와 8초 페이지 polling을 이용해 통상 10~15초 표시를 목표로 하나 네트워크·Apps Script 지연은 별도다. `docs/ENVIRONMENT_AND_GAS.md`에 호출량, 데이터 정책, 현장 검증 항목을 정리했다.
- 검증: 실제 nRF 소스 기반 MSVC 센서 테스트(모드/스케줄/누락/복구/wrap), ESP parser·schema 4 spool reboot replay·UI 호스트 테스트, Apps Script 모의 테스트 25개, Python fingerprint 테스트 5개를 통과했다. Arduino CLI `adafruit:nrf52:feather52840` 최종 컴파일 성공(Flash 159,128 / 815,104 bytes, RAM 16,308 / 237,568 bytes). 지정된 ESP32-S3 4MB/Huge APP/PSRAM off/CDC 빌드도 성공(Flash 1,465,741 / 3,145,728 bytes, RAM 60,100 / 327,680 bytes); 설치된 LiquidCrystal I2C 라이브러리의 AVR 호환성 경고가 있었지만 컴파일은 통과했다. 실제 장치, `SPOOL fault=1` 원인, 클라우드 배포 및 장기 운전은 미검증이다. 기존 SPIFFS는 자동 포맷하거나 삭제하지 않았다.

## 2026-10-11 nRF no-real-gas watchdog 시각 수정

- 센서 처리 중 `lastSensorField`가 루프 시작 시각보다 뒤로 갱신될 수 있어 20초 watchdog의 `uint32_t` 뺄셈이 언더플로할 수 있었다. watchdog 비교 직전에 `millis()`를 다시 읽고, 발동 시 저장하는 기준 시각에도 같은 값을 사용한다. 측정·BLE·OLED·복구 정책은 변경하지 않았다.
- 호스트 센서 테스트에서 19,999/20,000ms 경계, `millis()` wraparound, 루프 중 새 real-gas 필드 수신 및 wrap 직후 수신을 검증했다. MSVC 테스트 통과. Arduino CLI `adafruit:nrf52:feather52840` 컴파일 성공(Flash 156,244 / 815,104 bytes, RAM 16,244 / 237,568 bytes). 실제 하드웨어 장기 운전은 이번 변경에서 확인하지 않았다.

## 2026-10-11 nRF 스케치 폴더 이식성

- Bosch `BME68x Sensor library` 1.3.40408 원본 폴더를 `third_party/`에서 `firmware/nrf52840/bme688_node/src/Bosch-BME68x-Library/`로 옮겼다. 소스·`library.properties`·두 LICENSE를 그대로 보존했으며 동일 Bosch 소스의 중복본은 두지 않았다. nRF 스케치는 내부 상대 경로로 헤더를 포함한다.
- 저장소 밖에 nRF 스케치 폴더만 복사하고 Bosch가 없는 임시 스케치북을 지정한 Arduino CLI에서 `adafruit:nrf52:feather52840` 컴파일 성공(Flash 156,196 / 815,104 bytes, RAM 16,244 / 237,568 bytes). OLED용 Adafruit GFX/SSD1306/BusIO는 설치된 라이브러리를 사용했다. 실제 보드 업로드나 센서 동작은 이번 변경에서 확인하지 않았다.
- 기존 MSVC 센서 호스트 테스트 통과. Bosch include/파일 배치와 설치 안내만 변경했고 HP-354 측정, BLE, OLED, 핀 설정은 변경하지 않았다.

## 2026-10-09 gas-scan v3 작업 트리

- Bosch 공식 `BME68x Sensor library` 1.3.40408 (source commit `4f37df69dde205e4f58d2bb9a45568c61e128e86`)를 nRF 스케치 내부 `src/Bosch-BME68x-Library`에 고정했다. 공식 parallel-mode 예제의 HP-354 온도/배수 배열을 사용하며 shared heater duration은 `140ms - getMeasDur(PARALLEL_MODE)/1000`으로 계산한다. 실제 scan 주기와 식품 분류 성능은 미검증이다.
- nRF v3는 Bosch status와 finite/G>0를 검사해 유효한 step만 전송하고, scan/step/gas/meas index를 보존한다. Bosch 데이터시트상 parallel mode의 `GASM_VALID=0` dummy 변환은 정상 대기이며 실패/scan step으로 세지 않는다. `meas_index`는 dummy 변환도 세므로 유효 가스 step 사이에서 1보다 크게 증가할 수 있다. NUS chunk/retry, soft-reset 재초기화/backoff, 검증된 D21/D20 및 OLED는 유지한다. v3의 140ms 폴링은 v2의 약 2초 단일 고정 히터 측정과 다르다.
- ESP는 v2/v3 BLE를 분리해 파싱한다. v2 88-byte spool을 CRC 검사 후 메모리에서만 legacy row로 승격하고, 새 파일은 v3 120-byte 형식으로 쓴다. mount 실패 시 포맷하지 않는다. 같은 UID와 capture time은 ACK까지 유지한다.
- Apps Script는 v2/v3 upload를 수용하고 기존 12열 오른쪽에 10개 메타데이터 열을 추가한다. 기존 12열 pending journal 복구, 고유 UID, capture-time session, CSV pagination을 유지한다. `analysis/fingerprints.py`는 CSV의 10개 step을 complete/incomplete로 구분하되 ML 학습이나 부패 label은 만들지 않는다. 현재 실제 Sheets는 변경하지 않았다.
- ESP는 Wi-Fi/BLE/첫 유효 frame/POST/redirect GET/ACK/backlog drain의 uptime 로그를 추가했다. 일반 backoff 상한은 30초, 429/503은 보수적인 150초다. 과거 약 5분 지연의 정확한 원인은 확인되지 않았다.
- 검증: nRF Arduino Feather52840 컴파일 성공 (flash 156,188 / 815,104 bytes; static RAM 16,244 / 237,568 bytes), ESP32-S3 지정 설정 컴파일 성공 (flash 1,464,125 / 3,145,728 bytes; static RAM 59,996 / 327,680 bytes). MSVC nRF/ESP parser/spool 호스트 테스트, Node Apps Script 모의 테스트 21개, Python fingerprint 판정 테스트 5개 통과. 두 빌드는 실제 장치 동작이나 클라우드 배포 검증이 아니다. `docs/GAS_SCAN_V3.md`에 프로토콜, 용량 추정 및 수동 전환 절차를 기록했다.
- 같은 날짜의 ESP LCD/NeoPixel 관련 `gateway.ino`, `GatewayUi.h`, `tests/gateway_ui_test.cpp` 선행 미커밋 작업은 이번 요청 범위 밖이며 삭제·덮어쓰기 하지 않았다. 따라서 작업 트리 diff에는 선행 UI 변경도 함께 보인다.

## 사용자 보고로 확인된 실제 하드웨어 상태

- BME688 -> nRF52840 -> BLE NUS -> ESP32-S3 -> Wi-Fi(phone hotspot) -> Apps Script -> Google Sheets end-to-end 성공.
- V1940 Pro Micro nRF52840 / nice!nano V2 compatible clone, `Adafruit Feather nRF52840 Express` variant.
- `Wire.setPins(21,20)`: D21 -> P0.31 SDA, D20 -> P0.29 SCL. I2C 0x76 ACK 및 register 0xD0 chip ID 0x61 확인.
- 동일 I2C 버스의 SSD1306 0.91-inch 128x32 OLED가 현재 설정(0x3C)으로 정상 동작한다고 사용자 보고.
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

후속 실기에서도 약 31°C / 100%RH / 679~735hPa / G=0 조합의 고착이 여러 차례 관찰되었다. 이때는 `performReading()`이 성공하고 모든 값이 숫자로 남을 수도 있었으며, 단순 센서 재장착으로 회복되지 않은 사례가 nRF 재부팅 시의 BME688 soft reset+재초기화 후 회복되었다. 확정된 invalid 조건은 G<=0이며, H=100%, 낮은 pressure 또는 약 31°C 자체는 정상 환경에서도 가능하므로 단독 실패 조건이 아니다. 금속 실드 접촉, 정전기, 전원, I2C 또는 센서 내부 상태 중 정확한 원인은 확인되지 않았다.

사용자 보고상 2026-09-28 21:34:59 이후 반복된 31.61 / 100 / 735.46 / 0 구간은 stale-data bug의 invalid data다. 이번 작업은 기존 Google Sheets 행을 삭제하거나 수정하지 않는다. 구간 확인·정리는 사용자에게 맡긴다.

## reliability v2 구현 변경 — historical

- nRF: 올바른 Adafruit API, 검증된 핀/주소/측정 설정 유지, read failure/non-finite/G<=0 시 frame/sequence 생성 금지, 연속 3회 실패 검출, soft reset+재초기화, 5~60초 backoff, 성공 측정으로 recovery 확인.
- BLE v2: `BID=XXXXXXXX,SEQ=n,MS=n,T=...,H=...,P=...,G=...` 및 독립 STAT 진단 프레임. NUS/chunk/newline 유지.
- ESP: strict parser/프레임 재동기화, boot UUID+receive sequence UID, 128개 RAM queue, 최대 32개 batch, 기본 10초 upload, 정확한 application ACK 이후 해제, 10~60초 retry.
- SPIFFS: 기존 partition 사용, CRC 검증 immutable chunk, 재부팅 FIFO replay, 최대 128 chunks/동적 byte budget, ACK 이후 파일 회수. 자동 format/미전송 데이터 overwrite 없음.
- 시간: ESP receive 시 UTC(NTP가 유효한 경우)와 uptime 보존. unknown time은 Unassigned에 저장하여 잘못된 session 배정 방지.
- Apps Script: 기존 SPREADSHEET_ID 필수, SensorData 비파괴 12열 확장, Sessions/IngestState/Unassigned 보조 시트, lock, bulk write, write-ahead journal, boot별 high-water dedup.
- recording 기본 STOPPED. Start/Stop [start,stop) 구간을 capture time으로 비교. legacy short/long single POST 지원(서버 시각이라는 제한 명시).
- Dashboard: 10초 polling, freshness/오류/recording/큐 진단, absolute deployed URL의 CSV와 session filter. Dashboard.html 추가.
- CSV UI: 녹화 중에는 현재 session, 정지 후에는 최근 종료 session 링크를 유지하며 all/session별 파일명을 구분한다.
- ESP 중요 로그는 USB CDC Serial과 UART Serial0에 전달. credential 출력 제거. 전력 관련 변경은 짧은 yield와 연결 LED 억제에 한정한다.

## 검증 및 한계

실제로 수행한 빌드/자동 테스트 결과와 정확한 capacity 계산, 수동 업로드 순서는 `docs/RELIABILITY.md`에 기록한다. 이번 개선 버전을 실제 보드에 업로드하거나 Apps Script에 배포해서 E2E/장기 시험한 것은 아니다.

- nRF Feather52840 빌드 성공: flash 157,508 / 815,104 bytes, static RAM 16,100 / 237,568 bytes. 128x32 OLED framebuffer 약 512 bytes는 runtime heap 할당이다.
- ESP32-S3 지정 설정 빌드 성공: flash 1,428,505 / 3,145,728 bytes, static RAM 55,884 / 327,680 bytes. 태스크/큐 등 runtime heap은 이 static RAM 수치에 포함되지 않는다.
- Code.gs/Dashboard 실제 코드에 대한 Node 모의 테스트 15개 통과. 중복, recording 경계, 저장 실패·journal 복구, legacy POST, CSV 및 UI JS 포함.
- C++17/MSVC 실제 nRF 소스 테스트에서 정상값, read failure, T/H/P/G non-finite, G=0/음수 거부, G=0 연속 3회 이후 기존 recovery/backoff 및 성공 복귀를 확인했다. 실제 간헐 고착 자체를 전기적으로 재현한 시험은 아니다. 기존 parser/spool 테스트 기록도 유지한다.

- 진단 스케치 2개, 원본 TXT, historical review, 기존 notebook은 수정하지 않았다.
- 기존 endpoint와 AP 설정 값은 보존했다. TLS `setInsecure()`와 서버 배포 권한/Start·Stop 접근 제어는 남은 보안 부채다.
- filesystem mount/손상 시 자동 복구로 데이터를 버리지 않고 fault 상태로 중지한다. virgin SPIFFS 최초 준비가 필요할 수 있다.
- 실제 flash filesystem capacity/쓰기 지연, 재부팅·전원차단, RAM overflow, BLE reconnect, NTP/session 경계, CSV 다운로드는 현장 검증 필요.
- 보드 전력과 heater 안정 상태, 정확한 실패 원인은 미검증이다. BSEC/AI-Studio raw acquisition은 이번 범위가 아니다.
- 기존 baseline commit 90e5cc78ead2af1474672beb6b20956ad87cc9b7 및 historical review는 재작성하지 않는다.
