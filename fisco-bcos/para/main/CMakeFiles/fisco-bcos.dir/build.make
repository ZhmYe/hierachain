# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.21

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Disable VCS-based implicit rules.
% : %,v

# Disable VCS-based implicit rules.
% : RCS/%

# Disable VCS-based implicit rules.
% : RCS/%,v

# Disable VCS-based implicit rules.
% : SCCS/s.%

# Disable VCS-based implicit rules.
% : s.%

.SUFFIXES: .hpux_make_needs_suffix_list

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/local/Cellar/cmake/3.21.1/bin/cmake

# The command to remove a file.
RM = /usr/local/Cellar/cmake/3.21.1/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /Users/tanghaibo/fisco/FISCO-BCOS/fisco-bcos

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /Users/tanghaibo/fisco/FISCO-BCOS/fisco-bcos/para

# Include any dependencies generated for this target.
include main/CMakeFiles/fisco-bcos.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include main/CMakeFiles/fisco-bcos.dir/compiler_depend.make

# Include the progress variables for this target.
include main/CMakeFiles/fisco-bcos.dir/progress.make

# Include the compile flags for this target's objects.
include main/CMakeFiles/fisco-bcos.dir/flags.make

main/CMakeFiles/fisco-bcos.dir/main.o: main/CMakeFiles/fisco-bcos.dir/flags.make
main/CMakeFiles/fisco-bcos.dir/main.o: ../main/main.cpp
main/CMakeFiles/fisco-bcos.dir/main.o: main/CMakeFiles/fisco-bcos.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/Users/tanghaibo/fisco/FISCO-BCOS/fisco-bcos/para/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object main/CMakeFiles/fisco-bcos.dir/main.o"
	cd /Users/tanghaibo/fisco/FISCO-BCOS/fisco-bcos/para/main && /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT main/CMakeFiles/fisco-bcos.dir/main.o -MF CMakeFiles/fisco-bcos.dir/main.o.d -o CMakeFiles/fisco-bcos.dir/main.o -c /Users/tanghaibo/fisco/FISCO-BCOS/fisco-bcos/main/main.cpp

main/CMakeFiles/fisco-bcos.dir/main.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/fisco-bcos.dir/main.i"
	cd /Users/tanghaibo/fisco/FISCO-BCOS/fisco-bcos/para/main && /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /Users/tanghaibo/fisco/FISCO-BCOS/fisco-bcos/main/main.cpp > CMakeFiles/fisco-bcos.dir/main.i

main/CMakeFiles/fisco-bcos.dir/main.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/fisco-bcos.dir/main.s"
	cd /Users/tanghaibo/fisco/FISCO-BCOS/fisco-bcos/para/main && /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /Users/tanghaibo/fisco/FISCO-BCOS/fisco-bcos/main/main.cpp -o CMakeFiles/fisco-bcos.dir/main.s

# Object files for target fisco-bcos
fisco__bcos_OBJECTS = \
"CMakeFiles/fisco-bcos.dir/main.o"

# External object files for target fisco-bcos
fisco__bcos_EXTERNAL_OBJECTS =

main/fisco-bcos: main/CMakeFiles/fisco-bcos.dir/main.o
main/fisco-bcos: main/CMakeFiles/fisco-bcos.dir/build.make
main/fisco-bcos: main/CMakeFiles/fisco-bcos.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/Users/tanghaibo/fisco/FISCO-BCOS/fisco-bcos/para/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX executable fisco-bcos"
	cd /Users/tanghaibo/fisco/FISCO-BCOS/fisco-bcos/para/main && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/fisco-bcos.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
main/CMakeFiles/fisco-bcos.dir/build: main/fisco-bcos
.PHONY : main/CMakeFiles/fisco-bcos.dir/build

main/CMakeFiles/fisco-bcos.dir/clean:
	cd /Users/tanghaibo/fisco/FISCO-BCOS/fisco-bcos/para/main && $(CMAKE_COMMAND) -P CMakeFiles/fisco-bcos.dir/cmake_clean.cmake
.PHONY : main/CMakeFiles/fisco-bcos.dir/clean

main/CMakeFiles/fisco-bcos.dir/depend:
	cd /Users/tanghaibo/fisco/FISCO-BCOS/fisco-bcos/para && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /Users/tanghaibo/fisco/FISCO-BCOS/fisco-bcos /Users/tanghaibo/fisco/FISCO-BCOS/fisco-bcos/main /Users/tanghaibo/fisco/FISCO-BCOS/fisco-bcos/para /Users/tanghaibo/fisco/FISCO-BCOS/fisco-bcos/para/main /Users/tanghaibo/fisco/FISCO-BCOS/fisco-bcos/para/main/CMakeFiles/fisco-bcos.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : main/CMakeFiles/fisco-bcos.dir/depend

