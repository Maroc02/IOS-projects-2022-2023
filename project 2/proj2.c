/*
############################################################################################################
##                                           PROJECT INFORMATION                                          ##
## Subject        –   IOS – Project 2                                                                     ##
## Description    –   https://moodle.vut.cz/pluginfile.php/577383/mod_resource/content/1/zadani-2023.pdf  ##
## Author         –   Marek Čupr, xcuprm01                                                                ##
## Date           –   30. 4. 2023                                                                         ##
############################################################################################################
*/

/*#####################
##     INCLUDES     ##
###################*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>     // islapha()
#include <string.h>    // strlen()

#include <semaphore.h> // sem_init(), sem_destroy(), sem_wait(), sem_post()
#include <sys/wait.h>  // wait()
#include <sys/mman.h>  // mmap()
#include <unistd.h>    // fork()
#include <time.h>      // time()

/*######################
##     CONSTANTS     ##
####################*/

#define ARGS_CNT 6       // 5 arguments + argv[0] (the correct number of arguments)
#define ERR_EXIT_CODE 1  // error code for exit() 

/*####################
##     STRUCTS     ##
##################*/

typedef struct INPUT_ARGS // struct for storing arguments
{
    int NZ; // number of costumers
    int NU; // number of workers
    int TZ; // maximum customer waiting time in ms
    int TU; // maximum worker break time in ms
    int F;  // maximum time till the post closes for new customers in ms
} INPUT_ARGS_t;

typedef struct SEMAPHORES // struct for storing semaphores
{
    sem_t *mutex_sem; // mutex semaphore
    sem_t *output_sem; // semaphore for writing into a File
    sem_t *customer_queue_sem[3]; // semaphore array for each queue (3 in total)
} SEMAPHORES_t;

typedef struct SHARED_MEM // struct for storing shared memory
{
    int *action_cnt; // int to a keep track of the current action count
    int *post_closed; // to know whether the post is closed or not; 1 ~ closed, 0 ~ opened
    int *service; // service/queue number, between <1,3>
    int *queues_cnt[3]; // to keep a track of the amount of people in each queue
} SHARED_MEM_t;

/*###################
##      FILE      ##
#################*/

FILE *file_output; // File for output

/*######################################
##       FUNCTIONS DECLARATION       ##
####################################*/

bool valid_args(int argc, char **argv, INPUT_ARGS_t *args); // function to validate arguments
bool init_sems(SEMAPHORES_t *sem); // function to initialize all the semaphores
bool init_shared_mem(SHARED_MEM_t *mem); // function to initialize all the shared memory
void clear_sems(SEMAPHORES_t *sem); // function to destroy (clean) all the semaphores
void clear_shared_mem(SHARED_MEM_t *mem); // function to destroy (clean) all the shared memory
void process_worker(int idU, INPUT_ARGS_t *args, SEMAPHORES_t *sem, SHARED_MEM_t *mem); // function to handle 'worker' processes
void process_customer(int idZ, INPUT_ARGS_t *args, SEMAPHORES_t *sem, SHARED_MEM_t *mem); // function to handle 'customer' processes
void my_sleep(int min_range, int max_range); // function to usleep a random time between <min_range, max_range>
bool my_isdigit(char* var); // function to check if string is a number 
int random_num(int min_num, int max_num); // function to choose a random number between <min_num, max_num>
int random_queue(SHARED_MEM_t *mem); // function to check whether all the queues are empty or not

/*####################
##       MAIN      ##
##################*/

int main(int argc, char **argv){
    // create structs
    INPUT_ARGS_t input_args; // struct for holding arguments
    SEMAPHORES_t semaphores; // struct for holding semaphores
    SHARED_MEM_t shared_memory; // struct for holding shared memory

    // Arguments handling
    if(!valid_args(argc, argv, &input_args)) // returns FALSE if the arguments are not valid
        exit(ERR_EXIT_CODE);

    // File handling
    if((file_output = fopen("proj2.out", "w")) == NULL){ // check if the file was opened correctly
        fprintf(stderr, "FAILED AT FILE OPENING!\n");
        fclose(file_output); // close the file
        exit(ERR_EXIT_CODE);
    }

    setbuf(file_output, NULL); // set file buffer to NULL (so fprint writes data into the file in the correct order)
    
    // Initialize semaphores
    if(!init_sems(&semaphores)){ // returns FALSE if the semaphore inicialization failed
        fprintf(stderr, "FAILED AT SEMAPHORE INICIALIZATION!");
        clear_sems(&semaphores); // clean all the semaphores
        fclose(file_output); // close the file
        exit(ERR_EXIT_CODE);
    }
    
    // Initialize shared memory
    if (!init_shared_mem(&shared_memory)){ // returns FALSE if the shared memory inicialization failed
        fprintf(stderr, "FAILED AT SHARED MEMORY INICIALIZATION!");
        clear_shared_mem(&shared_memory); // clean all the shared memory
        clear_sems(&semaphores); // clean all the semaphores
        fclose(file_output); // close the file
        exit(ERR_EXIT_CODE);
    }

    // Processes handling

    // Worker process
    for (int idU = 1; idU < input_args.NU + 1; idU++){
        pid_t pid = fork(); // create a child process

        if (pid == -1){ // check if fork() worked correctly or not
            fprintf(stderr, "FAILED AT FORKING!\n");
            clear_shared_mem(&shared_memory); // clean all the shared memory
            clear_sems(&semaphores); // clean all the semaphores
            fclose(file_output); // close the file
            exit(ERR_EXIT_CODE);
        }

        else if (pid == 0) { // the child process
            process_worker(idU, &input_args, &semaphores, &shared_memory);
        }
    }
    
    // Customer process
    for (int idZ = 1; idZ < input_args.NZ + 1; idZ++){
        pid_t pid = fork(); // create a child process

        if (pid == -1){ // check if fork() worked correctly or not
            fprintf(stderr, "FAILED AT FORKING!\n");
            clear_shared_mem(&shared_memory); // clean all the shared memory
            clear_sems(&semaphores); // clean all the semaphores
            fclose(file_output); // close the file
            exit(ERR_EXIT_CODE);
        }

        else if (pid == 0){ // the child process
            process_customer(idZ, &input_args, &semaphores, &shared_memory);
        } 
    }

    // sleep random time between <F/2,F> 
    my_sleep(input_args.F / 2, input_args.F);

    // close the post

    sem_wait(semaphores.mutex_sem);

    sem_wait(semaphores.output_sem);
    fprintf(file_output, "%d: A: closing\n", ++(*shared_memory.action_cnt));
    sem_post(semaphores.output_sem);

    *shared_memory.post_closed = 1; // the post is closed
    sem_post(semaphores.mutex_sem);

    // wait for all the processes to end
    while(wait(NULL) > 0);

    // clean-up everything
    clear_sems(&semaphores); // clean all the semaphores
    clear_shared_mem(&shared_memory); // clean all the shared memory
    fclose(file_output); // close the file
    exit(0);
}


bool valid_args(int argc, char **argv, INPUT_ARGS_t *args){
    
    // check the amount of arguments
    if (argc != ARGS_CNT){
        fprintf(stderr, "INVALID INPUT COUNT! Expected 5 arguments, recieved %d.\n", argc-1);
        return false;
    }

    // check whether all the arguments are numbers (digits) or not
    for (int arg_i = 1; arg_i < 6; arg_i++){ // iterate over all the arguments
        if (!my_isdigit(argv[arg_i])){ // returns false if the argument is not a number
            fprintf(stderr, "IVALID INPUT VALUE! Argument number %d '%s' is not a number!\n", arg_i, argv[arg_i]);
            return false;
        }
    }

    // store all the arguments in the struct
    args->NZ = atoi(argv[1]);
    args->NU = atoi(argv[2]);
    args->TZ = atoi(argv[3]);
    args->TU = atoi(argv[4]);
    args->F = atoi(argv[5]);

    // check the arguments values
    if (args->TZ < 0 || args->TZ > 10000 || args->TU < 0  || args->TU > 100 || args->F < 0 || args->F > 10000 || args->NZ < 0 || args->NU <= 0){
        fprintf(stderr, "IVALID INPUT VALUES!\n");
        return false; // return false if the arguments are valid
    }

    return true; // return true if the arguments are valid
}

bool init_sems(SEMAPHORES_t *sem){

    // mmap all the semaphores
    if((sem->mutex_sem = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED ||
       (sem->output_sem = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED ||
       (sem->customer_queue_sem[0] = mmap(NULL, sizeof(sem_t*), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED ||
       (sem->customer_queue_sem[1] = mmap(NULL, sizeof(sem_t*), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED ||
       (sem->customer_queue_sem[2] = mmap(NULL, sizeof(sem_t*), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED)
       {
            return false; // return false if we failed at mmap
       }

    // initialize all the semaphores
    if (sem_init(sem->mutex_sem, 1, 1) == -1 ||
        sem_init(sem->output_sem, 1, 1) == -1 ||
        sem_init(sem->customer_queue_sem[0], 1, 0) == -1 ||
        sem_init(sem->customer_queue_sem[1], 1, 0) == -1 ||
        sem_init(sem->customer_queue_sem[2], 1, 0) == -1)
        {
            return false; // return false if we failed at semaphores initialization 
        }

    return true; // return true if the semaphore inicialization worked correctly
}

void clear_sems(SEMAPHORES_t *sem){

    // destroy all the semaphores
    sem_destroy(sem->mutex_sem);
    sem_destroy(sem->output_sem);
    sem_destroy(sem->customer_queue_sem[0]);
    sem_destroy(sem->customer_queue_sem[1]);
    sem_destroy(sem->customer_queue_sem[2]);

    // munmap all the semaphores
    munmap(sem->mutex_sem, sizeof(sem_t));
    munmap(sem->output_sem, sizeof(sem_t));
    munmap(sem->customer_queue_sem[0], sizeof(sem_t*));
    munmap(sem->customer_queue_sem[1], sizeof(sem_t*));
    munmap(sem->customer_queue_sem[2], sizeof(sem_t*));
}

bool init_shared_mem(SHARED_MEM_t *mem){

    // initialize all the shared memory
     if ((mem->action_cnt = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0)) == MAP_FAILED ||
         (mem->post_closed = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0)) == MAP_FAILED ||
         (mem->service = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0)) == MAP_FAILED ||
         (mem->queues_cnt[0] = (int *) mmap(NULL, sizeof(int*), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0)) == MAP_FAILED ||
         (mem->queues_cnt[1] = (int *) mmap(NULL, sizeof(int*), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0)) == MAP_FAILED ||
         (mem->queues_cnt[2] = (int *) mmap(NULL, sizeof(int*), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0)) == MAP_FAILED)
     {
        return false; // return false if we failed at the shared memory inicialization
     }

     return true; // return true if the shared memory inicialization worked correctly
}

void clear_shared_mem(SHARED_MEM_t *mem){

    // munmap all the shared memory 
    munmap(mem->action_cnt, sizeof(int));
    munmap(mem->post_closed, sizeof(int));
    munmap(mem->service, sizeof(int));
    munmap(mem->queues_cnt[0], sizeof(int*));
    munmap(mem->queues_cnt[1], sizeof(int*));
    munmap(mem->queues_cnt[2], sizeof(int*));
}

void process_worker(int idU, INPUT_ARGS_t *args, SEMAPHORES_t *sem, SHARED_MEM_t *mem){
    // process 'worker' started
    sem_wait(sem->output_sem);
    fprintf(file_output, "%d: U %d: started\n", ++(*mem->action_cnt), idU);
    sem_post(sem->output_sem);

    // cycle
    while(1){
        sem_wait(sem->mutex_sem); // lock mutex
        *mem->service = random_queue(mem); // choose a random non-empty queue 

        if (*mem->service == -1){ // all the queues are empty

            if ((*mem->post_closed) == 0){ // post is not closed
                // take a break
                sem_wait(sem->output_sem);
                fprintf(file_output, "%d: U %d: taking break\n", ++(*mem->action_cnt), idU);
                sem_post(sem->output_sem);

                sem_post(sem->mutex_sem); // free mutex

                // sleep for random time between <0, TU>
                my_sleep(0, args->TU);

                // finish break
                sem_wait(sem->output_sem);
                fprintf(file_output, "%d: U %d: break finished\n", ++(*mem->action_cnt), idU);
                sem_post(sem->output_sem);
            }
            else { // post is opened
                // going home
                sem_wait(sem->output_sem);
                fprintf(file_output, "%d: U %d: going home\n", ++(*mem->action_cnt), idU);
                sem_post(sem->output_sem);

                sem_post(sem->mutex_sem); // free mutex
                exit(0);
            }
        }

        else{ // there are non-empty queues
            // serving a service
            sem_wait(sem->output_sem);
            fprintf(file_output, "%d: U %d: serving a service of type %d\n", ++(*mem->action_cnt), idU, *mem->service);
            sem_post(sem->output_sem);

            // handle queues
            --(*(mem->queues_cnt[*mem->service - 1])); // decrement the amount of people in the correct queue 
            sem_post(sem->mutex_sem); // free mutex
            sem_post(sem->customer_queue_sem[*mem->service - 1]); // free the correct queue

            // sleep random time between <0,10> 
            my_sleep(0,10);
            
            // service finished
            sem_wait(sem->output_sem);
            fprintf(file_output, "%d: U %d: service finished\n", ++(*mem->action_cnt), idU);
            sem_post(sem->output_sem);
        }
    }
}

void process_customer(int idZ, INPUT_ARGS_t *args, SEMAPHORES_t *sem, SHARED_MEM_t *mem){
    // process 'customer' started
    sem_wait(sem->output_sem);
    fprintf(file_output, "%d: Z %d: started\n", ++(*mem->action_cnt), idZ);
    sem_post(sem->output_sem);

    // sleep random time between <0,TZ> 
    my_sleep(0, args->TZ);

    // lock mutex
    sem_wait(sem->mutex_sem);

    // check whether the post is closed or not
    if(*mem->post_closed == 1){ // post is closed
        // going home
        sem_wait(sem->output_sem);
        fprintf(file_output, "%d: Z %d: going home\n", ++(*mem->action_cnt), idZ);
        sem_post(sem->output_sem);

        sem_post(sem->mutex_sem); // free mutex
        exit(0);
    }

    else{ // post is opened
        *mem->service = random_num(1,3); // random num between <1,3>
        // entering service
        sem_wait(sem->output_sem);
        fprintf(file_output, "%d: Z %d: entering office for a service %d\n", ++(*mem->action_cnt), idZ, *mem->service);
        sem_post(sem->output_sem);

        // enter the correct queue for the chosen service
        ++(*(mem->queues_cnt[*mem->service - 1])); // increase the amount of people in the correct queue 
        sem_post(sem->mutex_sem); // free mutex
        sem_wait(sem->customer_queue_sem[*mem->service - 1]); // lock the correct queue 

        // called by office worker
        sem_wait(sem->output_sem);
        fprintf(file_output, "%d: Z %d: called by office worker \n", ++(*mem->action_cnt), idZ);
        sem_post(sem->output_sem);

        // sleep random time between <0,10> 
        my_sleep(0,10);

        // going home
        sem_wait(sem->output_sem);
        fprintf(file_output, "%d: Z %d: going home \n", ++(*mem->action_cnt), idZ);
        sem_post(sem->output_sem);

        exit(0);
    }
}

/*#####################################
##       FUNCTIONS DEFINITION       ##
###################################*/

void my_sleep(int min_range, int max_range){ // sleep a random time
    // get random seed for rand()
    srand(time(0) * getpid());
    // sleep random time in ms between <min_range, max_range>
    usleep((min_range + (rand() % (max_range - min_range + 1))) * 1000);
}

int random_num(int min_num, int max_num){ // choose random service / random queue
    // get random seed for rand()
    srand(time(0) * getpid());
    // get random number between <min_num, max_num>
    return (min_num + (rand() % max_num));
}

int random_queue(SHARED_MEM_t *mem){ // returns a number of random non-empty queue / returns -1 if all the queues are empty
    *mem->service = random_num(1, 3); // random number between <1,3>  

    for (int i = 0; i < 3; i++){ // iterate over all the queues
        if ((*mem->queues_cnt[*mem->service - 1]) != 0) // random queue is not empty 
            return *mem->service; // return the number of the first not-empty queue

        if (*mem->service == 3) // if we are at last 'empty' queue --> go to the first queue
            *mem->service = 1;
        else
            (*mem->service)++; // otherwise go to the next queue
    }

    return -1; // all the queues are empty
}

bool my_isdigit(char* var){ // checks if all the arguments are numbers
    for (long unsigned int i = 0; i < strlen(var); i++) // iterate over every char of the string
        if (isdigit(var[i]) == 0) // returns ~ 0 if the char is not a number (not between 0-9)
            return false; // argument is not a number
    return true; // argument is a number
}