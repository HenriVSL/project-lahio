CFLAGS = /nologo /W3 /O2 /G6 /DWIN32
vramprobe.exe: vramprobe.cpp
	cl $(CFLAGS) vramprobe.cpp user32.lib gdi32.lib ddraw.lib /link /subsystem:windows
