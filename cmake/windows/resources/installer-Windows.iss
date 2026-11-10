; installer-Windows.iss
; Windows 用インストーラー(exe)を作る Inno Setup のスクリプト。
;
; .github/scripts/Package-Windows.ps1 が、zip を作った後に ISCC.exe で呼び出す。
; 版番号・名前・入れるファイルの場所は、buildspec.json の値をコマンドラインの /D で受け取る。
; 入れる場所は %ProgramData%/obs-studio/plugins/<name> で、マニュアルの zip の手順と同じ。
; 画面の文言は Inno Setup の標準のものを使う。独自の文言は、OBS の版が古いときの警告([CustomMessages])だけ。
; 関連: #74

#ifndef ProductName
  #error ProductName が指定されていません(/DProductName=...)
#endif
#ifndef DisplayName
  #error DisplayName が指定されていません(/DDisplayName=...)
#endif
#ifndef AppVersion
  #error AppVersion が指定されていません(/DAppVersion=X.Y.Z)
#endif
#ifndef DisplayVersion
  #error DisplayVersion が指定されていません(/DDisplayVersion=...)
#endif
#ifndef Publisher
  #error Publisher が指定されていません(/DPublisher=...)
#endif
#ifndef Website
  #error Website が指定されていません(/DWebsite=...)
#endif
#ifndef SourceDir
  #error SourceDir が指定されていません(/DSourceDir=...)
#endif
#ifndef OutputDir
  #error OutputDir が指定されていません(/DOutputDir=...)
#endif
#ifndef OutputBaseFilename
  #error OutputBaseFilename が指定されていません(/DOutputBaseFilename=...)
#endif
#ifndef MinObsVersion
  #error MinObsVersion が指定されていません(/DMinObsVersion=X.Y.Z)
#endif

[Setup]
; AppId は版をまたいで変えない。同じ AppId なら、新しい版で上書きインストールとして扱われる。
AppId={{7866DCC3-E7FD-43C9-95C9-3BE6292F4140}
AppName={#DisplayName}
AppVersion={#DisplayVersion}
AppVerName={#DisplayName} {#DisplayVersion}
AppPublisher={#Publisher}
AppPublisherURL={#Website}
AppSupportURL={#Website}
AppUpdatesURL={#Website}
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#DisplayName}
; 入れる場所は固定する。OBS が読むのはこのフォルダだけで、別の場所を選ばれると動かないため。
DefaultDirName={commonappdata}\obs-studio\plugins\{#ProductName}
DisableDirPage=yes
DirExistsWarning=no
UsePreviousAppDir=no
DisableProgramGroupPage=yes
; %ProgramData%/obs-studio は OBS のインストーラー(管理者)が作るフォルダのため、管理者の権限が要る。
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; OBS がプラグインのファイルを開いているときは、標準の機能で OBS を閉じるように案内する。
; 閉じた OBS を勝手に起動し直すと配信中の人が驚くため、起動し直しはしない。
CloseApplications=yes
RestartApplications=no
; アンインストール用のファイルは、OBS が読む bin・data とは別のフォルダに置く。
UninstallFilesDir={app}\uninstall
UninstallDisplayName={#DisplayName}
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; Windows の表示言語に合わせて日本語か英語を選び、言語を選ぶ画面は出さない。
ShowLanguageDialog=auto

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"

[InstallDelete]
; 前の版(zip で入れたものを含む)のファイルが残らないよう、入れる前に消す。
; 設定は %APPDATA%/obs-studio/plugin_config にあるため、ここを消しても引き継がれる。
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\data"

[Files]
; デバッグ用の .pdb は利用者に要らないため入れない(zip には入ったまま)。
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "*.pdb"; Flags: ignoreversion recursesubdirs createallsubdirs

[CustomMessages]
; OBS Studio の版が古いときの警告。%1 は入っている版、%2 は必要な版(buildspec.json の obs-studio の版)。
english.ObsTooOld={#DisplayName} does not work with the installed OBS Studio (%1).%nPlease update to OBS Studio %2 or later.%n%nContinue the installation anyway?
japanese.ObsTooOld=インストールされている OBS Studio(%1)では、{#DisplayName} は動作しません。%nOBS Studio %2 以降に更新してください。%n%nこのままインストールを続けますか?

[Code]
const
  // OBS のインストーラーが「インストールされているアプリ」に版を記録する場所。
  ObsUninstallKey = 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio';

// 版の文字列(例: 31.0.2)を、大小を比べられる 1 つの数(例: 31000002)にする。
// 「.」で区切った先頭の 3 つの数を使い、足りない部分は 0 とする。数字と「.」以外の文字(-beta など)が来たら、そこで終わりとする。
// 先頭が数字でなければ、版として読めないので False を返す(OBS が記録の形を変えた場合に、誤って警告しないため)。
function VersionToNumber(const Version: String; var Number: Int64): Boolean;
var
  I, Part: Integer;
  Value: Int64;
begin
  Number := 0;
  Result := (Length(Version) > 0) and (Version[1] >= '0') and (Version[1] <= '9');
  if not Result then
    Exit;
  Part := 0;
  Value := 0;
  for I := 1 to Length(Version) do
  begin
    if (Version[I] >= '0') and (Version[I] <= '9') then
      Value := Value * 10 + Ord(Version[I]) - Ord('0')
    else if (Version[I] = '.') and (Part < 2) then
    begin
      Number := Number * 1000 + Value;
      Part := Part + 1;
      Value := 0;
    end
    else
      Break;
  end;
  Number := Number * 1000 + Value;
  for I := Part + 1 to 2 do
    Number := Number * 1000;
end;

// OBS Studio の版を、64 ビットと 32 ビットの両方の記録の場所から探す。見つからなければ False を返す。
function GetObsVersion(var Version: String): Boolean;
begin
  Result := RegQueryStringValue(HKLM64, ObsUninstallKey, 'DisplayVersion', Version);
  if not Result then
    Result := RegQueryStringValue(HKLM32, ObsUninstallKey, 'DisplayVersion', Version);
end;

// インストールを始める前に、OBS Studio の版を確かめる。必要な版より古ければ警告し、「はい」なら続け、「いいえ」ならやめる。
// 版が分からないとき(記録がない Steam 版・ポータブル版や、版として読めない値)は、警告せずに続ける。
// サイレントインストールでは警告を出さずに続ける(SuppressibleMsgBox の既定の答えを「はい」にする)。
function InitializeSetup(): Boolean;
var
  Installed: String;
  InstalledNumber, RequiredNumber: Int64;
begin
  Result := True;
  if not GetObsVersion(Installed) then
  begin
    Log('OBS Studio version not found; skipping the version check');
    Exit;
  end;
  if not (VersionToNumber(Installed, InstalledNumber) and VersionToNumber('{#MinObsVersion}', RequiredNumber)) then
  begin
    Log('OBS Studio version "' + Installed + '" is not a version number; skipping the version check');
    Exit;
  end;
  Log('OBS Studio version: ' + Installed + ' (required: {#MinObsVersion})');
  if InstalledNumber < RequiredNumber then
    Result := SuppressibleMsgBox(FmtMessage(CustomMessage('ObsTooOld'), [Installed, '{#MinObsVersion}']),
      mbConfirmation, MB_YESNO, IDYES) = IDYES;
end;
