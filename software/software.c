/*********************************************************
 Filename: software.c
 Version: 1.0
 Author Name: Mike Niyonkuru
 Purpose: will read hardware's registers
 ************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/neutrino.h>
#include <sys/siginfo.h>
#include "../share.h"
#define BPM_WAIT 60
void *read_register(void*);
void *bpm_calculator(void*);
void *wait_before_defib(void*);
void *defib(void*);
void set_pulse(int);
int majority_pulse();
int calculate_bpm(int);
void print_pulse(char);
void int_to_binary(int);
sensor_t sensors[NUM_PIECES];
shmem_t *registers[NUM_PIECES];
volatile int bpm;
volatile int response = 0;
volatile int svt_1 = 0;
volatile int svt_2 = 0; /* type 2 */
volatile int state;
pthread_mutex_t bpm_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
struct itimerspec defib_itime;
timer_t defib_timerId;

shmem_t register_r[NUM_PIECES];

int occurances[3];
pthread_mutex_t registr_mutex[NUM_PIECES];

int main(int argc, char *argv[]) {
	int fd, i, pulse, old_pulse;
	int chid, coid;
	struct sigevent event;
	struct itimerspec itime;
	timer_t timerId;
	int num_pulses;
	char name[NAME_LEN];
	pulse_t msg;
	int start_count;
	state = 0;
	for (i = 0; i < NUM_PIECES; i++) {
		snprintf(name, NAME_LEN, "register%d", i);
		/* open the shared memory object */
		fd = shm_open(name, O_RDWR, 0);
		if (fd == -1) {
			perror("shm_open");
			exit(EXIT_FAILURE);
		}
		/* get a pointer to a piece of the shared memory */
		registers[i] = mmap(0, sizeof(shmem_t), PROT_READ | PROT_WRITE,
				MAP_SHARED, fd, 0);
		if (registers[i] == MAP_FAILED) {
			perror("mmap");
			exit(EXIT_FAILURE);
		}
		close(fd);
		sensors[i].works = 1; /* sensors work by default */
	}
	chid = ChannelCreate( 0 );
	if (chid == -1) {
		perror("ChannelCreate");
		exit(EXIT_FAILURE);
	}
	/* set up the pulse event that will be delivered to us by the kernel
	     * whenever the timer expires
	    */
	coid = ConnectAttach( 0, 0, chid, _NTO_SIDE_CHANNEL, 0 );
	if (coid == -1) {
		perror("ConnectAttach");
		exit( EXIT_FAILURE );
	}
	start_count = 0;
	pulse = RESTING_BOUND;
	bpm = -1;
	num_pulses = 0;
	SIGEV_PULSE_INIT( &event, coid, 10, TIMER_PULSE_EVENT, 0 );
	/* Create a timer which will send the above pulse event
	 * 5 seconds from now and then repeatedly after that every
	 * 1500 milliseconds.  The event to use has already been filled in
	 * above and is in the variable called 'event'.
	*/
	timer_create (CLOCK_REALTIME, &event, &timerId);
	itime.it_value.tv_sec = 0; /* expiry of 1 nseconds */
	itime.it_value.tv_nsec = 1;
	itime.it_interval.tv_sec = 0; /* repeating every .4 seconds later */
	itime.it_interval.tv_nsec = INTERVAL*1000*1000;
	for (i=0; i<NUM_PIECES; i++) {
		pthread_mutex_init(&registr_mutex[i], NULL);
		pthread_create(NULL, NULL, read_register, (void *)&i);
	}
	pthread_create(NULL, NULL, wait_before_defib, NULL);
	pthread_create(NULL, NULL, defib, NULL);
	//pthread_create(NULL, NULL, bpm_calculator, NULL); /* start calculating bpm */
	timer_settime(timerId, 0, &itime, NULL);
	/*****
	 * READING Registers
	 */
	while (1) {
		/* wait for register writes */
		if (MsgReceive( chid, &msg, sizeof(msg), NULL ) == -1) {
			perror("MsgReceive");
			exit(EXIT_FAILURE);
		}
		for (i=0; i<NUM_PIECES; i++)
			occurances[i] = 0;

		switch (msg.pulse.code) {
		case TIMER_PULSE_EVENT:
			for (i = 0; i < NUM_PIECES; i++) {
				pthread_mutex_lock(&mutex);
				if (sensors[i].works) {
					//printf("register%d:", i+1);
					//sem_wait(&registers[i]->semaphore);
					pthread_mutex_lock(&registr_mutex[i]);
					set_pulse (register_r->pulse); /* save the pulse */
					//printf("%d", register_r->pulse);
					pthread_mutex_unlock(&registr_mutex[i]);
//					print_pulse(pulse);
					//printf("\n");
				}
				pthread_mutex_unlock(&mutex);
			}
			old_pulse = pulse;
			pulse = majority_pulse();
			if (pulse == old_pulse && pulse != 0)
				continue;

			print_pulse(pulse);
			//printf("  %d", pulse);
			//printf("\n");
			/* save the pulse into the beat */
			switch (pulse) {
			case RESTING_BOUND:
				if (response++ > 25) {
					state = STATE_DEFIB;
				}
				num_pulses++;
				break;
			case UPPER_BOUND:
				svt_2=0;
				response=0;
				if (svt_1++ > 1) {
					state = STATE_DEFIB;
				}
				else if(start_count) {
					bpm = calculate_bpm(num_pulses);
					printf("num pulses: %d  bpm: %d\n", num_pulses, bpm);

					/* 40-49  90-99 */
					if ( (bpm >= LOW_MIN && bpm <= LOW_MAX) || (bpm >= HIGH_MIN && bpm <= HIGH_MAX) ) {
						/* wait 5 minutes then defib */
						state = STATE_WAIT_DEFIB;
						timer_settime(defib_timerId, 0, &defib_itime, NULL);
					}
					/* 39-  100+ */
					else if ( bpm < LOW_MAX || bpm > HIGH_MAX) {
						state = STATE_DEFIB;
					}
				}
				num_pulses=0;
				start_count = 1;
				break;
			case LOWER_BOUND:
				num_pulses++;
				svt_1 =0;
				response=0;
				if (svt_2-- < -1) {
					state = STATE_DEFIB;
				}
				break;
			default:
				exit(EXIT_FAILURE);
			}

			pthread_cond_broadcast(&cond);
			break;
		default:
			printf( "unexpected pulse code: %d\n", msg.pulse.code );
			exit(EXIT_FAILURE);
		}

	} /* NOTE: ADD mechanism for exit */
	for (i = 0; i < NUM_PIECES; i++)
		munmap(registers[i], sizeof(shmem_t));
	return EXIT_SUCCESS;
}

void set_pulse(int pulse) {
	switch (pulse) {
	case RESTING_BOUND:
		occurances[0] += 1;
		//printf("*%d* ", occurances[0]);
		break;
	case UPPER_BOUND:
		occurances[1] += 1;
		//printf("*%d* ", occurances[1]);
		break;
	case LOWER_BOUND:
		occurances[2] += 1;
		//printf("*%d* ", occurances[2]);
		break;
	default:
		printf("invalid pulse");
		exit(EXIT_FAILURE);
	}
}

/* write majority vote and return failed device */
int majority_pulse() {
	int i;
	int majority;
	int occ = 0;

	/* by default 0 wins majority votes */
	for (i=0; i<NUM_PIECES; i++) {
		/* if there is an occurance greater than the current majority */
		if (occurances[i] > occ) {
			occ = occurances[i];
			majority = i;
		}
	}

	//printf("majority pulse: %d\n", majority);
	if (majority == 2) {
		majority = -1;
	}
	//printf("majority pulse: %d\n", majority);

	return majority;
}

void *read_register (void* arg) {
	int index, flags;
	struct sigevent event;
	uint64_t timeout = 2*1000000000; ; /* timeout after 2 seconds */
	flags = _NTO_TIMEOUT_SEM;

	index = *((int*)arg);
	printf("reading register: %d\n", index);

	event.sigev_notify = SIGEV_UNBLOCK;

	while (1) {
		//pthread_mutex_lock(&mutex);
		if (sensors[index].works) {
			sem_wait(&registers[index]->semaphore);

			if (errno == ETIMEDOUT) {
				printf("register %d timedout\n", index);
				sensors[index].works  = 0;
			}
			pthread_mutex_lock(&registr_mutex[index]); /* protect register values */
			register_r[index] = *registers[index];
			pthread_mutex_unlock(&registr_mutex[index]);
			TimerTimeout( CLOCK_REALTIME, flags, &event, &timeout, NULL );
		}
		//pthread_mutex_unlock(&mutex);
	}

	return (NULL);
}

void *wait_before_defib(void *arg) {
	int chid, coid;
	pulse_t msg;
	struct sigevent event;
	chid = ChannelCreate( 0 );
	if (ChannelCreate( 0 ) == -1) {
		perror("ChannelCreate");
		exit(EXIT_FAILURE);
	}
	/* set up the pulse event that will be delivered to us by the kernel
	 * whenever the timer expires
	*/
	coid = ConnectAttach( 0, 0, chid, _NTO_SIDE_CHANNEL, 0 );
	if (coid == -1) {
		perror("ConnectAttach");
		exit( EXIT_FAILURE );
	}
	SIGEV_PULSE_INIT( &event, coid, 10, DEFIB_PULSE_EVENT, 0 );
	/* Create a timer which will send the above pulse event
	 * 5 seconds from now and then repeatedly after that every
	 * 1500 milliseconds.  The event to use has already been filled in
	 * above and is in the variable called 'event'.
	*/
	timer_create (CLOCK_REALTIME, &event, &defib_timerId);
	defib_itime.it_value.tv_sec = MINUTE*5; /* expiry of 5 minutes */
	defib_itime.it_value.tv_nsec = 1;
	defib_itime.it_interval.tv_sec = 0; /* repeating every 5 minutes later */
	defib_itime.it_interval.tv_nsec = MINUTE*5;
	while (1) {
		pthread_mutex_lock(&defib_mutex);
		while (state != STATE_WAIT_DEFIB) {
			pthread_cond_wait(&cond, &defib_mutex);
		}
		pthread_cond_broadcast(&cond);
		pthread_mutex_unlock(&defib_mutex);
		/* start the timeout */
		timer_settime(defib_timerId, 0, &defib_itime, NULL);
		if (MsgReceive( chid, &msg, sizeof(msg), NULL ) == -1) {
			perror("MsgReceive");
			exit(EXIT_FAILURE);
		}
		/* defib if timeout */
		pthread_mutex_lock(&defib_mutex);
		switch (msg.pulse.code) {
		case DEFIB_PULSE_EVENT:
			printf("going to defib\n");
			state = STATE_DEFIB;
			break;
		default:
			printf( "unexpected pulse code: %d\n", msg.pulse.code );
			exit(EXIT_FAILURE);
		}
		pthread_cond_broadcast(&cond);
		pthread_mutex_unlock(&defib_mutex);
	}
	return (NULL);
}
void *defib (void *arg) {
	int fd;
	shmem_t* ptr;

	fd = shm_open( "shock", O_RDWR, 0 );
	if (fd == -1) {
		perror("shm_open");
		exit(EXIT_FAILURE);
	}
	/* get a pointer to a piece of the shared memory */
	ptr = mmap( 0, sizeof(shmem_t), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0 );
	if( ptr == MAP_FAILED )
	{
		perror( "mmap" );
		exit( EXIT_FAILURE );
	}
	/* don't need fd anymore */
	close( fd );

	while (1) {
		/* lock msg resource */
		pthread_mutex_lock(&defib_mutex);
		while (state != STATE_DEFIB) {
			pthread_cond_wait(&cond, &defib_mutex);
		}
		printf("defibed hardware\n");
		state = STATE_CALCULATE;
		sem_post( &ptr->semaphore ); /* shock the patient */
		//exit(0); /* REMOVE */
		pthread_cond_broadcast(&cond);
		pthread_mutex_unlock(&defib_mutex);
		delay(10*1000); /* delay 10s until I can zap again */
	}
	return (NULL);
}
void print_pulse(char pulse) {
	switch (pulse) {
	case RESTING_BOUND:
		printf("     |\n"); /* add 0mV */
		break;
	case UPPER_BOUND:
		printf("      ¯¯¯¯|\n");
		printf("          ·\n"); /* add +1mV */
		printf("      ____|\n");
		break;
	case LOWER_BOUND:
		printf("|¯¯¯¯\n");
		printf(".\n");
		printf("|____\n");
		break;
	default:
		exit(EXIT_FAILURE);
	}
}
/**
 * RIPPED from http://www.cfanatic.com/topic436/
 */
void int_to_binary(int x)
{
	int cnt, mask = 1 << 31;
	for(cnt=1;cnt<=32;++cnt)
	{
		putchar(((x & mask) == 0) ? '0' : '1');
		x <<= 1;
		if(cnt % 8 == 0 && cnt !=32)
			putchar(' ');
		if(cnt == 32)
			putchar('\n');
	}
}

int calculate_bpm(int num_pulses) {
	return (MINUTE*1000)/ (INTERVAL*num_pulses);
}
