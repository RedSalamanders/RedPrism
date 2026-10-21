# Prepares only a GitHub-owned desktop for the existing 1280x800 DIP window and 144-DPI captures.
# The application assertions remain unchanged. No registry display settings are persisted.
# https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-changedisplaysettingsexw
[CmdletBinding()]
param([switch]$InspectOnly)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if(-not $InspectOnly -and ($env:GITHUB_ACTIONS -cne 'true' -or $env:RUNNER_ENVIRONMENT -cne 'github-hosted')){
    throw 'Display initialization is restricted to a GitHub-hosted runner. Use -InspectOnly for read-only local inspection.'
}
if(-not ('RedPrismCiDisplay.Native' -as [type])){
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace RedPrismCiDisplay {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    public struct Mode {
        [MarshalAs(UnmanagedType.ByValTStr,SizeConst=32)] public string DeviceName;
        public ushort SpecVersion, DriverVersion, Size, DriverExtra;
        public uint Fields;
        public int PositionX, PositionY;
        public uint Orientation, FixedOutput;
        public short Color, Duplex, YResolution, TTOption, Collate;
        [MarshalAs(UnmanagedType.ByValTStr,SizeConst=32)] public string FormName;
        public ushort LogPixels;
        public uint BitsPerPel, Width, Height, DisplayFlags, Frequency;
        public uint IcmMethod, IcmIntent, MediaType, DitherType, Reserved1, Reserved2, PanningWidth, PanningHeight;
    }
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
    public static class Native {
        [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="EnumDisplaySettingsW")]
        [return:MarshalAs(UnmanagedType.Bool)] private static extern bool Enum(string device,int mode,ref Mode settings);
        [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="ChangeDisplaySettingsExW")]
        private static extern int Change(string device,ref Mode settings,IntPtr window,uint flags,IntPtr parameters);
        [DllImport("user32.dll")] public static extern uint GetDpiForSystem();
        [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
        [DllImport("user32.dll",SetLastError=true)]
        [return:MarshalAs(UnmanagedType.Bool)] public static extern bool AdjustWindowRectExForDpi(ref Rect bounds,uint style,bool menu,uint extendedStyle,uint dpi);
        [DllImport("user32.dll",EntryPoint="SystemParametersInfoW",SetLastError=true)]
        [return:MarshalAs(UnmanagedType.Bool)] public static extern bool WorkArea(uint action,uint parameter,ref Rect area,uint flags);
        public static bool Get(int index,ref Mode mode){return Enum(null,index,ref mode);}
        public static int Set(ref Mode mode,uint flags){return Change(null,ref mode,IntPtr.Zero,flags,IntPtr.Zero);}
        public static Mode Copy(Mode mode){return mode;}
        public static Mode NewMode(){ var mode=new Mode();mode.Size=(ushort)Marshal.SizeOf<Mode>();return mode; }
    }
}
'@
}
function Read-Mode([int]$Index=-1){
    $mode=[RedPrismCiDisplay.Native]::NewMode()
    if([RedPrismCiDisplay.Native]::Get($Index,[ref]$mode)){return $mode}
    if($Index -eq -1){throw 'Cannot read the CI display mode.'}
    return $null
}
$context=[RedPrismCiDisplay.Native]::SetThreadDpiAwarenessContext([IntPtr]::new(-4))
if($context -eq [IntPtr]::Zero){throw 'Cannot inspect physical CI display dimensions with Per-Monitor-V2 awareness.'}
try{
    $dpi=[RedPrismCiDisplay.Native]::GetDpiForSystem()
    if(-not $dpi){throw 'CI display DPI is unavailable.'}
    $layoutDpi=[Math]::Max(144,$dpi)
    $required=[RedPrismCiDisplay.Rect]::new()
    $required.Right=[int][Math]::Ceiling(1280*$layoutDpi/96)
    $required.Bottom=[int][Math]::Ceiling(800*$layoutDpi/96)
    if(-not [RedPrismCiDisplay.Native]::AdjustWindowRectExForDpi([ref]$required,0x02cf0000,$false,0x00240000,$layoutDpi)){
        throw 'Cannot calculate the CI window/capture fixture bounds.'
    }
    $width=$required.Right-$required.Left+64
    $height=$required.Bottom-$required.Top+64
    $before=Read-Mode
    $area=[RedPrismCiDisplay.Rect]::new()
    if(-not [RedPrismCiDisplay.Native]::WorkArea(0x30,0,[ref]$area,0)){throw 'Cannot inspect the CI work area.'}
    Write-Host "Display fixture: current=$($before.Width)x$($before.Height); work=$($area.Right-$area.Left)x$($area.Bottom-$area.Top); DPI=$dpi; required=$($width)x$height."
    if($InspectOnly){Write-Host 'Read-only inspection; display unchanged.';return}
    $changed=$false
    if($area.Right-$area.Left -lt $width -or $area.Bottom-$area.Top -lt $height){
        $candidates=@(for($index=0;;$index++){
            $mode=Read-Mode $index
            if($null -eq $mode){break}
            if($mode.Width -ge $width -and $mode.Height -ge ($height+64) -and $mode.BitsPerPel -ge 32){$mode}
        })
        # Virtual display drivers can accept sizes not listed by EnumDisplaySettings; CDS_TEST validates them first.
        foreach($size in @(@(2048,1536),@(2560,1440),@(2560,1600),@(3840,2160))){
            if($size[0] -ge $width -and $size[1] -ge ($height+64)){
                $mode=[RedPrismCiDisplay.Native]::Copy($before);$mode.Width=$size[0];$mode.Height=$size[1];$mode.Fields=0x00180000
                $candidates+=$mode
            }
        }
        foreach($mode in @($candidates|Sort-Object @{Expression={[long]$_.Width*$_.Height}},Frequency)){
            $candidate=[RedPrismCiDisplay.Native]::Copy($mode)
            $candidate.Fields=0x00180000 # DM_PELSWIDTH | DM_PELSHEIGHT
            if([RedPrismCiDisplay.Native]::Set([ref]$candidate,2) -ne 0){continue}
            $result=[RedPrismCiDisplay.Native]::Set([ref]$candidate,0)
            if($result -eq 0){$changed=$true;break}
            Write-Host "Display mode $($candidate.Width)x$($candidate.Height) could not be applied: $result"
        }
        if(-not $changed){throw "The hosted display cannot provide the required $($width)x$height fixture. Native qualification is unavailable on this runner."}
    }
    $after=Read-Mode
    $area=[RedPrismCiDisplay.Rect]::new()
    if(-not [RedPrismCiDisplay.Native]::WorkArea(0x30,0,[ref]$area,0) -or
        $area.Right-$area.Left -lt $width -or $area.Bottom-$area.Top -lt $height){
        throw 'The applied hosted display does not provide the required physical work area.'
    }
    $record=[ordered]@{dpi=$dpi;requiredWidth=$width;requiredHeight=$height;changed=$changed;
        before=@{width=$before.Width;height=$before.Height};after=@{width=$after.Width;height=$after.Height};
        workArea=@{width=$area.Right-$area.Left;height=$area.Bottom-$area.Top};imageVersion=$env:ImageVersion}
    $output=Join-Path (Split-Path $PSScriptRoot) '.build/reports/ci-display.json'
    [void][IO.Directory]::CreateDirectory((Split-Path $output))
    [IO.File]::WriteAllText($output,($record|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
    Write-Host "Hosted display fixture verified: $($after.Width)x$($after.Height), DPI $dpi."
}finally{[void][RedPrismCiDisplay.Native]::SetThreadDpiAwarenessContext($context)}
