TARGET 				= brightness
OBJS 				=  import.o exports.o main.o include/sysconhk.o include/blit.o include/minIni.o

INCDIR 				= include
LIBDIR				= lib
LIBS			 	= -lpspsystemctrl_kernel -lpsprtc -lpsppower_driver
MININI_DEFINES 		= -DNDEBUG -DINI_READONLY -DINI_FILETYPE=SceUID -DPORTABLE_STRNICMP -DINI_NOFLOAT
CFLAGS 				= -O2 -G0 -Wall -std=c99 $(MININI_DEFINES) -DKPRINTF_ENABLED
LDFLAGS 			= -nostdlib -nodefaultlibs
CXXFLAGS 			= $(CFLAGS) -fno-exceptions -fno-rtti -fno-pic
ASFLAGS 			= $(CFLAGS)

BUILD_PRX 			= 1
PRX_EXPORTS 		= exports.exp

USE_KERNEL_LIBC		= 1
USE_KERNEL_LIBS		= 1

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build_prx.mak