/**
 * @file oss.c
 * @author Wolfe Weeks
 * @date 2023-04-04
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <signal.h>

#define MSG_KEY 1234
#define SHM_KEY 5678
#define MAX_PROCS 18
#define TOTAL_PROCS 100
#define MAX_TIME_BETWEEN_NEW_PROCS_NS 1000000000
#define MAX_TIME_BETWEEN_NEW_PROCS_SEC 1
#define TIME_QUANTUM_NS 10000000
#define BLOCKED_QUEUE_CHECK_NS 5000000
#define MAX_LOG_LINES 10000
#define MAX_SECONDS 3

typedef struct message {
  long mtype;
  int time_quantum;
  int used_time;
  int termination_flag;
} message;

typedef struct process_control_block {
  pid_t pid;
  int local_pid;
  int total_cpu_time;
  int total_system_time;
  int in_use;
} process_control_block;

typedef struct shared_memory {
  unsigned int sec;
  unsigned int ns;
  process_control_block pcb[MAX_PROCS];
} shared_memory;

int msgid;
int shmid;
shared_memory* shm;
FILE* logfile;
int log_lines;

void cleanup(int signum) {
  shmdt(shm);
  shmctl(shmid, IPC_RMID, NULL);
  msgctl(msgid, IPC_RMID, NULL);
  fclose(logfile);
  exit(0);
}

process_control_block* find_free_pcb() {
  for (int i = 0; i < MAX_PROCS; i++) {
    if (shm->pcb[i].in_use == 0) {
      return &(shm->pcb[i]);
    }
  }
  return NULL;
}

void spawn_child(process_control_block* pcb) {
  pid_t pid = fork();
  if (pid < 0) {
    perror("fork()");
    exit(1);
  } else if (pid == 0) {
    char str_pid[16];
    sprintf(str_pid, "%d", pcb->local_pid);
    execl("./user", "user", str_pid, NULL);
    perror("execl()");
    exit(1);
  } else {
    pcb->pid = pid;
    pcb->in_use = 1;
  }
}

void increment_clock() {
  shm->ns += rand() % 10000 + 100;
  if (shm->ns >= 1000000000) {
    shm->sec += 1;
    shm->ns -= 1000000000;
  }
}

int main() {
  srand(time(NULL));

  signal(SIGINT, cleanup);
  signal(SIGALRM, cleanup);
  alarm(MAX_SECONDS);

  msgid = msgget(MSG_KEY, IPC_CREAT | 0666);
  if (msgid == -1) {
    perror("msgget(MSG_KEY)");
    exit(1);
  }

  shmid = shmget(SHM_KEY, sizeof(shared_memory), IPC_CREAT | 0666);
  if (shmid == -1) {
    perror("shmget(SHM_KEY)");
    exit(1);
  }

  shm = (shared_memory*)shmat(shmid, NULL, 0);
  if (shm == (void*)-1) {
    perror("shmat(shmid)");
    exit(1);
  }

  logfile = fopen("oss.log", "w");
  if (logfile == NULL) {
    perror("fopen(\"oss.log\")");
    exit(1);
  }
  initialize_shared_memory();
  generate_initial_processes();

  while (1) {
    increment_clock();

    if (should_create_new_process()) {
      if (process_table_not_full()) {
        create_new_process();
      } else {
        update_next_process_creation_time();
      }
    }

    if (process_ready_to_run()) {
      select_process_to_run();
      send_message_to_process();
      receive_message_from_process();
      update_process_state();
    } else {
      increment_clock_until_next_process_creation();
    }

    check_blocked_processes();

    if (termination_criteria_met()) {
      break;
    }
  }

  print_statistics();
  cleanup(0);

  return 0;
}