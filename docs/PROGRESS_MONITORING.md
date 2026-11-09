# 측정 진행 감시 및 CSV 전처리 — 로컬 구현

## 확인한 결함과 복구 범위

기존 `serviceGasFrames()`는 BLE 연결이 유지돼도 Notify가 꺼져 있으면 즉시
반환한다. 미전송 프레임은 유지되지만 다음 GAS 스캔은 pending이 없어야
시작하므로 무기한 지연될 수 있다. `sensor_progress_test.cpp`는 실제 센서
소스를 포함해 이를 5시간 가상 시간으로 재현한다. 수정 전에는 재연결
시도가 0회여서 실패했고, 수정 후에는 원본 프레임을 보존하며 통과한다.

nRF 2.0.13은 TX 진행이 120초 없을 때 BLE disconnect를 요청한다. 이후
60초, 120초, 최대 300초 간격으로 제한하며 자동 재광고와 게이트웨이의
기존 재연결 경로를 사용한다. 재연결해도 CCCD가 복구되지 않으면 다시
기다린다. 프레임·SEQ·MS를 삭제하거나 재생성하지 않는다. 성공 전까지
다음 GAS 스캔을 덮어쓰지 않는 정책은 유지된다. 로그에는 TX 정체,
Notify 상태, 재연결 횟수와 RAM 데이터의 재부팅 손실 위험이 나온다.

ENV 상태에서 유효 측정이 180초 넘게 진행되지 않으면 기존 센서 복구를
호출한다. 초기화만으로 정상으로 표시하지 않으며 유효 ENV가 필요하다.
기존 GAS 60초 제한, forced conversion 200ms 제한, 60초 냉각과 120초
loop liveness 감시는 유지한다. watchdog마다 무조건 MCU를 재시작하지 않는다.

**한계:** 연결 상대가 계속 Notify를 활성화하지 않으면 무손실 자동 진행을
보장할 수 없다. 유한 RAM 프레임을 보존하며 신규 스캔을 보류하고 정체를
기록한다. 전원 차단·기존 loop watchdog 리셋 시 RAM 프레임은 소실될 수 있다.
NUS write 성공은 서버 ACK가 아니며 기존 프로토콜의 전달 한계도 그대로다.
이번 코드 결함 재현이 실제 보고된 장애의 원인을 입증하지는 않는다.

## 클라우드만 적용하는 변경

`Code.gs`, `Dashboard.html`, `Monitoring.gs`를 같은 Apps Script 프로젝트에
사용한다. 현재는 로컬 파일만 수정했으며 배포·트리거 생성·메일 발송을
수행하지 않았다. 대시보드는 STALE에 환경 측정 정체 또는 cloud contact
미확인 설명을 덧붙인다. 연결됨이라는 지표만으로 정상 측정을 주장하지 않는다.

독립 `monitorTick_`은 LIVE cache와 이전 감시 스냅샷을 이용한다. cache가
사라져도 이전 접촉 시각은 Script Properties에 유지한다. 최초 설치 시
이력이 없으면 CONTACT_UNKNOWN이며 오프라인 원인을 Wi-Fi로 단정하지 않는다.

- heartbeat/클라우드 ACK 60초, ENV 5분, GAS 20분 지연 감지.
- GAS/COOL/REC 장기 체류, backlog/ACK 정체, spool 오류/용량, queue 손실 감지.
- USB가 아닌 유효 배터리 추정 잔량 5% 이하를 경고. 과방전 보호나 정확한
  용량 판정으로 취급하지 않는다.
- 녹화 중 완전 GAS 스캔 장기 미확인 감지. 기존 저널 커밋 뒤에만 관측
  스냅샷을 갱신하며 STOPPED/not_recording ACK를 저장 성공으로 세지 않는다.
- 정상 GAS/COOL 중 ENV 정지와 OTA UPDATING/VERIFYING을 구분한다.
- 동일 장애가 60초 지속되어야 최초 1회 알림, 지속 중 반복 알림 없음, 복구 시 1회 RECOVERED. 60초 미만 복구는 무알림이다. 새로운
  관측값이 없으면 과거 센서/저장 오류를 복구로 처리하지 않는다.
- 이메일 실패/할당량 부족 시 5분 이후 재시도. 메일 발송 중 ingest lock을
  점유하지 않는다. 원자적인 메일+상태 commit은 불가능하므로 발송 직후
  실행 중단 또는 lock 획득 실패 시 중복 메일 가능성이 남는다.
- 장치당 최근 이력 20개, 발송 대기 10개, 진행 스캔 4개로 제한한다.
  발송 대기 초과는 `outboxOverflow`에 기록한다. 이는 전체 장애 아카이브가
  아니며 장기 보존 기능으로 주장하지 않는다.

운영 승인을 받은 뒤에만 다음 Script Properties를 설정한다.

| 이름 | 값 |
|---|---|
| MONITOR_ENABLED | `true` (기본 비활성) |
| MONITOR_DEVICE_IDS | JSON 장치 ID 배열 |
| MONITOR_RECIPIENTS | 수신 주소, 여러 주소는 쉼표 구분 |

승인 후 Apps Script 편집기에서 `installMonitorTrigger_`를 1회 실행해
1분 트리거를 설치하고 메일 권한을 승인한다. 함수 이름 끝 `_`는 브라우저
RPC 호출을 막는다. 동일 핸들러 트리거가 있으면 중복 설치하지 않는다.
중지는 `MONITOR_ENABLED=false`로 한다. 메일 주소는 소스에 넣지 않는다.
[MailApp](https://developers.google.com/apps-script/reference/mail/mail-app)과
[서비스 할당량](https://developers.google.com/apps-script/guides/services/quotas)을
계정 기준으로 확인한다. cache/trigger/Properties 장애까지 독립적으로
감시하는 외부 서비스는 이번 범위에 없다.

ESP32 펌웨어는 이번 변경에서 수정하지 않았다. 기존 health의 ENV/GAS age,
cloud ACK, queue/spool을 클라우드에서 감시한다. cloud contact가 없으면
Wi-Fi·전원·HTTP 중 무엇이 원인인지 확정할 수 없다. 현재 telemetry에 없는
Notify 상태, PHY 손상, 개별 OTA 실패 사유도 감지됐다고 주장하지 않는다.

## CSV 실행

```powershell
python analysis/prepare_dataset.py "PAGE1.csv" "PAGE2.csv" --output data/audit-new
python analysis/prepare_dataset.py "PAGE1.csv" --sessions sessions.json --output data/audit-labeled
powershell -NoProfile -ExecutionPolicy Bypass -File tools/test_reliability.ps1
```

출력 폴더가 이미 있으면 거부한다. 원본 CSV는 읽기 전용으로 보존하며
입력 SHA-256을 기록한다. `quality_report.json`, `features.csv`, `sessions.csv`,
`gaps.csv`, `split_manifest.json`을 생성한다. schema 2/3/4를 구분하며 완전한
HP-354 10-step만 gas/log-gas/T/H/P 특징으로 만든다. 장치별 재구성 후
학습 그룹은 session ID로 유지한다. UID 충돌이 있는 스캔은 격리한다.

세션 메타데이터 예시:

```json
{"EXP-example":{"food":"banana","started_at":"2026-11-04T21:00:00+09:00","split":"train"}}
```

시작 시각이 없으면 경과시간을 추정하지 않는다. 음식 이름이나 신선/부패
정답을 시간·습도에서 만들지 않는다. split 미지정은 UNASSIGNED이며 자동
랜덤 행 분할을 하지 않는다. 이번 단계는 전처리이며 모델 학습/정확도 평가와
Edge AI 배포가 완료된 것은 아니다. 음식별 반복 세션과 검증된 레이블이 필요하다.

## 2026-11-07 실제 파일 실행 결과

`food-spoilage-all (3).csv`: 4,259 valid rows, 중복/UID 충돌 0, 7세션,
완전 스캔 366개, 미완전/무효 스캔 그룹 81개, 20분 초과 GAS 행 간격 8개.
산출물은 `data/reliability-20261107/csv-audit-final/`에 보존한다.
11월 5일 KST 02:53:48~09:06:48의 약 6시간 13분 공백 앞뒤에서 BID가
바뀌고 SEQ가 10→1로 바뀐다. 이는 재부팅 구간의 관측 증거이며 재부팅
원인·공백 중 연결 상태는 CSV만으로 판단할 수 없다. 사용자가 보고한
실험 3일 차 5시간 공백과 같은 사건이라고 확정하지 않는다.

장치 적용은 nRF 펌웨어만, 클라우드 적용은 위 3개 Apps Script 파일이다.
현재 운영 데이터·펌웨어·트리거는 그대로 두고 적용 승인 전까지 실기
검증 완료로 표시하지 않는다. 기존 UART/CSV만으로 무선 안테나 결함을
소프트웨어 결함으로 확정하지 않는다.
