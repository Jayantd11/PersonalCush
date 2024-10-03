#!/usr/bin/python
#
# history_test: tests the history command
#
# Requires the following commands to be implemented or otherwise usable:
#   history

import sys, atexit, pexpect, proc_check, signal, time, threading, os
from testutils import *

console = setup_tests()

# Ensure that shell prints expected prompt
expect_prompt()

# Run 'echo test1'
sendline('echo test1')
expect_exact('echo test1\r\n', "Shell output extraneous characters")
expect_exact('test1\r\n', "Shell did not output expected echo")

# Ensure that shell prints expected prompt
expect_prompt("Shell did not print expected prompt after echo test1")

# Run 'history'
sendline('history')
expect_exact('history\r\n', "Shell output extraneous characters")

# Expected history output:
# 1  echo test1
# 2  history

expected_history = [
    '1  echo test1',
    '2  history'
]

# Read the history output
for expected_line in expected_history:
    expect_exact(expected_line + '\r\n', f"Shell did not output expected history line: {expected_line}")

# Ensure that shell prints expected prompt
expect_prompt("Shell did not print expected prompt after history command")

# Run 'echo test2'
sendline('echo test2')
expect_exact('echo test2\r\n', "Shell output extraneous characters")
expect_exact('test2\r\n', "Shell did not output expected echo")

# Ensure that shell prints expected prompt
expect_prompt("Shell did not print expected prompt after echo test2")

# Run 'history'
sendline('history')
expect_exact('history\r\n', "Shell output extraneous characters")

# Expected history output:
# 1  echo test1
# 2  history
# 3  echo test2
# 4  history

expected_history = [
    '1  echo test1',
    '2  history',
    '3  echo test2',
    '4  history'
]

# Read the history output
for expected_line in expected_history:
    expect_exact(expected_line + '\r\n', f"Shell did not output expected history line: {expected_line}")

# Ensure that shell prints expected prompt
expect_prompt("Shell did not print expected prompt after history command (2)")

# Exit the shell
sendline('exit')
expect_exact('exit\r\n', "Shell output extraneous characters")

test_success()
