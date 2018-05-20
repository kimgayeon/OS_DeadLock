#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#define SEMPERM 0600
#define TRUE 1
#define FALSE 0
#define SIZE 100

typedef union   _semun {
            int val;
            struct semid_ds *buf;
            ushort *array;
            } semun;

int initsem (key_t semkey, int n) {
  int status = 0, semid;
  if ((semid = semget (semkey, 1, SEMPERM | IPC_CREAT | IPC_EXCL)) == -1)
  {
      if (errno == EEXIST)
               semid = semget (semkey, 1, 0);
  }
  else
  {
      semun arg;
      arg.val = n;
      status = semctl(semid, 0, SETVAL, arg);
  }
  if (semid == -1 || status == -1)
  {
      perror("initsem failed");
      return (-1);
  }
  return (semid);
}

int p (int semid) {
  struct sembuf p_buf;
  p_buf.sem_num = 0;
  p_buf.sem_op = -1;
  p_buf.sem_flg = SEM_UNDO;
  if (semop(semid, &p_buf, 1) == -1)
  {
     printf("p(semid) failed");
     exit(1);
  }
  return (0);
}

int v (int semid) {
  struct sembuf v_buf;
  v_buf.sem_num = 0;
  v_buf.sem_op = 1;
  v_buf.sem_flg = SEM_UNDO;
  if (semop(semid, &v_buf, 1) == -1)
  {
     printf("v(semid) failed");
     exit(1);
  }
  return (0);
}

// Shared variable by file
void reset(char *fileVar, int num) {
// fileVar라는 이름의 텍스트 화일을 새로 만들고 num값을 기록한다.
 pid_t pid = getpid();

 if(access(fileVar,F_OK) == -1){
   FILE* f = fopen(fileVar, "w");
   fprintf(f,"%d\n", num);
   fclose(f);
 }
}

void Store(char *fileVar, int num) {
// fileVar 화일 값을 지우고 num 값을 append한다.
 FILE* f = fopen(fileVar, "a");
 fprintf(f, "%d\n", num);
 fclose(f);
}

int Load(char *fileVar) {
// fileVar 화일의 마지막 값을 읽어 온다.
 int lastNum;
 FILE* f = fopen(fileVar, "r");
 fseek(f, -2L, 2);
 fscanf(f,"%d", &lastNum);
 fclose(f);
 return lastNum;
}

void add(char *fileVar, int i) {
// fileVar 화일의 마지막 값을 읽어서 i를 더한 후에 이를 끝에 append 한다.
  pid_t pid = getpid();
  int lastNum = Load(fileVar);

  FILE *f=fopen(fileVar, "a+");
  lastNum+=i;
  fprintf(f,"%d\n",lastNum);
  fclose(f);

}

void sub(char *fileVar,int i) {
// fileVar 화일의 마지막 값을 읽어서 i를 뺀 후에 이를 끝에 append 한다.
  pid_t pid = getpid();
  int lastNum = Load(fileVar);
  FILE *f=fopen(fileVar, "a+");
  lastNum-=i;
  fprintf(f,"%d\n", lastNum);
  fclose(f);
}

void historyRead(char doing[SIZE], char *file, char phil[SIZE]){
  FILE *Read = fopen("history", "a");
  printf("%s: %s, File : %s\n", phil, doing, file);
  fprintf(Read, "%s: %s, File : %s\n", phil, doing, file);
  fclose(Read);
}


// Class Lock
typedef struct _lock {
  int semid;
} Lock;

void initLock(Lock *l, key_t semkey) {
  if ((l->semid = initsem(semkey,1)) < 0)
  // 세마포를 연결한다.(없으면 초기값을 1로 주면서 새로 만들어서 연결한다.)
     exit(1);
}

void Acquire(Lock *l) {
  p(l->semid);
}

void Release(Lock *l) {
 v(l->semid);
}

// Class CondVar
typedef struct _cond {
  int semid;
  char *queueLength;
} CondVar;

void initCondVar(CondVar *c, key_t semkey) {
  if ((c->semid = initsem(semkey,0)) < 0)
  // 세마포를 연결한다.(없으면 초기값을 0로 주면서 새로 만들어서 연결한다.)
     exit(1);
}

void Wait(CondVar *c, Lock *lock, char *fileVar, char phil[SIZE]) {
  int waitnum = Load(fileVar);
  ++waitnum;
  Store(fileVar, waitnum);
  historyRead("waiting", fileVar, phil);
  Release(lock);
  p(c->semid);
  Acquire(lock);
  --waitnum;
  Store(fileVar, waitnum);
}

void Signal(CondVar *c, char* fileVar, char phil[SIZE]) {
  int waitnum = Load(fileVar);
  if(waitnum > 0){
    v(c->semid);
    historyRead("await", fileVar, phil);
  }
}

void Take_R1(Lock *l, Lock *l4, CondVar *c, char phil[SIZE], char *fileVar){
  Acquire(l);
  while(Load("R1")==0){  //1이면 젓가락이 있으니까 써도 된다! 0이면 없으니까 wait
    Wait(c, l, "WR1", phil);
  }
  Acquire(l4);
  if(Load("available")>1){ //마지막 젓가락이 아닌 경우
    Store("R1", 0);
    add(fileVar, 1); //allocated_A에 젓가락 +1
    sub("available", 1); //available한 젓가락 -1
    historyRead("take it up", "R1", phil);
  }
  else if(Load("available")==1){ //마지막 젓가락이면
    add(fileVar, 1); //일단 증가시켜서 2개를 가지게 되는지 확인
    if((Load("allocated_A")==2) | (Load("allocated_B")==2) | (Load("allocated_C")==2)){ //한 사람이 젓가락 2개를 가지고있는 경우
      Store("R1", 0);
      sub("available", 1); //available한 젓가락 -1
      historyRead("take it up", "R1", phil);
    }
    else{
      //safe하지 않은 경우 반복
      while(!((Load("available")>1) || ((Load("available")==1) && ((Load("allocated_A")==2) | (Load("allocated_B")==2) | (Load("allocated_C")==2))))){
        Release(l4);
        Wait(c, l, "WR1", phil);
        Acquire(l4);
        printf("wake up in take R1 \n");
      }
      Store("R1", 0);
      sub("available", 1); //available한 젓가락 -1
      historyRead("take it up", "R1", phil);
    }
  }
  Release(l4);
  Release(l);
}

void Take_R2(Lock *l, Lock *l4, CondVar *c, char phil[SIZE], char *fileVar){
  Acquire(l);
  while(Load("R2")==0){
    Wait(c, l, "WR2", phil);
  }
  Acquire(l4);
  if(Load("available")>1){ //마지막 젓가락이 아닌 경우
    Store("R2", 0);
    add(fileVar, 1); //allocated_A에 젓가락 +1
    sub("available", 1); //available한 젓가락 -1
    historyRead("take it up", "R2", phil);
  }
  else if(Load("available")==1){ //마지막 젓가락이면
    add(fileVar, 1); //젓가락수 +1
    if((Load("allocated_A")==2) | (Load("allocated_B")==2) | (Load("allocated_C")==2)){ //한 사람이 젓가락 2개를 가지고있는 경우
      Store("R2", 0);
      sub("available", 1); //available한 젓가락 -1
      historyRead("take it up", "R2", phil);
    }
    else{
      //safe하지 않은 경우 반복
      while(!((Load("available")>1) || ((Load("available")==1) && ((Load("allocated_A")==2) | (Load("allocated_B")==2) | (Load("allocated_C")==2))))){
        Release(l4);
        Wait(c, l, "WR2", phil);
        Acquire(l4);
        printf("wake up in take R2 \n");
      }
      Store("R2", 0);
      sub("available", 1); //available한 젓가락 -1
      historyRead("take it up", "R2", phil);
    }
  }
  Release(l4);
  Release(l);
}

void Take_R3(Lock *l, Lock *l4, CondVar *c, char phil[SIZE], char *fileVar){
  Acquire(l);
  while(Load("R3")==0){
    Wait(c, l, "WR3", phil);
  }
  Acquire(l4);
  if(Load("available")>1){
    Store("R3", 0);
    add(fileVar, 1); //젓가락수 +1
    sub("available", 1); //available한 젓가락 -1
    historyRead("take it up ", "R3", phil);
  }
  else if(Load("available")==1){ //마지막 젓가락이면
    add(fileVar, 1); //젓가락수 +1
    if((Load("allocated_A")==2) | (Load("allocated_B")==2) | (Load("allocated_C")==2)){ //한 사람이 젓가락 2개를 가지고있는 경우
      Store("R3", 0);
      sub("available", 1); //available한 젓가락 -1
      historyRead("take it up", "R3", phil);
    }
    else{
      //safe하지 않은 경우 반복
      while(!((Load("available")>1) || ((Load("available")==1) && ((Load("allocated_A")==2) | (Load("allocated_B")==2) | (Load("allocated_C")==2))))){
        Release(l4);
        Wait(c, l, "WR3", phil);
        Acquire(l4);
        printf("wake up in take R3 \n");
      }
      Store("R3", 0);
      sub("available", 1); //available한 젓가락 -1
      historyRead("take it up", "R3", phil);
    }
  }
  Release(l4);
  Release(l);
}

void Put_R1(Lock *l, Lock *l4, CondVar *c, char phil[SIZE], char *fileVar){
  Acquire(l);
  Acquire(l4);
  Store("R1", 1);  //젓가락 사용안함
  sub(fileVar, 1); // 젓가락수 -1
  add("available", 1); //available한 젓가락 +1
  historyRead("put it down", "R1", phil);
  Signal(c, "WR1", phil);
  Release(l4);
  Release(l);
}

void Put_R2(Lock *l, Lock *l4, CondVar *c, char phil[SIZE], char *fileVar){
  Acquire(l);
  Acquire(l4);
  Store("R2", 1);  //젓가락 사용안함
  sub(fileVar, 1); //젓가락수 -1
  add("available", 1); //available한 젓가락 +1
  historyRead("put it down", "R2", phil);
  Signal(c, "WR2", phil);
  Release(l4);
  Release(l);
}

void Put_R3(Lock *l, Lock *l4, CondVar *c, char phil[SIZE], char *fileVar){
  Acquire(l);
  Acquire(l4);
  Store("R3", 1); //젓가락 사용안함
  sub(fileVar, 1); // 젓가락수 -1
  add("available", 1); //available한 젓가락 +1
  historyRead("put it down", "R3", phil);
  Signal(c, "WR3", phil);
  Release(l4);
  Release(l);
}

void Phil_A(Lock *l1, Lock *l2, Lock *l4, CondVar *c1, CondVar *c2){
  Take_R1(l1, l4, c1, "Phil_A", "allocated_A");  //젓가락 가져간다.
  printf("Phil_A: Think \n");
  sleep(3);
  printf("Phil_A: Think End \n");

  Take_R2(l2, l4, c2, "Phil_A", "allocated_A");
  printf("Phil_A: Eating \n");
  sleep(3);
  printf("Phil_A: Eating End\n");

  Put_R1(l1, l4, c1, "Phil_A", "allocated_A");   // 젓가락 내려놓기.
  Put_R2(l2, l4, c2, "Phil_A", "allocated_A");
}

void Phil_B(Lock *l2, Lock *l3, Lock *l4, CondVar *c2, CondVar *c3){
  Take_R2(l2, l4, c2, "Phil_B", "allocated_B");  //젓가락 가져간다.
  printf("Phil_B: Think \n");
  sleep(3);
  printf("Phil_B: Think End \n");

  Take_R3(l3, l4, c3, "Phil_B", "allocated_B");
  printf("Phil_B: Eating \n");
  sleep(3);
  printf("Phil_B: Eating End\n");

  Put_R2(l2, l4, c2, "Phil_B", "allocated_B");    // 젓가락 내려놓기.
  Put_R3(l3, l4, c3, "Phil_B", "allocated_B");
}

void Phil_C(Lock *l3, Lock *l1, Lock *l4, CondVar *c3, CondVar *c1){
  Take_R3(l3, l4, c3, "Phil_C", "allocated_C");    //젓가락 가져간다.
  printf("Phil_C: Think \n");
  sleep(3);
  printf("Phil_C: Think End \n");

  Take_R1(l1, l4, c1, "Phil_C", "allocated_C");
  printf("Phil_C: Eating \n");
  sleep(3);
  printf("Phil_C: Eating End\n");

  Put_R3(l3, l4, c3, "Phil_C", "allocated_C");     // 젓가락 내려놓기.
  Put_R1(l1, l4, c1, "Phil_C", "allocated_C");
}


void main(int argc, char *argv[]) {
  key_t semkeyLock1 = 20163085;
  key_t semkeyLock2 = 30163085;
  key_t semkeyLock3 = 40163085;
  key_t semkeyCond1 = 50163085;
  key_t semkeyCond2 = 60163085; //conditional value
  key_t semkeyCond3 = 70163085;
  key_t semkeyLock4 = 80163085;

  pid_t pid;
  //l1 ~l3은 젓가락에 대한 lock
  //l4는 젓가락 take,put하는 동작이 mutual exclusive 하도록
  Lock l1, l2, l3, l4;
  CondVar c1, c2, c3;
  int num = argc;

  reset("R1", 1); //젓가락 사용 유무
  reset("R2", 1);
  reset("R3", 1);
  reset("WR1", 0); //기다려야할 곳
  reset("WR2", 0);
  reset("WR3", 0);
  reset("allocated_A", 0); //개인이 가지고 있는 젓가락 수
  reset("allocated_B", 0);
  reset("allocated_C", 0);
  reset("available", 3); //available한 젓가락 수

  pid = getpid();
  initLock(&l1, semkeyLock1); //lock 초기화
  initLock(&l2, semkeyLock2);
  initLock(&l3, semkeyLock3);
  initLock(&l4, semkeyLock4);
  initCondVar(&c1, semkeyCond1); //CondVar 초기화
  initCondVar(&c2, semkeyCond2);
  initCondVar(&c3, semkeyCond3);

  printf("start! pid: %d\n",pid);

  char *person = argv[1] ;
  if (!strcmp(person,"phil_A")){
    for(int i=0; i<3; i++){
      Phil_A(&l1, &l2, &l4, &c1, &c2);
    }
  }
  else if (!strcmp(person,"phil_B")){
    for(int i=0; i<3; i++){
      Phil_B(&l2, &l3, &l4, &c2, &c3);
    }
  }
  else if (!strcmp(person,"phil_C")){
    for(int i=0; i<3; i++){
      Phil_C(&l3, &l1, &l4, &c3, &c1);
    }
  }

  printf("end! pid: %d\n", pid);

  exit(0);
}
