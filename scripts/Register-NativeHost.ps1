param(
    [Parameter(Mandatory = $true)]
    [string]$HostPath,

    [Parameter(Mandatory = $true)]
    [string[]]$ExtensionIds,

    [ValidateSet("chrome", "edge", "brave", "opera", "all")]
    [string[]]$Browsers = @("chrome", "edge")
)

$resolvedHostPath = (Resolve-Path $HostPath).Path
$manifestDirectory = Join-Path $env:LOCALAPPDATA "drpc\native-host"
$manifestPath = Join-Path $manifestDirectory "com.drpc.browser_host.json"

$browserRegistryRoots = @{
    chrome = "HKCU:\Software\Google\Chrome\NativeMessagingHosts\com.drpc.browser_host"
    edge   = "HKCU:\Software\Microsoft\Edge\NativeMessagingHosts\com.drpc.browser_host"
    brave  = "HKCU:\Software\BraveSoftware\Brave-Browser\NativeMessagingHosts\com.drpc.browser_host"
    opera  = "HKCU:\Software\Opera Software\NativeMessagingHosts\com.drpc.browser_host"
}

if ($Browsers -contains "all") {
    $Browsers = @("chrome", "edge", "brave", "opera")
}

$manifest = @{
    name = "com.drpc.browser_host"
    description = "Native messaging bridge for drpc browser activity"
    path = $resolvedHostPath
    type = "stdio"
    allowed_origins = @($ExtensionIds | ForEach-Object { "chrome-extension://$_/" })
}

New-Item -ItemType Directory -Path $manifestDirectory -Force | Out-Null
$manifest | ConvertTo-Json -Depth 4 | Set-Content -Path $manifestPath -Encoding UTF8

foreach ($browser in $Browsers) {
    $registryPath = $browserRegistryRoots[$browser]
    if (-not $registryPath) {
        Write-Warning "Skipping unsupported browser key: $browser"
        continue
    }

    New-Item -Path $registryPath -Force | Out-Null
    Set-Item -Path $registryPath -Value $manifestPath
}

Write-Host "Native host manifest written to $manifestPath"
Write-Host "Registered browsers: $($Browsers -join ', ')"
