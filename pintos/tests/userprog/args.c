/* 커맨드 라인 인자를 출력합니다.
   이 프로그램은 모든 args-* 테스트에서 사용됩니다. 각 args-* 테스트는
   출력 결과에 따라 다르게 채점됩니다. */

#include "tests/lib.h"

int
main (int argc, char *argv[]) 
{
  int i;  // 반복문 인덱스 변수

  test_name = "args";  // 테스트 이름 설정

  /* argv 포인터가 8바이트(word)로 정렬되어 있는지 확인
     (x86-64에서 포인터는 8바이트이므로 8의 배수 주소에 있어야 함)
     & 7 연산은 하위 3비트를 확인하는 것 (8의 배수면 0이어야 함) */
  if (((unsigned long long) argv & 7) != 0)
    msg ("argv and stack must be word-aligned, actually %p", argv);

  msg ("begin");  // 테스트 시작 메시지 출력
  msg ("argc = %d", argc);  // 인자 개수 출력
  
  /* argv 배열을 순회하면서 각 인자 출력
     i <= argc 조건: argv[argc]는 NULL이어야 하므로 포함 */
  for (i = 0; i <= argc; i++)
    if (argv[i] != NULL)  // 인자가 존재하면
      msg ("argv[%d] = '%s'", i, argv[i]);  // 인자 내용 출력
    else  // 인자가 NULL이면
      msg ("argv[%d] = null", i);  // null 출력
  
  msg ("end");  // 테스트 종료 메시지 출력

  return 0;  // 정상 종료
}
