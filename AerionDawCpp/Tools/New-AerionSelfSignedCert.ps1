<#
.SYNOPSIS
    Generates a self-signed code-signing certificate for Aerion DAW and prints
    the values to store as GitHub Actions secrets.

.DESCRIPTION
    Creates the certificate in the CurrentUser store, so it does NOT require
    local administrator rights. Exports a password-protected PFX plus a base64
    copy for pasting into a GitHub Actions secret.

    A self-signed certificate produces valid, timestamped signatures but is not
    trusted by Windows SmartScreen. It is the "free" option: it proves the
    binary was signed by this key, but it will not remove the SmartScreen
    "unknown publisher" prompt (only a paid OV/EV certificate does that).

.PARAMETER Password
    Password used to protect the exported PFX. Store this same value in the
    GitHub secret WINDOWS_CERT_PASSWORD.

.EXAMPLE
    pwsh -File AerionDawCpp/Tools/New-AerionSelfSignedCert.ps1 -Password "a-strong-password"
#>
param(
    [Parameter(Mandatory = $true)]
    [string] $Password,

    [string] $Subject = "CN=Aethos Studio Ltd., O=Aethos Studio Ltd., C=GB",
    [string] $FriendlyName = "Aerion DAW Self-Signed Code Signing",
    [int]    $ValidYears = 5,
    [string] $OutputDirectory = $PSScriptRoot
)

$ErrorActionPreference = "Stop"

$cert = New-SelfSignedCertificate `
    -Type CodeSigningCert `
    -Subject $Subject `
    -FriendlyName $FriendlyName `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -KeyExportPolicy Exportable `
    -KeyUsage DigitalSignature `
    -KeySpec Signature `
    -HashAlgorithm SHA256 `
    -NotAfter (Get-Date).AddYears($ValidYears)

$pfxPath = Join-Path $OutputDirectory "aerion-codesign.pfx"
$b64Path = Join-Path $OutputDirectory "aerion-codesign.pfx.base64.txt"

$securePw = ConvertTo-SecureString -String $Password -Force -AsPlainText
Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password $securePw | Out-Null

$bytes = [System.IO.File]::ReadAllBytes($pfxPath)
[System.IO.File]::WriteAllText($b64Path, [System.Convert]::ToBase64String($bytes))

Write-Host ""
Write-Host "Self-signed code-signing certificate created." -ForegroundColor Green
Write-Host "  Thumbprint : $($cert.Thumbprint)"
Write-Host "  Valid until: $($cert.NotAfter.ToString('yyyy-MM-dd'))"
Write-Host "  PFX file   : $pfxPath"
Write-Host "  Base64 file: $b64Path"
Write-Host ""
Write-Host "Add these GitHub repository secrets:" -ForegroundColor Cyan
Write-Host "  Settings -> Secrets and variables -> Actions -> New repository secret"
Write-Host "    WINDOWS_CERT_PFX_BASE64  = the full contents of $b64Path"
Write-Host "    WINDOWS_CERT_PASSWORD    = the password passed to this script"
Write-Host ""
Write-Host "The release-package workflow signs the app + installer automatically" -ForegroundColor Cyan
Write-Host "when both secrets are present, and skips signing when they are not."
Write-Host ""
Write-Warning "aerion-codesign.pfx and its base64 are private keys. They are gitignored - never commit them."
