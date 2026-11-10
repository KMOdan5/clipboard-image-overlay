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
// 版の文字列(例: 31.0.2)の i 番目(0 始まり)の数を返す。数でない文字が来たら、そこで終わりとみなす。
// 足りない部分は 0 とする(例: 31.1 の 3 番目は 0)。
function VersionPart(const Version: String; Index: Integer): Integer;
var
  I, Part: Integer;
  Digits: String;
begin
  Result := 0;
  Part := 0;
  Digits := '';
  for I := 1 to Length(Version) do
  begin
    if (Version[I] >= '0') and (Version[I] <= '9') then
      Digits := Digits + Version[I]
    else if Version[I] = '.' then
    begin
      if Part = Index then
        Break;
      Part := Part + 1;
      Digits := '';
    end
    else
      Break;
  end;
  if (Part = Index) and (Digits <> '') then
    Result := StrToIntDef(Digits, 0);
end;

// 版 A が版 B より古ければ True を返す(メジャー・マイナー・パッチの 3 つを比べる)。
function IsOlderVersion(const A, B: String): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 0 to 2 do
  begin
    if VersionPart(A, I) < VersionPart(B, I) then
    begin
      Result := True;
      Exit;
    end;
    if VersionPart(A, I) > VersionPart(B, I) then
      Exit;
  end;
end;

// インストールを始める前に、OBS Studio の版を確かめる。
// OBS のインストーラーが「インストールされているアプリ」に記録した版を読み、必要な版より古ければ警告する。
// 「はい」なら続け、「いいえ」ならやめる。版が読めないとき(Steam 版・ポータブル版など)は、警告せずに続ける。
// サイレントインストールでは警告を出さずに続ける(SuppressibleMsgBox の既定の答えを「はい」にする)。
function InitializeSetup(): Boolean;
var
  Installed: String;
begin
  Result := True;
  if not RegQueryStringValue(HKLM64, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio', 'DisplayVersion', Installed) then
    if not RegQueryStringValue(HKLM32, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio', 'DisplayVersion', Installed) then
    begin
      Log('OBS Studio version not found; skipping the version check');
      Exit;
    end;
  Log('OBS Studio version: ' + Installed + ' (required: {#MinObsVersion})');
  if IsOlderVersion(Installed, '{#MinObsVersion}') then
    Result := SuppressibleMsgBox(FmtMessage(CustomMessage('ObsTooOld'), [Installed, '{#MinObsVersion}']),
      mbConfirmation, MB_YESNO, IDYES) = IDYES;
end;
