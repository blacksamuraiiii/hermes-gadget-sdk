' Start or stop the USB relay without a console window.
'
'   wscript.exe //nologo relay_hook.vbs start
'   wscript.exe //nologo relay_hook.vbs stop
'
' Why this wrapper: the gateway runs inside WSL, and every Windows console program it launches
' (powershell.exe) gets a fresh console window on the desktop — so the relay hook flashed a
' window at each gateway start and stop. wscript.exe is a GUI-subsystem host, and
' WshShell.Run with a window style of 0 starts the child hidden, so nothing appears.
' The script's exit code is passed through: the adapter's contract is "0 means the relay is up".
Option Explicit

Dim sh, fso, which, here, cmd, rc
Set sh = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")

If WScript.Arguments.Count < 1 Then
  WScript.Echo "usage: wscript relay_hook.vbs start|stop"
  WScript.Quit 2
End If

which = LCase(WScript.Arguments(0))
If which <> "start" And which <> "stop" Then
  WScript.Echo "relay_hook.vbs: unknown action '" & which & "' (start|stop)"
  WScript.Quit 2
End If

here = fso.GetParentFolderName(WScript.ScriptFullName)
' -WindowStyle Hidden keeps the child's own window out of sight even before Run(0) applies.
cmd = "powershell.exe -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File """ & _
      here & "\relay_" & which & ".ps1"""

rc = sh.Run(cmd, 0, True)
WScript.Quit rc
