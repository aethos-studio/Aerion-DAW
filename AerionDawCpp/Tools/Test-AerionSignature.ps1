<#
.SYNOPSIS
    Checks that a file was signed, intact, with the expected certificate.

.DESCRIPTION
    Used by the release-package workflow after signing. `signtool verify /pa`
    also demands that the certificate chains to a trusted root, which a
    self-signed certificate never does, so it fails on a perfectly good
    signature. This checks what a release build needs instead:
      - the file is signed, and not changed since (no hash mismatch),
      - the signer is the certificate that was meant to sign it (thumbprint),
      - the signature is timestamped (so it outlives the certificate),
      - the chain is trusted, or the only problem is an untrusted root, which
        is reported as a warning (self-signed; a paid certificate has none).

.PARAMETER Path
    The file to check.

.PARAMETER Thumbprint
    Thumbprint of the certificate that signed it.

.EXAMPLE
    powershell -File Test-AerionSignature.ps1 -Path "Aerion DAW.exe" -Thumbprint F81E82D6...
#>
param(
    [Parameter(Mandatory = $true)] [string] $Path,
    [Parameter(Mandatory = $true)] [string] $Thumbprint
)

$ErrorActionPreference = "Stop"

$sig = Get-AuthenticodeSignature -FilePath $Path
$name = Split-Path -Leaf $Path

if ($sig.Status -eq "NotSigned" -or $null -eq $sig.SignerCertificate) {
    Write-Error "$name is not signed."
    exit 1
}
if ($sig.SignerCertificate.Thumbprint -ne $Thumbprint) {
    Write-Error "$name is signed by $($sig.SignerCertificate.Thumbprint), not by the expected certificate $Thumbprint."
    exit 1
}
if ($sig.Status -eq "HashMismatch") {
    Write-Error "$name was changed after it was signed."
    exit 1
}
if ($null -eq $sig.TimeStamperCertificate) {
    Write-Error "The signature on $name has no timestamp (the timestamp server could not be reached)."
    exit 1
}

if ($sig.Status -eq "Valid") {
    Write-Output "$name : signed, timestamped, trusted (thumbprint $Thumbprint)."
} elseif ($sig.StatusMessage -match "not trusted|untrusted|not.*trusted root") {
    Write-Warning "$name : signed and timestamped by $Thumbprint; the certificate is not trusted (self-signed), so Windows SmartScreen will still warn."
} else {
    Write-Error "$name : signature problem: $($sig.Status): $($sig.StatusMessage)"
    exit 1
}
