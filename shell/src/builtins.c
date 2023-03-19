#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <dirent.h>

#include "builtins.h"
#include "config.h"

int lexit(char *[]);
int echo(char*[]);
int cd(char *[]);
int lkill(char *[]);
int lls(char *[]);
int undefined(char *[]);

builtin_pair builtins_table[]={
	{"exit",	&lexit},
	{"lecho",	&echo},
	{"lcd",		&cd},
	{"cd",		&cd},
	{"lkill",	&lkill},
	{"lls",		&lls},
	{NULL,NULL}
};

int convertstrtol(char * arg, int * val){
	int status=0;
	char* a;
	if(val==NULL){
		return -1;
	}
	errno = 0;
	*val = strtol(arg, &a, 0);
	if( arg == a || *a != '\0' || errno == ERANGE){ // string invalid
		return 1;
	}
	return 0; // string valid
}


void printbuiltinerror(char * argv[]){
	fprintf(stderr, "Builtin %s error.\n", argv[0]);
}

int lexit(char * argv[]){
	exit(0);
	return 0;
}

int 
echo( char * argv[])
{
	int i =1;
	if (argv[i]) {
		printf("%s", argv[i++]);
	}

	while  (argv[i]){
		printf(" %s", argv[i++]);
	}
	
	printf("\n");
	fflush(stdout);
	return 0;
}


int cd(char * argv[]){
	if(!argv[1]){
		if(chdir(getenv("HOME"))){
			printbuiltinerror(argv);
			return -1;
		}
	}
	else if(!argv[2]){
		if(chdir(argv[1])){
			printbuiltinerror(argv);
			return -1;
		}
	}
	else{
		printbuiltinerror(argv);
		return -1;
	}
	return 0;
}


int lkill(char * argv[]){
	if(!argv[1]){
		printbuiltinerror(argv);
		return -1;
	}
	if(!argv[2]){
		int pid;
		if(convertstrtol(argv[1], &pid) || kill(pid, SIGTERM)){
			printbuiltinerror(argv);
			return 1;
		}
	}
	else{
		int pid, signal;
		if(convertstrtol(argv[2], &pid) || convertstrtol(argv[1], &signal) || kill(pid, -signal)){
			printbuiltinerror(argv);
			return 1;
		}
	}
	return 0;
}


int lls(char * argv[]){
	if(argv[1]){
		printbuiltinerror(argv);
		return 1;
	} else {
		char cwd[PATH_MAX];
		getcwd(cwd,PATH_MAX);

		DIR *dir;
    	struct dirent *file;
		dir = opendir(cwd);
		while((file = readdir(dir)) != NULL){
			if(file->d_name[0] != '.'){
				printf("%s%c",file->d_name,'\n');
			}
		}
		fflush(stdout);
		if(closedir(dir)){
			printbuiltinerror(argv);
			return 1;
		}
	}
	return 0;
}


int 
undefined(char * argv[])
{
	fprintf(stderr, "Command %s undefined.\n", argv[0]);
	fflush(stderr);
	return BUILTIN_ERROR;
}
