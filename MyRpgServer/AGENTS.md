# RPG 프로젝트 작업 안내

이 파일은 AI와 개발자가 작업을 시작할 때 가장 먼저 읽는 진입 문서다.
상세 구조와 현황은 아래 문서를 기준으로 한다.

- 현재 구조와 실행 흐름: `docs/CURRENT_ARCHITECTURE.md`
- 완료·미완료 및 다음 작업: `docs/IMPLEMENTATION_STATUS.md`
- 서버 빌드·운영 개요: `README.md`
- 작업·성능·검증 기준: `docs/HARNESS_ENGINEERING.md`
- 코드와 연결한 학습 문서: `docs/PROJECT_TECHNIQUES_GUIDE.md`

## AI 작업 하네스

서버 변경은 `C:/Users/hsh/.codex/skills/rpg-server-harness/SKILL.md`,
클라이언트 변경은 `C:/Users/hsh/.codex/skills/rpg-client-harness/SKILL.md`를
관련 작업 절차로 사용한다. 스킬을 사용할 수 없는 환경에서도 위 하네스 문서를 따른다.
이 AGENTS.md는 작업 지침이며 자동 실행·감시 프로세스를 생성하지 않는다.

- 정확성·데이터 보존을 지키며 성능, 최적화, 유지보수성, 가독성을 우선한다.
- 먼저 실제 소스의 상태 소유자·스레드·실패 정리 경로를 확인한다. 미구현 게임 기능을 완료로 가정하지 않는다.
- 성능은 같은 구성·부하의 전후 측정으로 판단한다. 지연 분포·실패율·메모리를 함께 보고하고 미측정은 명시한다.
- 하네스의 위험별 표에 따라 관련 검사만 선택한다. DB 테스트는 부작용과 격리 대상을 먼저 확인한다.
- 보고에는 코드 위치와 이유, 실행한 검사/결과, 미검증 범위를 포함한다. 과거 검증을 이번 결과로 쓰지 않는다.
- 변경 시 학습 문서의 해당 설명이 여전히 맞는지도 확인한다. 코드 예제는 짧게, 기법과 절충 설명은 명확하게 유지한다.

## 기준 경로

- 서버: `C:\Users\hsh\Desktop\TcpIP\IOCP\MyRpgServer`
- 실제 UE 5.8 클라이언트: `F:\HSH\PF\UE_Workspace\RPG 5.8`
- UE 5.8 엔진: `F:\HSH\PF\UE\UE_5.8`
- Firebase Admin Web: `C:\Users\hsh\Desktop\TcpIP\IOCP\Web\FirebaseAdmin`

`F:\HSH\PF\UE_Workspace\RPG`는 현재 클라이언트 작업 경로가 아니다. 요청이
없는 한 이 경로의 소스를 수정하거나 새 변경을 복사하지 않는다.

## 현재 범위

현재 완료된 vertical slice는 다음까지다.

1. UE 클라이언트가 GateServer `127.0.0.1:9000`에 TCP 연결; Gate가 내부 LoginServer `:9001`로 전달
2. 신규 또는 저장된 게스트 자격 증명으로 로그인 요청
3. LoginServer가 Firebase Admin Web 및 AccountDataServer 내부 RPC로 인증 (직접 MySQL 호출 금지)
4. 성공 응답을 클라이언트가 Windows Credential Manager에 저장
5. 인증된 세션 UID로 CharacterDataServer에서 캐릭터 목록 조회
6. 종족/성별/장비 요약/레벨/이름을 캐릭터 선택 UI에 표시
7. heartbeat, 연결 상실 감지, 제한된 지수 backoff 재접속

GameServer 실행 프로젝트, bounded command queue, 맵별 고정 owner worker와 fixed tick의
기반은 구현됐다. 캐릭터 목록 조회와 생성 서버/API 및 로비 생성 UI는 구현됐다.
2026-09-14 서버 소유권 확인/선택, 일회용 티켓, Gate 경유 UE 게임 입장,
공유 정적 맵 기반 이동/예측/보정을 추가했다. Phase 1 검증 범위는 `docs/MAP_SYNC_PHASE1.md`를 우선한다.
2026-09-17 WP index/청크 캐시, 비동기 읽기/owner 게시, UE Activated 준비 판정을 연결했다.
현재 검증/지원 범위는 `docs/WORLD_PARTITION_PHASE2.md`를 따른다.
2026-09-22 Phase 2 연결 수명 보강과 PeaceWorld/개인 테스트 던전 왕복을 추가했다.
현재 맵 이전 범위와 검증은 `docs/MAP_TRANSITION_PHASE3.md`를 따른다.
2026-09-24 서버 확인 인간 남녀 외형, 카메라 기준 WASD/근거리 클릭 이동과 애니메이션을 추가했다.
현재 조작·wire 변경·인수 범위는 `docs/CHARACTER_GAMEPLAY_CONTROL.md`를 따른다.
2026-10-15 NPC 구매·Gold/32칸 가방·장착 저장/공개 외형·공격/방어 보정을 추가했다.
현재 계약·활성 프로필·검증 한계는 `docs/ITEM_SYSTEM.md`를 따른다.
2026-10-16 전투 평타/캐스팅 스킬·고블린 문서 패턴·피격/MP·사망/리스폰을 추가했다.
현재 전투 계약과 검증 범위는 `docs/COMBAT_SYSTEM.md`를 따른다.
AOI, 위치/HP/MP 영속 저장 및 여러 GameServer 사이의 이전은 미구현이다.
캐릭터 생성 계약과 UI 연결법은 `docs/CHARACTER_CREATION.md`를 참고한다.
현재 로비 레벨/위젯 경로와 검증 범위는 `docs/LOBBY_EDITOR_GUIDE.md`를 참고한다.

서버 역할 명명 원칙은 다음과 같다.

- `AccountDataServer`: 계정 단위 데이터 저장·조회 책임
- `CharacterDataServer`: 캐릭터 단위 저장·조회 책임; 목록 조회/생성 구현, 선택 소유권은 Login이 목록으로 확인
- `GameServer`: 게임 로직과 월드 시뮬레이션 책임, 영속 데이터 서버 역할을 맡지 않음

게이트의 구현 범위와 로컬 전용 보안 제한은 `docs/GATE_SERVER.md`를 참고한다.

## 반드시 지킬 불변 조건

- 프로토콜 다중 바이트 정수는 big-endian이다.
- 프레임은 `Magic(4) + Version(2) + Type(2) + PayloadSize(4)`의 12바이트 헤더다.
- `GuestLoginResponse` 필드 순서는 `result, requestId, token, uid`다.
- 토큰과 DB 비밀번호를 소스, 문서, 로그, 명령 출력에 노출하지 않는다.
- DB에는 토큰 원문을 저장하지 않고 SHA-256 해시만 저장한다.
- Unreal에서 `NewObject`로 만든 장기 생존 객체는 반드시 `UPROPERTY` 또는 다른
  GC 추적 참조로 소유한다.
- `UGameInstanceSubsystem` 내부에서는 전역 viewport 탐색보다
  `GetGameInstance()`를 사용한다.
- ServerCore dispatcher callback은 IOCP worker에서 호출된다. GameServer callback은
  역직렬화·검증 후 `MapScheduler`를 통해 소유 worker의 `GameCommandQueue`에 넣는다.
  각 맵의 상태는 해당 맵 owner worker만 변경한다. DB/CPU 결과도 예약된 owner 완료 큐로 전달한다. 새 게임 handler도 이 경계를 지킨다.
- blocking HTTP/DB 작업을 IOCP worker나 향후 game tick thread에서 실행하지 않는다.

- 캐릭터 생성 재시도는 같은 `operationId`와 같은 입력을 사용한다. 응답 timeout은 실패 확정이 아니다.
- 캐릭터 생성/향후 삭제는 `character_rosters`의 계정별 DB 잠금을 공유한다.
- `character_creation_operations`를 TTL로 삭제하지 않는다. 과거 요청 재실행을 막는 기록이다.

## 프로토콜 변경 체크리스트

패킷을 추가하거나 필드를 변경하면 최소한 다음을 함께 확인한다.

1. `ServerCore/BinaryProtocol.h`
2. 해당 서버의 `Packet/BinaryPackets.h`와 `Protocol/CharacterPackets.h`, `Protocol/CreateCharacterPackets.h`
3. 서버 `RecvManager`/`SendManager`와 handler 등록
4. `F:\HSH\PF\UE_Workspace\RPG 5.8\Source\RPG\Packet\BinaryProtocol.h`
5. UE `Packet/BinaryPackets.h`, `Packet/CreateCharacterPackets.h`, `Managers/CharacterTypes.h`
6. UE `SendProcessor`/`ReceiveDispatcher`
7. `Tests/BinaryProtocolTests.cpp`
8. `Tests/ProtocolParityTests.ps1`
9. 두 상세 문서의 packet catalog와 현황

## 검증 명령

서버:

```powershell
msbuild MyRpgServer.sln /m /p:Configuration=Debug /p:Platform=x64
.\x64\Debug\BinaryProtocolTests.exe
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tests\GameServerIntegrationTests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tests\CharacterDataIntegrationTests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tests\CharacterCreationIntegrationTests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tests\ProtocolParityTests.ps1 `
  -ClientRoot 'F:\HSH\PF\UE_Workspace\RPG 5.8'
```

UE 5.8 클라이언트:

```powershell
& 'F:\HSH\PF\UE\UE_5.8\Engine\Build\BatchFiles\Build.bat' `
  RPGEditor Win64 Development `
  -Project='F:\HSH\PF\UE_Workspace\RPG 5.8\RPG.uproject' `
  -WaitMutex -NoHotReloadFromIDE
```

에디터 또는 CrashReportClient가 `UnrealEditor-RPG.dll/.pdb`를 점유하면 강제로
소스를 되돌리지 않는다. 어떤 프로세스가 점유 중인지 확인하고, 사용자 작업을
잃을 가능성이 있는 UnrealEditor 종료는 사용자 동의 없이 수행하지 않는다.

## 문서 유지 규칙

기능 구현이 완료되면 같은 변경 안에서 다음을 갱신한다.

- 흐름이나 책임 경계가 바뀜: `docs/CURRENT_ARCHITECTURE.md`
- 완료/미완료 상태가 바뀜: `docs/IMPLEMENTATION_STATUS.md`
- 빌드·설치·환경 변수가 바뀜: `README.md`

문서에는 계획을 완료된 기능처럼 쓰지 않는다. 검증하지 않은 내용은 반드시
`미구현`, `설계 예정`, `검증 필요`로 표시한다.
