#define MyAppName "ScreenPilot"
#define MyAppVersion "0.2.0"
#define MyAppPublisher "Mayank Pratap"
#define MyAppExeName "ScreenPilot.exe"

[Setup]
AppId={{5E9BE920-E08F-4E6C-ADEB-1B76D990A1B7}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\ScreenPilot
DefaultGroupName=ScreenPilot
DisableProgramGroupPage=yes
OutputDir=..\installer-output
OutputBaseFilename=ScreenPilot-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#MyAppExeName}
SetupLogging=yes

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked
Name: "startup"; Description: "Start ScreenPilot when I sign in"; GroupDescription: "Startup:"; Flags: unchecked

[Files]
Source: "..\dist-native\ScreenPilot.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\ScreenPilot"; Filename: "{app}\{#MyAppExeName}"; AppUserModelID: "MayankPratap.ScreenPilot.2"
Name: "{autodesktop}\ScreenPilot"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon; AppUserModelID: "MayankPratap.ScreenPilot.2"
Name: "{userstartup}\ScreenPilot"; Filename: "{app}\{#MyAppExeName}"; Parameters: "--tray"; Tasks: startup; AppUserModelID: "MayankPratap.ScreenPilot.2"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch ScreenPilot"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{cmd}"; Parameters: "/C taskkill /IM ScreenPilot.exe /F"; Flags: runhidden; RunOnceId: "StopScreenPilot"

