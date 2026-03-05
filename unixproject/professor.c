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

// --- SEMAPHORE OPERATIONS ---
int init_semaphore(int semid) {
    union semun arg;
    arg.val = 1; 
    if (semctl(semid, 0, SETVAL, arg) == -1) err_sys("semctl SETVAL error");
    return 0;
}

void lock_semaphore(int semid) {
    struct sembuf sb = {0, -1, 0};
    if (semop(semid, &sb, 1) == -1) err_sys("semop LOCK error");
}

void unlock_semaphore(int semid) {
    struct sembuf sb = {0, 1, 0};
    if (semop(semid, &sb, 1) == -1) err_sys("semop UNLOCK error");
}

// --- HELPER FUNCTIONS ---
void generate_random_code(char *buffer, size_t len) {
    srand((unsigned int)time(NULL) ^ getpid());
    for (size_t i = 0; i < len; i++) buffer[i] = (rand() % 10) + '0';
    buffer[len] = '\0';
}

int find_empty_slot(SharedData *shm_ptr) {
    for (int i = 0; i < MAX_STUDENTS; i++) {
        if (shm_ptr->students[i].id[0] == '\0') return i;
    }
    return -1;
}

// --- FILE I/O: LOAD FRESH LIST ---
void load_initial_list(SharedData *shm_ptr) {
    FILE *file = fopen("student_list.txt", "r");
    if (file == NULL) {
        printf("WARNING: 'student_list.txt' not found. Starting empty.\n");
        return;
    }

    char line[100];
    int count = 0;
    printf("Initializing from student_list.txt...\n");

    while (fgets(line, sizeof(line), file) != NULL && count < MAX_STUDENTS) {
        line[strcspn(line, "\n")] = 0; // Remove newline
        char *id = strtok(line, ",");
        char *name = strtok(NULL, ",");

        if (id && name) {
            Student *s = &shm_ptr->students[count];
            strncpy(s->id, id, ID_LEN - 1);
            strncpy(s->name, name, NAME_LEN - 1);
            s->total_classes = 0;
            s->classes_attended = 0;
            s->last_check_in_status = 'N';
            count++;
        }
    }
    fclose(file);
    printf("Loaded %d new students.\n", count);
}

// --- FILE I/O: RESUME FROM BACKUP ---
int load_backup(SharedData *shm_ptr) {
    FILE *fp = fopen("attendance_record.csv", "r");
    if (fp == NULL) return 0; // No backup found

    char line[200];
    int count = 0;
    int max_sessions = 0;

    printf("Backup found! Restoring previous session data...\n");
    fgets(line, sizeof(line), fp); // Skip Header

    while (fgets(line, sizeof(line), fp) != NULL && count < MAX_STUDENTS) {
        // CSV Format: ID,Name,Total,Attended,Percent,Status
        char *id = strtok(line, ",");
        char *name = strtok(NULL, ",");
        char *total = strtok(NULL, ",");
        char *attended = strtok(NULL, ",");
        strtok(NULL, ","); // Skip percent
        char *status = strtok(NULL, ","); // Get status

        if (id && name && total && attended) {
            Student *s = &shm_ptr->students[count];
            strncpy(s->id, id, ID_LEN-1);
            strncpy(s->name, name, NAME_LEN-1);
            s->total_classes = atoi(total);
            s->classes_attended = atoi(attended);
            
            // Handle Status (remove possible newline)
            if (status) s->last_check_in_status = status[0];
            else s->last_check_in_status = 'N';

            if (s->total_classes > max_sessions) max_sessions = s->total_classes;
            count++;
        }
    }
    
    shm_ptr->class_session_count = max_sessions;
    fclose(fp);
    printf("Restored %d students. Resuming Session %d.\n", count, max_sessions);
    return 1; // Success
}

// --- FILE I/O: SAVE DATA ---
void save_data(SharedData *shm_ptr) {
    FILE *fp = fopen("attendance_record.csv", "w");
    if (fp == NULL) {
        perror("Save failed");
        return;
    }
    fprintf(fp, "Student_ID,Name,Total_Classes,Attended,Percentage,Last_Status\n");

    for (int i = 0; i < MAX_STUDENTS; i++) {
        Student *s = &shm_ptr->students[i];
        if (s->id[0] != '\0') {
            float pct = 0.0f;
            if (s->total_classes > 0) pct = ((float)s->classes_attended / s->total_classes) * 100.0f;
            
            // Ensure status char is valid
            char status = (s->last_check_in_status) ? s->last_check_in_status : 'N';

            fprintf(fp, "%s,%s,%d,%d,%.2f,%c\n", 
                s->id, s->name, s->total_classes, s->classes_attended, pct, status);
        }
    }
    fclose(fp);
    printf("Data saved to 'attendance_record.csv'.\n");
}

// --- MAIN MENU ACTIONS ---
void print_report(SharedData *shm_ptr) {
    printf("\n=== SESSION %d REPORT (Code: %s) ===\n", shm_ptr->class_session_count, shm_ptr->current_code);
    printf("%-10s %-20s %-10s %s\n", "ID", "Name", "Status", "Att %");
    printf("------------------------------------------------\n");

    for (int i = 0; i < MAX_STUDENTS; i++) {
        Student *s = &shm_ptr->students[i];
        if (s->id[0] != '\0') {
            float pct = 0.0f;
            if (s->total_classes > 0) pct = ((float)s->classes_attended / s->total_classes) * 100.0f;
            
            char status_str[10];
            if (s->last_check_in_status == 'P') strcpy(status_str, "PRESENT");
            else if (s->last_check_in_status == 'A') strcpy(status_str, "ABSENT");
            else if (s->last_check_in_status == 'L') strcpy(status_str, "LATE");
            else strcpy(status_str, "N/A");

            printf("%-10s %-20s %-10s %.1f%%\n", s->id, s->name, status_str, pct);
        }
    }
    printf("------------------------------------------------\n");
}


void modify_attendance(SharedData *shm_ptr, int semid) {
    char input_id[ID_LEN];
    char new_status;
    
    lock_semaphore(semid);

    printf("\n--- Modify Student Attendance ---\n");
    printf("Enter Student ID to modify: ");
    scanf("%s", input_id);

    int idx = -1;
    // Find the student
    for (int i = 0; i < MAX_STUDENTS; i++) {
        if (shm_ptr->students[i].id[0] != '\0' && strcmp(shm_ptr->students[i].id, input_id) == 0) {
            idx = i;
            break;
        }
    }

    if (idx == -1) {
        printf("ERROR: Student ID '%s' not found.\n", input_id);
        unlock_semaphore(semid);
        return;
    }

    Student *s = &shm_ptr->students[idx];
    printf("Student Found: %s\n", s->name);
    printf("Current Status: %c (Attended: %d/%d)\n", 
           s->last_check_in_status, s->classes_attended, s->total_classes);
    
    printf("Set status to [P]resent, [A]bsent, [L}ate? ");
    scanf(" %c", &new_status);
    while(getchar()!='\n'); // Clear buffer

    // LOGIC: Update the count based on the change
    if ((new_status == 'P' || new_status == 'p') && s->last_check_in_status != 'P') {
        // If changing from Absent/Null to Present -> Increment count
        s->classes_attended++;
        s->last_check_in_status = 'P';
        printf("SUCCESS: Marked %s as PRESENT.\n", s->name);

    } else if ((new_status == 'A' || new_status == 'a') && s->last_check_in_status == 'P') {
        // If changing from Present to Absent -> Decrement count
        if (s->classes_attended > 0) s->classes_attended--;
        s->last_check_in_status = 'A';
        printf("SUCCESS: Marked %s as ABSENT.\n", s->name);
        
    } else {
        printf("No change made (Status is already %c).\n", s->last_check_in_status);
    }

    unlock_semaphore(semid);
}

// --- MAIN ---
int main() {
    int shmid, semid;
    SharedData *shm_ptr;

    // 1. Setup Resources
    shmid = shmget(SHM_KEY, sizeof(SharedData), IPC_CREAT | 0666);
    if (shmid == -1) err_sys("shmget");
    shm_ptr = (SharedData *)shmat(shmid, NULL, 0);
    if (shm_ptr == (void *)-1) err_sys("shmat");

    semid = semget(SEM_KEY, 1, IPC_CREAT | 0666);
    init_semaphore(semid);

    // 2. Initialize Data
    lock_semaphore(semid);
    if (shm_ptr->class_session_count == 0) {
        memset(shm_ptr, 0, sizeof(SharedData));
        // Try to load backup, if fail, load initial list
        if (!load_backup(shm_ptr)) {
            load_initial_list(shm_ptr);
        }
    }
    unlock_semaphore(semid);

    // 3. Loop
    while (1) {
        char c;
        printf("\n Professor: [N]ew Session | [M]odify| [R]eport | [Q]uit: ");
        scanf(" %c", &c);
        while(getchar()!='\n');

        if (c == 'N' || c == 'n') {
            lock_semaphore(semid);
            shm_ptr->class_session_count++;
            shm_ptr->session_start_time = time(NULL);
            for (int i=0; i<MAX_STUDENTS; i++) {
                if(shm_ptr->students[i].id[0] != '\0') {
                    shm_ptr->students[i].total_classes++;
                    shm_ptr->students[i].last_check_in_status = 'A'; // Reset to Absent
                }
            }
            generate_random_code(shm_ptr->current_code, MAX_CODE_LEN);
            printf(">>> NEW CODE: %s <<<\n", shm_ptr->current_code);
            unlock_semaphore(semid);
        } 
        else if (c == 'R' || c == 'r') {
            lock_semaphore(semid);
            print_report(shm_ptr);
            unlock_semaphore(semid);
        }
        else if (c == 'M' || c == 'm') {
            modify_attendance(shm_ptr, semid);
        }
        else if (c == 'Q' || c == 'q') {
            lock_semaphore(semid);
            save_data(shm_ptr); // Save before exit
            unlock_semaphore(semid);
            break;
        }
    }

    // 4. Clean
    shmdt(shm_ptr);
    shmctl(shmid, IPC_RMID, NULL);
    semctl(semid, 0, IPC_RMID);
    printf("Bye.\n");
    return 0;
}
