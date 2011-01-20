/*********************************************************
 Filename: share.h
 Version: 1.0
 Author Name: Mike Niyonkuru
 Purpose: store definitions shared between software and hardware
 ************************************************************/

#include <semaphore.h>
#include <pthread.h>
#include <sys/dispatch.h>

#define NAME_LEN 10
#define NUM_PIECES 3
#define TIMER_PULSE_EVENT (_PULSE_CODE_MINAVAIL + 7)
#define DEFIB_PULSE_EVENT (_PULSE_CODE_MINAVAIL + 8)
#define HW_TIMEOUT_EVENT  (_PULSE_CODE_MINAVAIL + 9)

#define INTERVAL 40
#define MINUTE 60
#define TIMEOUT_REGISTER 5

#define LOW_MIN 40
#define LOW_MAX 49

#define NORM_MIN 50
#define NORM_MAX 89

#define HIGH_MIN 90
#define HIGH_MAX 99

/* heart pulse values */
#define UPPER_BOUND 1
#define RESTING_BOUND 0
#define LOWER_BOUND -1

#define NORM_HEARTBEAT 70

typedef struct {
    sem_t   semaphore;
    char pulse;
} shmem_t;

typedef struct {
	char pulse;
    unsigned char works;
} sensor_t;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t defib_mutex = PTHREAD_MUTEX_INITIALIZER;

/* union of all different types of message(s) we will receive
*/
typedef union {
    struct _pulse pulse;
} pulse_t;

#define STATE_CALCULATE 0
#define STATE_WAIT_DEFIB 1
#define STATE_DEFIB 2
