/* MLFQS 로드 평균 테스트
   
   이 테스트는 0부터 59까지 번호가 매겨진 60개의 스레드를 시작합니다.
   #i번 스레드는 (10+i)초 동안 잠들고, 그 다음 60초 동안 루프를 돌며 
   스핀하고, 총 120초가 지날 때까지 다시 잠듭니다. 
   메인 스레드는 10초부터 시작하여 2초마다 로드 평균을 출력합니다.

   아래에 예상 출력이 나열되어 있습니다. 약간의 오차는 허용됩니다.

   이 테스트가 실패하지만 다른 대부분의 테스트는 통과한다면, 
   타이머 인터럽트에서 너무 많은 작업을 하고 있는지 고려해보세요.
   타이머 인터럽트 핸들러가 너무 오래 걸리면, 테스트의 메인 스레드가 
   자신의 작업(메시지 출력)을 수행하고 다음 틱이 도착하기 전에 
   다시 잠들 시간이 충분하지 않을 수 있습니다. 그러면 메인 스레드가 
   틱이 도착할 때 잠들어 있지 않고 준비 상태로 있게 되어, 
   로드 평균이 인위적으로 올라갈 수 있습니다.

   After 0 seconds, load average=0.00.
   After 2 seconds, load average=0.05.
   After 4 seconds, load average=0.16.
   After 6 seconds, load average=0.34.
   After 8 seconds, load average=0.58.
   After 10 seconds, load average=0.87.
   After 12 seconds, load average=1.22.
   After 14 seconds, load average=1.63.
   After 16 seconds, load average=2.09.
   After 18 seconds, load average=2.60.
   After 20 seconds, load average=3.16.
   After 22 seconds, load average=3.76.
   After 24 seconds, load average=4.42.
   After 26 seconds, load average=5.11.
   After 28 seconds, load average=5.85.
   After 30 seconds, load average=6.63.
   After 32 seconds, load average=7.46.
   After 34 seconds, load average=8.32.
   After 36 seconds, load average=9.22.
   After 38 seconds, load average=10.15.
   After 40 seconds, load average=11.12.
   After 42 seconds, load average=12.13.
   After 44 seconds, load average=13.16.
   After 46 seconds, load average=14.23.
   After 48 seconds, load average=15.33.
   After 50 seconds, load average=16.46.
   After 52 seconds, load average=17.62.
   After 54 seconds, load average=18.81.
   After 56 seconds, load average=20.02.
   After 58 seconds, load average=21.26.
   After 60 seconds, load average=22.52.
   After 62 seconds, load average=23.71.
   After 64 seconds, load average=24.80.
   After 66 seconds, load average=25.78.
   After 68 seconds, load average=26.66.
   After 70 seconds, load average=27.45.
   After 72 seconds, load average=28.14.
   After 74 seconds, load average=28.75.
   After 76 seconds, load average=29.27.
   After 78 seconds, load average=29.71.
   After 80 seconds, load average=30.06.
   After 82 seconds, load average=30.34.
   After 84 seconds, load average=30.55.
   After 86 seconds, load average=30.68.
   After 88 seconds, load average=30.74.
   After 90 seconds, load average=30.73.
   After 92 seconds, load average=30.66.
   After 94 seconds, load average=30.52.
   After 96 seconds, load average=30.32.
   After 98 seconds, load average=30.06.
   After 100 seconds, load average=29.74.
   After 102 seconds, load average=29.37.
   After 104 seconds, load average=28.95.
   After 106 seconds, load average=28.47.
   After 108 seconds, load average=27.94.
   After 110 seconds, load average=27.36.
   After 112 seconds, load average=26.74.
   After 114 seconds, load average=26.07.
   After 116 seconds, load average=25.36.
   After 118 seconds, load average=24.60.
   After 120 seconds, load average=23.81.
   After 122 seconds, load average=23.02.
   After 124 seconds, load average=22.26.
   After 126 seconds, load average=21.52.
   After 128 seconds, load average=20.81.
   After 130 seconds, load average=20.12.
   After 132 seconds, load average=19.46.
   After 134 seconds, load average=18.81.
   After 136 seconds, load average=18.19.
   After 138 seconds, load average=17.59.
   After 140 seconds, load average=17.01.
   After 142 seconds, load average=16.45.
   After 144 seconds, load average=15.90.
   After 146 seconds, load average=15.38.
   After 148 seconds, load average=14.87.
   After 150 seconds, load average=14.38.
   After 152 seconds, load average=13.90.
   After 154 seconds, load average=13.44.
   After 156 seconds, load average=13.00.
   After 158 seconds, load average=12.57.
   After 160 seconds, load average=12.15.
   After 162 seconds, load average=11.75.
   After 164 seconds, load average=11.36.
   After 166 seconds, load average=10.99.
   After 168 seconds, load average=10.62.
   After 170 seconds, load average=10.27.
   After 172 seconds, load average=9.93.
   After 174 seconds, load average=9.61.
   After 176 seconds, load average=9.29.
   After 178 seconds, load average=8.98.
*/

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

/* 테스트 시작 시간을 저장하는 전역 변수 */
static int64_t start_time;

/* 로드 스레드 함수 선언 */
static void load_thread (void *seq_no);

/* 생성할 스레드 개수 (0~59번, 총 60개) */
#define THREAD_CNT 60

/* MLFQS 로드 평균 테스트 메인 함수
   
   이 함수는:
   1. 60개의 로드 스레드를 생성
   2. 메인 스레드의 nice 값을 -20으로 설정 (최고 우선순위)
   3. 10초부터 시작하여 2초마다 로드 평균을 측정하고 출력 (총 90회, 178초까지)
 */
void
test_mlfqs_load_avg (void) 
{
  int i;
  
  /* MLFQS 스케줄러가 활성화되어 있는지 확인 */
  ASSERT (thread_mlfqs);

  /* 테스트 시작 시간 기록 */
  start_time = timer_ticks ();
  msg ("Starting %d load threads...", THREAD_CNT);
  
  /* 60개의 로드 스레드 생성 (0번부터 59번까지) */
  for (i = 0; i < THREAD_CNT; i++) 
    {
      char name[16];
      snprintf(name, sizeof name, "load %d", i);
      /* 각 스레드에 순서 번호(seq_no)를 인자로 전달 */
      thread_create (name, PRI_DEFAULT, load_thread, (void *) i);
    }
  msg ("Starting threads took %d seconds.",
       timer_elapsed (start_time) / TIMER_FREQ);
  
  /* 메인 스레드의 nice 값을 -20으로 설정 (가장 높은 우선순위) */
  thread_set_nice (-20);

  /* 10초부터 시작하여 2초마다 로드 평균 측정 및 출력 (총 90회) */
  for (i = 0; i < 90; i++) 
    {
      /* 다음 측정 시간 계산: 시작 시간 + (2*i + 10)초 */
      int64_t sleep_until = start_time + TIMER_FREQ * (2 * i + 10);
      int load_avg;
      
      /* 해당 시간까지 대기 */
      timer_sleep (sleep_until - timer_ticks ());
      
      /* 현재 로드 평균 가져오기 (정수로 저장, 소수점 2자리까지 표현) */
      load_avg = thread_get_load_avg ();
      
      /* 로드 평균 출력 (load_avg는 100배된 값이므로 100으로 나누어 표시) */
      msg ("After %d seconds, load average=%d.%02d.",
           i * 2, load_avg / 100, load_avg % 100);
    }
}

/* 로드 스레드 함수
   
   각 스레드는 다음 순서로 동작합니다:
   1. (10 + seq_no)초 동안 잠듦
   2. 60초 동안 스핀 루프 실행 (CPU를 점유)
   3. 총 120초가 될 때까지 다시 잠듦
   
   seq_no: 스레드 번호 (0~59)
 */
static void
load_thread (void *seq_no_) 
{
  int seq_no = (int) seq_no_;
  
  /* 첫 번째 슬립 시간: (10 + seq_no)초 */
  int sleep_time = TIMER_FREQ * (10 + seq_no);
  
  /* 스핀 종료 시간: 첫 번째 슬립 시간 + 60초 */
  int spin_time = sleep_time + TIMER_FREQ * THREAD_CNT;
  
  /* 스레드 종료 시간: 총 120초 (THREAD_CNT * 2 = 60 * 2) */
  int exit_time = TIMER_FREQ * (THREAD_CNT * 2);

  /* 1단계: (10 + seq_no)초까지 잠듦 */
  timer_sleep (sleep_time - timer_elapsed (start_time));
  
  /* 2단계: 60초 동안 스핀 루프 (CPU를 점유하여 로드 평균에 기여) */
  while (timer_elapsed (start_time) < spin_time)
    continue;
  
  /* 3단계: 120초까지 잠듦 (스레드 종료 대기) */
  timer_sleep (exit_time - timer_elapsed (start_time));
}
