[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Package-Windows.ps1 requires CI environment"
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "Packaging script requires a 64-bit system to build and run."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The packaging script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Package {
    trap {
        Write-Error $_
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."
    $BuildSpecFile = "${ProjectRoot}/buildspec.json"

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach( $Utility in $UtilityFunctions ) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    $BuildSpec = Get-Content -Path ${BuildSpecFile} -Raw | ConvertFrom-Json
    $ProductName = $BuildSpec.name
    $ProductVersion = $BuildSpec.version

    $OutputName = "${ProductName}-${ProductVersion}-windows-${Target}"

    $RemoveArgs = @{
        ErrorAction = 'SilentlyContinue'
        Path = @(
            "${ProjectRoot}/release/${ProductName}-*-windows-*.zip"
            "${ProjectRoot}/release/${ProductName}-*-windows-*.exe"
        )
    }

    Remove-Item @RemoveArgs

    Log-Group "Archiving ${ProductName}..."
    $CompressArgs = @{
        Path = (Get-ChildItem -Path "${ProjectRoot}/release/${Configuration}" -Exclude "${OutputName}*.*")
        CompressionLevel = 'Optimal'
        DestinationPath = "${ProjectRoot}/release/${OutputName}.zip"
        Verbose = ($Env:CI -ne $null)
    }
    Compress-Archive -Force @CompressArgs
    Log-Group

    # インストーラー(exe)を作る。中身は zip と同じフォルダから取る(#74)。
    # リリースの作成(push.yaml)は *.exe も拾うため、ここで release に置けば配布物に入る。
    Log-Group "Building installer for ${ProductName}..."
    $Iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source
    if ( ! $Iscc ) {
        $Iscc = "${env:ProgramFiles(x86)}/Inno Setup 6/ISCC.exe"
    }
    if ( ! ( Test-Path $Iscc ) ) {
        # CI のイメージに Inno Setup がない場合だけ入れる
        Invoke-External choco install innosetup --no-progress -y
        $Iscc = "${env:ProgramFiles(x86)}/Inno Setup 6/ISCC.exe"
    }

    # 画面とアンインストールの一覧に出す版は、タグと同じ表記(例: 0.2.0-beta)にする
    $DisplayVersion = $ProductVersion
    if ( $BuildSpec.beta ) {
        $DisplayVersion = "${ProductVersion}-beta"
    }

    $IsccArgs = @(
        "/Qp"
        "/DProductName=${ProductName}"
        "/DDisplayName=$($BuildSpec.displayName)"
        "/DAppVersion=${ProductVersion}"
        "/DDisplayVersion=${DisplayVersion}"
        "/DPublisher=$($BuildSpec.author)"
        "/DWebsite=$($BuildSpec.website)"
        "/DSourceDir=${ProjectRoot}/release/${Configuration}/${ProductName}"
        "/DOutputDir=${ProjectRoot}/release"
        "/DOutputBaseFilename=${OutputName}-installer"
        "${ProjectRoot}/cmake/windows/resources/installer-Windows.iss"
    )
    Invoke-External $Iscc @IsccArgs
    Log-Group
}

Package
