/**
 * @file oss.c
 * @author Wolfe Weeks
 * @date 2023-04-27
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdbool.h>
#include <errno.h>

int msgq_id = -1;

typedef struct {
  unsigned int seconds;
  unsigned int nanoseconds;
} shared_clock_t;

int clockShmid = NULL;
shared_clock_t* sharedClock = NULL;

shared_clock_t* create_shared_clock() {
  // Generate a key for shared memory
  key_t key = ftok("README.txt", 'R');
  if (key == -1) {
    perror("Error generating key for shared memory");
    exit(1);
  }

  // Allocate and initialize shared memory for the clock
  clockShmid = shmget(key, sizeof(shared_clock_t), 0666 | IPC_CREAT);
  if (clockShmid == -1) {
    perror("Error allocating shared memory");
    exit(1);
  }

  // Attach the shared memory segment to the process's address space
  shared_clock_t* clock = (shared_clock_t*)shmat(clockShmid, NULL, 0);
  if (clock == (void*)-1) {
    perror("Error attaching shared memory");
    exit(1);
  }

  // Initialize the clock values
  clock->seconds = 0;
  clock->nanoseconds = 0;

  return clock;
}

void increment_shared_clock(shared_clock_t* clock, unsigned int nanoseconds) {
  // Increment the shared clock by the specified nanoseconds
  unsigned long long total_nanoseconds = clock->nanoseconds + nanoseconds;
  clock->seconds += total_nanoseconds / 1000000000;
  clock->nanoseconds = total_nanoseconds % 1000000000;
}

unsigned long long subtract_clocks(shared_clock_t clock1, shared_clock_t clock2) {
  unsigned long long nanoseconds1 = clock1.seconds * 1000000000ULL + clock1.nanoseconds;
  unsigned long long nanoseconds2 = clock2.seconds * 1000000000ULL + clock2.nanoseconds;
  return nanoseconds1 - nanoseconds2;
}

void clear_shared_clock(shared_clock_t* clock) {
  // Detach the shared memory segment
  if (shmdt(clock) == -1) {
    perror("Error detaching shared memory for clock");
    exit(1);
  }

  // Remove the shared memory segment
  if (shmctl(clockShmid, IPC_RMID, NULL) == -1) {
    perror("Error removing shared memory for clock");
    exit(1);
  }
}

void print_clock(shared_clock_t* clock) {
  printf("%d:%d\n", clock->seconds, clock->nanoseconds);
}

typedef struct {
  int pid; //-1 if available
} resource_instance_t;

typedef struct {
  int resource_id;
  resource_instance_t instances[20];
} resource_descriptor_t;


int resourcesShmid = NULL;
resource_descriptor_t* resources = NULL;

resource_descriptor_t* initialize_shared_resources() {
  // Generate a key for shared memory
  key_t key = ftok(".", 'R');
  if (key == -1) {
    perror("Error generating key for shared memory");
    exit(1);
  }

  // Allocate and initialize shared memory for the clock
  resourcesShmid = shmget(key, 10 * sizeof(resource_descriptor_t), 0666 | IPC_CREAT);
  if (resourcesShmid == -1) {
    perror("Error allocating shared memory");
    exit(1);
  }

  // Attach the shared memory segment to the process's address space
  resource_descriptor_t* resources = (resource_descriptor_t*)shmat(resourcesShmid, NULL, 0);
  if (resources == (void*)-1) {
    perror("Error attaching shared memory");
    exit(1);
  }

  int i, j;
  for (i = 0; i < 10; i++) {
    resources[i].resource_id = i;
    for (j = 0; j < 20; j++) {
      resources[i].instances[j].pid = -1;
    }
  }
  return resources;
}

void clear_shared_resources() {
  // Detach the shared memory segment
  if (shmdt(resources) == -1) {
    perror("Error detaching shared memory for resources");
    exit(1);
  }

  // Remove the shared memory segment
  if (shmctl(resourcesShmid, IPC_RMID, NULL) == -1) {
    perror("Error removing shared memory for resources");
    exit(1);
  }
}

int get_allocated_resources(int resource_type, int pid) {
  int count = 0;
  for (int i = 0; i < 20; i++) {
    if (resources[resource_type].instances[i].pid == pid) {
      count++;
    }
  }
  return count;
}

int get_available_resources(int resource_type) {
  int allocated_resources = 0;
  for (int i = 0; i < 20; i++) {
    if (resources[resource_type].instances[i].pid != -1) {
      allocated_resources++;
    }
  }
  return 20 - allocated_resources;
}

void allocate_resources(int resource_type, int pid, int num_resources) {
  int count = 0;
  int i, j;
  for (i = 0; i < 20; i++) {
    if (resources[resource_type].instances[i].pid == -1) {
      resources[resource_type].instances[i].pid = pid;
      count++;
    }
    if (count == num_resources) {
      break;
    }
  }
  if (count != num_resources) {
    printf("Error: Unable to allocate %d resources of type %d to process %d\n", num_resources, resource_type, pid);
  }
}

void deallocate_resources(int resource_type, int pid, int num_resources) {
  int count = 0;
  int i;
  for (i = 0; i < 20; i++) {
    if (resources[resource_type].instances[i].pid == pid) {
      resources[resource_type].instances[i].pid = -1;
      count++;
    }
    if (count == num_resources) {
      break;
    }
  }
  if (count != num_resources) {
    printf("Error: Unable to deallocate %d resources of type %d from process %d\n", num_resources, resource_type, pid);
  }
}

void deallocate_all_resources(int pid) {
  int i, j;
  int count = 0;
  for (i = 0; i < 10; i++) {
    for (j = 0; j < 20; j++) {
      if (resources[i].instances[j].pid == pid) {
        resources[i].instances[j].pid = -1;
        count++;
      }
    }
  }
  printf("Resources released:");
  for (i = 0; i < 10; i++) {
    int num_released = 0;
    for (j = 0; j < 20; j++) {
      if (resources[i].instances[j].pid == -1) {
        num_released++;
      }
    }
    if (num_released > 0) {
      printf(" R%d:%d,", i, num_released);
    }
  }
  printf("\n");
}

void print_resources_by_pid() {
  int i, j, k;
  int pids[40];
  int pid_count = 0;

  // Collect all unique PIDs
  for (i = 0; i < 10; i++) {
    for (j = 0; j < 20; j++) {
      int pid = resources[i].instances[j].pid;
      if (pid != -1) {
        // Check if the PID is already in the list
        int pid_found = 0;
        for (k = 0; k < pid_count; k++) {
          if (pids[k] == pid) {
            pid_found = 1;
            break;
          }
        }
        if (!pid_found) {
          pids[pid_count++] = pid;
        }
      }
    }
  }

  // Print the table header
  printf("%-10s", "");
  for (i = 0; i < 10; i++) {
    printf("%-10d", i);
  }
  printf("\n");

  // Print the table rows
  for (i = 0; i < pid_count; i++) {
    printf("%-10d", pids[i]);
    for (j = 0; j < 10; j++) {
      int count = 0;
      for (k = 0; k < 20; k++) {
        if (resources[j].instances[k].pid == pids[i]) {
          count++;
        }
      }
      printf("%-10d", count);
    }
    printf("\n");
  }
}

typedef struct {
  long mtype;
  int from;
  int resource_id;
  int request_amount;
} message_t;

void handle_alarm(int signum) {
  printf("\nTerminating due to 5 seconds timeout.\n");
  clear_shared_clock(sharedClock);
  clear_shared_resources();
  if (msgctl(msgq_id, IPC_RMID, NULL) == -1) {
    perror("msgctl failed");
  }
  kill(0, SIGTERM); // Send SIGTERM to the entire process group
  exit(0);
}

void handle_interrupt(int signum) {
  printf("\nTerminating due to CTRL-C.\n");
  clear_shared_clock(sharedClock);
  clear_shared_resources();
  if (msgctl(msgq_id, IPC_RMID, NULL) == -1) {
    perror("msgctl failed");
  }
  kill(0, SIGTERM); // Send SIGTERM to the entire process group
  exit(0);
}

int main(int argc, char const* argv[]) {
  // Make the main process the group leader
  setpgid(0, 0);

  // Set up signal handling
  signal(SIGALRM, handle_alarm);
  signal(SIGINT, handle_interrupt);

  // Set alarm for 5 seconds
  alarm(5);

  resources = initialize_shared_resources();

  sharedClock = create_shared_clock();

  key_t msgq_key = ftok(".", 'Q');
  if (msgq_key == -1) {
    perror("ftok failed");
    exit(1);
  }

  // Create the message queue
  msgq_id = msgget(msgq_key, IPC_CREAT | 0666);
  if (msgq_id == -1) {
    perror("msgget failed");
    exit(1);
  }

  int num_resources = 10;
  int num_instances = 20;
  int max_processes = 18;
  int total_processes = 40;

  int created_children = 0;
  int running_children = 0;

  shared_clock_t previousLaunchTime;
  previousLaunchTime.nanoseconds = 0;
  previousLaunchTime.seconds = 0;

  shared_clock_t previousDeadlockTime;
  previousDeadlockTime.nanoseconds = 0;
  previousDeadlockTime.seconds = 0;

  srand(time(0));

  int randGap = (rand() % (500000000 - 1000000 + 1)) + 1000000;

  while (true) {
    if (created_children >= total_processes && running_children == 0) {
      break;
    }

    message_t msg;
    // Check if a message is available
    if (msgrcv(msgq_id, &msg, sizeof(message_t) + 8192, 1, IPC_NOWAIT) == -1) {
      if (errno != ENOMSG) { // Ignore ENOMSG errors
        perror("msgrcv failed");
        exit(1);
      }
    } else {
      // printf("From: %d resource_id=%d request_amount=%d\n", msg.from, msg.resource_id, msg.request_amount);
      if (msg.request_amount > 0) {
        printf("Master has detected Process %d requesting %d instances R%d at time %u:%u\n", msg.from, msg.request_amount, msg.resource_id, sharedClock->seconds, sharedClock->nanoseconds);
        if (get_available_resources(msg.resource_id) >= msg.request_amount) {
          printf("Master granting Process %d's request at time %u:%u\n", msg.from, sharedClock->seconds, sharedClock->nanoseconds);
          allocate_resources(msg.resource_id, msg.from, msg.request_amount);
        }
      } else {
        printf("Master has acknowledged Process %d releasing %d instances R%d at time %u:%u\n", msg.from, msg.request_amount, msg.resource_id, sharedClock->seconds, sharedClock->nanoseconds);
        deallocate_resources(msg.resource_id, msg.from, msg.request_amount);
        printf("\tResources released: %d:%d\n", msg.resource_id, msg.request_amount);
      }
    }

    increment_shared_clock(sharedClock, 500);
    unsigned long long elapsedNano = subtract_clocks(*sharedClock, previousLaunchTime);

    unsigned long long timeSinceLastDeadlock = subtract_clocks(*sharedClock, previousDeadlockTime);
    if (timeSinceLastDeadlock >= 1000000000) {
      print_resources_by_pid();
      printf("Master running deadlock detection at time %u:%u:\n", sharedClock->seconds, sharedClock->nanoseconds);
      printf("\tNo deadlocks detected\n");

      previousDeadlockTime.nanoseconds = sharedClock->nanoseconds;
      previousDeadlockTime.seconds = sharedClock->seconds;
    }

    if (elapsedNano >= randGap) {
      if (running_children < max_processes && created_children < total_processes) {
        previousLaunchTime.seconds = sharedClock->seconds;
        previousLaunchTime.nanoseconds = sharedClock->nanoseconds;

        // Fork a new process.
        pid_t pid = fork();

        // If fork failed.
        if (pid < 0) {
          printf("Fork failed!\n");
          exit(1);
        }// If this is the child process.
        else if (pid == 0) {
          // Set the process group ID of the child process to that of the parent
          setpgid(0, getppid());

          printf("child %d launched at %u:%u\n", created_children, sharedClock->seconds, sharedClock->nanoseconds);

          execl("./user_proc", "./user_proc", NULL);
          exit(1);
        }
        // If this is the parent process.
        else {
          created_children++;
          running_children++;
          randGap = (rand() % (500000000 - 1000000 + 1)) + 1000000;
          previousLaunchTime.nanoseconds = sharedClock->nanoseconds;
          previousLaunchTime.seconds = sharedClock->seconds;
        }
      } else {
        // Check if any child processes have finished
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
          printf("Process %d terminated\n", pid);
          deallocate_all_resources(pid);
          running_children--;
        }
      }
    }
  }

  //wait for all children to die
  int status;
  pid_t pid;
  while ((pid = wait(&status)) != -1);

  clear_shared_clock(sharedClock);
  clear_shared_resources(resources);

  // Delete the message queue
  if (msgctl(msgq_id, IPC_RMID, NULL) == -1) {
    perror("msgctl failed");
    exit(1);
  }

  return 0;
}