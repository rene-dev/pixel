CC = gcc
EXE = pixel

# configuration
PIXEL_WIDTH = 1280
PIXEL_HEIGHT = 720
PORT = 1234

DEFINES = -DPIXEL_WIDTH=$(PIXEL_WIDTH) -DPIXEL_HEIGHT=$(PIXEL_HEIGHT) -DPORT=$(PORT)
IP = $(shell ip addr | grep 'state UP' -A2 | tail -n1 | awk '{print $$2}' | cut -f1 -d'/')
INFO = $(IP):$(PORT) $(PIXEL_WIDTH)x$(PIXEL_HEIGHT)

all:
	$(CC) $(DEFINES) -O3 -g -DUSE_OPENGL -DUSE_EGL -DIS_RPI -DSTANDALONE -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DTARGET_POSIX -D_LINUX -fPIC -DPIC -D_REENTRANT -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -Wall -g -DHAVE_LIBOPENMAX=2 -DOMX -DOMX_SKIP64BIT -ftree-vectorize -pipe -DUSE_EXTERNAL_OMX -DHAVE_LIBBCM_HOST -DUSE_EXTERNAL_LIBBCM_HOST -DUSE_VCHIQ_ARM -Wno-psabi -I/opt/vc/include/ -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux -I./ -I/opt/vc/src/hello_pi/libs/ilclient -I/opt/vc/src/hello_pi/libs/vgfont -g -c main.c -o main.o -Wno-deprecated-declarations -Wno-missing-braces
	$(CC) -O3 -o $(EXE) -Wl,--whole-archive main.o -L/opt/vc/lib/ -lGLESv2 -lEGL -lbcm_host -lvcos -lvchiq_arm -lpthread -lrt -L/opt/vc/src/hello_pi/libs/vgfont -ldl -lm -Wl,--no-whole-archive -rdynamic

run:
	make
	./pixel &
	convert -size 320x20 xc:Transparent -pointsize 20 -fill black -draw "text 2,19 '$(INFO)'" -fill white -draw "text 0,17 '$(INFO)'" ip.png
	watch -n 10 python client.py 127.0.0.1 $(PORT) ip.png > /dev/null
