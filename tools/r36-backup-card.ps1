param(
    [Parameter(Mandatory = $true)]
    [ValidateRange(0, 25)]
    [int]$DiskNumber,
    [Parameter(Mandatory = $true)]
    [UInt64]$ExpectedSize,
    [switch]$Plan
)

# Read the source with GNU ddrescue; write only regular files under dist/recovery.
# This script does not format, repair, restore or alter disk/partition attributes.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$taskRepoRoot = Split-Path -Parent $PSScriptRoot
$taskTools = Join-Path $taskRepoRoot 'dist\recovery-tools\bin'
$taskDdrescue = Join-Path $taskTools 'ddrescue.exe'
$taskDdrescuelog = Join-Path $taskTools 'ddrescuelog.exe'
$taskDisk = Get-Disk -Number $DiskNumber
if ($taskDisk.IsBoot -or $taskDisk.IsSystem) { throw 'Disco do sistema recusado.' }
if ([string]$taskDisk.BusType -notin @('USB', 'SD', 'MMC')) { throw 'O disco precisa ser o cartao removivel.' }
if ($taskDisk.Size -ne $ExpectedSize -or $ExpectedSize -lt 1GB -or $ExpectedSize -gt 128GB) {
    throw 'O tamanho do cartao mudou ou esta fora do limite esperado.'
}
if (-not (Test-Path -LiteralPath $taskDdrescue) -or -not (Test-Path -LiteralPath $taskDdrescuelog)) {
    throw 'Prepare primeiro as ferramentas com python tools/r36-prepare-recovery.py.'
}
$taskRecoveryRoot = Join-Path $taskRepoRoot 'dist\recovery'
$taskOutputRoot = [IO.Path]::GetPathRoot($taskRecoveryRoot)
$taskDestinationDrive = $taskOutputRoot.Substring(0, 1)
$taskDestinationDisk = Get-Partition -DriveLetter $taskDestinationDrive | Get-Disk
if ($taskDestinationDisk.Number -eq $DiskNumber) { throw 'A imagem nao pode ficar no cartao de origem.' }
$taskFreeBytes = (Get-PSDrive -Name $taskDestinationDrive).Free
if ($taskFreeBytes -lt ($ExpectedSize + 1GB)) { throw 'Espaco insuficiente no disco de destino.' }
$taskDevice = '/dev/sd' + [char]([int][char]'a' + $DiskNumber)
$taskPlan = [ordered]@{
    source_disk = $DiskNumber
    source_model = $taskDisk.FriendlyName
    source_device = $taskDevice
    source_bytes = $ExpectedSize
    destination_root = $taskRecoveryRoot
    destination_free = $taskFreeBytes
    writes_to_source = $false
    repair_filesystem = $false
    retries = 0
}
$taskPlan | ConvertTo-Json
if ($Plan) { exit 0 }

$taskIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
$taskPrincipal = New-Object Security.Principal.WindowsPrincipal($taskIdentity)
if (-not $taskPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'O Windows exige PowerShell como administrador para ler o disco fisico completo.'
}
$taskSuffix = [Guid]::NewGuid().ToString('N').Substring(0, 8)
$taskBackupDir = Join-Path $taskRecoveryRoot ('r36-card-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $taskSuffix)
New-Item -ItemType Directory -Path $taskBackupDir | Out-Null
$taskImage = Join-Path $taskBackupDir 'r36-card.img'
$taskMap = Join-Path $taskBackupDir 'r36-card.map'
$taskManifest = Join-Path $taskBackupDir 'metadata.json'
$taskPlan['backup_directory'] = $taskBackupDir
$taskPlan['status'] = 'copying'
$taskPlan | ConvertTo-Json | Set-Content -LiteralPath $taskManifest -Encoding UTF8
$taskImageCygwin = $taskImage.Replace('\', '/')
$taskMapCygwin = $taskMap.Replace('\', '/')
Write-Host ('Copiando somente leitura para: ' + $taskImage)
Write-Host 'Uma primeira passagem, sem raspagem, trimming ou repeticoes dos setores com erro.'
& $taskDdrescue --verbose --no-trim --no-scrape --retry-passes=0 --sector-size=512 $taskDevice $taskImageCygwin $taskMapCygwin
$taskCopyExit = $LASTEXITCODE
& $taskDdrescuelog --show-status $taskMapCygwin
& $taskDdrescuelog --done-status $taskMapCygwin
$taskMapComplete = $LASTEXITCODE -eq 0
$taskPlan['copy_exit_code'] = $taskCopyExit
$taskPlan['map_complete'] = $taskMapComplete
if (Test-Path -LiteralPath $taskImage) { $taskPlan['image_bytes'] = (Get-Item -LiteralPath $taskImage).Length }
if ($taskCopyExit -eq 0 -and $taskMapComplete -and $taskPlan['image_bytes'] -eq $ExpectedSize) {
    $taskPlan['status'] = 'complete'
    $taskPlan['sha256'] = (Get-FileHash -LiteralPath $taskImage -Algorithm SHA256).Hash.ToLowerInvariant()
} else {
    $taskPlan['status'] = 'incomplete'
}
$taskPlan | ConvertTo-Json | Set-Content -LiteralPath $taskManifest -Encoding UTF8
$taskPlan | ConvertTo-Json
if ($taskPlan['status'] -ne 'complete') {
    throw 'A copia ficou incompleta. Preserve a imagem e o mapfile; o original nao foi reparado.'
}
Write-Host 'Imagem completa e hash salvos. O cartao original continua sem reparo.'
