/**
 * @file worker.c
 * @author Wolfe Weeks
 * @date 2023-04-04
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <time.h>

#include "shared_memory.h"

#define PERMS 0644

struct MessageBuffer {
  long mtype;
  // int durationSec;
  int durationNano;
};

int* block; //shared memory block

// signal handler function to detach shared memory block and exit the program on receiving SIGPROF, SIGTERM, or SIGINT
static void myhandler(int s) {
  if (s == SIGPROF || s == SIGTERM) {
    detach_memory_block(block);
    exit(-1);
  } else if (s == SIGINT) {
    detach_memory_block(block);
    exit(-1);
  }
}

// function to set up signal handler function for SIGPROF
static int setupinterrupt(void) {
  struct sigaction act;
  act.sa_handler = myhandler;
  act.sa_flags = 0;
  return (sigemptyset(&act.sa_mask) || sigaction(SIGPROF, &act, NULL));
}

// function to print termination message and detach shared memory block
void terminate(int* block, int clockSec, int clockNano, int quitSec, int quitNano) {
  printf("WORKER PID:%d PPID:%d SysClockS:%d SysClockNano:%d TermTimeS:%d TermTimeNano:%d\n", getpid(), getppid(), clockSec, clockNano, quitSec, quitNano);
  printf("--Terminating\n");
  detach_memory_block(block);
  exit(1);
}

int getReturnValue(struct MessageBuffer buf) {
  srand(time(NULL) + getpid());  // Initialize the random number generator with the current time
  int r = rand() % 100 + 1;  // Generate a random number between 1 and 100

  if (r >= 1 && r <= 95) {
    return buf.durationNano;
  } else if (r >= 96 && r <= 99) {
    return rand() % (buf.durationNano);
  } else {  // r == 100
    return (rand() % (buf.durationNano)) * -1;
  }
}

int main(int argc, char* argv[]) {

  // set up signal handler for SIGPROF
  if (setupinterrupt() == -1) {
    printf("Failed to set up handler for SIGPROF\n");
    exit(-1);
  }

  struct MessageBuffer buf;
  buf.mtype = 1;
  int msqid;
  key_t key;

  if ((key = ftok("README.txt", 1)) == -1) {
    perror("child ftok");
    exit(1);
  }

  if ((msqid = msgget(key, PERMS | IPC_CREAT)) == -1) { /* connect to the queue */
    perror("child msgget");
    exit(1);
  }

  if (msgrcv(msqid, &buf, sizeof(struct MessageBuffer), getpid(), 0) == -1) {
    perror("failed to receive message from parent\n");
    exit(1);
  }

  printf("message to child: %d\nSending value back...\n", buf.durationNano);

  struct MessageBuffer sendBuf;
  sendBuf.mtype = getppid();
  // buf.durationSec = randSeconds;
  sendBuf.durationNano = getReturnValue(buf);

  if (msgsnd(msqid, &sendBuf, sizeof(struct MessageBuffer) - sizeof(long), 0) == -1) {
    printf("msgsnd to %d failed\n", getppid());
    exit(1);
  }


  return 0;
}
