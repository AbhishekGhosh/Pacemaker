/*********************************************************
 Filename: hardware.c
 Version: 1.0
 Author Name: Mike Niyonkuru
 Purpose: simulates pacemaker hardware
 ************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "../share.h"
void *control_panel(void *);
void *wait_shock(void *);
void set_heartrate(int);
volatile int  run, svt, heart_rate, additional_pulses;
sensor_t sensors[NUM_PIECES];
int main(int argc, char *argv[]) {
	int fd, i, pulse;
	int adjust_counter;
	char name[NAME_LEN];
	shmem_t *registers[NUM_PIECES];
	/*****
	 * INITIALISATION
	 */
	for (i = 0; i < NUM_PIECES; i++) {
		snprintf(name, NAME_LEN, "register%d", i);
		/* create the shared memory object */
		fd = shm_open(name, O_RDWR | O_CREAT, 0666);
		if (fd == -1) {
			perror("shm_open");
			exit(EXIT_FAILURE);
		}
		/* set the size of the shared memory object */
		ftruncate(fd, sizeof(shmem_t));
		/* get a pointer to a piece of the shared memory */
		registers[i] = mmap(0, sizeof(shmem_t), PROT_READ | PROT_WRITE,
				MAP_SHARED, fd, 0);
		close(fd);
		/*
		 * initialize the semaphore
		 * The 1 means it can be shared between processes.  The 0 is the
		 * count, 0 meaning sem_wait() calls will block until a sem_post()
		 * is done.
		 */
		sem_init(&registers[i]->semaphore, 1, 0);
		sensors[i].works = 1; /* sensors work by default */
	}
	pulse = RESTING_BOUND;
	set_heartrate(NORM_HEARTBEAT); /* normal heart rate */
	pthread_create(NULL, NULL, control_panel, NULL); //display control panel
	pthread_create(NULL, NULL, wait_shock, NULL); //wait for shocks form server

	/*****
	 * READING AND WRITING TO REGISTERS
	 */
	do {
		/* simulate sensor reading */
		for (i = 0; i < NUM_PIECES; i++) {
			if (sensors[i].works)
				sensors[i].pulse = pulse;
		}
		/* simulate writting sensor readings to the register */
		for (i = 0; i < NUM_PIECES; i++) {
			if (sensors[i].works) {
				registers[i]->pulse = sensors[i].pulse;
				sem_post(&registers[i]->semaphore); /* allow software to read register values */
			}
		}
		/* changing of heart beats */
		pthread_mutex_unlock(&mutex); /* access svt */
		switch (pulse) {
		case RESTING_BOUND:
			//printf("adjust_counter:%d additional_pulses:%d\n", adjust_counter, additional_pulses);
			if (adjust_counter++ < additional_pulses) {
				//printf("adjusting pulse rate: %d\n", adjust_counter);
				pulse = RESTING_BOUND;
				break;
			}
			adjust_counter = 0;

			if (svt == UPPER_BOUND) { /* never hit upper bound */
				pulse = LOWER_BOUND;
				break;
			}

			pulse = UPPER_BOUND; /* 0 to +1 */
			break;
		case UPPER_BOUND:
			if (svt == LOWER_BOUND) { /* never hit lower bound */
				pulse = RESTING_BOUND;
				break;
			}
			pulse = LOWER_BOUND; /* +1 to -1 */
			break;
		case LOWER_BOUND:
			pulse = RESTING_BOUND; /* -1 to 0 */
			break;
		default:
			printf("unexpected pulse value: %d\n", pulse);
			exit(EXIT_FAILURE);
		}
		pthread_mutex_unlock(&mutex);
		delay(INTERVAL);
	} while (run);
	for (i = 0; i < NUM_PIECES; i++) {
		munmap(registers[i], sizeof(shmem_t));
		snprintf(name, NAME_LEN, "register%d", i);
		shm_unlink(name);
	}
	return EXIT_SUCCESS;
}
void *control_panel(void *arg) {
	char input[10];
	char sec_input[5];
	int i;
	svt=0;
	run=1;
	while (1) {
		printf("1. Change heart-rate \n");
		printf("2. Create SVT 1\n");
		printf("3. Create SVT 2\n");
		printf("4. Break a piece\n");
		printf("5. Reset\n");
		printf("q. Quit\n");
		fgets(input, sizeof(input), stdin);
		input[1] = '\0';
		pthread_mutex_lock(&mutex);
		switch(*input) {
		case '1':
			printf("Enter new heart rate: ");
			fgets(sec_input, sizeof(sec_input), stdin);
			set_heartrate(atoi(sec_input));

			fflush(stdout);
			break;
		case '2':
			svt = 1;
			break;
		case '3':
			svt = -1;
			break;
		case '4':
			printf("Enter piece to break: ");
			fgets(sec_input, sizeof(sec_input), stdin);
			//use shm
			sensors[atoi(sec_input)-1].works = 0;
			break;
		case '5':
			svt = 0; /* reset svt */
			/* reset all the non working pieces */
			for (i=1; i <= NUM_PIECES; i++) {
				//use shm
				sensors[i-1].works = 1;
			}
			heart_rate = NORM_HEARTBEAT;
			break;
		case 'q':
			run = 0; /* stop running the program */
			break;
		}
		pthread_mutex_unlock(&mutex);
	} while (*input != 'q');
}

void set_heartrate(int heart_rate) {
	int expected_beat_duration; /* how long should one beat be expected to run? default is 120ms*/

	/* expected beat duration = time/number of beats */
	expected_beat_duration = (MINUTE*1000)/heart_rate;
	/* additional pulses = (single beat duration - regular beat duration)/time*/
	additional_pulses = (expected_beat_duration-120)/INTERVAL;

	printf("additional pulses needed: %d\n", additional_pulses);
}

void *wait_shock(void *arg) {
	int fd;
	shmem_t* ptr;

	fd = shm_open("shock", O_RDWR | O_CREAT, 0666);
	if (fd == -1) {
		perror("shm_open");
		exit(EXIT_FAILURE);
	}
	/* set the size of the shared memory object */
	ftruncate(fd, sizeof(shmem_t));
	/* get a pointer to a piece of the shared memory */
	ptr = mmap(0, sizeof(shmem_t), PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, 0);
	close(fd);
	/*
	 * initialize the semaphore
	 * The 1 means it can be shared between processes.  The 0 is the
	 * count, 0 meaning sem_wait() calls will block until a sem_post()
	 * is done.
	 */
	sem_init(&ptr->semaphore, 1, 0);

	while (1){
		/* wait for the software to initiate shock */
		sem_wait( &ptr->semaphore );
		printf("got defibed!\n");

		pthread_mutex_lock(&mutex);
		heart_rate = NORM_HEARTBEAT;
		svt = 0;
		pthread_mutex_unlock(&mutex);
	}

    munmap( ptr, sizeof(shmem_t) );

	return (NULL);
}
