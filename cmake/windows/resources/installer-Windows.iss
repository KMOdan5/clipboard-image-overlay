; installer-Windows.iss
; Windows 用インストーラー(exe)を作る Inno Setup のスクリプト。
;
; .github/scripts/Package-Windows.ps1 が、zip を作った後に ISCC.exe で呼び出す。
; 版番号・名前・入れるファイルの場所は、buildspec.json の値をコマンドラインの /D で受け取る。
; 入れる場所は %ProgramData%/obs-studio/plugins/<name> で、マニュアルの zip の手順と同じ。
; 画面の文言は Inno Setup の標準のものだけを使う(独自の文言は足さない)。
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
