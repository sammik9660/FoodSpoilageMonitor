# ESP32-S3 독립 Wi-Fi 연결 진단

`firmware/esp32s3/wifi_connection_diagnostic/wifi_connection_diagnostic.ino`은
기존 게이트웨이와 별도로 컴파일하는 진단 스케치다. Wi-Fi 자격 증명을
시리얼로 입력받아 RAM에서만 사용한다. 스케치는 SPIFFS/NVS API를 직접
호출하지 않고 기존 Wi-Fi 목록이나 OTA/DFU 설정을 변경하지 않는다.
`WiFi.persistent(false)`로 진단 연결 설정의 플래시 저장을 끈다.
비밀번호를 소스와 로그에 넣지 않는다. **컴파일 성공은 실기 연결 성공의
증거가 아니다.**

## 실기 시험 전

- 가능하면 예비 ESP32-S3 N16R8에서 먼저 실행한다. 운영 게이트웨이에
  USB 업로드하면 실행 중인 애플리케이션을 대체한다. 기록 중이거나 미전송
  RAM·SPIFFS 샘플이 있을 때 업로드하지 않는다. 원래 펌웨어로 복구할 수
  있는 BIN과 Arduino IDE 설정을 먼저 확보한다.
- Arduino IDE 설정: ESP32S3 Dev Module, ESP32 core 3.3.12, Flash Size 16MB,
  PSRAM OPI, USB CDC On Boot Enabled, USB Mode Hardware CDC and JTAG,
  Partition Scheme Custom, Erase All Flash Disabled. 진단 폴더의
  `partitions.csv`는 게이트웨이 파일과 동일한 기존 배치다. 전체 플래시
  삭제와 SPIFFS 이미지 업로드는 하지 않는다.
- 실기 업로드는 사용자가 직접 한다. 시리얼 모니터는 115200 baud,
  **Newline** 설정으로 열고 SSID와 비밀번호를 각 프롬프트에 입력한다.
  입력창에 비밀번호가 남을 수 있으므로 공유할 로그나 화면에서 가린다.

## 출력 읽기

1. `CONNECT AUTO`는 스캔 전에 게이트웨이와 같은 두 인자
   `WiFi.begin(ssid,password)`를 사용한다. 첫 해제 이벤트 뒤에도 총
   20초 관찰해, 게이트웨이의 조기 슬롯 전환이 결과에 영향을 주는지
   분리한다. 성공하면 연결 RSSI를 출력한다.
2. AUTO 실패 뒤 `SCAN after-auto-1/2`는 대상 SSID를 찾은 횟수,
   RSSI(dBm), 채널, 인증 방식과 BSSID 마지막 세 바이트를 출력한다.
   비밀번호는 출력하지 않는다. 찾은 AP가 있으면 가장 강한 BSSID/채널을
   고정한 `CONNECT PINNED`를 한 번 수행한다.
   비밀번호와 보안 임계값은 바꾸지 않는다.
3. `STA_CONNECTED`와 `STA_GOT_IP` 및 `CONNECT ... success`가 나와야 연결
   성공이다. 스캔 발견이나 입력 저장만으로 성공으로 보지 않는다.

AUTO가 성공하고 현재 게이트웨이가 실패한다면 게이트웨이의 재시도 상태
머신·BLE 공존·저장 자격 증명 경로를 우선 비교한다. PINNED만 성공하면
자동 검색·BSSID 선택 경로를 조사한다. 강한 RSSI로 대상 AP가 반복 검색돼도
둘 다 인증 실패하면 공유기 인증 설정·입력값·기기별 허용 정책을 비교한다.
두 스캔 모두 AP를 찾지 못하면 2.4GHz 신호·위치·채널부터 확인한다.
어느 결과도 단독으로 보드 고장을 확정하지 않는다.

시험 후 운영 장치였다면 기존 게이트웨이 펌웨어를 같은 16MB/OPI/Custom
partition/Erase Disabled 설정으로 사용자가 복구하고, 부팅 버전,
SPIFFS mount, spool pending/fault, BLE 연결, Wi-Fi, 실제 application ACK를
각각 확인한다. 진단 스케치는 원래 펌웨어나 기존 OTA 패키지를 수정하지 않는다.
