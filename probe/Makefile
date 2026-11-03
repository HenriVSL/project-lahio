# NMAKE makefile for the Kaupunki render probe (Visual C++ 6.0)
CFLAGS = /nologo /W3 /O2 /G6 /DWIN32

probe.exe: probe.cpp art.h
	cl $(CFLAGS) probe.cpp user32.lib gdi32.lib winmm.lib ddraw.lib /link /subsystem:windows

clean:
	-del *.obj
	-del probe.exe
