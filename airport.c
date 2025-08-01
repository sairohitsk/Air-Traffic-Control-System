#include "../include/airport.h"

int main() {
  int messageQueueID;
  key_t messageQueueKey;

  if ((messageQueueKey = ftok(PATHNAME, PROJ_ID)) == -1) {
    perror("Error generating key in ftok");
    exit(1);
  }

  if ((messageQueueID = msgget(messageQueueKey, PERMS)) == -1) {
    perror(
        "Error connecting to message queue in msgget. Is the Air Traffic "
        "Controller active?");
    exit(1);
  }

  int airportID;
  struct Runways runways;

  printf("Enter the Airport ID: ");
  scanf(" %d", &airportID);

  printf("Enter number of Runways: ");
  scanf(" %d", &(runways.numberOfRunways));

  pthread_mutex_t mutexes[runways.numberOfRunways + 1];

  if (pthread_mutex_init(&(mutexes[runways.numberOfRunways]), NULL) == -1) {
    perror("Error initializing mutex");
    exit(1);
  }
  runways.runwayLoadCapacity[runways.numberOfRunways][0] = 15000;
  runways.runwayLoadCapacity[runways.numberOfRunways][1] =
      runways.numberOfRunways;

  printf(
      "Enter loadCapacity of Runways (give as a space separated list in a "
      "single line): ");
  for (int i = 0; i < runways.numberOfRunways; i++) {
    scanf(" %d", &(runways.runwayLoadCapacity[i][0]));
    runways.runwayLoadCapacity[i][1] = i;

    if (pthread_mutex_init(&(mutexes[i]), NULL) == -1) {
      perror("Error initializing mutex");
      exit(1);
    }
  }

  qsort(runways.runwayLoadCapacity, (size_t)runways.numberOfRunways,
        sizeof runways.runwayLoadCapacity[0], compare);

  for (int i = 0; i <= runways.numberOfRunways; i++) {
    runways.runwayMutexes[i] = &mutexes[i];
  }

  pthread_t threads[100];
  struct ThreadArgs threadArgs[100];
  int t = 0;

  while (1) {
    threadArgs[t].messageQueueID = messageQueueID;
    if (msgrcv(messageQueueID, &threadArgs[t].messageBuffer,
               sizeof(threadArgs[t].messageBuffer) -
                   sizeof(threadArgs[t].messageBuffer.mtype),
               airportID + 10, 0) == -1) {
      perror("Error receiving message in msgrcv");

      exit(1);
    }

    threadArgs[t].airportID = airportID;
    threadArgs[t].runways = runways;

    if (threadArgs[t].messageBuffer.sequenceNumber == -1) {
      for (int i = 0; i < t; i++) {
        if (pthread_join(threads[i], NULL)) {
          perror("Error in pthread_join");
          exit(1);
        }
      }

      printf("Terminating Airport %d...\n", airportID);

      struct MessageBuffer terminationRequestBuffer;
      terminationRequestBuffer.mtype = 1;
      terminationRequestBuffer.sequenceNumber = -2;
      terminationRequestBuffer.airportDetails.airportID = airportID;

      for (int i = 0; i <= runways.numberOfRunways; i++) {
        if (pthread_mutex_destroy(&mutexes[i])) {
          perror("Error in pthread_mutex_destroy");
          exit(1);
        }
      }

      if (msgsnd(messageQueueID, &terminationRequestBuffer,
                 sizeof(terminationRequestBuffer) -
                     sizeof(terminationRequestBuffer.mtype),
                 0) == -1) {
        perror("Error sending message in msgsnd");
        exit(1);
      }

      exit(0);
    }

    if (pthread_create(&threads[t], NULL, threadFunc, &threadArgs[t])) {
      perror("Error in pthread_create");
      exit(1);
    }

    t++;
  }
}

int compare(const void *pa, const void *pb) {
  const int *a = pa;
  const int *b = pb;
  if (a[0] == b[0])
    return a[1] - b[1];
  else
    return a[0] - b[0];
}

static void *threadFunc(void *args) {
  struct ThreadArgs *threadArgs = (struct ThreadArgs *)args;

  pthread_t thread;

  if (threadArgs->messageBuffer.sequenceNumber == 1) {
    // Departure
    struct PlaneThreadArgs planeThreadArgs;
    planeThreadArgs.messageQueueID = threadArgs->messageQueueID;
    planeThreadArgs.planeDetails = threadArgs->messageBuffer.planeDetails;
    planeThreadArgs.airportID = threadArgs->airportID;
    planeThreadArgs.runways = threadArgs->runways;

    if (pthread_create(&thread, NULL, departure, &planeThreadArgs)) {
      perror("Error in pthread_create");
      exit(1);
    }
  }

  if (threadArgs->messageBuffer.sequenceNumber == 2) {
    // Arrival
    struct PlaneThreadArgs planeThreadArgs;
    planeThreadArgs.messageQueueID = threadArgs->messageQueueID;
    planeThreadArgs.planeDetails = threadArgs->messageBuffer.planeDetails;
    planeThreadArgs.airportID = threadArgs->airportID;
    planeThreadArgs.runways = threadArgs->runways;

    if (pthread_create(&thread, NULL, arrival, &planeThreadArgs)) {
      perror("Error in pthread_create");
      exit(1);
    }
  }

  if (pthread_join(thread, NULL)) {
    perror("Error in pthread_join");
    exit(1);
  }

  pthread_exit(NULL);
}

static void *departure(void *args) {
  struct PlaneThreadArgs *planeThreadArgs = (struct PlaneThreadArgs *)args;

  int useBackupRunway = 1, currentIndex = 0;
  for (int i = 0; i < planeThreadArgs->runways.numberOfRunways; i++) {
    if (planeThreadArgs->planeDetails.totalWeight <=
        planeThreadArgs->runways.runwayLoadCapacity[i][0]) {
      currentIndex = i;
      useBackupRunway = 0;
      break;
    }
  }

  if (planeThreadArgs->planeDetails.planeType) {
    printf("Plane %d is now commencing boarding\n",
           planeThreadArgs->planeDetails.planeID);
    sleep(3);
    printf("Plane %d has now finished boarding\n",
           planeThreadArgs->planeDetails.planeID);
  }

  if (useBackupRunway) {
    if (pthread_mutex_lock(
            planeThreadArgs->runways
                .runwayMutexes[planeThreadArgs->runways.numberOfRunways]) ==
        -1) {
      perror("Error in pthread_mutex_lock");
      exit(1);
    }

    printf("Plane %d is now taking off from Runway No. %d of Airport No. %d\n",
           planeThreadArgs->planeDetails.planeID,
           planeThreadArgs->runways.numberOfRunways + 1,
           planeThreadArgs->airportID);
    sleep(2);
    printf("Plane %d has taken off from Runway No. %d of Airport No. %d\n",
           planeThreadArgs->planeDetails.planeID,
           planeThreadArgs->runways.numberOfRunways + 1,
           planeThreadArgs->airportID);

    if (pthread_mutex_unlock(
            planeThreadArgs->runways
                .runwayMutexes[planeThreadArgs->runways.numberOfRunways]) ==
        -1) {
      perror("Error in pthread_mutex_unlock");
      exit(1);
    }

  } else {
    int flag = 1, runwayIndex, mutexIndex;

    while (flag) {
      for (int i = currentIndex; i < planeThreadArgs->runways.numberOfRunways;
           i++) {
        if (pthread_mutex_trylock(planeThreadArgs->runways.runwayMutexes[i]) ==
            0) {
          mutexIndex = i;
          runwayIndex = planeThreadArgs->runways.runwayLoadCapacity[i][1];
          flag = 0;
          break;
        }
      }
    }

    printf("Plane %d is now taking off from Runway No. %d of Airport No. %d\n",
           planeThreadArgs->planeDetails.planeID, runwayIndex + 1,
           planeThreadArgs->airportID);
    sleep(2);
    printf("Plane %d has taken off from Runway No. %d of Airport No. %d\n",
           planeThreadArgs->planeDetails.planeID, runwayIndex + 1,
           planeThreadArgs->airportID);

    if (pthread_mutex_unlock(
            planeThreadArgs->runways.runwayMutexes[mutexIndex]) == -1) {
      perror("Error in pthread_mutex_unlock");
      exit(1);
    }
  }

  struct MessageBuffer departureRequestBuffer;
  departureRequestBuffer.mtype = 1;
  departureRequestBuffer.sequenceNumber = 2;
  departureRequestBuffer.planeDetails = planeThreadArgs->planeDetails;

  if (msgsnd(
          planeThreadArgs->messageQueueID, &departureRequestBuffer,
          sizeof(departureRequestBuffer) - sizeof(departureRequestBuffer.mtype),
          0) == -1) {
    perror("Error sending message in msgsnd");
    exit(1);
  }

  pthread_exit(NULL);
}

static void *arrival(void *args) {
  struct PlaneThreadArgs *planeThreadArgs = (struct PlaneThreadArgs *)args;

  printf("Airport %d is waiting for Plane %d to land.\n",
         planeThreadArgs->airportID, planeThreadArgs->planeDetails.planeID);

  struct MessageBuffer messageBuffer;

  if (msgrcv(planeThreadArgs->messageQueueID, &messageBuffer,
             sizeof(messageBuffer) - sizeof(messageBuffer.mtype),
             planeThreadArgs->airportID + 20, 0) == -1) {
    perror("Error receiving message in msgrcv");

    exit(1);
  }

  if (messageBuffer.sequenceNumber == 3) {
    int useBackupRunway = 1, currentIndex = 0;
    for (int i = 0; i < planeThreadArgs->runways.numberOfRunways; i++) {
      if (planeThreadArgs->planeDetails.totalWeight <=
          planeThreadArgs->runways.runwayLoadCapacity[i][0]) {
        currentIndex = i;
        useBackupRunway = 0;
        break;
      }
    }

    sleep(30);
    if (useBackupRunway) {
      if (pthread_mutex_lock(
              planeThreadArgs->runways
                  .runwayMutexes[planeThreadArgs->runways.numberOfRunways]) ==
          -1) {
        perror("Error in pthread_mutex_lock");
        exit(1);
      }

      printf("Plane %d is now landing on Runway No. %d of Airport No. %d\n",
             planeThreadArgs->planeDetails.planeID,
             planeThreadArgs->runways.numberOfRunways + 1,
             planeThreadArgs->airportID);
      sleep(2);
      printf("Plane %d has landed on Runway No. %d of Airport No. %d\n",
             planeThreadArgs->planeDetails.planeID,
             planeThreadArgs->runways.numberOfRunways + 1,
             planeThreadArgs->airportID);

      if (pthread_mutex_unlock(
              planeThreadArgs->runways
                  .runwayMutexes[planeThreadArgs->runways.numberOfRunways]) ==
          -1) {
        perror("Error in pthread_mutex_unlock");
        exit(1);
      }

    } else {
      int flag = 1, runwayIndex, mutexIndex;

      while (flag) {
        for (int i = currentIndex; i < planeThreadArgs->runways.numberOfRunways;
             i++) {
          if (pthread_mutex_trylock(
                  planeThreadArgs->runways.runwayMutexes[i]) == 0) {
            mutexIndex = i;
            runwayIndex = planeThreadArgs->runways.runwayLoadCapacity[i][1];
            flag = 0;
            break;
          }
        }
      }

      printf("Plane %d is now landing on Runway No. %d of Airport No. %d\n",
             planeThreadArgs->planeDetails.planeID, runwayIndex + 1,
             planeThreadArgs->airportID);
      sleep(2);
      printf("Plane %d has landed on Runway No. %d of Airport No. %d\n",
             planeThreadArgs->planeDetails.planeID, runwayIndex + 1,
             planeThreadArgs->airportID);

      if (pthread_mutex_unlock(
              planeThreadArgs->runways.runwayMutexes[mutexIndex]) == -1) {
        perror("Error in pthread_mutex_unlock");
        exit(1);
      }
    }

    if (planeThreadArgs->planeDetails.planeType) {
      printf("Plane %d is now commencing deboarding\n",
             planeThreadArgs->planeDetails.planeID);
      sleep(3);
      printf("Plane %d has now finished deboarding\n",
             planeThreadArgs->planeDetails.planeID);
    }

    printf("Plane %d has landed successfully at Airport No. %d\n",
           planeThreadArgs->planeDetails.planeID, planeThreadArgs->airportID);

    struct MessageBuffer confirmArrivalBuffer;
    confirmArrivalBuffer.mtype = 1;
    confirmArrivalBuffer.sequenceNumber = 3;
    confirmArrivalBuffer.planeDetails = planeThreadArgs->planeDetails;

    if (msgsnd(
            planeThreadArgs->messageQueueID, &confirmArrivalBuffer,
            sizeof(confirmArrivalBuffer) - sizeof(confirmArrivalBuffer.mtype),
            0) == -1) {
      perror("Error sending message in msgsnd");
      exit(1);
    }
  }

  pthread_exit(NULL);
}
