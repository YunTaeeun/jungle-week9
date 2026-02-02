# 🧵 Pintos: Thread Scheduling & Synchronization

KAIST Pintos 프레임워크 기반의 스레드 스케줄링 및 동기화 메커니즘 구현 프로젝트입니다.

## 📌 프로젝트 개요

교육용 운영체제 Pintos의 스레드 관리 시스템을 구축하는 프로젝트입니다. 스케줄링 정책, 우선순위 기반 스케줄링, Priority Donation, 그리고 다양한 동기화 기법을 직접 구현하여 멀티스레딩 환경의 핵심 개념을 학습했습니다.

> **Based on**: [KAIST Pintos](https://casys-kaist.github.io/pintos-kaist/) - 64-bit x86 아키텍처

## 🎯 주요 구현 기능

### 1. **Alarm Clock**
- Busy waiting 제거를 통한 효율적인 대기 메커니즘
- Sleep queue를 활용한 스레드 관리
- 타이머 인터럽트 핸들러 최적화

### 2. **Priority Scheduling**
- 우선순위 기반 스레드 스케줄링
- Ready queue에서 최고 우선순위 스레드 선택
- 선점형 스케줄링 (Preemptive Scheduling)

### 3. **Priority Donation** ⭐
복잡한 Priority Inversion 문제를 해결하기 위한 구현:
- **Single Donation**: 단일 락에서의 우선순위 기부
- **Multiple Donation**: 여러 락에 대한 동시 기부
- **Nested Donation**: 중첩된 락 상황에서의 기부 전파
- **Chain Donation**: 여러 스레드를 거쳐 전파되는 우선순위

### 4. **Advanced Synchronization**
- Semaphore 우선순위 기반 동작
- Condition Variable 우선순위 지원
- Lock 메커니즘 구현

## 🛠️ 기술 스택

- **Language**: C
- **Architecture**: x86-64
- **Emulator**: QEMU
- **Development**: Docker

## 📂 주요 구현 파일

```
pintos/threads/
├── thread.c           # 스레드 핵심 로직
├── synch.c            # 동기화 primitives
└── interrupt.c        # 인터럽트 처리
```

## 🚀 빌드 및 테스트

### 빌드
```bash
cd pintos/threads
make
```

### 테스트 실행
```bash
# Alarm 테스트
cd build
for t in alarm-{single,multiple,simultaneous,priority}; do 
  make tests/threads/$t.result
done

# Priority Donation 테스트
for t in priority-{donate-one,donate-multiple,donate-nest,donate-chain}; do 
  make tests/threads/$t.result
done

# 전체 테스트
make check
```

## ✅ 구현 검증

모든 스레드 관련 테스트 케이스 통과:
- ✓ Alarm Clock (6/6)
- ✓ Priority Scheduling (5/5)
- ✓ Priority Donation (8/8)
- ✓ Advanced Scheduler

## 💡 핵심 학습 내용

### Priority Inversion 문제
```
상황: 낮은 우선순위 스레드(L)가 락 보유 중
     → 높은 우선순위 스레드(H)가 대기
     → 중간 우선순위 스레드(M)가 L을 선점
     → H가 무한정 대기!

해결: Priority Donation
     → H의 우선순위를 L에게 일시적으로 기부
     → L이 빠르게 실행 완료
     → H가 락 획득
```

### 동기화 메커니즘
- **Semaphore**: 카운팅 기반 리소스 접근 제어
- **Lock**: 상호 배제(Mutual Exclusion) 보장
- **Condition Variable**: 특정 조건 대기/신호

### 인터럽트 처리
- 타이머 인터럽트를 통한 스케줄링
- 인터럽트 컨텍스트에서의 안전한 작업 처리
- Critical Section 보호

## 📚 상세 문서

프로젝트 진행 중 작성한 학습 자료:
- [`Priority_Donation_정리.md`](./Priority_Donation_정리.md) - Priority Donation 구현 상세 가이드

## 🔗 참고 자료

- [Pintos 공식 문서](https://casys-kaist.github.io/pintos-kaist/)
- [KAIST CS330 - Operating Systems](https://casys-kaist.github.io/)

## 📝 License

This project is based on the Stanford Pintos project, modified by KAIST.

---

**Note**: 이 프로젝트는 운영체제의 스레드 스케줄링과 동기화 메커니즘 학습을 목적으로 작성되었습니다.
