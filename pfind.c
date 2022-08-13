//these links helped me :
//https://github.com/petercrona/StsQueue
//https://stackoverflow.com/questions/1271064/how-do-i-loop-through-all-files-in-a-folder-using-c
//https://man7.org/linux/man-pages/man3/readdir.3.html
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <unistd.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
pthread_cond_t wake_up;//to tell the threads that the queue isn't empty anymore
pthread_cond_t start_threads;//to make all the threads start together
pthread_cond_t thread_sleeping;//to tell the main thread that there is a new thread sleeping
static int threads_num;
char *str_key_search;
static int founded_files_num = 0;
static int is_terminated = 0;
static int blocked_threads_num = 0;
static int resting_threads_numb = 0;
typedef struct directory//struct for FIFO
{
    struct directory *next;
    char *value;
} directory;
//pointers for head and tail of the queue
directory *head=NULL;
directory *tail=NULL;
pthread_mutex_t q_mutex;
pthread_mutex_t resting_threads;
pthread_mutex_t prog_term;
pthread_mutex_t files_found;
pthread_mutex_t green_light_mutex;
// a function that returns the value of is_terminated
bool is_terminated_func()
{
    bool res;
    pthread_mutex_lock(&prog_term);
    res = is_terminated;
    pthread_mutex_unlock(&prog_term);
    return res;
}
//increase the number of sleeping threads
void increase_resting_threads_num()
{
    pthread_mutex_lock(&resting_threads);
    resting_threads_numb+=1;
    pthread_mutex_unlock(&resting_threads);
}
//decrease the number of sleeping threads
void decrease_resting_threads_num()
{
    pthread_mutex_lock(&resting_threads);
    resting_threads_numb-=1;
    pthread_mutex_unlock(&resting_threads);
}
// a function that increases the number of blocked threads
void increase_blocked_threads_num()
{
    pthread_mutex_lock(&files_found);
    blocked_threads_num+=1;
    pthread_mutex_unlock(&files_found);
}
directory* create_directory(char* value)
{
    directory *res =(struct directory*)malloc(sizeof(struct directory));
    res->value=value;
    res->next=NULL;
    return res;
}
//push atomicly a new node to the data structure
bool push_node(char *value)
{
    directory * new_node = create_directory(value);
    if(new_node == NULL){
        return false;
    }
    if (head != NULL)
    {
        tail->next = new_node;
        tail = new_node;
    }
    else
    {
        head = new_node;
        tail = new_node;
    }
    return true;
}
//atomicly get the element from the queue
directory* pop_node()
{
    pthread_mutex_lock(&q_mutex);
    if (head != NULL)
    {
        int last_elem = 0;
        if (head->next==NULL)
        {
            last_elem = 1;
        }
        directory *res = head;
        head=head->next;
        if(last_elem) {
            tail = NULL;
        }
        pthread_mutex_unlock(&q_mutex);
        return res;
    }
    else
    {
        pthread_mutex_unlock(&q_mutex);
        return NULL;
    }
}
void free_queue() // frees the queue
{
    while(head!=NULL)
    {
        free(head->value);
        free(head);
        head=head->next;
    }
}
//check the given item is not . or .. or link or refference and then checks if its dir or file
//if it is a directory then we add it to the queue, else check it's name
bool check_item(char * item_path,char *item_name)
{
    char *full_path = malloc(sizeof(char) * PATH_MAX);
    if(strcmp(item_name, ".") == 0 || strcmp(item_name, "..") == 0) // check if its . or ..
    {
        return false;
    }
    sprintf(full_path,"%s/%s",item_path,item_name);
    struct stat item_stat;
    if(lstat(full_path,&item_stat)==-1)
    {
        fprintf(stderr, "ERROR with stat: %s\n", full_path);
        return true;
    }
    if(S_ISDIR(item_stat.st_mode))
    {
        bool res = push_node(full_path);
        if(res == false)
        {
            return true;
        }
        pthread_cond_broadcast(&wake_up);
        if(!(item_stat.st_mode & S_IRUSR || item_stat.st_mode & S_IRGRP))
        {
            printf("problem with directory %s: Permission denied.\n",full_path);
        }
        return !res;
    }
    else
    {
        if(strstr(item_name,str_key_search) != NULL)
        {
            pthread_mutex_lock(&files_found);
            founded_files_num+=1;
            pthread_mutex_unlock(&files_found);
            printf("%s\n", full_path);
            return false;
        }
    }
    return false;
}

//search dir and run the previous function over all the elements in the dir_path dir and error handling
int search(char *dir_path)
{
    DIR *dirc;
    dirc = opendir(dir_path);
    if (dirc == NULL)
    {
        printf("problem with directory %s: Permission denied.\n",dir_path);
        return 0;
    }
    struct dirent *sub_dir;
    int res = false;
    while((sub_dir = readdir(dirc)) != NULL)
    {
        if(check_item(dir_path,sub_dir->d_name) != 0)
            res = true;
    }
    return res;
}

void *thrd_func()
{
    pthread_mutex_lock(&green_light_mutex);
    pthread_cond_wait(&start_threads,&green_light_mutex);
    pthread_mutex_unlock(&green_light_mutex);
    while(1)
    {
        if(is_terminated_func())
        {
            increase_blocked_threads_num();
            pthread_cond_signal(&thread_sleeping);
            sleep(0);
            pthread_exit(NULL);
        }
        directory *dir;
        if((dir = pop_node())!=NULL)
        {
            char* dir_name = dir->value;
            if(search(dir_name))
            {
                free(dir->value);
                free(dir);
                increase_blocked_threads_num();
                pthread_cond_signal(&thread_sleeping);
                sleep(0);
                pthread_exit(NULL);
            }
            free(dir->value);
            free(dir);
        }
        else
        {
            increase_resting_threads_num();
            pthread_cond_broadcast(&thread_sleeping);
            sleep(0);
            pthread_mutex_lock(&q_mutex);
            pthread_cond_wait(&wake_up, &q_mutex);
            decrease_resting_threads_num();
            pthread_mutex_unlock(&q_mutex);
        }
        sleep(0);
    }
}

void mutex_init()
{
    pthread_mutex_init(&q_mutex,NULL);
    pthread_mutex_init(&resting_threads,NULL);
    pthread_mutex_init(&prog_term,NULL);
    pthread_cond_init(&wake_up,NULL);
    pthread_mutex_init(&files_found,NULL);
    pthread_cond_init(&start_threads,NULL);
    pthread_mutex_init(&green_light_mutex,NULL);
}
void mutex_destroy()
{
    pthread_mutex_destroy(&q_mutex);
    pthread_mutex_destroy(&resting_threads);
    pthread_mutex_destroy(&prog_term);
    pthread_mutex_destroy(&files_found);
    pthread_cond_destroy(&wake_up);
    pthread_mutex_destroy(&green_light_mutex);
    pthread_cond_destroy(&start_threads);
}

int main(int argc,char *argv[])
{
    mutex_init();
    if(argc != 4)
    {
        printf("you should provide 4 args\n");
        mutex_destroy();
        exit(1);
    }
    char * root;
    int ret_val = 0;
    DIR* dir = opendir(argv[1]);
    if(dir)
    {
        closedir(dir);
    }
    else
    {
        if (ENOENT == errno || errno == ENOTDIR)
        {
            exit(1);
        }
    }
    root = malloc(sizeof(char) * PATH_MAX);
    strcpy(root,argv[1]);
    push_node(root);
    str_key_search = argv[2];
    threads_num = atoi(argv[3]);
    pthread_t *threads_array = malloc(sizeof(pthread_t)*threads_num);
    if(!threads_array)
    {
        printf("error while making an array for the threads\n");
        exit(1);
    }
    for(int i=0;i<threads_num;i++)
    {
        int err;
        if((err=pthread_create(&(threads_array[i]), NULL, &thrd_func, NULL))!=0)
        {
            printf("error while making the thread\n");
            increase_blocked_threads_num();
        }
    }
    sleep(1);
    pthread_cond_broadcast(&start_threads);
    while(1)
    {
        if(is_terminated_func() || blocked_threads_num == threads_num || resting_threads_numb==threads_num)
        {
            break;
        }
        pthread_mutex_lock(&resting_threads);
        pthread_cond_wait(&thread_sleeping,&resting_threads );
        pthread_mutex_unlock(&resting_threads);
        sleep(0);
    }
    for (int i = 0; i < threads_num; i++)
    {
        pthread_cancel(threads_array[i]);
    }
    printf("finished the search, found %d files\n",founded_files_num);
    if(blocked_threads_num > 0)
    {
        ret_val = 1;
    }
    mutex_destroy();
    free_queue();
    free(threads_array);
    return ret_val;
}
