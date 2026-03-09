"""
PlatformIO extra script: adds MSYS2 mingw64/bin to PATH before
running the native simulator, so SDL2.dll is found at runtime.
"""
import os
Import("env")

if env.get("PIOPLATFORM") == "native":
    mingw_bin = r"C:\msys64\mingw64\bin"
    if os.path.isdir(mingw_bin):
        env.PrependENVPath("PATH", mingw_bin)
