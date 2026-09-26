# RIDE.app - the editor in a macOS window. The third front end over the one
# core: the terminal editor is ../Makefile's, the Windows Forms window is
# ../winforms/RIDEGui.vcxproj, and this is AppKit through Objective-C++.
#
#     make            build ../../build/RIDE-4.5/RIDE.app
#     make run        build it and open it
#     make clean
#
# Like the Windows window it compiles the core and ../winforms/bridge.cpp -
# the C interface both windows speak - and none of the terminal's drawing.
# The .mm files are Objective-C++; ../src stays C++14 and is not touched.

# Built on a Mac only; check-sources and clean work anywhere.
ON_MAC := $(filter Darwin,$(shell uname -s))

CXX ?= clang++
ifeq ($(origin CXX),default)
  CXX := clang++
endif

# The core's own flags, as ../Makefile has them. The window's files add ARC
# and leave out -pedantic, which argues with Objective-C syntax rather than
# with anything written here.
MACOS_MIN ?= 12.0
WERROR ?= -Werror
CXXFLAGS := -std=c++14 -Wall -Wextra $(WERROR) -O2 -mmacosx-version-min=$(MACOS_MIN)
CORE_FLAGS := $(CXXFLAGS) -pedantic
MAC_FLAGS := $(CXXFLAGS) -fobjc-arc -I../src -I../winforms
FRAMEWORKS := -framework Cocoa

# Kept equal to CORE_SRC and SHM_SRC in ../Makefile - the list the Windows
# window compiles too. `make check-sources` says when they have drifted.
CORE_SRC := src/buffer.cpp src/compile.cpp src/convert.cpp \
       src/indent.cpp src/syntax.cpp \
       src/toolchain.cpp src/json.cpp src/project.cpp src/find.cpp \
       src/utf8.cpp src/workspace.cpp src/symbols.cpp src/demangle_win.cpp \
       src/path.cpp src/process.cpp src/debugger.cpp src/settings.cpp src/about.cpp
SHM_SRC := src/shalimar/channel.cpp src/shalimar/session.cpp
BRIDGE_SRC := winforms/bridge.cpp
MAC_SRC := main.mm RIDEWindowController.mm RIDECodeView.mm RIDELineNumbers.mm

# Outside the checkout, beside the terminal editor's objects, so that a
# listing of the repository is source and nothing else.
BUILD ?= ../../build/RIDE-4.5
OBJDIR ?= $(BUILD)/mac-obj
APP ?= $(BUILD)/RIDE.app

CORE_OBJ := $(patsubst %.cpp,$(OBJDIR)/core/%.o,$(CORE_SRC) $(SHM_SRC) $(BRIDGE_SRC))
MAC_OBJ := $(patsubst %.mm,$(OBJDIR)/mac/%.o,$(MAC_SRC))

EXE := $(APP)/Contents/MacOS/RIDE

all: $(APP)

mac-only:
	@[ -n "$(ON_MAC)" ] || { echo "RIDE.app is built on a Mac - use ../Makefile for the terminal editor" >&2; exit 1; }

$(OBJDIR)/core/%.o: ../%.cpp | mac-only
	@mkdir -p $(dir $@)
	$(CXX) $(CORE_FLAGS) -I../src -MMD -MP -c -o $@ $<

$(OBJDIR)/mac/%.o: %.mm | mac-only
	@mkdir -p $(dir $@)
	$(CXX) $(MAC_FLAGS) -MMD -MP -c -o $@ $<

-include $(CORE_OBJ:.o=.d) $(MAC_OBJ:.o=.d)

$(EXE): $(CORE_OBJ) $(MAC_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -fobjc-arc $(FRAMEWORKS) -o $@ $(CORE_OBJ) $(MAC_OBJ)

# The bundle: the program, its Info.plist, the manual, and - docked beside the
# editor as on Windows - whatever compilers ../bin holds, with the Shalimar
# runtime in lib/ beside them. The editor finds them there first.
$(APP): $(EXE) Info.plist
	@mkdir -p $(APP)/Contents/Resources
	cp Info.plist $(APP)/Contents/Info.plist
	@printf 'APPL????' > $(APP)/Contents/PkgInfo
	@for tool in ../bin/*.exe; do \
	    [ -f "$$tool" ] || continue; \
	    case "$$tool" in */RIDE.exe) continue;; esac; \
	    cp -p "$$tool" $(APP)/Contents/MacOS/; \
	done
	@if [ -d ../bin/lib ]; then rm -rf $(APP)/Contents/MacOS/lib; cp -Rp ../bin/lib $(APP)/Contents/MacOS/lib; fi
	@rm -rf $(APP)/Contents/Resources/help && cp -R ../help $(APP)/Contents/Resources/help
	@if [ -f ../winforms/ride.ico ] && [ ! -f $(APP)/Contents/Resources/RIDE.icns ]; then \
	    sips -s format icns ../winforms/ride.ico --out $(APP)/Contents/Resources/RIDE.icns >/dev/null 2>&1 && \
	    /usr/libexec/PlistBuddy -c 'Add :CFBundleIconFile string RIDE.icns' $(APP)/Contents/Info.plist >/dev/null 2>&1 || true; \
	fi
	codesign --force --deep --sign - $(APP)
	@touch $(APP)
	@echo "built $(APP)"

run: $(APP)
	open $(APP)

check-sources:
	@want=$$(sed -n '/^CORE_SRC :=/,/[^\\]$$/p' ../Makefile | tr -s ' \\\t\n' ' ' | sed 's/CORE_SRC :=//'); \
	have=$$(sed -n '/^CORE_SRC :=/,/[^\\]$$/p' Makefile | tr -s ' \\\t\n' ' ' | sed 's/CORE_SRC :=//'); \
	[ "$$want" = "$$have" ] && echo "the core list matches ../Makefile" || \
	    { echo "CORE_SRC differs from ../Makefile:"; echo "  there: $$want"; echo "  here:  $$have"; exit 1; }

clean:
	rm -rf $(OBJDIR) $(APP)

.PHONY: all run check-sources clean mac-only
