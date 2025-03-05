#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"


//注意：strcmp()如果返回值 < 0，则表示 str1 小于 str2。如果返回值 > 0，则表示 str2 小于 str1。如果返回值 = 0，则表示 str1 等于 str2。


char* fmtname(char *path) //参考ls中的fmtname代码
{
	static char buf[DIRSIZ+1];
 	char *p;
  	// Find first character after last slash.
  	for(p=path+strlen(path); p >= path && *p != '/'; p--)
    	;
  	p++;
  	// Return blank-padded name.
  	if(strlen(p) >= DIRSIZ)
    	return p;
  	memmove(buf, p, strlen(p));
	buf[strlen(p)] = 0;  //字符串结束符
  	return buf;
}

void find(char* path,char* file){	
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, 0)) < 0){
    fprintf(2, "ls: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }


   switch(st.type){
  case T_FILE:
	if(!strcmp(fmtname(path),file)) printf("%s\n",path);
    break;
  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf("find: path too long\n");
      break;
    }
    strcpy(buf, path); //将输入的目录字符串复制到buf中
    p = buf+strlen(buf);
    *p++ = '/';//将`/`拼接在后面
	   	//读取 fd ，如果 read 返回字节数与 de 长度相等则循环
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)        //参考知乎
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
	  //不去递归处理.和..
	  if(!strcmp(de.name,".")||!strcmp(de.name,".."))
	  continue;
	  find(buf,file);
    }
    break;
  }
  close(fd);



}

int main(int argc,char* argv[]){
	if(argc != 3){
    fprintf(2, "usage:find <path> <name>\n");
    exit(1);
	}
	find(argv[1], argv[2]);
	exit(0);
}