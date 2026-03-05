#ifndef DEFS_H
#define DEFS_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <time.h>

#define SHM_KEY ((key_t)1234)
#define SEM_KEY ((key_t)5678)

#define MAX_STUDENTS 50
#define ID_LEN 10
#define NAME_LEN 30
#define MAX_CODE_LEN 3

#define LATE_THRESHOLD_SECONDS 60

typedef struct {
    char id[ID_LEN];
    char name[NAME_LEN];
    int grade;
    int total_classes;
    int classes_attended;
    time_t last_check_in_time;
    char last_check_in_status;
} Student;

typedef struct {
    char current_code[MAX_CODE_LEN + 1];
    int class_session_count;
    time_t session_start_time;
    Student students[MAX_STUDENTS];
} SharedData;

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

#endif

