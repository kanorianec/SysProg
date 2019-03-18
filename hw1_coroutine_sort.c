//#define _XOPEN_SOURCE /* Mac compatibility. */
#include <ucontext.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <time.h> 

#define handle_error(msg) \
   do { perror(msg); exit(EXIT_FAILURE); } while (0)

#define stack_size 1024 * 1024

static void *
allocate_stack_sig()
{
    void *stack = malloc(stack_size);
    stack_t ss;
    ss.ss_sp = stack;
    ss.ss_size = stack_size;
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);
    return stack;
}

/*
 * uctx_main - контекст main, uctx_func - массив контекстов для корутин
 * cor_num - число корутин
 * A - двумерный массив, хранящий данные всех корутин, считанные из файла
 * N - массив количество данных в каждом файле
 * cor_time - массив времени работы каждой корутины
 * is_finished - массив флагов, сигнализирующих о завершении работы корутины

 * start_t, end_t - глобальные переменные для подсчета времени работы
*/

static ucontext_t uctx_main, *uctx_func;

static int cor_num;
static int **A;
static int *N;
static double *cor_time;
static int *is_finished;

static clock_t start_t, end_t;

/*
 * swap_to_next - функция для перехода к следующей корутине, 
 *                включает в себя подсчет времени работы и определение
 *                следующей работающей корутины
*/

static void inline swap_to_next(int id){
    end_t = clock();
    cor_time[id] += (double)(end_t - start_t)/CLOCKS_PER_SEC * 1000000.0;
    int next = (id + 1) % cor_num;
    while (is_finished[next] && next != id)
        next = (next + 1) % cor_num;

    if (next != id){     
        if (swapcontext(&uctx_func[id], &uctx_func[next]) == -1)
            handle_error("swapcontext");
    }
    else if (is_finished[id]){ 
        if (swapcontext(&uctx_func[id], &uctx_func[0]) == -1) 
            handle_error("swapcontext");    
    }
    start_t = clock();
}

// реализация функции сортировки
void NotMyQSort(int *arr, int first, int last, int id)
{
    if (first < last)
    {
        int left = first, right = last, middle = arr[(left + right) / 2];
        do
        {
            while (arr[left] < middle) left++;
            while (arr[right] > middle) right--;
            if (left <= right)
            {
                int temp = arr[left];
                arr[left] = arr[right];
                arr[right] = temp;
                left++;
                right--;
            }
        } while (left <= right);
        NotMyQSort(arr, first, right, id);
        swap_to_next(id);
        NotMyQSort(arr, left, last, id);
        swap_to_next(id);
    }
}

// функция, которую выполняет корутина
static void my_coroutine(int id, char *name)
{
    start_t = clock();
    printf("func%d: started\n", id); 
    
    FILE *F; 
    if ((F = fopen(name, "r")) == NULL){
        printf("Cor %d: File %s not found!\n", id, name);
        handle_error("FILE NOT FOUND");
    } 
    swap_to_next(id);

    int temp;
    
    while ((fscanf(F, "%d", &temp) != EOF)){
        N[id]++;
        swap_to_next(id);
    }       

    rewind(F);
    swap_to_next(id);

    A[id] = malloc(N[id] * sizeof(int));
    swap_to_next(id);

    for (int k = 0; k < N[id]; k++){
           fscanf(F,"%d ",&A[id][k]);
           swap_to_next(id);
    }
        
    NotMyQSort(A[id], 0, N[id] - 1, id);    
    
    is_finished[id] = 1;
    swap_to_next(id);

    printf("func%d: returning; Time of work = %2.lf mkS\n", id, cor_time[id]);
    fclose(F);
}

// алгоритм слияния двух массивов
void Merge(int *A, int SizeA, int *B, int SizeB, int *C){
    int IndA = 0, IndB = 0, IndC = 0;

    while (IndA < SizeA && IndB < SizeB){
        if (A[IndA] < B[IndB]){
            C[IndC] = A[IndA];
            IndA++;
        } else {
            C[IndC] = B[IndB];
            IndB++;
        }
        IndC++;
    }
    
    while (IndA < SizeA){
        C[IndC] = A[IndA];
        IndA++;
        IndC++;
    }
    while (IndB < SizeB){
        C[IndC] = B[IndB];
        IndB++;
        IndC++;
    }
}


int main(int argc, char *argv[]){
    clock_t main_start = clock();
    char **func_stack;

    if (argc > 1){
        cor_num = argc - 1;
        printf("cor_num = %d\n", cor_num);

        A = (int**) malloc(sizeof(int*) * (cor_num));
        N = (int*) calloc(sizeof(int), (cor_num));
        cor_time = (double*) calloc(sizeof(double), cor_num);

        
        func_stack = (char**) malloc(sizeof(char*) * (cor_num));
        uctx_func = (ucontext_t*) malloc(sizeof(ucontext_t) * (cor_num));

        is_finished = (int*) calloc(sizeof(int), (cor_num));

        for (int i = 0; i < cor_num; i++){
            func_stack[i] = allocate_stack_sig();
            if (getcontext(&uctx_func[i]) == -1)
                handle_error("getcontext");
            uctx_func[i].uc_stack.ss_sp = func_stack[i];
            uctx_func[i].uc_stack.ss_size = stack_size;

            if (i < cor_num - 1)
                uctx_func[i].uc_link = &uctx_func[i + 1];
            else
                uctx_func[i].uc_link = &uctx_main;

            makecontext(&uctx_func[i], my_coroutine, 2, i, argv[i + 1]);
        }
    } else {
        printf("No arguments!\n");
        return 0;
    }

    printf("main: swapcontext(&uctx_main, &uctx_func0)\n");
    if (swapcontext(&uctx_main, &uctx_func[0]) == -1)
        handle_error("swapcontext");

    printf("main: exiting\n");

    FILE *FOut = fopen("Sorted.txt","w");
    int **C = (int**) malloc (sizeof(int*) * (argc - 1));
    C[0] = A[0];
    int FullSize = N[0];

    for (int i = 1; i < argc - 1; i++){
        C[i] = (int*) malloc (sizeof(int) * (FullSize + N[i]));
        Merge(C[i-1], FullSize, A[i], N[i], C[i]);
        FullSize += N[i];
    }

    for (int k = 0; k < FullSize; k++){
        fprintf(FOut,"%d ",C[argc - 2][k]);
    }

    fclose(FOut);
    for (int i = 0; i < cor_num; i++){
        free(A[i]);  
        free(func_stack[i]);   
    }
    free(A);
    free(func_stack); 
    free(cor_time);
    free(is_finished);
    free(N);

    clock_t main_end = clock();

    printf("Main: Time of work = %2.lf mkS\n", (double) (main_end - main_start)/CLOCKS_PER_SEC * 1000000);
    
    return 0;
}
