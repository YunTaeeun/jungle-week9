#include <string.h>
#include <debug.h>

/* Copies SIZE bytes from SRC to DST, which must not overlap.
   Returns DST. */
/* SIZE 바이트를 SRC에서 DST로 복사합니다. 겹치면 안 됩니다.
   DST를 반환합니다. */
void* memcpy(void* dst_, const void* src_, size_t size)
{
    unsigned char* dst = dst_;        // 목적지 포인터를 unsigned char로 캐스팅
    const unsigned char* src = src_;  // 소스 포인터를 unsigned char로 캐스팅

    ASSERT(dst != NULL || size == 0);  // 목적지가 NULL이 아니거나 크기가 0이어야 함
    ASSERT(src != NULL || size == 0);  // 소스가 NULL이 아니거나 크기가 0이어야 함

    while (size-- > 0)    // 크기가 0이 될 때까지 반복
        *dst++ = *src++;  // 소스에서 목적지로 바이트 복사 후 포인터 증가

    return dst_;  // 목적지 포인터 반환
}

/* Copies SIZE bytes from SRC to DST, which are allowed to
   overlap.  Returns DST. */
/* SIZE 바이트를 SRC에서 DST로 복사합니다. 겹치는 것이 허용됩니다.
   DST를 반환합니다. */
void* memmove(void* dst_, const void* src_, size_t size)
{
    unsigned char* dst = dst_;        // 목적지 포인터를 unsigned char로 캐스팅
    const unsigned char* src = src_;  // 소스 포인터를 unsigned char로 캐스팅

    ASSERT(dst != NULL || size == 0);  // 목적지가 NULL이 아니거나 크기가 0이어야 함
    ASSERT(src != NULL || size == 0);  // 소스가 NULL이 아니거나 크기가 0이어야 함

    if (dst < src)  // 목적지가 소스보다 앞에 있는 경우 (앞에서 뒤로 복사)
    {
        while (size-- > 0)    // 크기가 0이 될 때까지 반복
            *dst++ = *src++;  // 앞에서 뒤로 순차적으로 복사
    }
    else  // 목적지가 소스보다 뒤에 있는 경우 (뒤에서 앞으로 복사)
    {
        dst += size;          // 목적지 포인터를 끝으로 이동
        src += size;          // 소스 포인터를 끝으로 이동
        while (size-- > 0)    // 크기가 0이 될 때까지 반복
            *--dst = *--src;  // 뒤에서 앞으로 역순으로 복사 (겹침 방지)
    }

    return dst;  // 목적지 포인터 반환
}

/* Find the first differing byte in the two blocks of SIZE bytes
   at A and B.  Returns a positive value if the byte in A is
   greater, a negative value if the byte in B is greater, or zero
   if blocks A and B are equal. */
/* SIZE 바이트의 두 블록 A와 B에서 첫 번째로 다른 바이트를 찾습니다.
   A의 바이트가 더 크면 양수, B의 바이트가 더 크면 음수,
   블록 A와 B가 같으면 0을 반환합니다. */
int memcmp(const void* a_, const void* b_, size_t size)
{
    const unsigned char* a = a_;  // 블록 A를 unsigned char 포인터로 캐스팅
    const unsigned char* b = b_;  // 블록 B를 unsigned char 포인터로 캐스팅

    ASSERT(a != NULL || size == 0);  // 블록 A가 NULL이 아니거나 크기가 0이어야 함
    ASSERT(b != NULL || size == 0);  // 블록 B가 NULL이 아니거나 크기가 0이어야 함

    for (; size-- > 0; a++, b++)       // 모든 바이트를 비교
        if (*a != *b)                  // 바이트가 다르면
            return *a > *b ? +1 : -1;  // A가 크면 +1, B가 크면 -1 반환
    return 0;                          // 모든 바이트가 같으면 0 반환
}

/* Finds the first differing characters in strings A and B.
   Returns a positive value if the character in A (as an unsigned
   char) is greater, a negative value if the character in B (as
   an unsigned char) is greater, or zero if strings A and B are
   equal. */
/* 문자열 A와 B에서 첫 번째로 다른 문자를 찾습니다.
   A의 문자(부호 없는 char로)가 더 크면 양수,
   B의 문자(부호 없는 char로)가 더 크면 음수,
   문자열 A와 B가 같으면 0을 반환합니다. */
int strcmp(const char* a_, const char* b_)
{
    const unsigned char* a = (const unsigned char*)a_;  // 문자열 A를 unsigned char로 캐스팅
    const unsigned char* b = (const unsigned char*)b_;  // 문자열 B를 unsigned char로 캐스팅

    ASSERT(a != NULL);  // 문자열 A가 NULL이 아니어야 함
    ASSERT(b != NULL);  // 문자열 B가 NULL이 아니어야 함

    while (*a != '\0' && *a == *b)  // null 문자를 만나거나 문자가 다를 때까지 반복
    {
        a++;  // 다음 문자로 이동
        b++;  // 다음 문자로 이동
    }

    return *a < *b ? -1 : *a > *b;  // A가 작으면 -1, 크면 1, 같으면 0 반환
}

/* Returns a pointer to the first occurrence of CH in the first
   SIZE bytes starting at BLOCK.  Returns a null pointer if CH
   does not occur in BLOCK. */
/* BLOCK에서 시작하는 SIZE 바이트 내에서 CH의 첫 번째 발생 위치에 대한 포인터를 반환합니다.
   CH가 BLOCK에 없으면 null 포인터를 반환합니다. */
void* memchr(const void* block_, int ch_, size_t size)
{
    const unsigned char* block = block_;  // 블록을 unsigned char 포인터로 캐스팅
    unsigned char ch = ch_;               // 찾을 문자를 unsigned char로 캐스팅

    ASSERT(block != NULL || size == 0);  // 블록이 NULL이 아니거나 크기가 0이어야 함

    for (; size-- > 0; block++)   // SIZE 바이트를 순회
        if (*block == ch)         // 현재 바이트가 찾는 문자와 같으면
            return (void*)block;  // 해당 위치의 포인터 반환

    return NULL;  // 찾지 못하면 NULL 반환
}

/* Finds and returns the first occurrence of C in STRING, or a
   null pointer if C does not appear in STRING.  If C == '\0'
   then returns a pointer to the null terminator at the end of
   STRING. */
/* STRING에서 C의 첫 번째 발생을 찾아 반환하거나,
   C가 STRING에 나타나지 않으면 null 포인터를 반환합니다.
   C == '\0'이면 STRING 끝의 null 종료자에 대한 포인터를 반환합니다. */
char* strchr(const char* string, int c_)
{
    char c = c_;  // 찾을 문자를 char로 캐스팅

    ASSERT(string);  // 문자열이 NULL이 아니어야 함

    for (;;)                       // 무한 루프
        if (*string == c)          // 현재 문자가 찾는 문자와 같으면
            return (char*)string;  // 해당 위치의 포인터 반환
        else if (*string == '\0')  // 문자열의 끝에 도달하면
            return NULL;           // NULL 반환
        else
            string++;  // 다음 문자로 이동
}

/* Returns the length of the initial substring of STRING that
   consists of characters that are not in STOP. */
/* STRING의 초기 부분 문자열 중 STOP에 없는 문자로만 구성된 부분의 길이를 반환합니다. */
size_t strcspn(const char* string, const char* stop)
{
    size_t length;  // 길이를 저장할 변수

    for (length = 0; string[length] != '\0'; length++)  // 문자열의 끝까지 반복
        if (strchr(stop, string[length]) != NULL)       // 현재 문자가 STOP에 있으면
            break;                                      // 반복 종료
    return length;                                      // 찾은 길이 반환
}

/* Returns a pointer to the first character in STRING that is
   also in STOP.  If no character in STRING is in STOP, returns a
   null pointer. */
/* STRING에서 STOP에도 있는 첫 번째 문자에 대한 포인터를 반환합니다.
   STRING의 어떤 문자도 STOP에 없으면 null 포인터를 반환합니다. */
char* strpbrk(const char* string, const char* stop)
{
    for (; *string != '\0'; string++)       // 문자열의 끝까지 반복
        if (strchr(stop, *string) != NULL)  // 현재 문자가 STOP에 있으면
            return (char*)string;           // 해당 위치의 포인터 반환
    return NULL;                            // 찾지 못하면 NULL 반환
}

/* Returns a pointer to the last occurrence of C in STRING.
   Returns a null pointer if C does not occur in STRING. */
/* STRING에서 C의 마지막 발생 위치에 대한 포인터를 반환합니다.
   C가 STRING에 없으면 null 포인터를 반환합니다. */
char* strrchr(const char* string, int c_)
{
    char c = c_;           // 찾을 문자를 char로 캐스팅
    const char* p = NULL;  // 마지막 발생 위치를 저장할 포인터 (초기값 NULL)

    for (; *string != '\0'; string++)  // 문자열의 끝까지 반복
        if (*string == c)              // 현재 문자가 찾는 문자와 같으면
            p = string;                // 마지막 발생 위치 업데이트
    return (char*)p;                   // 마지막 발생 위치 반환 (없으면 NULL)
}

/* Returns the length of the initial substring of STRING that
   consists of characters in SKIP. */
/* STRING의 초기 부분 문자열 중 SKIP에 있는 문자로만 구성된 부분의 길이를 반환합니다. */
size_t strspn(const char* string, const char* skip)
{
    size_t length;  // 길이를 저장할 변수

    for (length = 0; string[length] != '\0'; length++)  // 문자열의 끝까지 반복
        if (strchr(skip, string[length]) == NULL)       // 현재 문자가 SKIP에 없으면
            break;                                      // 반복 종료
    return length;                                      // 찾은 길이 반환
}

/* Returns a pointer to the first occurrence of NEEDLE within
   HAYSTACK.  Returns a null pointer if NEEDLE does not exist
   within HAYSTACK. */
/* HAYSTACK 내에서 NEEDLE의 첫 번째 발생 위치에 대한 포인터를 반환합니다.
   NEEDLE이 HAYSTACK에 없으면 null 포인터를 반환합니다. */
char* strstr(const char* haystack, const char* needle)
{
    size_t haystack_len = strlen(haystack);  // HAYSTACK의 길이 계산
    size_t needle_len = strlen(needle);      // NEEDLE의 길이 계산

    if (haystack_len >= needle_len)  // HAYSTACK이 NEEDLE보다 길거나 같으면
    {
        size_t i;  // 반복 변수

        for (i = 0; i <= haystack_len - needle_len; i++)    // 가능한 모든 시작 위치 확인
            if (!memcmp(haystack + i, needle, needle_len))  // 현재 위치에서 NEEDLE과 비교
                return (char*)haystack + i;                 // 일치하면 해당 위치 반환
    }

    return NULL;  // 찾지 못하면 NULL 반환
}

/* Breaks a string into tokens separated by DELIMITERS.  The
   first time this function is called, S should be the string to
   tokenize, and in subsequent calls it must be a null pointer.
   SAVE_PTR is the address of a `char *' variable used to keep
   track of the tokenizer's position.  The return value each time
   is the next token in the string, or a null pointer if no
   tokens remain.

   This function treats multiple adjacent delimiters as a single
   delimiter.  The returned tokens will never be length 0.
   DELIMITERS may change from one call to the next within a
   single string.

   strtok_r() modifies the string S, changing delimiters to null
   bytes.  Thus, S must be a modifiable string.  String literals,
   in particular, are *not* modifiable in C, even though for
   backward compatibility they are not `const'.

   Example usage:

   char s[] = "  String to  tokenize. ";
   char *token, *save_ptr;

   for (token = strtok_r (s, " ", &save_ptr); token != NULL;
   token = strtok_r (NULL, " ", &save_ptr))
   printf ("'%s'\n", token);

outputs:

'String'
'to'
'tokenize.'
*/
/* DELIMITERS로 구분된 문자열을 토큰으로 분리합니다.
   이 함수가 처음 호출될 때, S는 토큰화할 문자열이어야 하며,
   이후 호출에서는 null 포인터여야 합니다.
   SAVE_PTR은 토크나이저의 위치를 추적하는 데 사용되는 `char *' 변수의 주소입니다.
   매번 반환되는 값은 문자열의 다음 토큰이거나, 토큰이 더 이상 없으면 null 포인터입니다.

   이 함수는 여러 개의 인접한 구분자를 하나의 구분자로 취급합니다.
   반환된 토큰은 절대 길이가 0이 아닙니다.
   DELIMITERS는 단일 문자열 내에서 한 호출에서 다음 호출로 변경될 수 있습니다.

   strtok_r()은 문자열 S를 수정하여 구분자를 null 바이트로 변경합니다.
   따라서 S는 수정 가능한 문자열이어야 합니다.
   특히 문자열 리터럴은 C에서 수정할 수 없으며, 하위 호환성을 위해 `const'가 아니더라도 그렇습니다.

   사용 예시:

   char s[] = "  String to  tokenize. ";
   char *token, *save_ptr;

   for (token = strtok_r (s, " ", &save_ptr); token != NULL;
   token = strtok_r (NULL, " ", &save_ptr))
   printf ("'%s'\n", token);

출력:

'String'
'to'
'tokenize.'
*/
char* strtok_r(char* s, const char* delimiters, char** save_ptr)
{
    char* token;  // 반환할 토큰의 시작 주소를 저장할 포인터

    ASSERT(delimiters != NULL);  // 구분자 문자열이 NULL이 아니어야 함을 확인
    ASSERT(save_ptr != NULL);    // 저장 포인터가 NULL이 아니어야 함을 확인

    /* If S is nonnull, start from it.
       If S is null, start from saved position. */
    /* S가 NULL이 아니면 그것부터 시작합니다.
       S가 NULL이면 저장된 위치부터 시작합니다. */
    if (s == NULL)      // 첫 번째 호출이 아니고 이전 위치부터 계속하는 경우
        s = *save_ptr;  // save_ptr이 가리키는 저장된 위치로 시작점 설정
    ASSERT(s != NULL);  // 시작 문자열이 NULL이 아니어야 함을 확인

    /* Skip any DELIMITERS at our current position. */
    /* 현재 위치의 모든 구분자를 건너뜁니다. */
    while (strchr(delimiters, *s) != NULL)  // 현재 문자가 구분자인 동안 반복
    {
        /* strchr() will always return nonnull if we're searching
           for a null byte, because every string contains a null
           byte (at the end). */
        /* strchr()는 null 바이트를 찾는 경우 항상 nonnull을 반환합니다.
           왜냐하면 모든 문자열은 끝에 null 바이트를 포함하기 때문입니다. */
        if (*s == '\0')  // 현재 문자가 문자열의 끝(null 문자)인 경우
        {
            *save_ptr = s;  // save_ptr을 현재 위치(문자열 끝)로 설정
            return NULL;    // 더 이상 토큰이 없으므로 NULL 반환
        }

        s++;  // 다음 문자로 이동 (구분자 건너뛰기)
    }

    /* Skip any non-DELIMITERS up to the end of the string. */
    /* 문자열의 끝까지 모든 비구분자를 건너뜁니다. */
    token = s;  // 토큰의 시작 주소를 저장 (구분자가 아닌 첫 문자)
    while (strchr(delimiters, *s) == NULL)  // 현재 문자가 구분자가 아닌 동안 반복
        s++;         // 다음 문자로 이동 (토큰의 끝을 찾기 위해)
    if (*s != '\0')  // 현재 문자가 문자열의 끝이 아닌 경우 (구분자를 만남)
    {
        *s = '\0';          // 구분자를 null 문자로 변경하여 토큰을 종료
        *save_ptr = s + 1;  // 다음 토큰의 시작 위치를 save_ptr에 저장 (구분자 다음)
    }
    else                // 현재 문자가 문자열의 끝인 경우 (마지막 토큰)
        *save_ptr = s;  // save_ptr을 문자열 끝으로 설정 (다음 호출 시 NULL 반환)
    return token;       // 찾은 토큰의 시작 주소 반환
}

/* Sets the SIZE bytes in DST to VALUE. */
/* DST의 SIZE 바이트를 VALUE로 설정합니다. */
void* memset(void* dst_, int value, size_t size)
{
    unsigned char* dst = dst_;  // 목적지를 unsigned char 포인터로 캐스팅

    ASSERT(dst != NULL || size == 0);  // 목적지가 NULL이 아니거나 크기가 0이어야 함

    while (size-- > 0)   // 크기가 0이 될 때까지 반복
        *dst++ = value;  // 각 바이트를 VALUE로 설정 후 포인터 증가

    return dst_;  // 목적지 포인터 반환
}

/* Returns the length of STRING. */
/* STRING의 길이를 반환합니다. */
size_t strlen(const char* string)
{
    const char* p;  // 포인터 변수

    ASSERT(string);  // 문자열이 NULL이 아니어야 함

    for (p = string; *p != '\0'; p++)  // null 문자를 만날 때까지 포인터 이동
        continue;                      // 반복만 수행 (실제 작업 없음)
    return p - string;                 // 포인터 차이로 길이 계산하여 반환
}

/* If STRING is less than MAXLEN characters in length, returns
   its actual length.  Otherwise, returns MAXLEN. */
/* STRING의 길이가 MAXLEN보다 작으면 실제 길이를 반환합니다.
   그렇지 않으면 MAXLEN을 반환합니다. */
size_t strnlen(const char* string, size_t maxlen)
{
    size_t length;  // 길이를 저장할 변수

    for (length = 0; string[length] != '\0' && length < maxlen;
         length++)  // null 문자를 만나거나 maxlen에 도달할 때까지 반복
        continue;   // 반복만 수행 (실제 작업 없음)
    return length;  // 찾은 길이 반환
}

/* Copies string SRC to DST.  If SRC is longer than SIZE - 1
   characters, only SIZE - 1 characters are copied.  A null
   terminator is always written to DST, unless SIZE is 0.
   Returns the length of SRC, not including the null terminator.

   strlcpy() is not in the standard C library, but it is an
   increasingly popular extension.  See
http://www.courtesan.com/todd/papers/strlcpy.html for
information on strlcpy(). */
/* 문자열 SRC를 DST로 복사합니다. SRC가 SIZE - 1보다 길면
   SIZE - 1 문자만 복사됩니다. null 종료자는 항상 DST에 기록되며,
   SIZE가 0인 경우는 제외됩니다.
   null 종료자를 제외한 SRC의 길이를 반환합니다.

   strlcpy()는 표준 C 라이브러리에 없지만 점점 더 인기 있는 확장입니다.
   strlcpy()에 대한 정보는 http://www.courtesan.com/todd/papers/strlcpy.html을 참조하세요. */
size_t strlcpy(char* dst, const char* src, size_t size)
{
    size_t src_len;  // 소스 문자열의 길이

    ASSERT(dst != NULL);  // 목적지가 NULL이 아니어야 함
    ASSERT(src != NULL);  // 소스가 NULL이 아니어야 함

    src_len = strlen(src);  // 소스 문자열의 길이 계산
    if (size > 0)           // 크기가 0보다 크면
    {
        size_t dst_len = size - 1;  // 목적지에 복사할 수 있는 최대 길이 (null 종료자 공간 제외)
        if (src_len < dst_len)      // 소스가 목적지보다 짧으면
            dst_len = src_len;      // 소스 길이만큼만 복사
        memcpy(dst, src, dst_len);  // 소스에서 목적지로 복사
        dst[dst_len] = '\0';        // null 종료자 추가
    }
    return src_len;  // 소스 문자열의 전체 길이 반환 (복사된 길이가 아님)
}

/* Concatenates string SRC to DST.  The concatenated string is
   limited to SIZE - 1 characters.  A null terminator is always
   written to DST, unless SIZE is 0.  Returns the length that the
   concatenated string would have assuming that there was
   sufficient space, not including a null terminator.

   strlcat() is not in the standard C library, but it is an
   increasingly popular extension.  See
http://www.courtesan.com/todd/papers/strlcpy.html for
information on strlcpy(). */
/* 문자열 SRC를 DST에 연결합니다. 연결된 문자열은
   SIZE - 1 문자로 제한됩니다. null 종료자는 항상 DST에 기록되며,
   SIZE가 0인 경우는 제외됩니다. 충분한 공간이 있다고 가정했을 때
   연결된 문자열이 가질 길이를 반환합니다 (null 종료자 제외).

   strlcat()는 표준 C 라이브러리에 없지만 점점 더 인기 있는 확장입니다.
   strlcpy()에 대한 정보는 http://www.courtesan.com/todd/papers/strlcpy.html을 참조하세요. */
size_t strlcat(char* dst, const char* src, size_t size)
{
    size_t src_len, dst_len;  // 소스와 목적지 문자열의 길이

    ASSERT(dst != NULL);  // 목적지가 NULL이 아니어야 함
    ASSERT(src != NULL);  // 소스가 NULL이 아니어야 함

    src_len = strlen(src);           // 소스 문자열의 길이 계산
    dst_len = strlen(dst);           // 목적지 문자열의 길이 계산
    if (size > 0 && dst_len < size)  // 크기가 0보다 크고 목적지가 크기보다 짧으면
    {
        size_t copy_cnt =
            size - dst_len - 1;  // 복사할 수 있는 최대 문자 수 (null 종료자 공간 제외)
        if (src_len < copy_cnt)                // 소스가 복사 가능한 길이보다 짧으면
            copy_cnt = src_len;                // 소스 길이만큼만 복사
        memcpy(dst + dst_len, src, copy_cnt);  // 목적지 끝에 소스 복사
        dst[dst_len + copy_cnt] = '\0';        // null 종료자 추가
    }
    return src_len + dst_len;  // 소스와 목적지의 전체 길이 합 반환 (실제 연결된 길이가 아님)
}
