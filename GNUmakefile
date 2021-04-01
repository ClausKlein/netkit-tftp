#
# Standard stuff
#
.SUFFIXES:

# Disable the built-in implicit rules.
MAKEFLAGS+= --no-builtin-rules

.PHONY: update setup show all test lcov install check format clean distclean

UNAME:=$(shell uname)
PROJECT_NAME:=$(shell basename $${PWD})

##################################################
# begin of config part
# see https://www.kdab.com/clang-tidy-part-1-modernize-source-code-using-c11c14/
# and https://github.com/llvm-mirror/clang-tools-extra/blob/master/clang-tidy/tool/run-clang-tidy.py
#
### checkAllHeader:='include/spdlog/[acdlstv].*'
## checkAllHeader?='include/spdlog/[^f].*'
checkAllHeader?='$(CURDIR)/.*'

# NOTE: there are many errors with boost::test, doctest, catch test framework! CK
CHECKS?='-*-non-private-member-variables-in-classes,-cppcoreguidelines-pro-bounds-*,-cppcoreguidelines-avoid-*,-cppcoreguidelines-macro-usage,-readability-magic-numbers'
CHECKS?='-*,cppcoreguidelines-*,-cppcoreguidelines-pro-*,-cppcoreguidelines-avoid-*,-cppcoreguidelines-macro-usage,-cppcoreguidelines-narrowing-*'
CHECKS?='-*,portability-*,readability-*,-readability-magic-numbers'
CHECKS?='-*,misc-*,boost-*,cert-*,misc-unused-parameters'

# TODO setup:
# source /opt/sdhr/SDHR/core/v0.4.1-0-ga6ed523/imx8mm-sdhr/develop/sdk/environment-setup-aarch64-poky-linux
#
# prevent hard config of find_package(asio 1.14.1 CONFIG CMAKE_FIND_ROOT_PATH_BOTH)
ifeq (NO${CROSS_COMPILE},NO)
    ifeq (${UNAME},Darwin)
        CC:=/usr/local/opt/llvm/bin/clang
        CXX:=/usr/local/opt/llvm/bin/clang++
        BOOST_ROOT:=/usr/local/opt/boost
        export BOOST_ROOT
    endif

    # NOTE: Do not uses with DESTDIR! CMAKE_INSTALL_PREFIX?=/
    DESTDIR?=/tmp/staging/$(PROJECT_NAME)
    export DESTDIR

    CMAKE_STAGING_PREFIX?=/usr/local
    CMAKE_PREFIX_PATH?="${CMAKE_STAGING_PREFIX};/opt/local;/usr"
else
    CMAKE_STAGING_PREFIX?=/tmp/staging/${CROSS_COMPILE}$(PROJECT_NAME)
    CMAKE_PREFIX_PATH?="${CMAKE_STAGING_PREFIX};/opt/sdhr/SDHR/staging/imx8mm-sdhr/develop/"
    #FIXME CMAKE_FIND_ROOT_PATH?="${CMAKE_STAGING_PREFIX};${OECORE_TARGET_SYSROOT}"
endif

# NOTE: use
#NO!    BUILD_TYPE=Coverage make lcov
BUILD_TYPE?=Debug
BUILD_TYPE?=Release
# GENERATOR:=Xcode
GENERATOR?=Ninja

# end of config part
##################################################


BUILD_DIR:=../.build-$(PROJECT_NAME)-${CROSS_COMPILE}$(BUILD_TYPE)
ifeq ($(BUILD_TYPE),Coverage)
    USE_LOCV=ON
    ifeq (NO${CROSS_COMPILE},NO)
        CC:=/usr/bin/gcc
        CXX:=/usr/bin/g++
    endif
else
    USE_LOCV=OFF
endif

all: setup .configure-$(BUILD_TYPE)
	cmake --build $(BUILD_DIR)

test: all
	cd $(BUILD_DIR) && ctest -C $(BUILD_TYPE) --rerun-failed --output-on-failure .
	cd $(BUILD_DIR) && ctest -C $(BUILD_TYPE) .

# update CPM.cmake
update:
	pip3 install jinja2 Pygments gcovr cmake_format==0.6.13 pyyaml tftpy
	wget -q -O cmake/CPM.cmake https://github.com/cpm-cmake/CPM.cmake/releases/latest/download/get_cpm.cmake
	wget -q -O cmake/WarningsAsErrors.cmake https://raw.githubusercontent.com/approvals/ApprovalTests.cpp/master/CMake/WarningsAsErrors.cmake


# NOTE: we do only check the new cpp file! CK
check: setup .configure-$(BUILD_TYPE) compile_commands.json
	run-clang-tidy.py -header-filter=$(checkAllHeader) -checks=$(CHECKS) | tee run-clang-tidy.log 2>&1
	egrep '\b(warning|error):' run-clang-tidy.log | perl -pe 's/(^.*) (warning|error):/\2/' | sort -u

setup: $(BUILD_DIR) .clang-tidy compile_commands.json

.configure-$(BUILD_TYPE): CMakeLists.txt
	cd $(BUILD_DIR) && cmake -G $(GENERATOR) -Wdeprecated -Wdev \
      -DUSE_LCOV=$(USE_LOCV) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
      -DCMAKE_PREFIX_PATH=$(CMAKE_PREFIX_PATH) \
      -DCMAKE_STAGING_PREFIX=$(CMAKE_STAGING_PREFIX) \
      -DCMAKE_MODULE_PATH=${HOME}/Workspace/cmake \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_C_COMPILER=${CC} -DCMAKE_CXX_COMPILER=${CXX} $(CURDIR)
	touch $@

compile_commands.json: .configure-$(BUILD_TYPE)
	ln -sf $(CURDIR)/$(BUILD_DIR)/compile_commands.json .

$(BUILD_DIR): GNUmakefile
	mkdir -p $@


format: .clang-format .cmake-format
	find . -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.c' -o -name '*.cpp' \) -print0 | xargs -0 clang-format -style=file -i
	find . -type f \( -name '*.cmake' -o -name 'CMakeLists.txt' \) -print0 | xargs -0 cmake-format -i


show: setup
	cmake -S $(CURDIR) -B $(BUILD_DIR) -L

lcov: $(BUILD_DIR) .configure-Coverage
	cmake --build $(BUILD_DIR) --target $@

install: $(BUILD_DIR)
	cmake --build $(BUILD_DIR) --target $@

clean: $(BUILD_DIR)
	cmake --build $(BUILD_DIR) --target $@

distclean:
	rm -rf $(BUILD_DIR) .configure-$(BUILD_TYPE) compile_commands.json *~ .*~ tags
	find . -name '*~' -delete


# These rules keep make from trying to use the match-anything rule below
# to rebuild the makefiles--ouch!

## CMakeLists.txt :: ;
GNUmakefile :: ;
.clang-tidy :: ;
.clang-format :: ;

# Anything we don't know how to build will use this rule.  The command is
# a do-nothing command.
% :: ;

