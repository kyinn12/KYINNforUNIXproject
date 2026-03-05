#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include "defs.h"

// --- ERROR HANDLING ---
void err_sys(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

// --- SEMAPHORE UTILS ---
void lock_semaphore(int semid) {
    struct sembuf sb = {0, -1, 0};
    if (semop(semid, &sb, 1) == -1) err_sys("semop LOCK error");
}

void unlock_semaphore(int semid) {
    struct sembuf sb = {0, 1, 0};
    if (semop(semid, &sb, 1) == -1) err_sys("semop UNLOCK error");
}

int find_student_index(SharedData *shm_ptr, const char *id) {
    for (int i = 0; i < MAX_STUDENTS; i++) {
        // Check if slot is not empty AND ID matches
        if (shm_ptr->students[i].id[0] != '\0' && strcmp(shm_ptr->students[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

// --- MAIN PROGRAM ---
int main() {
    int shmid, semid;
    SharedData *shm_ptr;
    char input_id[ID_LEN];
    char input_code[MAX_CODE_LEN + 1];

    // 1. Get Shared Memory (0666 only, do not create)
    shmid = shmget(SHM_KEY, sizeof(SharedData), 0666);
    if (shmid == -1) {
        err_sys("Could not find Shared Memory. Is the Professor running?");
    }
    shm_ptr = (SharedData *)shmat(shmid, NULL, 0);
    if (shm_ptr == (void *)-1) err_sys("shmat error");

    // 2. Get Semaphore
    semid = semget(SEM_KEY, 1, 0666);
    if (semid == -1) {
        err_sys("Could not find Semaphore.");
    }

    printf("--- Student Attendance Client ---\n");

    // 3. User Input (Perform inputs BEFORE locking to prevent freezing the system)
    printf("Enter Student ID (e.g., S101): ");
    scanf("%s", input_id);

    printf("Enter Attendance Code: ");
    scanf("%s", input_code);

    // 4. CRITICAL SECTION START
    lock_semaphore(semid);

    // Check if a session is actually active
    if (shm_ptr->class_session_count == 0) {
        printf("\nERROR: No active session started by the Professor.\n");
        unlock_semaphore(semid);
        shmdt(shm_ptr);
        return 0;
    }

    // Find the student
    int idx = find_student_index(shm_ptr, input_id);

    if (idx == -1) {
        printf("\nERROR: Student ID '%s' not found in the class list.\n", input_id);
    } 
    else {
        Student *s = &shm_ptr->students[idx];

        // Validate Code
        if (strcmp(shm_ptr->current_code, input_code) == 0) {
            
            if (s->last_check_in_status == 'P' || s->last_check_in_status == 'L') {
                 printf("\n---> INFO: %s, you are ALREADY marked present.\n", s->name);
            } else {
                // NEW: CHECK TIME
                time_t now = time(NULL);
                double seconds_elapsed = difftime(now, shm_ptr->session_start_time);

                if (seconds_elapsed > LATE_THRESHOLD_SECONDS) {
                    s->last_check_in_status = 'L'; // Mark LATE
                    printf("\n---> WARNING: You are LATE! (%.0f seconds after start).\n", seconds_elapsed);
                    printf("     Marked as LATE for Session %d.\n", shm_ptr->class_session_count);
                } else {
                    s->last_check_in_status = 'P'; // Mark PRESENT
                    printf("\n---> SUCCESS: Marked PRESENT for Session %d!\n", shm_ptr->class_session_count);
                }
                
                // Both Present and Late count towards "classes_attended"
                s->classes_attended++; 
            }
            // ... (Stats print logic) ...

            // Show Stats
            float percent = 0.0f;
            if (s->total_classes > 0) {
                percent = ((float)s->classes_attended / s->total_classes) * 100.0f;
            }
            printf("Current Stats: %d/%d (%.2f%%)\n", s->classes_attended, s->total_classes, percent);

        } else {
            printf("\n---> FAILED: Incorrect Code. You remain marked ABSENT.\n");
        }
    }

    // 5. CRITICAL SECTION END
    unlock_semaphore(semid);

    // Cleanup local attachment (does not delete the actual shared memory)
    shmdt(shm_ptr);
    return 0;
}
