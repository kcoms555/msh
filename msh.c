#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#define FALSE 0
#define TRUE 1

#define EOL	1
#define ARG	2
#define AMPERSAND 3
#define REDIR_OUT 4
#define REDIR_IN 5
#define PIPE 6

#define FLAG_REDIR_OUT 1
#define FLAG_REDIR_IN 2
#define FLAG_PIPE 4

#define FOREGROUND 0
#define BACKGROUND 1

#define INPUT_SIZE 512
#define TOKENS_SIZE 1024

#define MAX_COMMAND 5
#define MAX_ARG 16

#define PROCESS_QUIT 0
#define PROGRAM_QUIT 255

static char	input[512];
static char	tokens[1024];
char		*ptr, *tok;
unsigned int flag;
unsigned int command_count=0;

void child_handler(int sig){
	int status;
	pid_t pid;
	while((pid = waitpid(-1, &status, WNOHANG))>0) 
		fprintf(stdout, "[%d] Exit Status : %d", pid, WEXITSTATUS(status));
	fflush(stdout);
}
void type(char * path){
	int i, fid, readcount;
	char buf[512];
	fid = open(path, O_RDONLY);
	if(fid >=0){
		do{ readcount = read(fid, buf, 512);
			buf[readcount]='\0';
			fprintf(stdout, "%s", buf);
		} while(readcount>0);
		close(fid);
	}
}
int get_token(char **outptr) {
	int	token_type;
	*outptr = tok;

	while ((*ptr == ' ') || (*ptr == '\t')) ptr++; 
	*tok++ = *ptr;

	switch (*ptr++) {
		case '\0' : token_type = EOL; break;
		case '&': token_type = AMPERSAND; break;
		case '>': token_type = REDIR_OUT; break;
		case '<': token_type = REDIR_IN; break;
		case '|': token_type = PIPE; break;
		default : token_type = ARG;
			while ((*ptr != '|') && (*ptr != '<') && (*ptr != '>') && (*ptr != ' ') && (*ptr != '&') && (*ptr != '\t') && (*ptr != '\0')) *tok++ = *ptr++;
	}
	*tok++ = '\0';
	return(token_type);
}
void run(const char * file, char * argv[]){
	for(int i=1; argv[i]; i++){
		char c = argv[i][0];
		if(c == '<' || c == '>'){
			int fd;
			if(c == '<') fd = open(argv[i+1], O_RDONLY);
			if(c == '>') fd = open(argv[i+1], O_RDWR | O_CREAT | S_IROTH, 0644);
			if(fd<0){
				fprintf(stderr, "redirection file open error(%s)\n", argv[i+1]);
				exit(PROCESS_QUIT);
			}
			if(c == '<') dup2(fd, stdin->_fileno);
			if(c == '>') dup2(fd, stdout->_fileno);
			close(fd);
			int j=i;
			while(argv[j+2]){argv[j]=argv[j+2];j++;}
			argv[j] = NULL;
			i=0;
			continue;
		}
	}
	if(!strcmp(file, "quit")) exit(PROGRAM_QUIT);
	if(!strcmp(file, "exit")) exit(PROGRAM_QUIT);
	if(!strcmp(file, "type")){
		if(argv[1]) type(argv[1]);
		else fprintf(stderr, "need 2nd argument for type\n");
		exit(PROCESS_QUIT);
	}
	execvp(file, argv);
	fprintf(stderr, "minish : command not found (%s)\n", file);
	exit(PROCESS_QUIT);
}
int execute(char *comm[][MAX_ARG], int last_arg[], int how) {
	if(!strcmp(comm[0][0], "cd")){ //cd는 fork를 안하고 부모 프로세스에서 진행한다
		if(comm[0][1]) chdir(comm[0][1]);
		else chdir(".");
		return PROCESS_QUIT;
	}
	int	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "minish : fork error\n");
		return(PROGRAM_QUIT);
	}
	else if (pid == 0) {
		int i;
		for(i=0; i+1<command_count; i++){ //pipe등으로 붙일 필요가 있을때
			if(comm[i][last_arg[i]][0] == '|'){
				comm[i][last_arg[i]--] = NULL;
				int p[2]; //0:읽기 스트림(파이프 출구) 1:출력 스트림(파이프 입구)
				pipe(p);
				if(fork()){
					dup2(p[0], stdin->_fileno);
					close(p[0]);
					close(p[1]);
					continue;
				}
				else{
					dup2(p[1], stdout->_fileno);
					close(p[0]);
					close(p[1]);
					run(comm[i][0], &comm[i][0]);
				}
			}
		}
		run(comm[i][0], &comm[i][0]);
		exit(-1);
	}
	int status=0;
	if (how == BACKGROUND) printf("[%d]\n", pid);
	else waitpid(pid, &status, 0);
	return WEXITSTATUS(status);
}

int parse_and_execute(char *input) {
	char * arg[MAX_COMMAND][MAX_ARG];
	int last_arg[MAX_COMMAND];
	memset(arg, 0x00, sizeof(char *)*MAX_COMMAND*MAX_ARG);
	memset(last_arg, 0xFF, sizeof(int)*MAX_COMMAND);

	int	token_type, how=FOREGROUND;
	int	quit = FALSE;
	int iarg = 0, icommand=0;
	int	finished = FALSE;

	flag = 0;
	ptr = input;
	tok = tokens;

	while (!finished) {
		switch (token_type = get_token(&arg[icommand][iarg])) {
		case REDIR_IN :
			if(flag){
				fprintf(stderr, "minish : syntax error near token '<'\n");
				finished=TRUE;
				break;
			}
			flag |= FLAG_REDIR_IN;
			iarg++;
			break;
		case REDIR_OUT :
			if(flag){
				fprintf(stderr, "minish : syntax error near token '>'\n");
				finished=TRUE;
				break;
			}
			flag |= FLAG_REDIR_OUT;
			iarg++;
			break;
		case PIPE :
			if(flag){
				fprintf(stderr, "minish : syntax error near token '|'!\n");
				finished=TRUE;
				break;
			}
			flag |= FLAG_PIPE;
			last_arg[icommand] = iarg; //다른 연결 실행을 위해 남겨둠
			icommand++;
			iarg = 0;
			break;
		case ARG :
			if(flag & FLAG_REDIR_IN) flag &= ~FLAG_REDIR_IN;
			if(flag & FLAG_REDIR_OUT) flag &= ~FLAG_REDIR_OUT;
			if(flag & FLAG_PIPE) flag &= ~FLAG_PIPE;
			iarg++;
			break;
		case AMPERSAND: how = BACKGROUND; break;
		case EOL : 
			finished = TRUE;
			arg[icommand][iarg] = NULL;
			last_arg[icommand] = iarg-1;
			command_count = icommand+1;
			if(flag){
				if(flag & FLAG_REDIR_IN) fprintf(stderr, "minish : syntax error near unexpected token '<'\n");
				if(flag & FLAG_REDIR_OUT) fprintf(stderr, "minish : syntax error near unexpected token '>'\n");
				if(flag & FLAG_PIPE) fprintf(stderr, "minish : syntax error near unexpected token '|'\n");
				break;
			}
			if(arg[0][0]) quit = execute(arg, last_arg, how);
			break; 
		}
	}
	return quit;
}

int main() {
	int num;
	char * line_p;
	char working_dir[1024];
	signal(SIGCHLD, child_handler);
	do{ getcwd(working_dir, 1024);
		printf("msh:%s # ", working_dir);
		fgets(input, INPUT_SIZE, stdin);
		if((line_p = strchr(input, '\n')) != NULL) *line_p = '\0';
		if(parse_and_execute(input)==PROGRAM_QUIT) break;
	} while(1);
	return 0;
}
