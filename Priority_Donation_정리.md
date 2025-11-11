# Priority Donation 구현 정리

## 테스트 명령어

### Alarm 테스트
```bash
cd .. && make clean && make && cd build 
for t in alarm-{single,multiple,simultaneous,priority,zero,negative}; do make tests/threads/$t.result; done
```

### Priority 테스트 (기본)
```bash
for t in priority-{change,fifo,preempt,sema,condvar}; do make tests/threads/$t.result; done
```

### Priority Donation 테스트 (전체)
```bash
for t in priority-{change,donate-one,donate-multiple,donate-multiple2,donate-nest,donate-sema,donate-lower,fifo,preempt,sema,condvar,donate-chain}; do make tests/threads/$t.result; done
```

### 한 번에 빌드 + 테스트
```bash
cd .. && make clean && make && cd build && for t in priority-{change,fifo,preempt,sema,condvar}; do make tests/threads/$t.result; done
```

---

## 코딩 컨벤션

- **함수명, 변수명**: 소문자 + 언더스코어(`_`)
- 예시:
  - `int priority;`
  - `int original_priority;`
  - `void donate_priority();`

---

## 인터럽트 핸들러와 인터럽트 컨텍스트 개념

### 1. 인터럽트(Interrupt)란?

**정의:** CPU가 실행 중인 작업을 중단하고 긴급한 이벤트를 처리한 후, 원래 작업으로 돌아가는 메커니즘

**종류:**
- **하드웨어 인터럽트**: 타이머, 키보드, 디스크 등 외부 장치가 발생
- **소프트웨어 인터럽트**: 시스템 콜, 예외 등

**예시:**
```
CPU가 스레드 A 실행 중...
  ↓
타이머가 10ms마다 인터럽트 발생!
  ↓
CPU가 스레드 A 실행 멈춤
  ↓
인터럽트 핸들러(timer_interrupt) 실행
  ↓
sleep_list 확인하고 깨울 스레드 처리
  ↓
인터럽트 핸들러 종료
  ↓
스레드 A로 복귀 (또는 다른 스레드로 전환)
```

---

### 2. 인터럽트 핸들러(Interrupt Handler)

**정의:** 인터럽트가 발생했을 때 실행되는 함수

**Pintos 예시:**
```c
// devices/timer.c
static void timer_interrupt (struct intr_frame *args UNUSED) {
    ticks++;  // 시간 증가
    thread_tick();
    
    // sleep_list에서 깨울 스레드 확인
    while (!list_empty(&sleep_list)) {
        struct thread *t = ...;
        if (t->wakeup_tick <= ticks) {
            thread_unblock(t);  // 스레드 깨우기
        }
    }
}
```

**특징:**
- CPU가 일반 코드 실행을 멈추고 인터럽트 핸들러 실행
- 매우 빠르게 실행되어야 함 (다른 인터럽트를 막을 수 있음)
- 특정 작업(예: `thread_yield()`)은 금지됨

---

### 3. 인터럽트 컨텍스트(Interrupt Context)

**정의:** 인터럽트 핸들러가 실행되는 동안의 CPU 상태

**일반 컨텍스트 vs 인터럽트 컨텍스트:**

| 구분 | 일반 컨텍스트 | 인터럽트 컨텍스트 |
|------|--------------|------------------|
| 상태 | 정상적인 스레드 실행 중 | 인터럽트 처리 중 |
| 스케줄링 | `thread_yield()` 가능 | `thread_yield()` **불가능** ❌ |
| 블로킹 | 대기(sleep) 가능 | 대기(sleep) **불가능** ❌ |
| 락 획득 | 가능 | **불가능** ❌ |
| 시간 | 무제한 | 최소한의 시간만 |

**확인 방법:**
```c
bool intr_context(void);  // 인터럽트 컨텍스트면 true 반환
```

---

### 4. 왜 인터럽트 컨텍스트에서 `thread_yield()` 불가능?

#### 문제 상황:
```c
static void timer_interrupt(struct intr_frame *args) {
    ticks++;
    
    struct thread *t = thread_unblock(깨운 스레드);
    if (t->priority > current->priority) {
        thread_yield();  // ❌ 여기서 호출하면 패닉!
    }
}
```

#### 이유:

1. **스택 문제**
   - 인터럽트는 현재 스레드의 스택을 사용
   - `thread_yield()`는 컨텍스트 스위칭 → 스택 교체
   - 인터럽트 핸들러가 끝나지 않았는데 스택을 바꾸면 복귀 불가능

2. **인터럽트 중첩 문제**
   - 인터럽트 처리 중 스케줄링하면 다른 인터럽트가 발생할 수 있음
   - 인터럽트 핸들러는 빠르게 끝나야 함

3. **Pintos의 설계**
   - `thread_yield()`는 명시적으로 인터럽트 컨텍스트에서 호출 금지
   - `ASSERT(!intr_context())` 체크

---

### 5. 해결 방법: `intr_yield_on_return()`

#### 올바른 방법:
```c
static void timer_interrupt(struct intr_frame *args) {
    ticks++;
    
    bool yield_needed = false;
    struct thread *cur = thread_current();
    
    while (!list_empty(&sleep_list)) {
        struct thread *t = ...;
        if (t->wakeup_tick <= ticks) {
            thread_unblock(t);
            if (t->priority > cur->priority) {
                yield_needed = true;  // 표시만 하기
            }
        }
    }
    
    if (yield_needed) {
        intr_yield_on_return();  // ✅ 인터럽트 종료 후 yield
    }
}
```

#### `intr_yield_on_return()` 동작 원리:

```c
// threads/interrupt.c
void intr_yield_on_return(void) {
    ASSERT(intr_context());  // 인터럽트 컨텍스트에서만 호출 가능
    yield_on_return = true;   // 플래그 설정
}

// 인터럽트 핸들러가 끝나면...
static void intr_handler(struct intr_frame *frame) {
    // ... 인터럽트 처리 ...
    
    // 인터럽트 종료 시점
    if (yield_on_return) {
        thread_yield();  // ✅ 이제는 안전하게 호출 가능!
    }
}
```

**흐름:**
1. 인터럽트 발생 → `timer_interrupt()` 실행
2. 스레드를 깨우고 `intr_yield_on_return()` 호출
3. 플래그(`yield_on_return = true`)만 설정
4. 인터럽트 핸들러 종료 → 스택이 안전한 상태로 복귀
5. **인터럽트 종료 직후** `thread_yield()` 실행 ✅

---

### 6. Priority Donation에서의 적용

#### `sema_up()` 함수:
```c
void sema_up(struct semaphore *sema) {
    // ... 세마포어 값 증가 ...
    
    struct thread *t = (깨운 스레드);
    thread_unblock(t);
    
    // 인터럽트 컨텍스트 확인 후 처리
    if (t->priority > thread_current()->priority) {
        if (intr_context()) {
            intr_yield_on_return();  // 인터럽트에서 호출되면
        } else {
            thread_yield();          // 일반에서 호출되면
        }
    }
}
```

#### 왜 `sema_up()`이 인터럽트에서 호출?

**타이머 알람 예시:**
```c
void timer_sleep(int64_t ticks) {
    // 스레드가 세마포어에서 대기
    sema_down(&sema);
}

static void timer_interrupt(...) {
    // 인터럽트 핸들러에서 스레드 깨우기
    sema_up(&sema);  // ← 인터럽트 컨텍스트!
}
```

---

### 7. 실제 발생한 에러

#### 에러 메시지:
```
PANIC at ../../threads/thread.c:389 in thread_yield(): 
assertion `!intr_context ()' failed.
```

#### 에러 발생 코드 (수정 전):
```c
// timer.c (340줄)
static void timer_interrupt(...) {
    while (!list_empty(&sleep_list)) {
        thread_unblock(t);
        if (t->priority > current->priority) {
            thread_yield();  // ❌ 인터럽트 중에 호출!
        }
    }
}

// synch.c (169줄)
void sema_up(struct semaphore *sema) {
    thread_unblock(t);
    if (t->priority > current->priority) {
        thread_yield();  // ❌ 인터럽트에서 호출될 수 있음!
    }
}
```

#### 수정 후:
```c
// timer.c
if (yield_needed) {
    intr_yield_on_return();  // ✅
}

// synch.c
if (intr_context()) {
    intr_yield_on_return();  // ✅
} else {
    thread_yield();
}
```

---

## 핵심 정리

### 규칙
1. **인터럽트 핸들러는 빠르게 실행**
2. **인터럽트 컨텍스트에서는 `thread_yield()` 직접 호출 금지**
3. **대신 `intr_yield_on_return()` 사용**

### 체크리스트
- [ ] `intr_context()` 확인
- [ ] 인터럽트 컨텍스트면 `intr_yield_on_return()` 사용
- [ ] 일반 컨텍스트면 `thread_yield()` 사용

### 함수 비교

| 함수 | 사용 시점 | 동작 |
|------|----------|------|
| `thread_yield()` | 일반 컨텍스트 | 즉시 스케줄링 |
| `intr_yield_on_return()` | 인터럽트 컨텍스트 | 인터럽트 종료 후 스케줄링 |
| `intr_context()` | 어디서나 | 현재 컨텍스트 확인 |

---