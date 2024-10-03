Student Information
-------------------
Ishita Gupta
Jayant Dulani
How to execute the shell
------------------------
<describe how to execute from the command line>
run ./cush on the terminal and run the various commands on the command prompt and they should execute properly. to run the tests: 
custom tests: /home/ugrads/majors/ishitagupta/CS3214/project-1/tests/stdriver.py -o output_spec.py -v custom_tests.tst
basic and advanced test:  /home/ugrads/majors/ishitagupta/CS3214/project-1/tests/stdriver.py -o output_spec.py -b -a

Important Notes
---------------
The basic functionality that we implemented is the cd command and the advanced one was history!

Description of Base Functionality
---------------------------------
jobs: 
- The jobs command is implemented in the jobs_command() function in cush.c.
- It iterates over the job_list, which contains all the current jobs.
- For each job, it calls print_job(), which displays the job's ID, status, and command line.

fg: 
- The fg command brings a background or stopped job to the foreground.
- Implemented in the fg_command(int job_id) function.
- Retrieves the job using get_job_from_jid(job_id).
- Gives terminal control to the job's process group using termstate_give_terminal_to().
- Changes the job's status to FOREGROUND and sends a SIGCONT signal to resume it.
- Calls wait_for_job(job) to wait until the job finishes or stops.

bg: 
- The bg command resumes a stopped job in the background.
- Implemented in the bg_command(int job_id) function.
- Changes the job's status to BACKGROUND.
- Sends a SIGCONT signal to the job's process group to resume it.

kill: 
- The kill command terminates a job by sending it a SIGKILL signal.
- Implemented in the kill_command(int job_id) function.
- Retrieves the job and sends SIGKILL to its process group using kill(-job->gpid, SIGKILL).

stop: 
- The stop command stops a running job by sending it a SIGTSTP signal.
- Implemented in the stop_command(int job_id) function.
- Changes the job's status to STOPPED.
- Sends SIGTSTP to the job's process group.

^C:
- The shell ignores SIGINT signals using signal_set_handler(SIGINT, SIG_IGN).
- Foreground jobs receive SIGINT when the user presses Ctrl+C.
- Ensures the shell doesn't terminate when Ctrl+C is pressed.

^Z
- The shell handles SIGTSTP signals to stop the foreground job.
- Foreground jobs receive SIGTSTP when the user presses Ctrl+Z.
- The shell updates the job's status to STOPPED and saves the terminal state.

Description of Extended Functionality
-------------------------------------
<describe your IMPLEMENTATION of the following functionality:
I/O, Pipes, Exclusive Access >
I/O Redirection:
- Handled using posix_spawn_file_actions_addopen() and posix_spawn_file_actions_adddup2().
- For input redirection (<), the shell opens the specified file and duplicates it to STDIN_FILENO.
- For output redirection (> and >>), the shell opens or appends to the specified file and duplicates it to STDOUT_FILENO.

Pipes:
- Implemented by setting up pipes between commands in a pipeline.
- Uses pipe() system call to create pipes.
- Uses posix_spawn_file_actions_adddup2() to duplicate the read and write ends to STDIN_FILENO and STDOUT_FILENO respectively.
- Manages multiple commands connected via pipes.

Exclusive Access:
- Ensures only the foreground job has control of the terminal.
- Uses tcsetpgrp() to set the foreground process group ID.
- Saves and restores terminal state using termstate_save() and termstate_give_terminal_back_to_shell().
- Handles stopped jobs by saving terminal state and restoring it when the job resumes.

List of Additional Builtins Implemented
---------------------------------------
cd: 
- Changes the current working directory of the shell.
- Implemented in the cd_command(struct ast_command *cmd) function.
- Supports:
    - cd without arguments: Changes to the user's home directory.
    - cd [directory]: Changes to the specified directory.

history: 
- Displays the list of commands entered by the user during the current session.
- Implemented in the history_command(struct ast_command *cmd) function.
- Retrieves command history using history_get_history_state() and history_list() from the readline library.

exit: 
- Exits the shell.
- Implemented in the exit_command() function.
- Before exiting, it terminates all running or stopped jobs by sending SIGKILL.