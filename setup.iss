; Inno Setup 6 script for Recaps.
; Build it with:  iscc /DMyAppVersion=0.8 /F"recaps-0.8-setup" setup.iss
; (build.bat does this for you)

#ifndef MyAppVersion
  #define MyAppVersion "0.8"
#endif
#define MyAppName "Recaps"
#define MyAppPublisher "gooli.org"
#define MyAppExeName "recaps.exe"

[Setup]
AppId={{D3E743A3-E0F9-47F5-956A-CD15C6548789}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
VersionInfoVersion={#MyAppVersion}
DefaultDirName={autopf}\Recaps
DisableDirPage=auto
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
SetupIconFile=recaps.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
OutputDir=Output
; Refuse to install over a running copy
AppMutex=recaps-D3E743A3-E0F9-47f5-956A-CD15C6548789
; Install per user by default, so no UAC prompt is needed. The wizard still
; lets the user pick an all-users install. ("poweruser" was removed from
; Windows after XP and is no longer a meaningful value here.)
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
MinVersion=10.0
ArchitecturesInstallIn64BitMode=x64 arm64

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "startup"; Description: "{cm:AutoStartProgram,{#MyAppName}}"; GroupDescription: "{cm:AutoStartProgramGroupDescription}"

[Files]
; On ARM64 Windows the x64 build is installed and runs under emulation; an
; ARM64-native build can be produced from the project if you prefer one.
Source: "bin\x64\Release\{#MyAppExeName}";   DestDir: "{app}"; Flags: ignoreversion; Check: Is64BitInstallMode
Source: "bin\Win32\Release\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion; Check: not Is64BitInstallMode
Source: "README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "LICENSE";   DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"

[Registry]
; Start with Windows through a Run value rather than a Startup-folder shortcut:
; a Run value shows up in Settings > Apps > Startup, where the user expects it,
; and needs no administrator rights.
Root: HKA; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Recaps"; ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue; Tasks: startup
Root: HKCU; Subkey: "Software\Recaps"; Flags: uninsdeletekey

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent
