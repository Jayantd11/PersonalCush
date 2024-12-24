#!/usr/bin/python
#
# cd_test: tests the cd command
#
#

import sys, atexit, signal, time, threading, os
from testutils import *
from tempfile import mkstemp

console = setup_tests()

# Ensure that the shell prints the expected prompt
expect_prompt()

# Save the initial directory
initial_dir = os.getcwd()

# Test 1: Change to a specific directory (e.g., '/tmp')
test_dir = '/tmp'
sendline(f'cd {test_dir}')

# Ensure that the shell prints the expected prompt
expect_prompt("Shell did not print expected prompt after changing directory (1)")

# Verify that the current directory is '/tmp' using 'pwd'
sendline('pwd')
expect_exact('pwd\r\n', "Shell output extraneous characters")
expect_exact(f'{test_dir}\r\n', "Shell did not change to the expected directory")

# Ensure that the shell prints the expected prompt
expect_prompt("Shell did not print expected prompt after pwd (1)")

# Test 2: Change to the home directory using 'cd' without arguments
sendline('cd')

# Ensure that the shell prints the expected prompt
expect_prompt("Shell did not print expected prompt after changing to home directory")

# Get the home directory from the environment variable
home_dir = os.path.expanduser('~')

# Verify that the current directory is the home directory using 'pwd'
sendline('pwd')
expect_exact('pwd\r\n', "Shell output extraneous characters")
expect_exact(f'{home_dir}\r\n', "Shell did not change to the home directory")

# Ensure that the shell prints the expected prompt
expect_prompt("Shell did not print expected prompt after pwd (2)")

# Test 3: Attempt to change to a non-existent directory
nonexistent_dir = '/nonexistent_directory_for_testing'
sendline(f'cd {nonexistent_dir}')

# Expect an error message
expect_exact(f'cd: No such file or directory\r\n', "Shell did not print expected error message")

# Ensure that the shell prints the expected prompt
expect_prompt("Shell did not print expected prompt after attempting to change to a non-existent directory")

# Test 4: Change back to the initial directory
sendline(f'cd {initial_dir}')

# Ensure that the shell prints the expected prompt
expect_prompt("Shell did not print expected prompt after changing back to initial directory")

# Verify that the current directory is the initial directory using 'pwd'
sendline('pwd')
expect_exact('pwd\r\n', "Shell output extraneous characters")
expect_exact(f'{initial_dir}\r\n', "Shell did not change back to the initial directory")

# Ensure that the shell prints the expected prompt
expect_prompt("Shell did not print expected prompt after pwd (3)")

# Exit the shell
sendline("exit")
expect_exact("exit\r\n", "Shell output extraneous characters")

test_success()
