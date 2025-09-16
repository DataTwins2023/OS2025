// Simple grep.  Only supports ^ . * $ operators.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

char buf[1024];
int match(char*, char*);

void
grep(char *pattern, int fd)
{
  int n, m;
  char *p, *q;

  m = 0;
  // read 定義在 user/user.h 中
  // n 是 read 的返回值，表示讀到的字節數
  // fd 是 file descriptor，buf 是讀取的緩衝區
  while((n = read(fd, buf+m, sizeof(buf)-m-1)) > 0){
    m += n;
    // buf[m] = '\0'，確保 buf 是以 null 結尾的字串
    buf[m] = '\0';
    // p 指向 buf 的開始位置
    p = buf;
    // q 指向 buf 中第一個換行符號的位置
    // strchr 函數用來尋找字元在字串中第一次出現的位置
    // p 向後尋找 '\n'
    while((q = strchr(p, '\n')) != 0){
      // 將換行符號替換為 null 字元，這樣 p 就成為一個完整的字串
      *q = 0;
      if(match(pattern, p)){
        // 如果匹配成功，將換行符號恢復並輸出該行
        *q = '\n';
        // write 函數用來將資料寫到檔案描述符中，1 代表標準輸出
        // 這裡寫入從 p 開始到 q+1 的字元（包含換行符號）
        write(1, p, q+1 - p);
      }
      // 將 p 移動到下一行的開始位置
      p = q+1;
    }
    // 將未處理完的部分移到 buf 的開始位置，準備下一次讀取
    if(m > 0){
      // buf 從頭到尾都沒有變動， p - buf 代表已處理的字元數
      // m 減去已處理的字元數，得到未處理的字元數
      m -= p - buf;
      // 使用 memmove 函數來移動記憶體區塊
      // 這裡將從 p 開始的 m 個字元移動到 buf 的開始位置
      memmove(buf, p, m);
    }
  }
}

int
main(int argc, char *argv[])
{
  int fd, i;
  char *pattern;

  if(argc <= 1){
    fprintf(2, "usage: grep pattern [file ...]\n");
    exit(1);
  }
  pattern = argv[1];

  if(argc <= 2){
    grep(pattern, 0);
    exit(0);
  }

  for(i = 2; i < argc; i++){
    if((fd = open(argv[i], O_RDONLY)) < 0){
      printf("grep: cannot open %s\n", argv[i]);
      exit(1);
    }
    grep(pattern, fd);
    close(fd);
  }
  exit(0);
}

// Regexp matcher from Kernighan & Pike,
// The Practice of Programming, Chapter 9, or
// https://www.cs.princeton.edu/courses/archive/spr09/cos333/beautiful.html

int matchhere(char*, char*);
int matchstar(int, char*, char*);

int
match(char *re, char *text)
{
  if(re[0] == '^')
    return matchhere(re+1, text);
  do{  // must look at empty string
    if(matchhere(re, text))
      return 1;
  }while(*text++ != '\0');
  return 0;
}

// matchhere: search for re at beginning of text
int matchhere(char *re, char *text)
{
  if(re[0] == '\0')
    return 1;
  if(re[1] == '*')
    return matchstar(re[0], re+2, text);
  if(re[0] == '$' && re[1] == '\0')
    return *text == '\0';
  if(*text!='\0' && (re[0]=='.' || re[0]==*text))
    return matchhere(re+1, text+1);
  return 0;
}

// matchstar: search for c*re at beginning of text
int matchstar(int c, char *re, char *text)
{
  do{  // a * matches zero or more instances
    if(matchhere(re, text))
      return 1;
  }while(*text!='\0' && (*text++==c || c=='.'));
  return 0;
}

