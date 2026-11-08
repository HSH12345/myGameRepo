# RPG Server Control

`RpgServerManager.exe` 하나에서 IOCP 네이티브 서버와 Node 기반 Web 서버를 각각 독립 프로세스로 실행하고 관리하는 WPF 도구입니다.

2026-10-19: `World Data Server`(`WorldDataServer.exe`, loopback 9400, `world_db`)를 추가했습니다.
전체 시작은 Account/Character/World 데이터 서버를 Login/Game보다 먼저 시작하고 종료는 역순입니다.
`RPG_WORLD_DB_PASSWORD`는 보안 값으로 받으며 기존 Account DB 자격 증명을 초기 fallback으로 지원합니다.
설치는 `python tools/InstallWorldDatabase.py --apply`, [상세 계약·검사](../docs/WORLD_DATA_SERVER.md)를 참고합니다.

## 실행 파일 만들기

```powershell
.\ServerManager\publish.ps1
.\artifacts\ServerManager\RpgServerManager.exe
```

publish 결과는 .NET 런타임을 포함하는 self-contained `win-x64` 실행 파일입니다.

## 서버 추가

UI 왼쪽 아래의 `servers.json 열기` 버튼으로 현재 적용 중인 설정을 열고 `servers` 배열에 항목을 추가한 뒤 우측 상단의 새로고침 버튼을 누릅니다. publish된 EXE는 같은 폴더의 `servers.json`을 사용하며, 소스 기본값은 [`ServerManager/servers.json`](servers.json)입니다. 실행 중인 서버가 있으면 먼저 전체 중지해야 합니다.

```json
{
  "id": "game-server-01",
  "displayName": "Game Server 01",
  "description": "월드 인스턴스 서버",
  "group": "IOCP",
  "executable": "{RepoRoot}\\x64\\Debug\\GameServer.exe",
  "arguments": "--shard 1",
  "workingDirectory": "{RepoRoot}\\x64\\Debug",
  "shutdownCommand": "-shutdown",
  "shutdownUrl": "",
  "port": 9200,
  "startOrder": 40,
  "autoStart": false,
  "environment": {},
  "requiredEnvironment": []
}
```

경로 토큰:

- `{RepoRoot}`: `MyRpgServer.sln`이 있는 폴더
- `{WebRoot}`: 기본값 `RepoRoot\..\Web`
- `${ENV_NAME}`: 현재 프로세스 환경 변수 값

다른 설정 파일을 사용하려면 `RPG_SERVER_MANAGER_CONFIG` 환경 변수에 JSON 절대 경로를 지정합니다.

## 구조화 로그 규격

관리 도구는 stdout에서 다음 prefix의 JSON line을 발견하면 레벨과 카테고리를 인식합니다.

```text
@rpglog {"timestamp":"2026-08-25T13:00:00.000Z","level":"Info","category":"Lifecycle","message":"Server started."}
```

인식 레벨은 `Trace`, `Debug`, `Info`, `Warning`, `Error`, `Critical`입니다. 일반 stdout은 `Info/Console`, stderr는 `Error/Console`로 보존되므로 기존 서버도 바로 등록할 수 있습니다.

로그 행을 하나 이상 선택한 뒤 `복사`, 우클릭의 `선택 로그 복사`, 또는 `Ctrl+C`를 사용하면 `시간\t레벨\t카테고리\t메시지` 형식으로 클립보드에 복사됩니다. 선택한 행이 없을 때 상단 `복사` 버튼을 누르면 현재 필터와 검색 조건에 표시되는 로그 전체를 복사합니다.

## 서버 명령 콘솔

실행 중인 서버를 선택한 뒤 LIVE LOGS 아래의 `>` 입력창에 명령을 입력하고 Enter 또는 `전송`을 누르면 해당 프로세스의 stdin으로 전달됩니다. `↑`/`↓` 키로 전송 기록을 탐색할 수 있습니다. 기본 서버 명령은 `help`, `status`, `ping`, `shutdown`(`-shutdown`), `exit`입니다. 관리자가 비정상 종료된 뒤 인계한 프로세스는 기존 stdin pipe를 복구할 수 없으므로 한 번 재시작해야 명령 입력이 활성화됩니다.

## 프로세스 정책

- 각 서버는 별도의 자식 프로세스로 유지됩니다.
- 중지는 먼저 `shutdownCommand`를 stdin으로 전달합니다.
- 관리자가 비정상 종료되어도 `%LocalAppData%\RpgServerManager\managed-processes.json`의 PID와 시작 시각을 검증해 기존 프로세스를 다시 인계합니다.
- 인계된 Web 서버는 `shutdownUrl`이 설정되어 있으면 loopback HTTP 종료 요청을 사용합니다. 그 외 인계 프로세스는 표준 입력을 복구할 수 없어 프로세스 트리 종료로 정리합니다.
- 제한 시간 내 종료되지 않으면 해당 프로세스 트리만 강제 종료합니다.
- 도구 밖에서 실행된 서버는 port probe를 통해 `EXTERNAL`로 표시합니다.
- 도구 종료 시 관리 중인 서버의 정상 종료 여부를 확인합니다.

## 비밀 환경 변수

`requiredEnvironment` 값이 현재 환경에 없으면 서버 시작 시 보안 입력창이 열립니다. `Windows 자격 증명에 안전하게 저장`을 선택하면 값은 Windows Credential Manager의 `RpgServerManager/<환경 변수명>` 항목에 보관되며 `servers.json`, 프로세스 기록, 로그에는 기록되지 않습니다.

서버 상세 영역의 `보안 값` 버튼으로 저장된 값을 언제든 덮어쓸 수 있습니다. 저장 옵션을 해제하고 입력하면 기존 Windows 자격 증명은 삭제되고 현재 관리자 세션에서만 사용됩니다.
