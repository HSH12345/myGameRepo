<# Runs a real, interactive Naver browser login. Keeps all credentials/tokens in memory. #>
[CmdletBinding()]
param(
    [string]$BaseUrl = 'http://localhost:3000',
    [ValidateRange(10, 300)][int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$uri = [Uri]$BaseUrl
if ($uri.Scheme -ne 'https' -and !($uri.Scheme -eq 'http' -and $uri.IsLoopback)) {
    throw 'Use HTTPS, or HTTP loopback for local development.'
}
$base = $BaseUrl.TrimEnd('/')
$attempt = $null
$completed = $false
try {
    $attempt = Invoke-RestMethod -Method Post -Uri "$base/auth/naver/start" -ContentType 'application/json' -Body '{}' -TimeoutSec 15
    $headers = @{ Authorization = 'Bearer ' + $attempt.exchangeSecret }
    $body = @{ requestId = $attempt.requestId } | ConvertTo-Json -Compress
    Write-Output '브라우저에서 네이버 로그인을 완료하세요. 인증정보는 출력하지 않습니다.'
    Start-Process -FilePath $attempt.loginUrl
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $status = Invoke-RestMethod -Method Post -Uri "$base/auth/naver/status" -Headers $headers -ContentType 'application/json' -Body $body -TimeoutSec 15
        if ($status.status -eq 'ready') {
            $result = Invoke-RestMethod -Method Post -Uri "$base/auth/naver/exchange" -Headers $headers -ContentType 'application/json' -Body $body -TimeoutSec 15
            if ([string]::IsNullOrWhiteSpace($result.customToken) -or [string]::IsNullOrWhiteSpace($result.uid)) { throw 'Token response is incomplete.' }
            $completed = $true
            Write-Output '성공: 네이버 인증과 Firebase Custom Token 발급·일회용 교환을 확인했습니다.'
            Write-Output '이 검사는 Firebase ID Token 교환이나 게임 서버 로그인을 수행하지 않습니다.'
            return
        }
        if ($status.status -ne 'pending') { throw 'Login was cancelled, failed, or already consumed.' }
        Start-Sleep -Milliseconds 1500
    }
    throw 'Login timed out.'
} catch {
    # Do not emit provider response bodies, request headers, or token objects.
    throw '네이버 인증 검사를 완료하지 못했습니다. 서버 실행·등록 콜백 주소·로그인 취소/만료 여부를 확인하세요.'
} finally {
    if ($null -ne $attempt -and !$completed) {
        try {
            $cancelHeaders = @{ Authorization = 'Bearer ' + $attempt.exchangeSecret }
            $cancelBody = @{ requestId = $attempt.requestId } | ConvertTo-Json -Compress
            $null = Invoke-RestMethod -Method Post -Uri "$base/auth/naver/cancel" -Headers $cancelHeaders -ContentType 'application/json' -Body $cancelBody -TimeoutSec 5
        } catch { }
    }
    $result = $null
    $attempt = $null
    $headers = $null
    $cancelHeaders = $null
}
