; Multistream for OBS - Windows installer (Inno Setup 6)
; Build:  ISCC.exe /DAppVersion=1.0.0 /DSourceDir=<release\Config> /DOutputDir=<release> obs-multistream.iss
; SourceDir must contain obs-multistream\bin\64bit\obs-multistream.dll and obs-multistream\data\...

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\release\RelWithDebInfo"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\release"
#endif
#define AppName "Multistream for OBS"
#define PluginName "obs-multistream"
#define MinObsMajor 31

[Setup]
AppId={{6C1C6B53-1F7A-4D8E-9C55-2E4B9C7F0A31}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=The Visionary Society
AppPublisherURL=https://thevisionarysociety.com
DefaultDirName={code:GetObsDir}
DirExistsWarning=no
AppendDefaultDirName=no
UsePreviousAppDir=no
DisableProgramGroupPage=yes
PrivilegesRequired=admin
; Lets the automated test install into a portable OBS copy without admin:
;   setup.exe /CURRENTUSER /VERYSILENT /DIR=<portable obs>
PrivilegesRequiredOverridesAllowed=commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename={#PluginName}-{#AppVersion}-windows-x64-installer
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName}
UninstallFilesDir={app}\data\obs-plugins\{#PluginName}\uninstall
; No license-accept page: the GPL needs no acceptance to use. LICENSE.txt is installed.
CloseApplications=no

[Messages]
SelectDirLabel3=Setup found your OBS Studio folder below. Click Next to install the plugin there.
SelectDirBrowseLabel=If OBS is installed somewhere else, click Browse and pick the folder that contains "bin", "data" and "obs-plugins".

[Files]
Source: "{#SourceDir}\{#PluginName}\bin\64bit\{#PluginName}.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
Source: "{#SourceDir}\{#PluginName}\data\*"; DestDir: "{app}\data\obs-plugins\{#PluginName}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\LICENSE"; DestDir: "{app}\data\obs-plugins\{#PluginName}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Code]
function GetObsDir(Param: String): String;
var
  Path: String;
begin
  if RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', Path) and DirExists(Path) then
    Result := Path
  else if RegQueryStringValue(HKLM32, 'SOFTWARE\OBS Studio', '', Path) and DirExists(Path) then
    Result := Path
  else
    Result := ExpandConstant('{commonpf64}\obs-studio');
end;

function IsObsRunning(): Boolean;
var
  Locator, Service, Procs: Variant;
begin
  Result := False;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Procs := Service.ExecQuery('SELECT Name FROM Win32_Process WHERE Name = ''obs64.exe''');
    Result := Procs.Count > 0;
  except
    Result := False;
  end;
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  while IsObsRunning() do
  begin
    if SuppressibleMsgBox('OBS Studio is open.' + #13#10 + #13#10 +
              'Please close OBS, then click Retry.', mbInformation, MB_RETRYCANCEL, IDCANCEL) = IDCANCEL then
    begin
      Result := False;
      Exit;
    end;
  end;
end;

function ObsMajorVersion(ObsDir: String; var VersionText: String): Integer;
var
  V: String;
  Dot: Integer;
begin
  Result := -1;
  VersionText := '';
  if GetVersionNumbersString(ObsDir + '\bin\64bit\obs64.exe', V) then
  begin
    VersionText := V;
    Dot := Pos('.', V);
    if Dot > 0 then
      Result := StrToIntDef(Copy(V, 1, Dot - 1), -1);
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Dir, VersionText: String;
  Major: Integer;
begin
  Result := True;
  if CurPageID <> wpSelectDir then
    Exit;

  Dir := WizardDirValue();
  if not FileExists(Dir + '\bin\64bit\obs64.exe') then
  begin
    SuppressibleMsgBox('OBS Studio was not found in:' + #13#10 + Dir + #13#10 + #13#10 +
           'Click Browse and choose the folder where OBS is installed ' +
           '(usually C:\Program Files\obs-studio).' + #13#10 + #13#10 +
           'Don''t have OBS yet? Get it free from obsproject.com, then run this installer again.',
           mbError, MB_OK, IDOK);
    Result := False;
    Exit;
  end;

  Major := ObsMajorVersion(Dir, VersionText);
  if (Major >= 0) and (Major < {#MinObsMajor}) then
  begin
    Result := SuppressibleMsgBox('Your OBS is version ' + VersionText + '.' + #13#10 +
                     'Multistream needs OBS {#MinObsMajor}.0 or newer.' + #13#10 + #13#10 +
                     'Please update OBS first (obsproject.com/download). Your scenes and settings are kept.' + #13#10 + #13#10 +
                     'Install anyway? (Multistream will stay switched off until OBS is updated.)',
                     mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES;
  end;
end;

procedure DeleteSavedSetups();
var
  Base: String;
  FindRec: TFindRec;
begin
  Base := ExpandConstant('{userappdata}\obs-studio\basic\profiles');
  if FindFirst(Base + '\*', FindRec) then
  try
    repeat
      if ((FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0) and
         (FindRec.Name <> '.') and (FindRec.Name <> '..') then
        DeleteFile(Base + '\' + FindRec.Name + '\{#PluginName}.json');
    until not FindNext(FindRec);
  finally
    FindClose(FindRec);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    while IsObsRunning() do
    begin
      if SuppressibleMsgBox('Please close OBS Studio, then click Retry.', mbInformation, MB_RETRYCANCEL, IDCANCEL) = IDCANCEL then
        Abort;
    end;
  end;
  if CurUninstallStep = usPostUninstall then
  begin
    if SuppressibleMsgBox('Multistream was removed from OBS.' + #13#10 + #13#10 +
              'Also delete your saved platforms and stream keys?' + #13#10 +
              '(Choose No if you might install it again later.)',
              mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES then
      DeleteSavedSetups();
  end;
end;
