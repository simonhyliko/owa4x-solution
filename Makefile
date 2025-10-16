#!make
#Environment
OWA4_11.3_ENV=/opt/crosstool/setup-owa4x-11.3_env

MKDIR = mkdir
CP = cp
RM = rm
DPKG = dpkg-deb

CND_DISTDIR=dist

#OBJECT - CAN Data Collector
VERSION=1.0.0
OBJECT_SOCKET=can_socket_collector
DPKG_VERSION = 1

#INCLUDE paths - ARM cross-compile
SYSROOT=/opt/crosstool/owa4x/CC11.3/arm-gnu-toolchain-11.3.rel1-x86_64-arm-none-linux-gnueabihf/arm-none-linux-gnueabihf
INCLUDE=-I.
INCLUDE += -Iinclude
INCLUDE += -Isrc
INCLUDE += -I$(SYSROOT)/usr/include
INCLUDE += -I$(SYSROOT)/usr/include/mdf

PKG_CONFIG ?= pkg-config
PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags dbcppp mdflib 2>/dev/null)
PKG_LIBS := $(shell $(PKG_CONFIG) --libs dbcppp mdflib 2>/dev/null)

# Use PKG_CONFIG if available, otherwise fallback to manual paths
ifneq ($(strip $(PKG_CFLAGS)),)
INCLUDE += $(PKG_CFLAGS)
endif

#DEFINITIONS
DEFS = -DOWA4X
DEFINE= -DVERSION=\"${VERSION}\"

#Source Files
SOURCE_SOCKET = src/main.cpp src/can_reader.cpp src/dbc_decoder.cpp src/mf4_writer.cpp src/signal_handler.cpp

#LIBS to include - ARM cross-compile
LIBS = -lpthread -lstdc++ -lsystemd
LIBS += -L$(SYSROOT)/usr/lib -L$(SYSROOT)/libc/usr/lib
LIBS += $(SYSROOT)/usr/lib/libmdf.a $(SYSROOT)/usr/lib/libdbcppp.so -lxml2 -lexpat -lz -lm

#Compiler flags
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
STRIP_OPTION= -s
GDB=-g

MSG_BUILD = '**** Building CAN Socket Collector'

# Build tasks
clean:
	find . -type f -name "$(OBJECT_SOCKET)" -exec rm {} \;
	find . -type f -name "*.deb" -exec rm {} +
	find . -type f -name "*.o" -exec rm {} +
	rm -rf $(CND_DISTDIR)

all: clean owa4-11.3

#Build entries
owa4-11.3: build_socket_owa4_cc11.3

build_socket_owa4_cc11.3:
	@echo
	@echo $(MSG_BUILD) "owa4x CC11.3"
	${MKDIR} -p ${CND_DISTDIR}/owa4x/CC-11.3
	. $(OWA4_11.3_ENV); \
	echo "**** Cross-Compiler CXX="${CXX}; \
	echo "**** Building $(OBJECT_SOCKET)..."; \
	$${CXX} $(CXXFLAGS) $(DEFINE) $(DEFS) -o$(CND_DISTDIR)/owa4x/CC-11.3/$(OBJECT_SOCKET) $(INCLUDE) $(SOURCE_SOCKET) $(STRIP_OPTION) $(LIBS);
	@echo $(MSG_BUILD) owa4x CC11.3 Done!!
	@echo "Binaries created:"
	@ls -la $(CND_DISTDIR)/owa4x/CC-11.3/

# Build debug version (with symbols)
debug: clean
	@echo
	@echo $(MSG_BUILD) owa4x CC11.3 DEBUG
	${MKDIR} -p ${CND_DISTDIR}/owa4x/CC-11.3
	. $(OWA4_11.3_ENV); \
	echo "**** Cross-Compiler CXX="$${CXX}; \
	echo "**** Building $(OBJECT_SOCKET) DEBUG..."; \
	$${CXX} $(CXXFLAGS) $(GDB) $(DEFINE) $(DEFS) -o$(CND_DISTDIR)/owa4x/CC-11.3/$(OBJECT_SOCKET) $(INCLUDE) $(SOURCE_SOCKET) $(LIBS);
	@echo $(MSG_BUILD) owa4x CC11.3 DEBUG Done!!

# Install to OWA4X device (set OWA_HOST environment variable)
install: owa4-11.3
	@if [ -z "$(OWA_HOST)" ]; then \
		echo "❌ Please set OWA_HOST environment variable (e.g., export OWA_HOST=192.168.1.100)"; \
		exit 1; \
	fi
	@echo "Installing to OWA4X device $(OWA_HOST)..."
	scp $(CND_DISTDIR)/owa4x/CC-11.3/$(OBJECT_SOCKET) root@$(OWA_HOST):/home/seloni/acq_json/
	scp *.json root@$(OWA_HOST):/home/seloni/acq_json/
	@echo "✅ Installation completed on $(OWA_HOST)"

# Test compilation info
info:
	@echo "=== Makefile Configuration ==="
	@echo "Environment: $(OWA4_11.3_ENV)"
	@echo "Object: $(OBJECT_SOCKET)"
	@echo "Sources: $(SOURCE_SOCKET)"
	@echo "Include: $(INCLUDE)"
	@echo "Libs: $(LIBS)"
	@echo "CXX Flags: $(CXXFLAGS)"

.PHONY: all clean debug install info
