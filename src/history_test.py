#!/usr/bin/python
#
# history_test: tests the history command
#

import sys, atexit, signal, time, threading, os
from testutils import *

console = setup_tests()

# Ensure that the shell prints the expected prompt
expect_prompt()

# Test 1: Execute a few commands and check history
commands_to_test = ['ls', 'pwd', 'echo Hello', 'cd /tmp', 'cd']

for command in commands_to_test:
    sendline(command)
    expect_exact(f'{command}\r\n', "Shell output extraneous characters")
    expect_prompt("Shell did not print expected prompt after executing command")

# Test 2: Run the history command
sendline('history')

# Ensure that the shell prints the expected prompt
expect_prompt("Shell did not print expected prompt after issuing history command")

# Verify that the history command outputs the correct number of commands
for i, command in enumerate(commands_to_test, start=1):
    expect_exact(f'{i}  {command}\r\n', "History command output is incorrect")

# Test 3: Check if history only shows the executed commands and is correct
sendline('history')

# Expect the correct output format
for i, command in enumerate(commands_to_test, start=1):
    expect_exact(f'{i}  {command}\r\n', "History command output is incorrect for command " + command)

# Ensure that the shell prints the expected prompt
expect_prompt("Shell did not print expected prompt after checking history")

# Test 4: Check history with no commands executed
sendline('clear_history')  # Assume this command clears the history (implement if needed)
expect_prompt("Shell did not print expected prompt after clearing history")

sendline('history')
expect_exact('No history available\r\n', "Shell did not print expected message for empty history")

# Ensure that the shell prints the expected prompt
expect_prompt("Shell did not print expected prompt after checking empty history")

# Exit the shell
sendline("exit")
expect_exact("exit\r\n", "Shell output extraneous characters")

test_success()
