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

[Setup]
; NOTE: AppId uniquely identifies this application. Do not reuse it elsewhere.
AppId={{BBDB1D93-C077-4E46-B0E8-8843ED2961A4}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
; Per-user install (no elevation). Two reasons: (1) the in-app updater can then run
; the installer without UAC - and UAC elevation resets the environment, which would
; discard the TMP/TEMP redirect the updater sets to work around a hardened %TEMP%
; that denies execute (Inno's "Unable to execute file in the temporary directory.
; Error 5"); (2) no admin rights are needed to update on a locked-down shop PC.
; With lowest, {autopf} resolves to %LOCALAPPDATA%\Programs and HKA maps to HKCU.
PrivilegesRequired=lowest
DefaultDirName={autopf}\{#MyAppName}
DisableProgramGroupPage=yes
LicenseFile=..\License.txt
; Default output dir; the manual release.ps1 overrides this with ISCC /O so its
; installer lands in build-release\ (gitignored) instead of inside releases\.
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
; Each language shows its own release notes (UTF-8 with BOM, so accents render).
Name: "english"; MessagesFile: "compiler:Default.isl"; InfoBeforeFile: "..\releases_notes.txt"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"; InfoBeforeFile: "..\releases_notes_es.txt"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; Qt/MinGW DLLs and plugin folders of earlier versions go before the new set is copied,
; so a library dropped or renamed between versions is not left behind. User data
; (database, backups, settings) lives outside {app}.
Type: files; Name: "{app}\*.dll"
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\styles"
Type: filesandordirs; Name: "{app}\sqldrivers"
Type: filesandordirs; Name: "{app}\imageformats"
Type: filesandordirs; Name: "{app}\iconengines"
Type: filesandordirs; Name: "{app}\tls"
Type: filesandordirs; Name: "{app}\networkinformation"
Type: filesandordirs; Name: "{app}\generic"

[Files]
Source: "{#MyAppVersion}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; NOTE: Don't use "Flags: ignoreversion" on any shared system files

[Registry]
; The installer language becomes the app's initial UI language (read by
; AppLanguage::installerChoice() only while the settings have none yet).
Root: HKA; Subkey: "Software\La Ideal"; ValueType: string; ValueName: "Language"; ValueData: "en"; Languages: english; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\La Ideal"; ValueType: string; ValueName: "Language"; ValueData: "es"; Languages: spanish; Flags: uninsdeletevalue

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
; The post-update self-check (docs/testing/smoke_test.md), run by the shop user.
Name: "{autoprograms}\{#MyAppName} - Comprobar tras actualizar"; Filename: "{app}\{#MyAppExeName}"; Parameters: "--self-check"

[Run]
; After an update the check is what the shop should run first: ticked; starting the app is not.
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent unchecked
Filename: "{app}\{#MyAppExeName}"; Parameters: "--self-check"; Description: "Comprobar la instalación (recomendado tras actualizar)"; Flags: nowait postinstall skipifsilent
