# 프로젝트 현재 상태

갱신일: 2026-10-19. 아래 reliability v2 기록은 역사적 검증 상태다. 현재 작업 트리는 HP-354 gas-scan v3와 히터 OFF 환경/저빈도 스캔 전환 코드를 포함한다. 아래의 초기 SPIFFS 조사는 읽기 전용이었고, 이어서 사용자가 기존 SPIFFS 데이터 폐기와 장치 업로드를 명시적으로 허용해 제한된 복구를 수행했다. 기존 baseline, 과거 리뷰 및 작업 시작 전의 ESP LCD/NeoPixel 미커밋 변경은 보존했다.

## 2026-10-20 Wi-Fi event-driven recovery (소스/호스트 검증)

- 연결 시도 중 STA disconnect 이벤트가 오면 슬롯 전체 timeout을 기다리지 않고 즉시 실패로 처리한다. 명시적 연결 취소는 실제 disconnect 이벤트를 기다리며, 이벤트가 누락된 경우에만 2초 제한 fallback을 사용한다. 다음 `WiFi.begin()` 전에는 이벤트 이후 250ms settle을 둬 연결 설정 충돌을 피한다.
- 무응답 연결 timeout은 20초에서 12초, 두 네트워크 전체 실패 뒤 재시도 간격은 30초에서 10초로 줄였다. 성공 슬롯 번호만 기존 `fsm-wifi` namespace의 별도 `last` 1-byte key에 기록한다. 기존 `ap0`/`ap1` credential 레코드와 내용은 바꾸지 않으며 재부팅·연결 끊김 뒤 마지막 성공 슬롯을 먼저 시도하고 다른 슬롯도 계속 순회한다.
- HTTPS/TLS, BLE/DFU, OTA, SPIFFS/spool, NeoPixel, 센서 펌웨어와 파티션은 변경하지 않았다. MSVC C++17 `wifi_recovery_test`와 `git diff --check`가 통과했다. ESP32 컴파일과 실제 AP 전환·장기 자동 복구는 사용자 실기 전까지 미검증이다.
- 후속 실기에서 빠른 연결 직후 `udp_new_ip_type ... Required to lock TCPIP core functionality!` assertion과 reason 8이 관찰됐다. 설치된 mDNS 1.11.2는 UDP PCB 작업을 `tcpip_api_call()`로 넘기며 Wi-Fi 이벤트 callback도 상태 기록만 하므로, 제공된 로그만으로 assert의 정확한 호출자는 확정하지 못했다. 링크 변동 때 `MDNS.end()`/`begin()`을 반복하던 수명 경합은 responder를 1회만 초기화하고 ESP-NETIF 상태 전이를 따르도록 제거했다. 명시적 disconnect의 늦은 reason 8(`ASSOC_LEAVE`)이 다음 슬롯 시도를 실패로 처리하지 않도록 무시하며 2/201/203은 기존 즉시 failover 대상으로 유지한다. 호스트 정책 테스트와 정적 mDNS 수명 검사, `git diff --check`는 통과했다. assert 해소, 1.5초/126초 연결 사례 및 Brownout 원인은 실기 재확인이 필요하다.
- 재발 로그와 SHA-256 `776df427b17c...`가 일치하는 Arduino ELF를 디코딩했다. 호출 경로는 `networkTask()`의 HTTPS POST → `HTTPClient` → `NetworkClientSecure::connect()` → core 3.3.12 `NetworkManager::hostByName()` → 잠금 없는 `dns_clear_cache()` → SNTP DNS callback → `dns_gethostbyname()` → `udp_new_ip_type()` assert였다. mDNS는 이 crash의 호출 스택에 없었다. Arduino core 파일 대신 gateway의 네 HTTPS 경로만 thread-safe `lwip_getaddrinfo()`로 해석하고, 해석된 IP와 원래 hostname(SNI), 기존 인증 설정으로 TLS를 연결하도록 변경했다. Wi-Fi 정책 테스트, 모든 HTTPS client 경로 정적 검사와 `git diff --check`는 통과했으며 ESP32 전체 빌드·업로드와 실기 crash 해소는 아직 미검증이다.

## 2026-10-19 ESP32 → nRF52840 Legacy BLE DFU 중계 Phase 1 (소스/호스트 검증)

- 2026-10-20 사용자 실기에서 웹 페어링, ZIP 검증·저장, PREPARE/ACTIVE 및 nRF의 실제 bootloader 진입까지 확인했으나 전송은 0%에서 `DFU control write failed`로 중단됐다. 로그상 애플리케이션은 진입 명령 직후 예상대로 연결을 끊고 bootloader에 머물렀다. ESP 구현이 write-with-response의 `false`를 즉시 실패로 처리해 bootloader 검색으로 진행하지 않은 것이 확인됐다.
- 애플리케이션 진입과 최종 activate/reset 명령에 한해 GATT 반환값과 별도로 실제 연결 해제를 확인하도록 수정했다. 연결이 유지된 채 write가 실패하면 계속 실패 처리한다. 나머지 Legacy DFU control 명령, 응답, 20-byte payload, PRN 8, ZIP/CRC 검증은 변경하지 않았다. 단계·opcode·write 결과·bootloader 검색/연결·특성 속성 로그를 추가했다. 프로토콜 호스트 테스트와 `git diff --check`는 통과했으며, 이 수정 이후의 Arduino IDE 컴파일과 실제 ESP→nRF 전송/재연결은 사용자 검증 전까지 미검증이다.
- 후속 실기에서 예상 연결 해제와 동일 주소 bootloader 검색·연결까지 성공했고 ESP32 core 3.3.12가 협상한 MTU 247 때문에 기존 `MTU == 23` 검사에서 중단됐다. 현재 qio_opi SDK 설정은 NimBLE이며 `BLEClient::getMTU()`는 `ble_att_mtu(connection)`의 협상 결과를 반환한다. Legacy 전송 크기는 검증된 20 bytes로 유지하고 MTU가 최소 23이며 그 payload를 수용하면 진행하도록 수정했다. MTU 23/247/517 허용 및 22 거부 호스트 테스트는 통과했으며 서비스 발견 이후 실제 전송은 아직 실기 미검증이다.

- 기존 포트 80 관리 서버의 OTA token Basic 인증과 Host/Origin 동일 출처 검사를 재사용하는 로컬 `/nrf-dfu` 업로드 화면을 추가했다. 녹화 STOPPED가 확인되고 RAM queue, SPIFFS spool, HTTP/ACK 작업 및 다른 업데이트가 모두 비어 있을 때만 `/nrf-dfu.upload.tmp`로 스트리밍한다. SPIFFS spool budget 60%와 8 KiB 파일시스템 여유를 남기며, 기존 `/nrf-dfu.zip`은 ZIP entry CRC32, BIN CRC16, Legacy DFU 0.5 구조, device type `0x0052`, SoftDevice `0x00B6`, 앱 크기 검사가 끝날 때까지 유지한다. 검증된 파일은 backup/rename 순서로 교체하고 부팅 시 중단된 교체를 복구한다. 중단된 전송은 60초 무진행 timeout으로 임시 파일만 정리한다. 업로드는 DFU를 시작하지 않으며 기존 `NRF DFU START`만 실행 승인 경로다.
- 업로드 정책/정적 회귀, 기존 protocol/UI/integrity/spool/sensor/cloud 테스트, OTA 파티션 검사와 `git diff --check`가 통과했다. 기본 샌드박스에서는 기존 Windows 오류 623(`Illegal System DLL Relocation`)가 재현됐지만, 같은 Arduino IDE 설정을 사용한 외부 Arduino CLI에서 ESP32 core 3.3.12 전체 컴파일·링크가 통과했다. application BIN은 1,520,224 bytes로 3 MiB app slot 안이다. `.ino` 자동 prototype이 기존 `extern "C" verifyRollbackLater()`와 충돌한 것은 동일 linkage의 전방 선언만 추가해 해결했으며 함수 본문과 rollback 정책은 바꾸지 않았다. 장치 접근, SPIFFS 쓰기, 펌웨어 업로드 및 실제 브라우저 업로드는 수행하지 않았다.
- ESP32 signed Wi-Fi OTA는 이미 사용자 실기에서 성공한 기존 기능이다. 이번 변경은 해당 OTA 수신 코드, `partitions.csv`, SPIFFS/NVS 배치와 서명·anti-rollback 경로를 수정하지 않고 별도 nRF Legacy DFU 중계 모듈만 추가한다.
- 중계기는 SPIFFS의 고정 경로 `/nrf-dfu.zip`에서 application-only Legacy DFU 0.5 ZIP을 스트리밍한다. STORE/no-encryption/no-data-descriptor, manifest/BIN/DAT 3개 항목, ZIP CRC32, BIN CRC16, device type `0x0052`, SoftDevice requirement `0x00B6`, 815,104-byte 앱 한도를 전송 전에 검사한다. 전체 ZIP 또는 BIN을 RAM에 적재하지 않는다.
- `NRF DFU START`는 NVS에 명시적으로 pairing된 sensor ID가 현재 연결 ID와 같고 cloud recording이 STOPPED로 확인됐을 때만 PREPARE로 들어간다. PREPARE 중 이미 받은 RAM queue/SPIFFS spool/HTTP를 기존 ACK 정책으로 비운 뒤 ACTIVE로 전환하며 기존 sample을 삭제하지 않는다. ACTIVE는 앱의 buttonless DFU service로 진입하고 동일하거나 주소가 1만 차이 나는 DFU service 광고만 선택해 MTU 23, 20-byte packet, PRN 8로 전송한다. 성공 표시는 validate/activate 뒤 NUS 재연결 및 Device Information Firmware Revision 읽기까지 완료돼야 한다.
- 스마트폰 성공 ZIP의 BIN 161,240 bytes와 manifest CRC16 `0xB9F1`, ZIP STORE 구조를 다시 확인했고, protocol opcode/response/PRN/CRC host test, 기존 OTA 파티션 검사와 `git diff --check`가 통과했다. ESP32 core 3.3.12, Flash 16MB, sketch custom partition, OPI PSRAM, CDC/HW CDC 설정의 업로드 기능 추가 전 Arduino CLI 빌드도 통과했다(Flash 1,513,315/3,145,728 bytes, static RAM 62,616/327,680 bytes, application BIN 1,513,456 bytes). 실제 ESP 중계 DFU, 브라우저 ZIP 저장, 취소/전원 중단 복구, 주소 +1 동작 및 NUS 재연결은 장치 시험 전까지 미검증이다. Dashboard 통합은 구현하지 않았다.

## 2026-10-19 nRF52840 BLE DFU 2.0.6 실기 성공 (사용자 확인)

- nRF 센서 버전을 2.0.6으로 올리고 기존 부팅 로그에 새 버전을 유지했다. 표준 BLE Device Information Service의 Firmware Revision 0x2A26에도 2.0.6을 노출한다. NUS 광고, 20-byte/newline 프레임, ENV/GAS/STAT 형식, 2초 ENV 및 10분 gas scan 주기는 변경하지 않았다.
- 설치된 Adafruit nRF52 core 1.7.0의 `adafruit:nrf52:feather52840`/S140 6.1.1 대상으로 빌드했다. Flash 161,232/815,104 bytes, RAM 16,544/237,568 bytes이며 실제 소스를 포함하는 MSVC sensor host test와 `git diff --check`가 통과했다.
- core의 기존 `adafruit-nrfutil dfu genpkg --dev-type 0x0052 --sd-req 0x00B6` recipe가 만든 application-only legacy DFU ZIP을 `data/nrf-dfu-2.0.6/bme688-node-2.0.6-legacy-dfu.zip`에 보존했다. ZIP 162,102 bytes, application BIN 161,240 bytes, DAT 14 bytes이며 manifest의 device type 82, SoftDevice requirement 182, DFU format 0.5와 BIN의 2.0.6 문자열을 확인했다. SoftDevice/bootloader 이미지는 포함하지 않았다.
- 사용자 실기에서 Nordic Android DFU 앱의 high-MTU 요청을 끄고 기본 ATT MTU 23, 낮은 PRN 설정으로 `bme688-node-2.0.6-legacy-dfu.zip`을 전송했다. Bootloader enabled, DFU initialized, Firmware uploaded, Completed 단계가 모두 성공했다. 에이전트는 장치 업로드나 DFU를 실행하지 않았다.

## 2026-10-19 v2.0.8 OTA 시험 준비

- 사용자 보고: v2.0.7/build 2026101905를 USB로 설치한 뒤 BLE, HTTPS application ACK, spool 및 backlog drain을 확인했다. 이어 최신 2.0.8 파일을 사용한 OTA가 성공했으며, 이전 실패는 새 파일과 이전 파일을 혼동한 것이 원인이었다. 세부 post-boot 로그는 이 기록에서 독립 재검증하지 않았다.
- feature/multi-device-support의 cf76088에서 버전만 2.0.8/build 2026101906으로 올렸다. 서명, build 증가 검사, 파티션, OPI PSRAM 및 데이터 경로는 변경하지 않았다. tests/ota_package_test.ps1은 배포 BIN/manifest의 형식, 버전, build, SHA-256, 내장 공개키 서명과 변조 거부를 검사한다. 이는 장치의 mbedTLS/sscanf 실행 검증을 대신하지 않는다.
- 클라우드 모의 테스트 30개, fingerprint 5개, MSVC gateway_ui/integrity/spool, OTA 서명 도구 및 파티션 테스트와 git diff --check 통과. 새 패키지 검사기를 기존 2.0.7 패키지에 적용하여 내장 공개키 검증 및 변조 거부를 확인했다.
- 빌드: Codex CLI 빌드는 샌드박스의 Arduino15 경로 변경으로 초기화에 실패했고 외부 환경에서도 의존성 탐색이 길어져 중단했다. 이후 사용자가 Arduino IDE에서 application BIN을 export했다. 해당 BIN은 현재 소스보다 새롭고 ESP 이미지 헤더 `0xE9`, `2.0.8` 문자열, manifest 단계 진단 및 지연 lease 정리 코드를 포함하며 `2.0.7` 문자열은 없다. 크기는 1,496,960 bytes, SHA-256은 `26d4e336637207052703f93319465562cc1e5de4209407fadcde512664b02f8c`다.
- `data/ota-2.0.8/`에 application BIN과 기존 개인키로 생성한 manifest를 준비했다. manifest는 target esp32s3, version 2.0.8, build 2026101906, LF-only/no-BOM, BIN SHA-256 일치이며 펌웨어 내장 공개키 ECDSA 검증 및 canonical hash 변조 거부가 통과했다. 이는 실제 OTA 설치 성공 증거가 아니다. 장치 접근/설치, SPIFFS/NVS 변경 및 운영 배포는 수행하지 않았다.

## 2026-10-19 OTA manifest 진단 및 lease 정리 직렬화 (2.0.7 소스, 실기 미검증)

- 실기 v2.0.5/build 2026101903은 v2.0.6 manifest 수신 후 generic `manifest signature, version, or format invalid`로 거부했다. 로컬에서는 v2.0.6 package의 형식, build 증가, BIN SHA-256과 내장 공개키 서명이 모두 통과했지만, v2.0.5 수신기는 세부 실패 조건을 기록하지 않아 장치 실패 원인은 아직 확정할 수 없다.
- 2.0.7/build 2026101905는 기존 형식·서명·anti-rollback 조건을 바꾸지 않고 manifest 실패를 FORMAT/TARGET/VERSION/BUILD/BASE64/HASH/KEY/SIGNATURE로 구분한다. 수신 바이트, HTTP upload가 보고한 예상 바이트와 고정 버퍼 용량만 기록하며 키·서명·토큰 내용은 출력하지 않는다.
- OTA 실패 callback은 cloud lease 해제 HTTPS를 직접 실행하지 않는다. lease 해제를 예약하고 `handleClient()`가 반환된 다음 한 loop를 더 지난 뒤 main loop에서 한 번 수행한다. 예약·해제 시도와 그 직후 loop에서는 일반 upload job을 시작하지 않으며, 실패 시 기존처럼 이후 정상 cloud health가 상태 복구를 재시도한다. queue/spool을 삭제하지 않고 v2.0.6의 빈 `samples:[]` POST 차단을 유지한다.
- 호스트/정적 테스트만 수행한다. 새 ESP BIN과 manifest, 실제 USB 설치, OTA 성공, 실패 로그와 TLS 힙 회복은 Arduino IDE export 및 사용자 실기 전까지 미검증이다. 기존 `data/ota-2.0.6/` package는 변경하지 않는다.

## 2026-10-19 ESP OTA 우선 및 빈 HTTPS 제거 (2.0.6 소스, 실기 미검증)

- 실기 설치된 2.0.5에서 RAM queue와 SPIFFS spool이 비어도 일반 업로드 스케줄러가 5초마다 `samples:[]` job을 만들었다. HTTPS 동안 `busy`가 CLOUD_WAIT를 만들어 GPIO48이 노란색, ACK 뒤 CLOUD_OK가 초록색이 되어 네트워크 시간을 포함한 약 7초 반복과 일치한다. nRF의 2초 ENV는 모두 저장되지 않고 5분마다 schema 4 한 건만 queue에 들어가므로 이 빈 요청 반복의 직접 원인이 아니다.
- 2.0.6/build 2026101904는 active/staged/spool/RAM 중 실제 데이터가 하나라도 있을 때만 일반 UploadJob을 만든다. 데이터가 있으면 기존 UID, application ACK, retry, spool replay 및 실패 보존 경로를 그대로 사용한다. OTA용 cloud lease 요청은 녹화 STOPPED를 검증하므로 별도 유지한다.
- `/prepare`를 새 일반 job 스케줄보다 먼저 처리하고, PREPARE부터 BLE를 연결 해제해 신규 유입을 멈춘다. 요청 전에 이미 수신된 RAM/spool과 진행 중 HTTP는 기존 ACK 정책으로 비운 뒤, BLE 단절·queue/spool/active/staged/results 비움·HTTP idle·SPIFFS 정상·Wi-Fi 연결을 모두 확인한 시점에만 cloud STOPPED lease를 얻는다. lease와 최종 drain 검사가 모두 통과해야 manifest에서 ACTIVE로 전환한다. 실패·취소·timeout은 NORMAL로 돌아가 BLE를 재연결하며 기존 데이터는 삭제하지 않는다.
- nRF에는 전송 실패분을 보존하는 애플리케이션 큐가 없으므로 PREPARE/ACTIVE 동안 생성된 센서 측정값 자체를 게이트웨이가 보존할 수는 없다. 이 구간은 의도적으로 BLE를 끊고 `OTA measurement gap`으로 명시한다. OTA 전에 ESP가 이미 받은 sample은 모두 ACK 또는 spool/RAM에 보존하며, ACTIVE 중 SPIFFS 쓰기와 OTA 플래시 쓰기를 겹치지 않는다.
- 호스트 gateway UI/스케줄 테스트, integrity 테스트, spool CRC/FIFO/reboot/ACK 테스트와 mock cloud 30개가 통과했다. ESP32 Arduino CLI OPI build는 설치 환경에서 90초 이상 출력 없이 진행되어 장시간 실행을 중단했으므로 펌웨어 컴파일은 미검증이다. 장치 접근·업로드·OTA·SPIFFS/NVS 변경은 수행하지 않았다.
- 사용자가 Arduino IDE로 export한 ESP32 core 3.3.12 산출물은 컴파일 입력과 BIN에서 version `2.0.6`, build `2026101904`를 확인했고, 16MB/custom partition/OPI PSRAM/erase disabled 설정 및 프로젝트 파티션 CSV 적용을 확인했다. 1,495,872-byte BIN을 `data/ota-2.0.6/`에 복사해 기존 키로 서명했으며, 원본·복사본 SHA-256, manifest 메타데이터와 내장 공개키 ECDSA 서명을 검증했다. 실제 OTA 설치와 장치 동작은 아직 검증하지 않았다.

## 2026-10-19 가스 10분 주기 및 OTA 2.0.5 준비 (ESP 패키지 대기)

- nRF의 실제 HP-354 시작 간격을 `GAS_SCAN_INTERVAL_MS=600000`으로 변경했다. 첫 스캔은 부팅 10초 후로 유지하고, 히터 OFF forced-mode 환경 측정은 2초 start-to-start, 가스 스캔 poll 140ms, timeout 60초, cooling 60초를 유지한다. ENV에는 가스값을 넣지 않으며 새 가스 step만 기존 `SCAN`/`STEP`/`SEQ`로 송신한다. 이 변경은 ESP OTA로 nRF에 전달되지 않으므로 nRF 펌웨어는 별도로 설치해야 한다.
- ESP gateway는 2.0.5/build 2026101903으로 올렸다. core 3.3.12의 빌드 설정은 Flash 16MB, sketch custom OTA partition, OPI PSRAM, CDC enabled, Hardware CDC/JTAG, Erase All Flash disabled다. 부팅 `FIRMWARE` 로그에 런타임 PSRAM 크기를 추가해 실제 초기화를 확인할 수 있게 했다. 기존 NVS, SPIFFS `0x310000`/`0xE0000`, 두 3MiB OTA 슬롯, spool/UID/ACK/서명/anti-rollback 로직은 변경하지 않았다. 실제 OTA 설치와 하드웨어 PSRAM 초기화는 사용자 장치 검증 전까지 미검증이다.
- 센서 실제 소스를 포함하는 MSVC 호스트 테스트, mock cloud 30개, fingerprint 5개, OTA partition 검사와 dummy-image 서명 검사가 통과했다. DFU 서비스 추가 전 nRF 빌드는 Flash 159,208/815,104 bytes, RAM 16,308/237,568 bytes였고, DFU 서비스 추가 후 최종 `adafruit:nrf52:feather52840` 빌드는 Flash 160,368/815,104 bytes, RAM 16,420/237,568 bytes로 성공했다. ESP OPI 빌드는 Codex 샌드박스 밖에서 한 번 시작했으나 약 9분간 완료 출력 없이 지속되어 장시간 빌드 금지에 따라 중단했다. 따라서 2.0.5 ESP BIN/manifest는 생성·서명하지 않았고, 기존 1,490,000-byte BIN은 2.0.5가 아님을 확인해 재사용하지 않았다. Arduino IDE에서 OPI PSRAM 설정으로 새 BIN을 export한 뒤에만 `data/ota-2.0.5/` 패키지를 만들 수 있다.

## 2026-10-19 nRF52840 BLE DFU 준비 (실기 미검증)

- 사용자 보고 장치는 V1940/nice!nano V2 호환 nRF52840 clone, Board-ID `nRF52840-nicenano`, UF2 bootloader 0.6.0, S140 6.1.1이다. nice!nano 문서와 Adafruit upstream은 이 계열이 Adafruit nRF52 bootloader를 사용하며 BLE OTA를 지원한다고 설명한다. 설치된 Adafruit core 1.7.0의 Feather target도 S140 6.1.1/FWID `0x00B6`와 application max 815,104 bytes를 사용한다.
- 기존 부트로더를 재사용하도록 애플리케이션에 core `BLEDfu` 서비스를 NUS보다 먼저 등록했다. 이 서비스는 `GPREGRET=0xB1`로 기존 bootloader에 진입할 뿐 bootloader/SoftDevice/플래시 레이아웃을 쓰지 않는다. NUS 광고, 프레임, 측정 및 ESP 파서는 변경하지 않았다.
- core가 생성하는 DFU ZIP은 application BIN/DAT만 포함하고 manifest의 device type은 `0x0052`, SoftDevice requirement는 `0x00B6`이다. 이는 legacy DFU이며 기본 구성에서 암호학적 서명을 보장하지 않으므로 ESP32 signed OTA와 별도 신뢰 경로로 관리한다. 첫 DFU-enabled application 설치에는 USB가 필요하다. 실제 clone에서 buttonless 진입, 스마트폰 전송, 전원 중단 복구는 아직 검증하지 않았다.
- 최종 nRF 빌드는 Flash 160,368/815,104 bytes, RAM 16,420/237,568 bytes로 통과했다. 생성 ZIP을 `data/nrf-dfu-2.0.5/bme688-node-2.0.5-legacy-dfu.zip`에 별도 보존했다. ZIP은 161,238 bytes이며 application BIN 160,376 bytes, DAT 14 bytes와 manifest만 포함한다. manifest의 device type 82, SoftDevice requirement 182와 DFU format 0.5를 확인했고, BIN에서 `2.0.5` 및 `BLE DFU service` 문자열을 확인했다. 같은 폴더의 `SHA256SUMS.txt`는 전송 무결성 확인용이며 legacy DFU에 서명 인증을 추가하지 않는다.

## 2026-10-19 HTTPS TLS 메모리 최소화 (2.0.4 소스, 실기 미검증)

- 2.0.3 실기 진단에서 POST 직전 internal 8-bit 최대 연속 블록은 31,732 bytes였고 TLS가 `-32512` (`SSL - Memory allocation failed`)로 실패했다. ESP32 core 설정의 TLS content 길이는 16KiB이며, 직전 요청 객체에 serialized JSON과 별도로 120-byte `Sample` 32개 전체 복사본(3,840 bytes)이 유지되고 있었다. 네트워크 작업은 ACK 확인에 `boot`와 `rx`만 사용하므로 2.0.4/build 2026101902에서는 이를 36-byte ACK identity 32개(1,152 bytes)로 바꿔 POST TLS handshake 동안의 job 점유를 2,688 bytes 줄였다. ACK UID와 허용 상태 비교도 임시 Arduino `String` 없이 고정 버퍼/`strcmp`로 수행한다. POST가 요청 본문을 모두 소비한 뒤 serialized JSON도 즉시 해제해 redirect GET의 두 번째 TLS handshake 동안 유지하지 않는다. UID, ACK 판정, SPIFFS 형식과 삭제 조건, OTA 및 Wi-Fi 로직은 변경하지 않았다.
- POST/GET 전후의 existing heap diagnostics를 유지하고 JSON 해제 직후 `POST-body-free` 진단을 추가했다. 따라서 2.0.4 실기에서는 `HTTPS heap POST-pre`의 `largest8`가 기존 31,732보다 증가하는지, `POST-body-free`에서 GET 전 가용 연속 블록이 회복되는지, POST 302/GET 200/application ACK가 반복 성공하는지를 확인해야 한다. Arduino IDE 컴파일 및 장치 시험은 사용자가 수행하며, 이 변경만으로 TLS 문제가 해결됐다고 아직 검증하지 않았다.
- spool에는 application ACK 뒤 한 파일만 제거하는 기능만 있고 수동 전체 삭제 명령은 없다. 자동 삭제나 포맷은 추가하지 않았다. Wi-Fi는 부팅 시 NVS의 slot 0부터 각 20초 동안 순서대로 시도하고 실패하면 다음 slot으로 이동하며, 연결된 뒤에는 스캔하거나 전환하지 않는다. 이번 변경에서 Wi-Fi 코드는 수정하지 않았다.

## 2026-10-19 HTTPS TLS 메모리 진단 (소스 수정, 실기 미검증)

- 사용자 실기 로그에서 2.0.2가 한 차례 POST 302/redirect GET 200/application ACK에 성공해 spool을 796에서 790으로 줄인 뒤, POST TLS 단계에서 mbedTLS `-32512` (`SSL - Memory allocation failed`)를 반복 반환했다. 실패 후 표시된 free heap은 약 67~69KiB였으나 ESP32 core 3.3.12는 TLS 연결 실패 시 `NetworkClientSecure::connect()` 안에서 `stop()`으로 TLS context를 먼저 해제하므로 이 수치만으로 handshake 직전 메모리나 누수를 판정할 수 없다.
- gateway의 POST/GET `HTTPClient`, `WiFiClientSecure`, 응답 `String`, ACK cJSON은 각 요청에서 scope 종료 또는 명시적 `end()`/`stop()`/`cJSON_Delete()`로 정리된다. 현재 소스에서 성공·오류별 조기 return으로 이를 건너뛰는 경로는 확인되지 않았다. 명백한 소유권 누수는 확인되지 않았고, 16KiB TLS content buffer가 필요한 환경에서 연속 최대 할당 블록 부족, 힙 단편화 또는 동시 할당에 의한 순간 부족을 아직 구분하지 못했다.
- 전송·ACK·UID·spool·OTA 동작은 유지하고 2.0.3/build 2026101901 진단 소스에서 POST 전/정리 후, GET 전/정리 후, job 해제 후의 internal 및 `MALLOC_CAP_8BIT` free/largest/minimum heap을 기록한다. 기존 2.0.2 BIN/manifest는 변경하지 않았다. `git diff --check`만 통과했으며 Arduino IDE 컴파일, 장치 설치와 새 로그 실기 검증은 사용자가 수행해야 한다.

## 2026-10-18 OTA 수신 안전 상태 수정 (소스 및 로컬 검증)

- 사용자가 Arduino IDE에서 내보낸 2.0.2/build 2026101801 BIN을 `data/ota-2.0.2/`에 별도 보존하고 기존 도구로 서명 manifest를 생성했다. 빌드 설정은 ESP32-S3 16MB, custom partition, PSRAM disabled, Hardware CDC/JTAG, erase disabled이며 BIN은 1,490,000 bytes로 3MiB OTA 슬롯에 들어간다. BIN 복사본 SHA-256, manifest SHA-256, 펌웨어 내장 공개키의 ECDSA 서명, target/version/build 형식과 2.0.1/build 2026101702 대비 anti-rollback 조건을 독립 검증했다. BIN이 소스보다 늦게 생성됐고 2.0.2 및 HTTPS 단계 진단 문자열을 포함함을 확인했다. 기존 `data/ota-2.0.1/` 파일은 변경하지 않았으며 실제 장치 OTA는 수행하지 않았다.
- HTTPS `-1` 장기 장애를 DNS/TCP/TLS 단계로 구분하기 위해 진단 빌드를 2.0.2/build 2026101801로 식별하고, 기존 POST 및 redirect client의 실패 직후 `WiFiClientSecure::lastError()`만 기록한다. 새 DNS 조회나 진단 소켓을 만들지 않으며 대상 host/port, DNS 성공 여부, 단계, secure 오류 코드·설명, Wi-Fi 상태/RSSI/heap만 출력한다. endpoint 경로·payload·token은 출력하지 않고 POST/redirect/ACK/retry/spool 동작은 변경하지 않았다. 기존 서명된 2.0.1 패키지는 변경하지 않았다. Arduino IDE 컴파일은 사용자가 완료했으며 장치 설치와 이 변경의 실기 결과는 미검증이다.
- **사용자 OTA 실기 검증 및 후속 클라우드 장애:** 사용자는 2.0.0/build 2026101701에서 서명된 2.0.1/build 2026101702로 OTA 설치, 재부팅, 부팅 자체 검사와 rollback 해제, SPIFFS 마운트, BLE 및 Wi-Fi 연결을 확인했다. 이후 일반 업로드가 `HTTP POST result=-1`/`connection refused`로 반복 실패하며 ACK 전 UID와 spool을 보존하고 있다. 2.0.1 BIN에는 현 소스와 같은 Apps Script endpoint가 들어 있고 일반 `networkTask()`는 정상 기준 커밋 `ee14429`와 공백 정규화 후 동일하다. ESP32 core 3.3.12의 이 오류는 HTTPS client의 `connect(host, port, timeout)`가 false인 DNS/TCP/TLS 연결 단계 공통 오류라 기존 로그만으로 세부 원인은 확정할 수 없다. OTA rollback 해제 코드는 같은 부팅의 정상 application ACK를 요구하므로 영구적인 URL/포트 오류와도 맞지 않는다. 앱 슬롯 변경은 NVS/SPIFFS를 변경하지 않으며 endpoint는 펌웨어 상수다.
- `OTA?` 파서는 줄바꿈이 들어온 `Serial`만 읽지만 진단 로그는 `Serial`과 `Serial0` 양쪽에 출력한다. 따라서 `Serial0` 쪽 모니터 또는 `No line ending` 설정에서는 로그가 보여도 명령 응답이 없을 수 있다. 실제 사용 포트/line-ending 상태는 확인하지 않아 원인으로 확정하지 않았다.
- 기존 서명된 2.0.1 패키지의 build `2026101702`와 USB 설치한 수신 펌웨어의 build가 같아 anti-rollback 조건(`manifest build > running build`)에서 manifest가 거부됨을 확인했다. 패키지의 형식, BIN SHA-256, 공개키 서명은 모두 유효했다. 재사용할 수 있도록 수신용 2.0.0의 build만 `2026101701`로 복구했으며, 컴파일·장치 설치·OTA 재시험은 사용자가 수행해야 한다.
- ESP32 OTA에 NORMAL → OTA_PREPARE → OTA_ACTIVE를 추가했다. PREPARE는 클라우드가 녹화 STOPPED를 확인한 뒤 기존 RAM·SPIFFS·HTTP 작업의 ACK/완료를 기다린다. ACTIVE 진입은 BLE 샘플 큐 등록과 같은 mutex 안에서 최종 빈 상태를 검사한다. ACTIVE에는 BLE 수집 공백을 명시적으로 기록하고 큐/스풀 신규 쓰기 및 일반 업로드를 중지한다. 실패·취소·타임아웃에는 OTA 쓰기를 중단하고 NORMAL 수집 및 클라우드 상태 복귀를 시도한다. 기존 미승인 UID, spool 파일, BLE 프로토콜, Wi-Fi 설정과 파티션은 변경하지 않았다.
- BIN 실제 수신 바이트 수와 WebServer 보고 크기, `Update.write()` 결과, 서명된 manifest의 SHA-256을 검사한 뒤에만 `Update.end(true)`를 호출한다. HTTP 409 안전 조건과 HTTP 400 패키지·기록·이미지 오류는 별도 로그로 남긴다. `Update.end(true)`의 이미지 검증 경로는 설치된 ESP32 core 3.3.12 소스에서 확인했다.
- `node --test tests/cloud.test.cjs` 30개, `python tests/ota_partition_test.py`, `tests/ota_signing_test.ps1`, 수정 ESP 파일의 `git diff --check`가 통과했다. 이는 모의 서버/정적 구성을 다루는 로컬 검증이며, 변경된 ESP 펌웨어의 실제 OTA 및 하드웨어 동작은 아직 검증하지 않았다. 장치 접근·펌웨어 업로드·플래시 쓰기는 하지 않았다.

## 2026-10-17 ESP32 OTA 현장 준비와 상태 표시

- **2.0.1 최초 OTA 실기 시험 준비(후속 읽기 전용 진단):** 사용자 승인 후 COM10에서 `0x8000` 파티션 테이블 4KiB만 `esptool --no-stub read-flash`로 읽었다. 실제 장치 테이블은 NVS `0x9000`/20KiB, otadata `0xE000`/8KiB, app0 `0x10000`/3MiB, SPIFFS `0x310000`/896KiB, coredump `0x3F0000`/64KiB, app1 `0x400000`/3MiB이고 로컬 빌드 테이블의 3072바이트와 일치한다(나머지 1024바이트는 `0xFF`). 플래시는 쓰지 않았다. 현재 실행 앱 슬롯/다음 OTA 슬롯은 이 테이블만으로는 확정하지 않았다.
- 기존 2.0.1 `.bin`(1,487,616 bytes)을 `data/ota-2.0.1/`에 복사하고 기존 서명 스크립트로 `.manifest`를 만들었다. 실제 파일의 version `2.0.1`, build `2026101702`, SHA-256과 펌웨어에 내장된 공개키에 대한 ECDSA 서명을 검증했다. 개인키/토큰은 출력하지 않았다.
- 진단 재시작 뒤 COM10 `OTA?` 응답은 `upload=disabled version=2.0.0 build=2026101701`이었다. 이전 IP `192.168.0.142` 접속은 timeout이며 현재 IP는 미확인이다. 실시간 로그에서 `Batch upload start n=1 spool=4` 뒤 `HTTP -1 application ACK=0 samples=1`, `spool=8`, 30초 재시도가 관찰됐다. 과거 backlog drained/STOPPED 로그는 현시점 준비 증거가 아니다. 따라서 OTA 안전 조건(인증 설정, Wi-Fi/클라우드 연결, RAM/spool 완전 배출, 녹화 중지)을 충족하지 못해 **실제 OTA 전송은 보류**했다. 미승인 spool은 유지했고 추가 업로드/포맷/삭제는 하지 않았다.

- **사용자 실기 보고:** ESP32 펌웨어 2.0.0을 USB 업로드했고 부팅 로그에서 버전, SPIFFS 마운트, 로컬 OTA 주소, Wi-Fi/클라우드 ACK를 확인했다. 실제 장치의 파티션 테이블·실행 앱 슬롯·다음 OTA 슬롯·Wi-Fi OTA 성공/롤백은 아직 읽거나 시험하지 않았다.
- 설치된 ESP32 Arduino core 3.3.12에서 `esp32s3`/16MB/Huge APP/PSRAM disabled/USB CDC enabled/Hardware CDC-JTAG로 빌드할 때 스케치의 `partitions.csv`가 core 기본 CSV보다 우선한다. 빌드 산출물에서 app0/app1 각 3MiB, `otadata` `0xE000`, SPIFFS `0x310000`/`0xE0000`를 확인했다. N16R8의 PSRAM은 현재 검증된 펌웨어 설정대로 사용하지 않는다.
- 다음 OTA 이미지의 버전/빌드를 2.0.1/2026101702로 올리고, OTA 부팅 self-check와 rollback 확정 성공 후에만 GPIO48 NeoPixel이 3초간 비차단 무지개 표시 후 기존 상태색으로 복귀하게 했다. 장치 업로드/OTA 전송은 하지 않았다.
- 노란색(주황색)은 BLE와 Wi-Fi가 연결됐으나 클라우드 ACK 대기 또는 미전송 backlog가 있을 때의 `PIXEL_WAIT`이다. nRF `MODE_COOL` 타이머는 60초이고 스캔 timeout은 60초, 주기는 120초다. ESP는 BLE 단절 시 마지막 `sensorHealth.mode`를 지우지 않으므로 대시보드 COOL 표시가 오래 남을 가능성은 있으나, 보고된 약 12분 현상의 실제 원인은 해당 구간의 `STAT MODE`/스캔/복구/BLE 로그와 화면 경과 시각 없이 확정할 수 없다. 안전 타이머와 주기는 바꾸지 않았다.
- `arduino-cli compile` ESP32 2.0.1 통과(Flash 1,487,461/3,145,728 bytes; RAM 61,756/327,680 bytes; OTA용 `.bin` 1,487,616 bytes). 생성된 `partitions.bin`을 복호화해 NVS `0x9000`, otadata `0xE000`, app0 `0x10000`/3MiB, SPIFFS `0x310000`/896KiB, coredump `0x3F0000`, app1 `0x400000`/3MiB를 확인했다. `gateway_ui_test.cpp`, mock cloud 30개, 파티션 검사, 서명 검사, `git diff --check` 통과. 이는 로컬 검증이며 새 펌웨어의 실기 부팅/OTA 동작은 미검증이다.

## 2026-10-17 다중 장치 대시보드 초기 조회 (로컬 검증)

- 기존 22열 `EnvironmentHistory`가 있으면 `getDashboardDevices()`의 `sheets()`가 헤더 불일치로 실패하는 경로를 호스트 테스트에서 재현했다. 기존 행은 유지하고 마지막 `device_id` 헤더만 확장하도록 수정했다. 0대·오류·잘못된 응답·응답 지연은 대시보드 메시지로 표시한다. 모의 Sheets/브라우저 테스트 30개와 `git diff --check`가 통과했다. 운영 Apps Script 배포·실제 Sheets 조회는 이번 작업에서 수행하지 않았으며, 운영 화면의 정확한 실패 원인은 콘솔/Apps Script 실행 기록으로 확인해야 한다.

## 2026-10-17 다중 장치/ESP32 OTA stage 2 (OTA 구현 준비, 실기 미검증)

- Arduino IDE가 보고한 OTA SHA-256 컴파일 오류 3건에 대해 설치된 ESP32 core 3.3.12의 `mbedtls/sha256.h`를 확인했다. `mbedtls_sha256_starts`, `mbedtls_sha256_update`, `mbedtls_sha256_finish`가 모두 `int`를 반환하며 0은 성공이다. 펌웨어의 `_ret` 호출 3곳만 해당 선언으로 변경했고 기존 실패 시 reset 및 이미지 digest 비교는 유지했다. 호스트 테스트 28개, 파티션 검사, 서명/무결성 검사, `git diff --check`가 통과했다. CLI 재빌드는 `xtensa-esp32s3-elf-g++.exe` 실행 전 Windows `Illegal System DLL Relocation`에서 멈춰 OTA 소스 컴파일 성공은 아직 확인되지 않았다. 장치 업로드는 하지 않았다.
- 사용자는 컴퓨터 재부팅 후 다중 장치 stage 1의 nRF와 ESP32 Arduino IDE 컴파일 성공을 보고했다. 새 OTA source의 ESP32 컴파일을 CLI로 시도했지만 `xtensa-esp32s3-elf-g++.exe` 시작 시 Windows `Illegal System DLL Relocation`이 재현됐다. sandbox 외 CLI 재시도는 60초 이상 출력 없이 진행되어 중단했으며 성공으로 보지 않는다. OTA 변경의 펌웨어 빌드는 아직 미확인이다.
- ESP32 로컬 OTA updater, USB serial `OTA TOKEN` 인증, ECDSA P-256 서명 manifest + SHA-256 이미지 검증, 업데이트 중 명시적 사용자 승인, SPIFFS/큐 비어 있음 검사, 부팅 self-check 후 rollback 확정 경로를 준비했다. 앱 파티션 0/1은 각각 3 MiB이고 app1은 `0x400000`에 둔다. NVS/otadata/app0/SPIFFS/coredump의 현재 Huge APP 오프셋과 크기를 그대로 유지한 후보 `partitions.csv`를 추가했다. 현재 ESP32-S3 sdkconfig는 app rollback을 활성화한다.
- Apps Script는 장치별 `UPDATING`/`VERIFYING` 상태를 `DeviceRegistry` 마지막 열에 보존하고, recording 시작과 OTA 예약을 script lock 안에서 상호 배타적으로 확인한다. dashboard에는 장치의 local IP updater 링크와 OTA 상태를 표시한다. production Apps Script 배포는 하지 않았다.
- `node --test tests/cloud.test.cjs`: 28개 통과(mock Sheets); `python tests/ota_partition_test.py` 통과; `tests/ota_signing_test.ps1` 통과(임시 더미 이미지); `git diff --check` 통과. OTA source 빌드와 실제 rollback, 실제 partition table, OTA 성공/실패, 운영 배포는 미검증이다. USB를 통한 최초 파티션 표 설치는 현재 실기 table/SPIFFS 읽기 전용 백업과 별도 명시적 승인 후에만 진행해야 한다. 상세 실행 전제와 절차는 [ESP32 Wi-Fi OTA stage 2](docs/ESP32_WIFI_OTA_STAGE2.md)에 있다.

## 2026-10-15 다중 장치 stage 1 (구현 완료, Arduino 빌드·실기 미검증)

- 기준 `ee14429df974724311634cb6b7357a2cbda498a0`에서 `feature/multi-device-support` 브랜치를 만들었다. ESP32 eFuse MAC 기반 안정 `device_id`, nRF 공장 ID를 넣은 NUS 광고 이름, 별도 NVS namespace의 ESP 센서 pairing target, Apps Script 장치별 live/recording cache와 Sessions, 추가 `device_id` 열, 장치별 CSV 및 세로형 dashboard panel을 구현했다. 기존 gateway boot UID·ACK high-water·journal/retry와 Wi-Fi NVS namespace는 유지했다. 기존 행의 빈 device ID는 `legacy`로 취급한다.
- 가상 장치 cloud/UI 테스트 `node --test tests/cloud.test.cjs`: 27개 통과(mock Sheets only; 실제 운영 배포/Sheets 변경 아님). `python tests/fingerprint_test.py`: 5개 통과. MSVC C++17 integrity/sensor/spool/gateway_ui 4개 통과. `git diff --check`: 통과.
- 현재 실행 환경에서 Arduino CLI는 installed cores를 찾았으나 nRF/ESP 빌드의 컴파일러 실행이 Windows `Illegal System DLL Relocation`으로 중단되어 펌웨어 빌드는 확인하지 못했다. Apps Script 배포도 하지 않았다. 현재 실기기는 1호기만 있고, 새 pairing·동시 BLE 연결은 미검증이다.
- Apps Script 첫 실행 때 SensorData/EnvironmentHistory/Unassigned 마지막 열과 Sessions의 다섯 번째 열을 추가하고 `DeviceRegistry` 탭을 만든다. 기존 데이터와 열 순서는 보존하도록 설계했다. 변경·마이그레이션 및 롤백·2호기 점검 절차는 [MULTI_DEVICE_STAGE1.md](docs/MULTI_DEVICE_STAGE1.md)에 있다.
- ESP OTA, nRF BLE DFU, ESP 중계는 이번 단계에서 구현하지 않았다. 정상 설정 복구를 위해서는 정확한 파티션/바이너리 용량 비교와 nice!nano의 실제 bootloader DFU 호환성 확인 후 별도 승인을 받아야 한다.

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
