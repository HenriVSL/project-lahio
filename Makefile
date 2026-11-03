# Project Lahio (working title KAUPUNKI) - NMAKE makefile (Visual C++ 6.0). Owner: T00. Build in the VM: ~/Documents/86box/samba/vmb/w98 build lahio
# All objects depend on all headers (simple and safe). /Iart: generated art headers (art.h convention).
# New module? Add obj\<name>.obj to OBJS (and to PORTABLE in tests/Makefile.mac if it is portable).
CFLAGS = /nologo /W3 /O2 /G6 /DWIN32 /D_WINDOWS /Isrc /Iart
LIBS   = user32.lib gdi32.lib winmm.lib ddraw.lib dsound.lib
OBJS   = obj\main.obj obj\plat.obj obj\gfx.obj obj\snd.obj obj\config.obj obj\util.obj \
         obj\art.obj obj\world.obj obj\defs.obj obj\mapgen.obj obj\iso.obj obj\render.obj \
         obj\sim.obj obj\net.obj obj\fields.obj obj\budget.obj obj\traffic.obj obj\tools.obj \
         obj\ui.obj obj\text.obj obj\save.obj obj\menu.obj obj\game.obj obj\life.obj obj\politics.obj obj\lifecar.obj \
         obj\poltab.obj obj\ui_pol.obj obj\industry.obj obj\transit.obj obj\lifebus.obj
HDRS   = src\*.h

lahio.exe: obj $(OBJS)
	link /nologo /subsystem:windows /out:lahio.exe $(OBJS) $(LIBS)

obj:
	-mkdir obj

{src}.cpp{obj}.obj:
	cl $(CFLAGS) /c /Fo$@ $<

$(OBJS): $(HDRS)

clean:
	-del obj\*.obj
	-del lahio.exe
