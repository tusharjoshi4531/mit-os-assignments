#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>

#define PROMPT "$ "
#define BANNER "---------///SHELL///---------\n"

int parsePipe(const char *cmd, char ***pipedCmds, int *numCmd);
int parseRedirect(char **cmd, char **in_file, char **out_file);
int parseAndExecuteCommands(char *cmd);
int executeCommand(char **argv, char *in_file, char *out_file);
int parseAndExecuteCommand(char *cmd);
int executePipedCommands(char **pipedCmd, int numCmd);

int testPipe() {
  int fd[2];
  pipe(fd);

  pid_t c_id = fork();
  if(c_id == 0) {
    close(fd[0]);

    dup2(fd[1], STDOUT_FILENO);
    close(fd[1]);

    char *argv[] = {"cat", "test.txt", NULL};
    return execvp(argv[0], argv);
  } else {
    close(fd[1]);

    dup2(fd[0], STDIN_FILENO);
    close(fd[0]);

    char *argv[] = {"sort", NULL};

    wait(NULL);
    return execvp(argv[0], argv);
  }
}

int main() {
  printf(BANNER);

  while(1) {
    printf(PROMPT);
    char *line;
    ssize_t len = 0;

    len = getline(&line, &len, stdin);
    if(parseAndExecuteCommands(line)) {
      return -1;
    }
  }

  return 0;
}

char *trim(char *str) {
  char *end;

  // Trim leading space
  while(isspace((unsigned char)*str))
    str++;

  if(*str == 0) // All spaces?
    return str;

  // Trim trailing space
  end = str + strlen(str) - 1;
  while(end > str && isspace((unsigned char)*end))
    end--;

  // Write new null terminator
  *(end + 1) = '\0';

  return str;
}

int parsePipe(const char *cmd, char ***pipedCmds, int *numCmd) {
  char *cmdCpy = strdup(cmd);
  *numCmd = 0;
  for(char *token = strtok(cmdCpy, "|\n"); token;
      token = strtok(NULL, "|\n")) {
    (*numCmd)++;
  }
  free(cmdCpy);
  cmdCpy = strdup(cmd);
  *pipedCmds = malloc(sizeof(char *) * (*numCmd + 1));
  int idx = 0;

  for(char *token = strtok(cmdCpy, "|\n"); token;
      token = strtok(NULL, "|\n"), idx++) {
    (*pipedCmds)[idx] = strdup(trim(token));
  }

  free(cmdCpy);
  return 0;
}

int parseRedirect(char **cmd, char **in_file, char **out_file) {
  char *cmdInCpy = strdup(*cmd);
  char *cmdOutCpy = strdup(*cmd);

  char *tokenIn;
  if(tokenIn = strtok(cmdInCpy, "<")) {
    char *tok = strtok(NULL, " \n");
    if(tok)
      *in_file = strdup(tok);
  }

  char *tokenOut;
  if(tokenOut = strtok(cmdOutCpy, ">")) {
    char *tok = strtok(NULL, " \n");
    if(tok)
      *out_file = strdup(tok);
  }

  if(strlen(tokenIn) < strlen(tokenOut)) {
    strcpy(*cmd, tokenIn);
  } else {
    strcpy(*cmd, tokenOut);
  }
  free(cmdInCpy);
  free(cmdOutCpy);

  return 0;
}

int parseAndExecuteCommands(char *cmd) {
  char **pipedCommands = NULL;
  int numCmd;
  parsePipe(cmd, &pipedCommands, &numCmd);

  return executePipedCommands(pipedCommands, numCmd);
}

int executeCommand(char **argv, char *in_file, char *out_file) {
  if(in_file != NULL) {
    int fd = open(in_file, O_RDONLY);
    if(fd < 0) {
      perror("open");
      return -1;
    }

    if(dup2(fd, STDIN_FILENO) == -1) {
      perror("dup2");
      return -1;
    }

    close(fd);
  }

  if(out_file != NULL) {
    int fd = open(out_file, O_WRONLY | O_CREAT, 0644);
    if(fd < 0) {
      perror("open");
      return -1;
    }

    if(dup2(fd, STDOUT_FILENO) == -1) {
      perror("dup2");
      return -1;
    }

    close(fd);
  }

  if(execvp(argv[0], argv)) {
    perror("!execvp");
    return -1;
  }

  return 0;
}

int parseAndExecuteCommand(char *cmd) {
  char *in_file = NULL;
  char *out_file = NULL;
  parseRedirect(&cmd, &in_file, &out_file);

  char *cmdCpy = strdup(cmd);

  int argc = 0;
  for(char *token = strtok(cmdCpy, " \n"); token;
      token = strtok(NULL, " \n")) {
    argc++;
  }

  char **argv = malloc(sizeof(char *) * (argc + 1));

  free(cmdCpy);
  cmdCpy = strdup(cmd);
  int idx = 0;
  for(char *token = strtok(cmdCpy, " \n"); token;
      token = strtok(NULL, " \n"), idx++) {
    argv[idx] = strdup(token);
  }
  argv[argc] = NULL;

  free(cmdCpy);

  return executeCommand(argv, in_file, out_file);
}

int executePipedCommands(char **pipedCmd, int numCmd) {
  int prev_read_fd = -1;
  for(int i = 0; i < numCmd; i++) {
    int pipefd[2];
    if(i < numCmd - 1 && pipe(pipefd)) {
      perror("pipe");
      return -1;
    }

    int cid = fork();
    if(cid < 0) {
      perror("fork");
      return -1;
    } else if(cid == 0) {
      if(i > 0) {
        if(dup2(prev_read_fd, STDIN_FILENO) == -1) {
          perror("dup2");
          return -1;
        }
        close(prev_read_fd);
      }

      if(i < numCmd - 1) {
        if(dup2(pipefd[1], STDOUT_FILENO) == -1) {
          perror("dup2");
          return -1;
        }
        close(pipefd[1]);
        close(pipefd[0]);
      }

      return parseAndExecuteCommand(pipedCmd[i]);
    }

    if(i > 0)
      close(prev_read_fd);

    if(i < numCmd - 1) {
      close(pipefd[1]);
      prev_read_fd = pipefd[0];
    }
  }

  for(int idx = 0; idx < numCmd; idx++) {
    wait(NULL);
  }
  return 0;
}