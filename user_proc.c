/**
 * @file user_proc.c
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

#define BOUND_B 1000 // Bound for when a process should request (or release) a resource

typedef struct {
  unsigned int seconds;
  unsigned int nanoseconds;
} shared_clock_t;

unsigned long long subtract_clocks(shared_clock_t clock1, shared_clock_t clock2) {
  unsigned long long nanoseconds1 = clock1.seconds * 1000000000ULL + clock1.nanoseconds;
  unsigned long long nanoseconds2 = clock2.seconds * 1000000000ULL + clock2.nanoseconds;
  return nanoseconds1 - nanoseconds2;
}

typedef struct {
  int pid; //-1 if available
} resource_instance_t;

typedef struct {
  int resource_id;
  resource_instance_t instances[20];
} resource_descriptor_t;

int get_allocated_resources(resource_descriptor_t* resources, int resource_type, int pid) {
  int count = 0;
  int i;
  for (i = 0; i < 20; i++) {
    if (resources[resource_type].instances[i].pid == pid) {
      count++;
    }
  }
  return count;
}

typedef struct {
  long mtype;
  // resource_request_t request;

  int from;
  int resource_id;
  int request_amount;
} message_t;

void signal_handler(int signum) {
  // Detach from the shared memory segment
  // shmdt(clock);
  // shmdt()

  // Handle termination signals
  // printf("Terminating child %d due to signal %d\n", getpid(), signum);
  exit(0);
}


int main(int argc, char* argv[]) {
  // Register the signal handler
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  // Attach shared memory for the clock and resource descriptors
  key_t msgq_key = ftok(".", 'Q');
  if (msgq_key == -1) {
    perror("ftok failed");
    exit(1);
  }

  // Create or attach to the message queue
  int msgq_id = msgget(msgq_key, 0666);
  if (msgq_id == -1) {
    perror("msgget failed");
    exit(1);
  }

  // Generate the same key used to create the shared memory segment
  key_t key = ftok("README.txt", 'R');
  if (key == -1) {
    perror("Error generating key for shared memory");
    exit(1);
  }

  // Get the ID of the shared memory segment
  int clockShmid = shmget(key, sizeof(shared_clock_t), 0666);
  if (clockShmid == -1) {
    perror("Error accessing shared memory");
    exit(1);
  }

  // Attach to the shared memory segment and get a pointer to the clock struct
  shared_clock_t* clock = (shared_clock_t*)shmat(clockShmid, NULL, 0);
  if (clock == (void*)-1) {
    perror("Error attaching shared memory");
    exit(1);
  }


  key_t resourceKey = ftok(".", 'R');
  if (key == -1) {
    perror("Error generating key for shared memory");
    exit(1);
  }

  int resourcesShmid = shmget(resourceKey, 10 * sizeof(resource_descriptor_t), 0666);
  if (resourcesShmid == -1) {
    perror("Error accessing shared memory");
    exit(1);
  }

  resource_descriptor_t* resources = (resource_descriptor_t*)shmat(resourcesShmid, NULL, 0);
  if (resources == (void*)-1) {
    perror("Error attaching shared memory for resources");
    exit(1);
  }

  // Initialize random number generator
  srand(time(NULL) ^ getpid());

  int requestIncrement = (rand() % (BOUND_B + 1));

  printf("child %d number: %d | %u:%u\n", getpid(), requestIncrement, clock->seconds, clock->nanoseconds);

  shared_clock_t startTime;
  startTime.seconds = clock->seconds;
  startTime.nanoseconds = clock->nanoseconds;

  bool secondHasPassed = false;

  shared_clock_t lastTermCheck;
  lastTermCheck.seconds = clock->seconds;
  lastTermCheck.nanoseconds = clock->nanoseconds;

  int nanoSinceLastCheck = 0;

  int randGap = (rand() % (250000000 + 1));

  shared_clock_t lastResourceRequest;
  lastResourceRequest.seconds = clock->seconds;
  lastResourceRequest.nanoseconds = clock->nanoseconds;

  while (true) {
    if (!secondHasPassed) {
      int elapsedNano = subtract_clocks(*clock, startTime);
      if (elapsedNano >= 1000000000) {
        secondHasPassed = true;
      }
    }

    if (secondHasPassed) {
      nanoSinceLastCheck = subtract_clocks(*clock, lastTermCheck);
      if (nanoSinceLastCheck >= randGap) {
        int randNum = rand() % 100 + 1;
        if (randNum <= 5) {
          printf("terminating child %d\n", getpid());
          // Detach from the shared memory segment
          shmdt(clock);

          return 0;
        }

        lastTermCheck.seconds = clock->seconds;
        lastTermCheck.nanoseconds = clock->nanoseconds;

        randGap = (rand() % (250000000 + 1)) + 250000000;
      }
    }

    if (subtract_clocks(*clock, lastResourceRequest) >= requestIncrement) {
      message_t* msg = malloc(sizeof(message_t) + 8192);
      if (msg == NULL) {
        perror("malloc failed");
        exit(1);
      }

      msg->mtype = 1; // Set the message type to 1 for oss
      msg->from = getpid();
      msg->resource_id = (rand() % 10);

      // int request_amount = 0;
      int my_resources = get_allocated_resources(resources, msg->resource_id, getpid());

      if (rand() % 2 == 0) {
        int max = 20 - my_resources;
        msg->request_amount = rand() % max + 1;
      } else {
        msg->request_amount = -(rand() % my_resources + 1);
      }

      // Send the message to oss
      if (msgsnd(msgq_id, msg, sizeof(message_t) - sizeof(long), 0) == -1) {
        perror("msgsnd failed");
        exit(1);
      }

      free(msg);

      lastResourceRequest.seconds = clock->seconds;
      lastResourceRequest.nanoseconds = clock->nanoseconds;
    }
  }

  // Detach from the shared memory segment
  shmdt(clock);

  return 0;
}