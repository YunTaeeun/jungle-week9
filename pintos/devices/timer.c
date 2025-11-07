#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* 8254 타이머 칩의 하드웨어 상세 정보는 [8254]를 참조하세요. */

/* 전처리 오류검사 매크로 
전처리 단계에서 컴파일러에게 전달되어 컴파일 시간에 오류를 발생시키는 매크로
이 매크로는 컴파일 시간에 오류를 발생시키므로 런타임에 영향을 주지 않음 */
#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* OS 부팅 이후의 타이머 틱 수. */
static int64_t ticks;

/* 타이머 틱당 루프 수.
   timer_calibrate()에 의해 초기화됩니다. */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/* 8254 프로그래머블 간격 타이머(PIT)를 초당 PIT_FREQ번
   인터럽트하도록 설정하고, 해당 인터럽트를 등록합니다. */
void timer_init (void) {
	/* 8254 입력 주파수를 TIMER_FREQ로 나눈 값,
	   가장 가까운 값으로 반올림. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* 제어 워드: 카운터 0, LSB 다음 MSB, 모드 2, 바이너리. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* 짧은 지연을 구현하는 데 사용되는 loops_per_tick을 보정합니다. */
void timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* loops_per_tick을 하나의 타이머 틱보다 작은
	   가장 큰 2의 거듭제곱으로 근사합니다. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* loops_per_tick의 다음 8비트를 정밀화합니다. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* OS 부팅 이후의 타이머 틱 수를 반환합니다. */
int64_t timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}

/* THEN 이후 경과된 타이머 틱 수를 반환합니다.
   THEN은 timer_ticks()가 반환한 값이어야 합니다. */
int64_t timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}

/* 약 TICKS개의 타이머 틱 동안 실행을 일시 중단합니다. */
void timer_sleep (int64_t ticks) {
	/* 현재 타이머 틱 수를 저장 */
	int64_t start = timer_ticks ();

	ASSERT (intr_get_level () == INTR_ON);
	/* 현재 타이머 틱 수와 시작 타이머 틱 수의 차이가 TICKS보다 작을 때까지 반복 */
	while (timer_elapsed (start) < ticks)
		thread_yield ();
}

/* 약 MS 밀리초 동안 실행을 일시 중단합니다. */
void timer_msleep (int64_t ms) {
	/* 약 MS 밀리초를 타이머 틱으로 변환하여 대기 */
	real_time_sleep (ms, 1000);
}

/* 약 US 마이크로초 동안 실행을 일시 중단합니다. */
void timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* 약 NS 나노초 동안 실행을 일시 중단합니다. */
void timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* 타이머 통계를 출력합니다. */
void timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* 타이머 인터럽트 핸들러. */
static void timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;
	thread_tick ();
}

/* LOOPS번의 반복이 하나 이상의 타이머 틱을 기다리면
   true를 반환하고, 그렇지 않으면 false를 반환합니다. */
static bool too_many_loops (unsigned loops) {
	/* 타이머 틱을 기다립니다. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* LOOPS번의 루프를 실행합니다. */
	start = ticks;
	busy_wait (loops);

	/* 틱 카운트가 변경되었다면, 너무 오래 반복한 것입니다. */
	barrier ();
	return start != ticks;
}

/* 짧은 지연을 구현하기 위해 간단한 루프를 LOOPS번 반복합니다.
   코드 정렬이 타이밍에 큰 영향을 미칠 수 있으므로 NO_INLINE으로
   표시되었습니다. 이 함수가 다른 위치에서 다르게 인라인되면
   결과를 예측하기 어렵기 때문입니다. 
   
   인라인화란 최적화 기법으로
   // 원본 코드
	int add(int a, int b) {
		return a + b;
	}

	void main() {
		int result = add(5, 3); // (A) 함수 호출
	}

	// 인라인화된 후 (컴파일러 내부적으로)
	void main() {
		// int result = add(5, 3); 이 부분이
		int result = 5 + 3; // 함수 본체 코드로 대체됨
	}

	저렇게 함수 호출을 코드 본체로 대체하는 것을 인라인화라고 함
	불필요한 오버헤드를 줄일수 있고
	인라인화 해서 또 추가로 최적화가 가능해 지지만
	크기가 큰 코드를 인라인화 하면 전체 크기가 증가할수 있음
	그리고 인라인화 되버리면 디버깅이 어려워짐 
*/
static void NO_INLINE busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* 약 NUM/DENOM초 동안 대기합니다. */
static void real_time_sleep (int64_t num, int32_t denom) {
	/* NUM/DENOM초를 타이머 틱으로 변환합니다. 내림 처리합니다.

	   (NUM / DENOM) 초
	   ---------------------- = NUM * TIMER_FREQ / DENOM 틱.
	   1 초 / TIMER_FREQ 틱
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* 최소 하나의 전체 타이머 틱을 기다립니다.
		   CPU를 다른 프로세스에 양보하므로 timer_sleep()을 사용합니다. */
		timer_sleep (ticks);
	} else {
		/* 그렇지 않으면, 더 정확한 서브틱 타이밍을 위해
		   busy-wait 루프를 사용합니다. 오버플로우 가능성을 피하기 위해
		   분자와 분모를 1000으로 나눕니다. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
