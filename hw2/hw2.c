#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

// Simplifed xv6 shell.

#define MAXARGS 10

// All commands have at least a type. Have looked at the type, the code
// typically casts the *cmd to some specific cmd type.
struct cmd {
  int type;          //  ' ' (exec), | (pipe), '<' or '>' for redirection
};
	
struct execcmd {
  int type;              // ' '
  char *argv[MAXARGS];   // arguments to the command to be exec-ed
};

struct redircmd {
  int type;          // < or > 
  struct cmd *cmd;   // the command to be run (e.g., an execcmd)
  char *file;        // the input/output file
  int flags;         // flags for open() indicating read or write
  int fd;            // the file descriptor number to use for the file
};

struct pipecmd {
  int type;          // |
  struct cmd *left;  // left side of pipe
  struct cmd *right; // right side of pipe
};

int fork1(void);  // Fork but exits on failure.
struct cmd *parsecmd(char*);

void handleError(const char* symbol) {
	fprintf(stderr, "%s: %s\n", symbol,  strerror(errno));
	_exit(-1);
};
// Execute cmd.  Never returns.
void
runcmd(struct cmd *cmd)
{
  int p[2], r;
  struct execcmd *ecmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    _exit(0);
  
  switch(cmd->type){
  default:
    fprintf(stderr, "unknown runcmd\n");
    _exit(-1);

  case ' ':
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0)
      _exit(0);
    // Your code here ...
    int code = execvp(ecmd->argv[0], &ecmd->argv[0]);
    if (code < 0) {
		handleError("execvp");
    }
    break;

  case '>':
  case '<':
    rcmd = (struct redircmd*)cmd;
    //fprintf(stderr, "redir not implemented\n");
    // Your code here ...
    int redirFd = open(rcmd->file, rcmd->flags, S_IRWXU | S_IRWXO);
    if (redirFd < 0) {
		handleError("open");
    }
    int oldFd = cmd->type == '>' ? 1 : 0;
    close(oldFd);
    if (dup2(redirFd, oldFd) < 0)
		handleError("dup");
    rcmd->cmd->type = ' ';
    runcmd(rcmd->cmd);
    break;

  case '|':
    pcmd = (struct pipecmd*)cmd;
    // fprintf(stderr, "pipe not implemented\n");
    // Your code here ...
	struct pipecmd* prcmd = (struct pipecmd*)pcmd->right;
	struct execcmd *lcmd = (struct execcmd*)prcmd->left;
	struct execcmd *rcmd = (struct execcmd*)prcmd->right;
    int pfd[2];
    if (pipe(pfd) < 0)
		handleError("pipe");
    switch(fork()) {
	case -1:
          handleError("fork");
	case 0:
		if (close(pfd[0]) < 0)	
			handleError("close");
		close(1);
		dup2(pfd[1], 1);	
		runcmd(pcmd->left);
		break;
	default:
		if (close(pfd[1]) < 0)
			handleError("close");
		// int my_pid = getpid();
		// char filename[512];
		// sprintf(filename, "filename_%d.txt",my_pid);
		// int logFd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
		// sprintf(filename, "argv:%s", ecmd->argv[0]);
		// write(logFd, filename, strlen(filename));
		// char buf[1024];
		// int num = read(pfd[0], buf, 1024);
		close(0);
		dup2(pfd[0], 0);
		int status;
		wait(&status);
		runcmd(pcmd->right);
		break;
	}
    break;
  }    
  _exit(0);
}

int
getcmd(char *buf, int nbuf)
{
  if (isatty(fileno(stdin)))
    fprintf(stdout, "6.828$ ");
  memset(buf, 0, nbuf);
  if(fgets(buf, nbuf, stdin) == 0)
    return -1; // EOF
  return 0;
}

int
main(void)
{
  static char buf[100];
  int fd, r;

  // Read and run input commands.

  
  while(getcmd(buf, sizeof(buf)) >= 0){
    if(buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' '){
      // Clumsy but will have to do for now.
      // Chdir has no effect on the parent if run in the child.
      buf[strlen(buf)-1] = 0;  // chop \n
      if(chdir(buf+3) < 0)
        fprintf(stderr, "cannot cd %s\n", buf+3);
      continue;
    }
    if(fork1() == 0)
      runcmd(parsecmd(buf));
    wait(&r);
  }
  exit(0);
}

int
fork1(void)
{
  int pid;
  
  pid = fork();
  if(pid == -1)
    perror("fork");
  return pid;
}

struct cmd*
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = ' ';
  return (struct cmd*)cmd;
}

struct cmd*
redircmd(struct cmd *subcmd, char *file, int type)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = type;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->flags = (type == '<') ?  O_RDONLY : O_WRONLY|O_CREAT|O_TRUNC;
  cmd->fd = (type == '<') ? 0 : 1;
  return (struct cmd*)cmd;
}

struct cmd*
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = '|';
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>";

// 找到第一个不是空格的字符，然后返回类型根据这个字符分为三种NULL，|，<>, a,最后将字符指针移到下一个非空白字符
int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;
  
  s = *ps;
  // 找到第一个不是空格的字符
  while(s < es && strchr(whitespace, *s))
    s++;
  if(q)
    *q = s;
  ret = *s;
  switch(*s){
  // case 0 说明是字符结尾null
  case 0:
    break;
  case '|':
  case '<':
    s++;
    break;
  case '>':
    s++;
    break;
  default:
    ret = 'a';
    // 当s指向的字符既不是空格，也不是"<|>"时，向后移动指针
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if(eq)
    *eq = s;
  // 如果是ls命令的话q返回的话指向l,eq指向s后的字符，即NULL
  //移除空格
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

// peek 将ps开头的空格全部移除，然后判断ps指向的字符是否在toks中出现过
int
peek(char **ps, char *es, char *toks)
{
  char *s;
 	 
  s = *ps;
  // strchr返回*s在" \t\r\n\v"是否出现过，即判断*s是否是个whitespace`
  // 所以下面这两句是将ps开头的空格全部移除
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char**, char*);
struct cmd *parsepipe(char**, char*);
struct cmd *parseexec(char**, char*);

// make a copy of the characters in the input buffer, starting from s through es.
// null-terminate the copy to make it a string.
char 
*mkcopy(char *s, char *es)
{
  int n = es - s;
  char *c = malloc(n+1);
  assert(c);
  strncpy(c, s, n);
  c[n] = 0;
  return c;
}

struct cmd*
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if(s != es){
    fprintf(stderr, "leftovers: %s\n", s);
    exit(-1);
  }
  return cmd;
}

struct cmd*
parseline(char **ps, char *es)
{
  struct cmd *cmd;
  cmd = parsepipe(ps, es);
  return cmd;
}

// 最复杂的一个函数，递归
struct cmd*
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;
  // 首先取出参数，返回的是个execmd
  cmd = parseexec(ps, es);
  // 判读当前字符是不是"|"
  // 进入if后，cmd成为pipecmd,而cmd->left是execcmd，cmd->right则是一个pipecmd，若cmd->right是个execcmd则说明到了管道尽头
  if(peek(ps, es, "|")){
    // 是"|"，则过掉
    gettoken(ps, es, 0, 0);
    //  调用pipecmd创建cmd，新的cmd左侧为管道左边参数，右边是管道右边参数
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd*
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while(peek(ps, es, "<>")){
    tok = gettoken(ps, es, 0, 0);
    if(gettoken(ps, es, &q, &eq) != 'a') {
      fprintf(stderr, "missing file for redirection\n");
      exit(-1);
    }
    switch(tok){
    case '<':
      cmd = redircmd(cmd, mkcopy(q, eq), '<');
      break;
    case '>':
      cmd = redircmd(cmd, mkcopy(q, eq), '>');
      break;
    }
  }
  return cmd;
}

struct cmd*
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;
  // 顶你个肺，函数和结构体居然重名了  
  ret = execcmd();
  cmd = (struct execcmd*)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  // 当peek指向的字符不是"|"
  while(!peek(ps, es, "|")){
    if((tok=gettoken(ps, es, &q, &eq)) == 0)
      break;
    if(tok != 'a') {
      fprintf(stderr, "syntax error\n");
      exit(-1);
    }
    cmd->argv[argc] = mkcopy(q, eq);
    argc++;
    if(argc >= MAXARGS) {
      fprintf(stderr, "too many args\n");
      exit(-1);
    }
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  return ret;
}
