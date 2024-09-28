/*
 * cush - the customizable shell.
 *
 * Developed by Godmar Back for CS 3214 Summer 2020 
 * Virginia Tech.  Augmented to use posix_spawn in Fall 2021.
 */
#define _GNU_SOURCE    1
#include <stdio.h>
#include <readline/readline.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <sys/wait.h>
#include <assert.h>
#include <spawn.h>
#include <fcntl.h>
/* Since the handed out code contains a number of unused functions. */
#pragma GCC diagnostic ignored "-Wunused-function"

#include "termstate_management.h"
#include "signal_support.h"
#include "shell-ast.h"
#include "utils.h"

static void handle_child_status(pid_t pid, int status);
static void handle_child_status(pid_t pid, int status);
void stop_command(int job_id);
void kill_command(int job_id);
void exit_command(void);
void bg_command(int job_id);
void fg_command(int job_id);
void jobs_command(void);
void poisix_spawn_handler(struct ast_command_line *cline);
bool handle_builtin(struct ast_command *cmd);

static void
usage(char *progname)
{
    printf("Usage: %s -h\n"
        " -h            print this help\n",
        progname);

    exit(EXIT_SUCCESS);
}

/* Build a prompt */
//BUILD THE FUCKING PROMT U CUNT
static char *
build_prompt(void)
{
    return strdup("cush> ");
}

enum job_status {
    FOREGROUND,     /* job is running in foreground.  Only one job can be
                       in the foreground state. */
    BACKGROUND,     /* job is running in background */
    STOPPED,        /* job is stopped via SIGSTOP */
    NEEDSTERMINAL,  /* job is stopped because it was a background job
                       and requires exclusive terminal access */
};

struct job {
    struct list_elem elem;   /* Link element for jobs list. */
    struct ast_pipeline *pipe;  /* The pipeline of commands this job represents */
    int     jid;             /* Job id. */
    enum job_status status;  /* Job status. */ 
    int  num_processes_alive;   /* The number of processes that we know to be alive */
    struct termios saved_tty_state;  /* The state of the terminal when this job was 
                                        stopped after having been in foreground */

    /* Add additional fields here if needed. */
    //ADD list pids tty gpid

    pid_t gpid;              /* Group process ID of the job */
    pid_t *pids;             /* Array of PIDs for processes in the job */
    int num_pids;            /* Number of processes in the job */
    bool tty_allocated;      /* Flag indicating whether the job has allocated a terminal */

};

/* Utility functions for job list management.
 * We use 2 data structures: 
 * (a) an array jid2job to quickly find a job based on its id
 * (b) a linked list to support iteration
 */
#define MAXJOBS (1<<16)
static struct list job_list;

static struct job * jid2job[MAXJOBS];

/* Return job corresponding to jid */
static struct job * 
get_job_from_jid(int jid)
{
    if (jid > 0 && jid < MAXJOBS && jid2job[jid] != NULL)
        return jid2job[jid];

    return NULL;
}

/* Add a new job to the job list */
static struct job *
add_job(struct ast_pipeline *pipe)
{
    struct job * job = malloc(sizeof *job);
    job->pipe = pipe;

    int num_commands = 0;
    struct list_elem *e;
    for (e = list_begin(&pipe->commands); e != list_end(&pipe->commands); e = list_next(e)) {
        num_commands++;
    }
    //make a list of pids
    job->num_processes_alive = num_commands;
    job->pids =malloc(num_commands * sizeof(pid_t));
    job->num_pids = num_commands;
    //gpid
    job->gpid = -1;
    //tty saved
    termstate_sample();
    //parse pipe?
    list_push_back(&job_list, &job->elem);
    for (int i = 1; i < MAXJOBS; i++) {
        if (jid2job[i] == NULL) {
            jid2job[i] = job;
            job->jid = i;
            return job;
        }
    }
    fprintf(stderr, "Maximum number of jobs exceeded\n");
    abort();
    return NULL;
}

/* Delete a job.
 * This should be called only when all processes that were
 * forked for this job are known to have terminated.
 */
static void
delete_job(struct job *job)
{
    int jid = job->jid;
    assert(jid != -1);
    jid2job[jid]->jid = -1;
    jid2job[jid] = NULL;
    ast_pipeline_free(job->pipe);
    free(job);
}

static const char *
get_status(enum job_status status)
{
    switch (status) {
    case FOREGROUND:
        return "Foreground";
    case BACKGROUND:
        return "Running";
    case STOPPED:
        return "Stopped";
    case NEEDSTERMINAL:
        return "Stopped (tty)";
    default:
        return "Unknown";
    }
}

/* Print the command line that belongs to one job. */
static void
print_cmdline(struct ast_pipeline *pipeline)
{
    struct list_elem * e = list_begin (&pipeline->commands); 
    for (; e != list_end (&pipeline->commands); e = list_next(e)) {
        struct ast_command *cmd = list_entry(e, struct ast_command, elem);
        if (e != list_begin(&pipeline->commands))
            printf("| ");
        char **p = cmd->argv;
        printf("%s", *p++);
        while (*p)
            printf(" %s", *p++);
    }
}

/* Print a job */
static void
print_job(struct job *job)
{
    printf("[%d]\t%s\t\t(", job->jid, get_status(job->status));
    print_cmdline(job->pipe);
    printf(")\n");
}

/*
 * Suggested SIGCHLD handler.
 *
 * Call waitpid() to learn about any child processes that
 * have exited or changed status (been stopped, needed the
 * terminal, etc.)
 * Just record the information by updating the job list
 * data structures.  Since the call may be spurious (e.g.
 * an already pending SIGCHLD is delivered even though
 * a foreground process was already reaped), ignore when
 * waitpid returns -1.
 * Use a loop with WNOHANG since only a single SIGCHLD 
 * signal may be delivered for multiple children that have 
 * exited. All of them need to be reaped.
 */
static void
sigchld_handler(int sig, siginfo_t *info, void *_ctxt)
{
    pid_t child;
    int status;

    assert(sig == SIGCHLD);

    while ((child = waitpid(-1, &status, WUNTRACED|WNOHANG)) > 0) {
        handle_child_status(child, status);
    }
}

/* Wait for all processes in this job to complete, or for
 * the job no longer to be in the foreground.
 * You should call this function from a) where you wait for
 * jobs started without the &; and b) where you implement the
 * 'fg' command.
 * 
 * Implement handle_child_status such that it records the 
 * information obtained from waitpid() for pid 'child.'
 *
 * If a process exited, it must find the job to which it
 * belongs and decrement num_processes_alive.
 *
 * However, note that it is not safe to call delete_job
 * in handle_child_status because wait_for_job assumes that
 * even jobs with no more num_processes_alive haven't been
 * deallocated.  You should postpone deleting completed
 * jobs from the job list until when your code will no
 * longer touch them.
 *
 * The code below relies on `job->status` having been set to FOREGROUND
 * and `job->num_processes_alive` having been set to the number of
 * processes successfully forked for this job.
 */
static void
wait_for_job(struct job *job)
{
    assert(signal_is_blocked(SIGCHLD));

    while (job->status == FOREGROUND && job->num_processes_alive > 0) {
        int status;

        pid_t child = waitpid(-1, &status, WUNTRACED);

        // When called here, any error returned by waitpid indicates a logic
        // bug in the shell.
        // In particular, ECHILD "No child process" means that there has
        // already been a successful waitpid() call that reaped the child, so
        // there's likely a bug in handle_child_status where it failed to update
        // the "job" status and/or num_processes_alive fields in the required
        // fashion.
        // Since SIGCHLD is blocked, there cannot be races where a child's exit
        // was handled via the SIGCHLD signal handler.
        if (child != -1)
            handle_child_status(child, status);
        else
            utils_fatal_error("waitpid failed, see code for explanation");
    }
}

/*
 * Step 1. Given the pid, determine which job this pid is a part of
 *         (how to do this is not part of the provided code.)
 * Step 2. Determine what status change occurred using the
 *         WIF*() macros.
 * (three options: exits, terminates with a signal, stopped)
 * Step 3. Update the job status accordingly, and adjust
 *         num_processes_alive if appropriate.
 *         If a process was stopped, save the terminal state.
 */
static void
handle_child_status(pid_t pid, int status) {
    assert(signal_is_blocked(SIGCHLD));

    struct job *job = NULL;
    struct list_elem *e;
    bool pid_found = false;

    for (e = list_begin(&job_list); e != list_end(&job_list); e = list_next(e)) {
        job = list_entry(e, struct job, elem);
    
        if (job->num_processes_alive > 0) {

            for (int i = 0; i < job->num_pids; i++) {
                //pid check if statement chekck pid throught list iterartion
                if (job->pids[i] == pid) {
                    pid_found = true;
                    goto job_found;
                }
            }

        }
    }

    if (!pid_found) {
        printf("No job found for process %d\n", pid);
        return;
    }

    job_found:
    // Step 2: Check the status of the child process
    if (WIFEXITED(status)) {
        job->num_processes_alive--;
        printf("Process %d exited with status %d\n", pid, WEXITSTATUS(status));

        if (job->num_processes_alive == 0) {
            printf("Job [%d] has completed\n", job->jid);
            delete_job(job);//delete outside handle child status
            termstate_sample();
        }
    }
    else if (WIFSIGNALED(status)) {
        printf("Process %d was terminated by signal %d\n", pid, WTERMSIG(status));
        job->num_processes_alive--;
        if (job->num_processes_alive == 0) {
            printf("Job [%d] has been terminated\n", job->jid);
            delete_job(job);
        }
    }
    else if (WIFSTOPPED(status)) {
        //check all three branches
        //check if the stsus wants access to the tereminal
        //use the needs terminal frok job struct
        job->status = STOPPED;
        printf("Process %d was stopped\n", pid);
        termstate_save(&job->saved_tty_state);
    }

}
//make method to call posix spawn
//take in pipeline 
//decide when to make job or not
//loop through commands in pipeline addup2 for redirection
//settgroup 0 to make iot process grp record gpid
//record pids so i can track
//terminal control handler
//asserted all loop all comands and ad jobs to joblist then wait for job
void poisix_spawn_handler(struct ast_command_line *cline) {
    pid_t pid;
    int status;

    // Iterate over pipelines in the command line
    for (struct list_elem *e = list_begin(&cline->pipes); 
         e != list_end(&cline->pipes); 
         e = list_next(e)) {
        struct ast_pipeline *pipeline = list_entry(e, struct ast_pipeline, elem);

        posix_spawnattr_t attr;
        posix_spawn_file_actions_t actions;

        // Initialize attributes and file actions
        posix_spawnattr_init(&attr);
        posix_spawn_file_actions_init(&actions);

        // Combine desired flags
        int flags = POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_USEVFORK; // Combine flags here

        // Set the process group and other desired flags
        posix_spawnattr_setpgroup(&attr, 0); // Create a new process group
        posix_spawnattr_setflags(&attr, flags); // Set combined flags


        // Iterate over commands in the current pipeline
        for (struct list_elem *cmd_elem = list_begin(&pipeline->commands); 
             cmd_elem != list_end(&pipeline->commands); 
             cmd_elem = list_next(cmd_elem)) {
            struct ast_command *cmd = list_entry(cmd_elem, struct ast_command, elem);
            char **argv = cmd->argv;

            // Setup file actions for redirection if applicable
            if (pipeline->iored_input) {
                posix_spawn_file_actions_adddup2(&actions, open(pipeline->iored_input, O_RDONLY), STDIN_FILENO);
            }
            if (pipeline->iored_output) {
                int flags = pipeline->append_to_output ? O_APPEND | O_WRONLY | O_CREAT : O_WRONLY | O_CREAT;
                posix_spawn_file_actions_adddup2(&actions, open(pipeline->iored_output, flags, 0644), STDOUT_FILENO);
            }

            // // For file redirection
            // if (pipeline->iored_input) {
            //     posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, pipeline->iored_input, O_RDONLY, 0);
            // }
            // if (pipeline->iored_output) {
            //     posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, pipeline->iored_output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            // }

            // Call posix_spawnp to create the child process
            if (posix_spawnp(&pid, argv[0], &actions, &attr, argv, environ) != 0) {
                perror("posix_spawnp failed");
                break;
            }

            // Add the job 
            add_job(pipeline);

            // Clean up actions for the next command
            posix_spawn_file_actions_destroy(&actions);
            posix_spawnattr_destroy(&attr);
        }

        // Wait for the last job in the pipeline to finish
        waitpid(pid, &status, 0);
    }
}

bool handle_builtin(struct ast_command *cmd) {
    if (strcmp(cmd->argv[0], "jobs") == 0) {
        jobs_command();
        return true;
    }
    if (strcmp(cmd->argv[0], "fg") == 0) {
        if (cmd->argv[1]) {
            int job_id = atoi(cmd->argv[1]);
            fg_command(job_id);
        } else {
            printf("fg: job id required\n");
        }
        return true;
    }
    if (strcmp(cmd->argv[0], "bg") == 0) {
        if (cmd->argv[1]) {
            int job_id = atoi(cmd->argv[1]);
            bg_command(job_id);
        } else {
            printf("bg: job id required\n");
        }
        return true;
    }
    if (strcmp(cmd->argv[0], "kill") == 0) {
        if (cmd->argv[1]) {
            int job_id = atoi(cmd->argv[1]);
            kill_command(job_id);
        } else {
            printf("kill: job id required\n");
        }
        return true;
    }
    if (strcmp(cmd->argv[0], "stop") == 0) {
        if (cmd->argv[1]) {
            int job_id = atoi(cmd->argv[1]);
            stop_command(job_id);
        } else {
            printf("stop: job id required\n");
        }
        return true;
    }
    if (strcmp(cmd->argv[0], "exit") == 0) {
        exit_command();
        return true;
    }
    return false;
}

int main(int ac, char *av[]) {
    int opt;

    /* Process command-line arguments. See getopt(3) */
    while ((opt = getopt(ac, av, "h")) > 0) {
        switch (opt) {
        case 'h':
            usage(av[0]);
            break;
        }
    }

    list_init(&job_list);
    signal_set_handler(SIGCHLD, sigchld_handler);
    termstate_init();

    /* Read/eval loop. */
    //before proccesing the command line check for jobs with no active proccess and delete them
    for (;;) {

        /* If you fail this assertion, you were about to enter readline()
         * while SIGCHLD is blocked.  This means that your shell would be
         * unable to receive SIGCHLD signals, and thus would be unable to
         * wait for background jobs that may finish while the
         * shell is sitting at the prompt waiting for user input.
         */
        assert(!signal_is_blocked(SIGCHLD));

        /* If you fail this assertion, you were about to call readline()
         * without having terminal ownership.
         * This would lead to the suspension of your shell with SIGTTOU.
         * Make sure that you call termstate_give_terminal_back_to_shell()
         * before returning here on all paths.
         */
        assert(termstate_get_current_terminal_owner() == getpgrp());

        /* Do not output a prompt unless shell's stdin is a terminal */
        char * prompt = isatty(0) ? build_prompt() : NULL;
        char * cmdline = readline(prompt);
        free (prompt);

        if (cmdline == NULL)  /* User typed EOF */
            break;

        struct ast_command_line * cline = ast_parse_command_line(cmdline);
        free (cmdline);
        if (cline == NULL)                  /* Error in command line */
            continue;

        if (list_empty(&cline->pipes))
        { /* User hit enter */
            ast_command_line_free(cline);
            continue;
        }

        //handle background jobs signal block for loop use posiwx spawn
        for (struct list_elem *e = list_begin(&cline->pipes); e != list_end(&cline->pipes); e = list_next(e)) {
            struct ast_pipeline *pipeline = list_entry(e, struct ast_pipeline, elem);
            // Block SIGCHLD while setting up the job
            signal_block(SIGCHLD);

            // Add job to job list
            struct job *new_job = add_job(pipeline);
            //for loop iterate through list
            pid_t first_pid = -1;  // To track the group process ID (gpid)
            for (struct list_elem *cmd_elem = list_begin(&pipeline->commands);cmd_elem != list_end(&pipeline->commands);cmd_elem = list_next(cmd_elem)) {
                struct ast_command *cmd = list_entry(cmd_elem, struct ast_command, elem);
                pid_t pid;

                // Use posix_spawn to execute the command
                if (posix_spawnp(&pid, cmd->argv[0], NULL, NULL, cmd->argv, environ) != 0) {
                    perror("posix_spawnp failed");
                    continue;  // Skip to the next command on failure
                }

                // If it's the first command, set the job's group PID (gpid)
                if (first_pid == -1) {
                    first_pid = pid;
                    new_job->gpid = pid;
                    setpgid(pid, pid);  // Set the first process as the group leader
                } else {
                    setpgid(pid, first_pid);  // Set subsequent commands to the same process group
                }

                // Track the new process in the job's list of PIDs
                new_job->pids[new_job->num_processes_alive++] = pid;
            }

        if (!pipeline->bg_job) {
            //if status foreground wait for job
            new_job->status = FOREGROUND;
            tcsetpgrp(STDIN_FILENO, new_job->gpid);  // Give terminal control to the job
            wait_for_job(new_job);  // Wait for the job to complete
            termstate_give_terminal_back_to_shell();  // Return terminal control to the shell
        } else {
            //if background curr job jid curr job gpid
            new_job->status = BACKGROUND;
            printf("[%d] %d\n", new_job->jid, new_job->gpid);  // Print background job info
        }

        // Unblock signals once done
        signal_unblock(SIGCHLD);
    }

    //delete completed jobs iterate through list check if no processes print done otherwise remove from list
    struct list_elem *e = list_begin(&job_list);
    while (e != list_end(&job_list)) {
        struct job *job = list_entry(e, struct job, elem);

        if (job->num_processes_alive == 0) {  // Job is done
            printf("Job [%d] done\n", job->jid);
            e = list_remove(e);  // Remove job from the list
            delete_job(job);     // Free job memory
        } else {
            e = list_next(e);  // Move to the next job
        }
    }

    //unblock and give back the terminal
    signal_unblock(SIGCHLD);
        /* Free the command line.
         * This will free the ast_pipeline objects still contained
         * in the ast_command_line.  Once you implement a job list
         * that may take ownership of ast_pipeline objects that are
         * associated with jobs you will need to reconsider how you
         * manage the lifetime of the associated ast_pipelines.
         * Otherwise, freeing here will cause use-after-free errors.
         */
        //ast_command_line_free(cline); // comment this out later
    }
    return 0;
}

void jobs_command() {
    struct list_elem *e;
    for (e = list_begin(&job_list); e != list_end(&job_list); e = list_next(e)) {
        struct job *job = list_entry(e, struct job, elem);
        print_job(job);  
    }
}

void fg_command(int job_id) {
    struct job *job = get_job_from_jid(job_id);
    
    if (job) {
        job->status = FOREGROUND; 
        tcsetpgrp(STDIN_FILENO, job->jid);        
        kill(-job->jid, SIGCONT);
        wait_for_job(job);
        termstate_give_terminal_back_to_shell();
    } else {
        printf("No such job\n");
    }
}

void bg_command(int job_id) {
    struct job *job = get_job_from_jid(job_id);
    
    if (job && job->status == STOPPED) {
        job->status = BACKGROUND;
        printf("Resuming job [%d] in background\n", job->jid);
        kill(-job->jid, SIGCONT);
    } else {
        printf("No such stopped job\n");
    }
}

void kill_command(int job_id) {
    struct job *job = get_job_from_jid(job_id);
    
    if (job) {
        printf("Killing job [%d]\n", job->jid);
        kill(-job->jid, SIGKILL);
    } else {
        printf("No such job\n");
    }
}

void stop_command(int job_id) {
    struct job *job = get_job_from_jid(job_id);
    
    if (job) {
        printf("Stopping job [%d]\n", job->jid);
        kill(-job->jid, SIGTSTP);  
        job->status = STOPPED;
    } 
    else {
        printf("No such job\n");
    }
}

void exit_command() {
    struct list_elem *e = list_begin(&job_list);
    
    while (e != list_end(&job_list)) {
        struct job *job = list_entry(e, struct job, elem);
        
        printf("Killing job [%d] before exiting\n", job->jid);
        kill(-job->jid, SIGKILL);
        e = list_remove(e);
        delete_job(job);
    }
    
    printf("Exiting shell...\n");
    exit(0);
}