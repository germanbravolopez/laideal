; Inno Setup installer script for La Ideal.
;
; Compile from the command line:
;   ISCC.exe /DMyAppVersion=<version> laideal.iss
;
; The release.ps1 / release.bat scripts in this folder run this automatically.
; All paths below are relative to this .iss file (releases/), so the script is
; portable across machines as long as the repo layout is preserved.

#define MyAppName "La Ideal"
#ifndef MyAppVersion
  #define MyAppVersion "0.0"
#endif
#define MyAppPublisher "German Bravo Lopez"
#define MyAppURL "https://github.com/germanbravolopez/laideal"
#define MyAppExeName "laideal.exe"
#define MyAppAssocName MyAppName + ""
#define MyAppAssocExt ".myp"
#define MyAppAssocKey StringChange(MyAppAssocName, " ", "") + MyAppAssocExt

[Setup]
; NOTE: AppId uniquely identifies this application. Do not reuse it elsewhere.
AppId={{BBDB1D93-C077-4E46-B0E8-8843ED2961A4}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
ChangesAssociations=yes
DisableProgramGroupPage=yes
LicenseFile=..\License.txt
InfoBeforeFile=..\releases_notes.txt
OutputDir=setup_outputs
OutputBaseFilename=laideal_setup_{#MyAppVersion}
SetupIconFile=..\icon\lavadora.ico
Compression=lzma
SolidCompression=yes
WizardStyle=modern
; In-place upgrade support for the in-app updater (Ayuda -> Buscar
; actualizaciones). When the user starts the installer while laideal.exe is
; still running, Inno detects it, prompts to close, replaces files, and
; relaunches. CloseApplicationsFilter limits the scan to our exe so we don't
; touch any unrelated open file under {app}.
CloseApplications=yes
RestartApplications=yes
CloseApplicationsFilter=*.exe

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#MyAppVersion}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; NOTE: Don't use "Flags: ignoreversion" on any shared system files

[Registry]
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocExt}\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppAssocKey}"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocKey}"; ValueType: string; ValueName: ""; ValueData: "{#MyAppAssocName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocKey}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocKey}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExeName}\SupportedTypes"; ValueType: string; ValueName: ".myp"; ValueData: ""

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent