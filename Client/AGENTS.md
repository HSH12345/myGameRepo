# UE 5.8 RPG 클라이언트 작업 안내

이 디렉터리가 현재 실제 클라이언트다.

## AI 작업 하네스

- 작업·테스트·시각 검증 기준: `Docs/HARNESS_ENGINEERING.md`
- 전용 스킬: `C:/Users/hsh/.codex/skills/rpg-client-harness/SKILL.md`
- 공통 학습 문서: `C:/Users/hsh/Desktop/TcpIP/IOCP/MyRpgServer/docs/PROJECT_TECHNIQUES_GUIDE.md`

성능·최적화·유지보수성·가독성을 우선하되 GC, 스레드 소유권, 서버 계약을 깨지 않는다.
스킬 사용이 불가능해도 하네스 문서의 검증 기준을 따른다. AGENTS.md는 실행 중인 별도 에이전트가 아닌 작업 진입 규칙이다.
성능의 전후 측정 여부와 실제 화면 확인 여부를 별도로 보고한다.
테스트 종료 코드 0만으로 통과를 선언하지 않으며, 각 테스트의 발견·완료·성공을 확인한다.
NullRHI나 미리 동기 로드한 프리뷰 테스트를 실제 화면의 T-pose 미노출·cold load 검증으로 간주하지 않는다.

- 클라이언트: `F:\HSH\PF\UE_Workspace\RPG 5.8`
- 서버: `C:\Users\hsh\Desktop\TcpIP\IOCP\MyRpgServer`
- 전체 현재 구조:
  `C:\Users\hsh\Desktop\TcpIP\IOCP\MyRpgServer\docs\CURRENT_ARCHITECTURE.md`
- 전체 구현 현황:
  `C:\Users\hsh\Desktop\TcpIP\IOCP\MyRpgServer\docs\IMPLEMENTATION_STATUS.md`
- 공통 작업 규칙:
  `C:\Users\hsh\Desktop\TcpIP\IOCP\MyRpgServer\AGENTS.md`

`F:\HSH\PF\UE_Workspace\RPG`는 현재 작업 경로가 아니다. 그 경로에 변경을
작성하거나 그곳의 파일을 현재 프로젝트로 덮어쓰지 않는다.

## 현재 클라이언트 범위

2026-10-28 네이버 로그인/취소/로그아웃과 SDK ID Token→Gate/Login→계정 등록·목록 연결을 추가했다.
설정·packet25/26·현재 검증 한계는 서버 `docs/NAVER_GAME_LOGIN.md`를 따른다.
실제 외부 계정 종단/Android 인수는 검증 필요다.

- UE 5.8
- GateServer :9000 TCP 연결 → 내부 LoginServer :9001 (기존 Login 연결 API 이름 유지)
- RPGB binary protocol v2
- guest 신규/재로그인
- heartbeat/reconnect
- Credential Manager token 저장
- 캐릭터 선택 UI 표시까지 구현

캐릭터 목록/생성 API와 로비 UI, ID 기준 로컬 선택은 구현됐다.
2026-09-14 GameSessionSubsystem이 Gate를 통한 서버 선택 확정/티켓/맵 입장,
공유 정적 충돌 이동 예측/보정과 다른 플레이어 보간을 처리한다.
현재 검증 맵은 `/Game/RPG/Maps/Tests/L_MapSyncTest_v3`이며 상세 범위는 서버 `docs/MAP_SYNC_PHASE1.md`를 따른다.
2026-09-17 `/Game/RPG/Maps/Tests/L_PeaceWorldTest_20260915_WP`와 chunk index도 등록했다.
비동기 충돌 캐시/owner 게시와 WP Activated 준비를 분리하며, 검증 현황은 서버 `docs/WORLD_PARTITION_PHASE2.md`를 따른다.
2026-09-22 연결 종료 로딩 세대 무효화/로그인 UI 복원과 E 키 테스트 던전 왕복을 추가했다.
현재 wire/visit 계약과 범위는 서버 `docs/MAP_TRANSITION_PHASE3.md`를 따른다.
2026-09-24 실제 인간 남녀 외형과 WASD/근거리 클릭 이동, 카메라/애니메이션/HUD를 추가했다.
현재 입력·wire 계약과 검증 범위는 서버 `docs/CHARACTER_GAMEPLAY_CONTROL.md`를 따른다.
로비는 `/Game/RPG/Maps/Lobby/L_Lobby`이고 BP_LobbyGameMode가 UI를 생성한다.
에디터 작업 규칙은 서버 `docs/LOBBY_EDITOR_GUIDE.md`를 참고한다.

## Unreal 필수 규칙

- `NewObject`로 만들고 frame을 넘어 보존할 UObject는 `UPROPERTY` 또는 다른
  GC 추적 참조로 소유한다.
- subsystem에서는 소유 `GetGameInstance()`를 우선 사용한다.
- worker thread에서 UObject, UMG, Blueprint delegate를 직접 호출하지 않는다.
- worker→game thread 전달은 bounded queue를 거친다.
- packet 변경 시 서버 codec과 parity test도 같은 작업에서 변경한다.
- token/password를 로그에 출력하지 않는다.

## 빌드

2026-10-28부터 하나의 `RPG.uproject`에서 Windows/Android 빌드 프로필을 관리한다.
`Scripts/BuildClient.ps1 -Platform Windows|Android -Action Check|Build|Package`를 사용한다.
기본 Action은 Check이며 Android 엔진/SDK 누락과 실기기 미검증 범위는
`Docs/PLATFORM_BUILDS.md`를 따른다. 플랫폼별 설정은 `Config/Windows`, `Config/Android`에 둔다.
에디터 빌드는 아래 명령을 유지한다.

```powershell
& 'F:\HSH\PF\UE\UE_5.8\Engine\Build\BatchFiles\Build.bat' `
  RPGEditor Win64 Development `
  -Project='F:\HSH\PF\UE_Workspace\RPG 5.8\RPG.uproject' `
  -WaitMutex -NoHotReloadFromIDE
```

2026-10-16 전투/스킬/고블린 사망과 문서 패턴을 연결했다. 최신 범위는 서버 `docs/COMBAT_SYSTEM.md`를 따른다.
