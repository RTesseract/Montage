# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.14

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:


#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:


# Remove some rules from gmake that .SUFFIXES does not remove.
SUFFIXES =

.SUFFIXES: .hpux_make_needs_suffix_list


# Suppress display of executed commands.
$(VERBOSE).SILENT:


# A target that is always out of date.
cmake_force:

.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E remove -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /localdisk/rsanna/Montage/ext/Clevel-Hashing

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /localdisk/rsanna/Montage/ext/Clevel-Hashing

# Include any dependencies generated for this target.
include tests/CMakeFiles/test_backtrace.dir/depend.make

# Include the progress variables for this target.
include tests/CMakeFiles/test_backtrace.dir/progress.make

# Include the compile flags for this target's objects.
include tests/CMakeFiles/test_backtrace.dir/flags.make

tests/CMakeFiles/test_backtrace.dir/test_backtrace.c.o: tests/CMakeFiles/test_backtrace.dir/flags.make
tests/CMakeFiles/test_backtrace.dir/test_backtrace.c.o: tests/test_backtrace.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/localdisk/rsanna/Montage/ext/Clevel-Hashing/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building C object tests/CMakeFiles/test_backtrace.dir/test_backtrace.c.o"
	cd /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests && /usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/test_backtrace.dir/test_backtrace.c.o   -c /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests/test_backtrace.c

tests/CMakeFiles/test_backtrace.dir/test_backtrace.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/test_backtrace.dir/test_backtrace.c.i"
	cd /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests && /usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests/test_backtrace.c > CMakeFiles/test_backtrace.dir/test_backtrace.c.i

tests/CMakeFiles/test_backtrace.dir/test_backtrace.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/test_backtrace.dir/test_backtrace.c.s"
	cd /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests && /usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests/test_backtrace.c -o CMakeFiles/test_backtrace.dir/test_backtrace.c.s

# Object files for target test_backtrace
test_backtrace_OBJECTS = \
"CMakeFiles/test_backtrace.dir/test_backtrace.c.o"

# External object files for target test_backtrace
test_backtrace_EXTERNAL_OBJECTS =

tests/libtest_backtrace.a: tests/CMakeFiles/test_backtrace.dir/test_backtrace.c.o
tests/libtest_backtrace.a: tests/CMakeFiles/test_backtrace.dir/build.make
tests/libtest_backtrace.a: tests/CMakeFiles/test_backtrace.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/localdisk/rsanna/Montage/ext/Clevel-Hashing/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking C static library libtest_backtrace.a"
	cd /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests && $(CMAKE_COMMAND) -P CMakeFiles/test_backtrace.dir/cmake_clean_target.cmake
	cd /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/test_backtrace.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
tests/CMakeFiles/test_backtrace.dir/build: tests/libtest_backtrace.a

.PHONY : tests/CMakeFiles/test_backtrace.dir/build

tests/CMakeFiles/test_backtrace.dir/clean:
	cd /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests && $(CMAKE_COMMAND) -P CMakeFiles/test_backtrace.dir/cmake_clean.cmake
.PHONY : tests/CMakeFiles/test_backtrace.dir/clean

tests/CMakeFiles/test_backtrace.dir/depend:
	cd /localdisk/rsanna/Montage/ext/Clevel-Hashing && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /localdisk/rsanna/Montage/ext/Clevel-Hashing /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests /localdisk/rsanna/Montage/ext/Clevel-Hashing /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests /localdisk/rsanna/Montage/ext/Clevel-Hashing/tests/CMakeFiles/test_backtrace.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : tests/CMakeFiles/test_backtrace.dir/depend
