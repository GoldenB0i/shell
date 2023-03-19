#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "config.h"
#include "siparse.h"
#include "utils.h"
#include "builtins.h"

volatile int fg_count = 0;
int fg_pids[MAX_LINE_LENGTH / 2];

int bg_index = 0;
int bg_pids[MAX_LINE_LENGTH];
bool bg_exit[MAX_LINE_LENGTH];
int bg_status[MAX_LINE_LENGTH];

void comtovargs(command *x, char **vargs)
{
    argseq *stop = x->args;
    if (stop == NULL)
    {
        vargs[0] = NULL;
        return;
    }
    argseq *i = stop->next;
    vargs[0] = stop->arg;
    int j = 1;
    while (stop != i && j < MAX_LINE_LENGTH / 2)
    {
        vargs[j] = i->arg;
        i = i->next;
        j++;
    }
    vargs[j] = NULL;
}

void comtoredirs(command *x, redir **vargs)
{
    redirseq *stop = x->redirs;
    if (stop == NULL)
    {
        vargs[0] = NULL;
        return;
    }
    redirseq *i = stop->next;
    vargs[0] = stop->r;
    int j = 1;
    while (stop != i && j < MAX_LINE_LENGTH / 2)
    {
        vargs[j] = i->r;
        i = i->next;
        j++;
    }
    vargs[j] = NULL;
}

bool is_fg(int pid)
{
    int i = 0;
    while (fg_pids[i] != -1)
    {
        if (fg_pids[i++] == pid)
            return true;
    }
    return false;
}

void child_handler(int signal)
{
    while (true)
    {
        int status;
        int pid = waitpid(-1, &status, WNOHANG);
        if (pid < 1)
        {
            break;
        }
        if (is_fg(pid))
        {
            fg_count--;
        }
        else
        {
            if (WIFEXITED(status))
            {
                bg_exit[bg_index] = true;
                bg_status[bg_index] = WEXITSTATUS(status);
                bg_pids[bg_index] = pid;
                bg_index++;
            }
            else if (WIFSIGNALED(status))
            {

                bg_exit[bg_index] = false;
                bg_status[bg_index] = status; // is this correct macro????
                bg_pids[bg_index] = pid;
                bg_index++;
            }
            bg_index = bg_index % MAX_LINE_LENGTH;
        }
    }
}

void printexecerror(char **vargs)
{
    if (errno == ENOENT)
    {
        fprintf(stderr, "%s%s\n", vargs[0], FILE_NOT_FOUND_ERROR);
    }
    else if (errno == EACCES)
    {
        fprintf(stderr, "%s%s\n", vargs[0], PERMISSION_ERROR);
    }
    else
    {
        fprintf(stderr, "%s%s\n", vargs[0], ELSE_ERROR);
    }
}

int main(int argc, char *argv[])
{

    sigset_t emask;
    sigemptyset(&emask);
    sigset_t sigchild_mask;
    sigemptyset(&sigchild_mask);
    sigaddset(&sigchild_mask, SIGCHLD);

    struct sigaction sigint_action;
    sigint_action.sa_handler = SIG_IGN;
    sigint_action.sa_flags = 0;
    sigemptyset(&sigint_action.sa_mask);
    struct sigaction def;
    sigaction(SIGINT, &sigint_action, &def);

    struct sigaction sigchld_action;
    sigchld_action.sa_handler = child_handler;
    sigchld_action.sa_flags = 0;
    sigemptyset(&sigchld_action.sa_mask);
    sigaction(SIGCHLD, &sigchld_action, NULL);

    fg_pids[0] = -1;

    struct stat stdinput;
    if (fstat(0, &stdinput) != 0)
    {
        exit(FSTAT_FAILURE);
    }

    pipelineseq *ln;
    command *com;
    int r_val, child, count = 0;

    bool should_print = S_ISCHR(stdinput.st_mode), is_syntax_error = false;
    char buf[MAX_LINE_LENGTH + 1];
    char *beginning, *ending;
    beginning = buf;
    buf[MAX_LINE_LENGTH] = '\0';

    sigprocmask(SIG_BLOCK, &sigchild_mask, NULL);
    while (1)
    {

        if (should_print)
        {
            if (bg_index > 0)
            {
                for (int i = 0; i < bg_index; i++)
                {
                    int tmp = bg_pids[i];
                    printf("Background process %d terminated. ", tmp);
                    if (bg_exit[i])
                    {
                        printf("(exited with status %d)", bg_status[i]);
                    }
                    else
                    {
                        printf("(killed by signal %d)", bg_status[i]);
                    }
                    printf("\n");
                }
                bg_index = 0;
            }
            printf(PROMPT_STR);
            fflush(stdout);
        }
        sigprocmask(SIG_UNBLOCK, &sigchild_mask, NULL);
        do
        {
            r_val = read(0, beginning, MAX_LINE_LENGTH - count);
        } while (r_val == -1);
        sigprocmask(SIG_BLOCK, &sigchild_mask, NULL);
        if (r_val == 0)
        {
            break;
        }
        count = r_val + (beginning - buf);

        ending = strchr(buf, '\n');
        buf[count] = '\0';
        if (ending == NULL)
        {
            beginning = buf + count;
            if (count >= MAX_LINE_LENGTH)
            {
                is_syntax_error = true;
                count = 0;
                beginning = buf;
            }
            continue;
        }

        beginning = buf;
        do
        {
            ending = strchr(beginning, '\n');
            if (ending == NULL)
            {
                break;
            }

            *ending = '\0';
            if (is_syntax_error)
            {
                is_syntax_error = false;
                beginning = ending + 1;
                fprintf(stderr, "%s\n", SYNTAX_ERROR_STR);
                continue;
            }

            ln = parseline(beginning);

            if (!ln)
            {

                fprintf(stderr, "%s\n", SYNTAX_ERROR_STR);

                beginning = ending + 1;
                ending = strchr(beginning, '\n');

                continue;
            }
            pipelineseq *ln_iterator = ln;
            do
            {

                pipeline *line = ln_iterator->pipeline;
                commandseq *com_seq = line->commands;

                if (!com_seq->com)
                {
                    ln_iterator->next;
                    continue;
                }
                commandseq *com_seq_it = com_seq;
                int prev[2], next[2];
                next[0] = -1;
                prev[0] = -1;
                int fg_index = 0;

                do
                {
                    command *cm = com_seq_it->com;
                    prev[0] = next[0];
                    prev[1] = next[1];

                    if (com_seq_it->next != com_seq)
                    { // not the last one
                        if (pipe(next) < 0)
                            exit(1);
                    }

                    char *vargs[MAX_LINE_LENGTH / 2];
                    redir *redirs[MAX_LINE_LENGTH / 2];
                    comtovargs(cm, vargs);
                    comtoredirs(cm, redirs);

                    int shellcommand = -1;
                    for (int i = 0; builtins_table[i].name; i++)
                    {
                        if (strcmp(builtins_table[i].name, vargs[0]) == 0)
                        {
                            shellcommand = i;
                        }
                    }
                    if (shellcommand == -1)
                    {

                        child = fork();
                        if (child == -1)
                        {
                            fprintf(stderr, "new process fail\n");
                        }
                        if (child == 0)
                        {
                            sigaction(SIGINT, &def, NULL);
                            sigprocmask(SIG_UNBLOCK, &sigchild_mask, NULL);

                            if (line->flags == INBACKGROUND)
                            {
                                setsid();
                            }

                            if (com_seq_it != com_seq)
                            {
                                if (close(0) < 0 || dup2(prev[0], 0) < 0 || close(prev[0]) < 0 || close(prev[1]) < 0)
                                {
                                    exit(1);
                                }
                            }
                            if (com_seq_it->next != com_seq)
                            {
                                if (close(1) < 0 || dup2(next[1], 1) < 0 || close(next[0]) < 0 || close(next[1]) < 0)
                                {
                                    exit(2);
                                }
                            }
                            int redirs_iterator = 0;

                            while (redirs[redirs_iterator])
                            {
                                int flags, curr_flag = redirs[redirs_iterator]->flags;
                                if (IS_RIN(curr_flag))
                                {
                                    if (close(0) < 0)
                                        exit(1);
                                    flags = O_RDONLY;
                                }
                                else if (IS_RAPPEND(curr_flag))
                                {
                                    if (close(1) < 0)
                                        exit(1);
                                    flags = O_WRONLY | O_APPEND | O_CREAT;
                                }
                                else if (IS_ROUT(curr_flag))
                                {
                                    if (close(1) < 0)
                                        exit(1);
                                    flags = O_WRONLY | O_TRUNC | O_CREAT;
                                }
                                else
                                    exit(3);

                                if (open(redirs[redirs_iterator]->filename, flags, 0644) < 0)
                                {
                                    if (errno == ENOENT)
                                    {
                                        fprintf(stderr, "%s: no such file or directory\n", redirs[redirs_iterator]->filename);
                                    }
                                    else
                                    {
                                        fprintf(stderr, "%s: permission denied\n", redirs[redirs_iterator]->filename);
                                    }
                                    exit(1);
                                }
                                redirs_iterator++;
                            }
                            if (execvp(vargs[0], vargs))
                            {
                                printexecerror(vargs);
                                exit(EXEC_FAILURE);
                            }
                        }
                        else
                        {
                            if (line->flags != INBACKGROUND)
                            {
                                fg_pids[fg_index++] = child;
                                fg_pids[fg_index] = -1;
                                fg_count++;
                            }
                            if (prev[0] >= 0)
                            {
                                if (close(prev[0]) < 0 || close(prev[1]) < 0)
                                {
                                    exit(5);
                                }
                            }
                        }
                    }
                    else
                    {
                        (builtins_table[shellcommand].fun(vargs));
                    }

                    com_seq_it = com_seq_it->next;
                } while (com_seq_it != com_seq);

                while (fg_count > 0)
                {
                    sigsuspend(&emask);
                }

                ln_iterator = ln_iterator->next;
            } while (ln != ln_iterator);

            beginning = ending + 1;

        } while ((beginning - buf) < count);

        if (beginning - buf < count)
        {
            memmove(buf, beginning, (buf + count) - beginning);
            count = (buf + count) - beginning;
            beginning = buf + count;
        }
        else
        {
            beginning = buf;
            count = 0;
        }
    }
    exit(EXIT_SUCCESS);
}
