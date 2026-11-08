# Firebase Admin Web

게스트 Custom Token 발급과 네이버 OAuth → Firebase Custom Token 일회용 교환을 제공한다.
UE/게임 계정 연결과 ID Token verifier는 MyRpgServer/docs/NAVER_GAME_LOGIN.md를 따른다.
POST /auth/firebase/verify는 loopback 내부 전용이며 외부에 프록시하지 않는다.

## 실행

Node.js 22.14 이상에서 `npm ci` 후 `npm start`로 실행한다.
기존 ServerManager의 Firebase Admin Web 시작도 지원한다.
`index.js` 옆 `.env`를 자동 로딩하며 기존 프로세스 환경변수가 우선한다.
실제 비밀값은 `.env`/별도 서비스 계정 JSON에만 보관한다. `.env.example`은 설정 항목 예시다.
단독 실행에는 `FIREBASE_SERVICE_ACCOUNT`가 필요하다. ServerManager는 기존 환경 설정으로 전달한다.
기본 주소는 `127.0.0.1:3000`이며 `GET /health`에 naverEnabled를 포함한다.

## 검사

- `npm test`: 외부 계정/DB 변경 없이 HTTP/실패/수명/동시성 검사와 테스트 키 기반 실제 SDK 서명 검사.
- `powershell -NoProfile -ExecutionPolicy Bypass -File .\Scripts\TestNaverAuth.ps1`:
  실행 중인 웹 서버에서 실제 브라우저 네이버 인증·Custom Token 교환 검사. 키/토큰을 출력하지 않는다.
  이 수동 검사는 사용자가 네이버에 로그인해야 하며 자동 테스트에 포함되지 않는다.

API 계약·설정·검증 한계는
[NAVER_AUTH_WEB.md](../MyRpgServer/docs/NAVER_AUTH_WEB.md)를 참고한다.
