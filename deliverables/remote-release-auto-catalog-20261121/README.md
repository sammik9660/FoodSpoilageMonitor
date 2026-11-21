# 공개 GitHub Release 자동 목록

- `RemoteUpdates.gs`: 기존 인증·명령 경로를 유지하고 공개 GitHub Release 목록을 자동 조회하는 로컬 후보. 운영 배포 전 검토용입니다.
- `Dashboard.v45-user-supplied.html`: 사용자가 제공한 버전 45 후보 HTML의 별도 사본입니다. 운영 버전과 바이트 동일성은 독립 확인하지 못했습니다.
- GitHub 공개 릴리스 자산의 SHA-256·크기·정확한 파일명으로 ESP BIN/manifest와 nRF Legacy DFU ZIP을 인식합니다. `HOLD_*.txt` 포함, draft, prerelease, 자산 누락/중복은 제외합니다.
- 기존 `REMOTE_UPDATE_CATALOG` 속성은 읽기 장애 시 대체 목록으로 남습니다. 2.1.0을 추가하기 위해 이 속성을 다시 편집할 필요는 없습니다.
- 최초 한 번은 **실제 Apps Script 편집기**의 `RemoteUpdates.gs`를 이 파일로 교체하고, 현재 정상 동작하는 `Dashboard.html`을 유지한 채 별도 테스트 배포에서 확인해야 합니다. 편집기의 HTML이 여전히 고장 난 최신 코드라면 새 버전을 만들기 전에 버전 45 HTML을 먼저 복원해야 합니다.
- `UrlFetchApp` 외부 요청 권한이 새로 필요할 수 있습니다. 운영 배포와 장치 OTA/DFU는 수행하지 않았습니다.

2026-11-21 운영 피드백: 최초 자동 목록 파일은 GitHub 조회/권한 오류를 숨겨 기존 두 항목만 표시할 수 있었습니다. 현재 RemoteUpdates.gs는 실패 코드를 목록 오류로 표시하며, 이전 파일은 RemoteUpdates.previous-silent-fallback.gs로 보존했습니다. 이 진단 파일 역시 운영 배포 전 검토용이며 별도 배포는 수행하지 않았습니다.

진단: 현재 파일에는 토큰 없는 diagnosePublicReleaseCatalog 함수가 있습니다. Apps Script 편집기에 저장한 뒤 함수 선택에서 이 이름을 실행하면 실행 로그에 status와 공개 릴리스 ID만 나옵니다. 편집기에서 실행하는 것만으로 운영 웹 앱 버전이나 장치 상태는 바뀌지 않습니다. 필요한 외부 요청 권한이 있으면 이때 승인 창이 나타날 수 있습니다.
