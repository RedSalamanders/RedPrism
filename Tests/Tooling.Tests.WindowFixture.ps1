# Independent Win32 geometry preflight, not application/rendering qualification.
# Owns a hidden HWND only; never shows it, takes focus, or changes display settings.
# https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setwindowpos
[CmdletBinding()]
param([string]$ReportPath='')
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
if(-not $ReportPath){$ReportPath=Join-Path $root '.build/reports/ci-window-fixture.json'}
$ReportPath=[IO.Path]::GetFullPath($ReportPath)
$allowed=[IO.Path]::GetFullPath((Join-Path $root '.build')).TrimEnd('\')+'\'
if(-not $ReportPath.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'Fixture report must stay under this repository .build.'}
if(-not ('RedPrismWindowFixture.Probe' -as [type])){
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
namespace RedPrismWindowFixture {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
    public sealed class OwnedWindow : SafeHandleZeroOrMinusOneIsInvalid {
        public OwnedWindow() : base(true) {}
        [DllImport("user32.dll")] [return:MarshalAs(UnmanagedType.Bool)] private static extern bool DestroyWindow(IntPtr window);
        protected override bool ReleaseHandle(){return DestroyWindow(handle);}
    }
    public static class Probe {
        [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true,EntryPoint="CreateWindowExW")]
        private static extern OwnedWindow Create(uint extended,string kind,string title,uint style,int x,int y,int width,int height,IntPtr parent,IntPtr menu,IntPtr instance,IntPtr data);
        [DllImport("user32.dll",SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetWindowPos(OwnedWindow window,IntPtr after,int x,int y,int width,int height,uint flags);
        [DllImport("user32.dll",SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetClientRect(OwnedWindow window,out Rect bounds);
        [DllImport("user32.dll")] [return:MarshalAs(UnmanagedType.Bool)] private static extern bool IsWindowVisible(OwnedWindow window);
        [DllImport("user32.dll")] private static extern uint GetDpiForWindow(OwnedWindow window);
        [DllImport("user32.dll")] private static extern int GetSystemMetrics(int index);
        [DllImport("user32.dll")] private static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
        [DllImport("user32.dll",SetLastError=true)] [return:MarshalAs(UnmanagedType.Bool)]
        private static extern bool AdjustWindowRectExForDpi(ref Rect bounds,uint style,bool menu,uint extended,uint dpi);
        public static int[] Run(){
            var previous=SetThreadDpiAwarenessContext(new IntPtr(-4));
            if(previous==IntPtr.Zero)throw new Win32Exception(Marshal.GetLastWin32Error(),"Cannot establish fixture DPI context");
            try {
                const uint style=0x02cf0000,extended=0x00240000;
                using(var window=Create(extended,"STATIC","RedPrism hidden fixture",style,0,0,200,200,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero)){
                    if(window.IsInvalid)throw new Win32Exception(Marshal.GetLastWin32Error(),"Cannot create fixture HWND");
                    uint dpi=GetDpiForWindow(window);
                    if(dpi==0)throw new InvalidOperationException("Fixture DPI is zero");
                    int width=(int)Math.Round(1280*dpi/96.0,MidpointRounding.AwayFromZero),height=(int)Math.Round(800*dpi/96.0,MidpointRounding.AwayFromZero);
                    var outer=new Rect{Right=width,Bottom=height};
                    if(!AdjustWindowRectExForDpi(ref outer,style,false,extended,dpi))throw new Win32Exception(Marshal.GetLastWin32Error());
                    // NOACTIVATE | NOZORDER | NOMOVE | NOSENDCHANGING: exact off-screen harness bounds.
                    if(!SetWindowPos(window,IntPtr.Zero,0,0,outer.Right-outer.Left,outer.Bottom-outer.Top,0x416))throw new Win32Exception(Marshal.GetLastWin32Error());
                    Rect actual;
                    if(!GetClientRect(window,out actual) || actual.Right-actual.Left!=width || actual.Bottom-actual.Top!=height || IsWindowVisible(window))
                        throw new InvalidOperationException("Hidden fixture client differs from its exact DPI-scaled canvas");
                    return new[]{(int)dpi,width,height,GetSystemMetrics(0),GetSystemMetrics(1)};
                }
            }finally{if(SetThreadDpiAwarenessContext(previous)==IntPtr.Zero)throw new InvalidOperationException("Cannot restore fixture DPI context");}
        }
    }
}
'@
}
$values=[RedPrismWindowFixture.Probe]::Run()
$record=[ordered]@{passed=$true;dpi=$values[0];clientWidth=$values[1];clientHeight=$values[2];
    desktopWidth=$values[3];desktopHeight=$values[4];hidden=$true;displaySettingsChanged=$false;
    imageVersion=$env:ImageVersion;qualification='independent Win32 geometry preflight; application qualification is separate'}
[void][IO.Directory]::CreateDirectory((Split-Path $ReportPath))
[IO.File]::WriteAllText($ReportPath,($record|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
Write-Host "Hidden fixture verified: client=$($values[1])x$($values[2]), DPI=$($values[0]), desktop=$($values[3])x$($values[4]); display unchanged."
