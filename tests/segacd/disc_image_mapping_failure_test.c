#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
static unsigned mapping_attempts;
#ifdef _WIN32
#include <windows.h>
static LPVOID WINAPI fail_mapping(HANDLE file,DWORD access,DWORD high,DWORD low,SIZE_T bytes)
{
    (void)file;(void)access;(void)high;(void)low;(void)bytes;
    ++mapping_attempts;return NULL;
}
#define MapViewOfFile fail_mapping
#else
#include <sys/mman.h>
static void *fail_mapping(void *address,size_t length,int protection,int flags,int file,off_t offset)
{
    (void)address;(void)length;(void)protection;(void)flags;(void)file;(void)offset;
    ++mapping_attempts;return MAP_FAILED;
}
#define mmap fail_mapping
#endif
#include "../../runner/cd/disc_image.c"
#define main reader_suite
#include "disc_image_reader_test.c"
#undef main
int main(void)
{
    int result=reader_suite();CHECK(mapping_attempts>0);return result;
}
