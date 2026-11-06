; Spectra Windows installer (Inno Setup 6). Built by .github/workflows/release.yml:
;   iscc /DAppVersion=1.0.0 /DPluginDir=<path to Spectra.vst3> /DOutputDir=<dir> packaging\windows\installer.iss
; Installs the VST3 into C:\Program Files\Common Files\VST3, where FL Studio and other hosts look for it.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppId={{6F1C2B7E-4A3D-4C59-9C2E-5D7A1B3E8F21}
AppName=Spectra
AppVersion={#AppVersion}
AppPublisher=Shreyas
AppPublisherURL=https://github.com/shreyassuman2/spectra
DefaultDirName={commoncf64}\VST3
DirExistsWarning=no
DisableDirPage=yes
DisableProgramGroupPage=yes
Uninstallable=yes
UninstallFilesDir={commonpf64}\Spectra
UninstallDisplayName=Spectra (VST3)
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir={#OutputDir}
OutputBaseFilename=Spectra-{#AppVersion}-windows-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Files]
Source: "{#PluginDir}\*"; DestDir: "{commoncf64}\VST3\Spectra.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Spectra.vst3"

[Messages]
FinishedLabel=Spectra is installed.%n%nIn FL Studio, go to Options > Manage plugins and click Find plugins. Spectra will appear under effects.
