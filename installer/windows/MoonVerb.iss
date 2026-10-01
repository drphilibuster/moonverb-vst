; Inno Setup script: iscc /DAppVersion=1.0.0 /DBuildDir=..\..\build\MoonVerb_artefacts\Release /DOutDir=..\..\build\dist installer\windows\MoonVerb.iss
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build\MoonVerb_artefacts\Release"
#endif
#ifndef OutDir
  #define OutDir "..\..\build\dist"
#endif
[Setup]
AppId={{6E2F2E5B-3C54-4B7E-9D0A-5A1B7B1E4D21}
AppName=MoonVerb
AppVersion={#AppVersion}
AppPublisher=Moon Technologies
DefaultDirName={autopf}\MoonVerb
DefaultGroupName=MoonVerb
OutputDir={#OutDir}
OutputBaseFilename=MoonVerb-{#AppVersion}-Windows
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\..\LICENSE
InfoBeforeFile=welcome.txt
PrivilegesRequired=admin
[Types]
Name: "full"; Description: "VST3 plug-in and standalone app"
Name: "custom"; Description: "Custom"; Flags: iscustom
[Components]
Name: "vst3"; Description: "VST3 plug-in"; Types: full custom
Name: "standalone"; Description: "Standalone app"; Types: full custom
[Files]
Source: "{#BuildDir}\VST3\MoonVerb.vst3\*"; DestDir: "{commoncf64}\VST3\MoonVerb.vst3"; Components: vst3; Flags: recursesubdirs ignoreversion
Source: "{#BuildDir}\Standalone\MoonVerb.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "..\..\docs\MoonVerb.md"; DestDir: "{app}"; Flags: ignoreversion
[Icons]
Name: "{group}\MoonVerb"; Filename: "{app}\MoonVerb.exe"; Components: standalone
