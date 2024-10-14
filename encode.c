#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

// Written by Babak

int SIZE=512;

void encode(int fd, char text[])
{
  int _ids[3]={810101408,810101000,810101000};
  int key=0;
  for (int i = 0; i < sizeof(_ids)/sizeof(int); i++)
  {
    key+=_ids[i]%100;
  }
  int i = 0;
  while(text[i]!='\0')
  {
    char base;
    if(text[i]>='a' && text[i]<='z')
    {
      base='a';
      text[i]=(text[i]+key-base)%26+base;
    }
    else if (text[i]>='A' && text[i]<='Z')
    {
      base='A';
      text[i]=(text[i]+key-base)%26+base;
    }
    i++;
  }
  if (write(fd, text, i) != i) {
    printf(1, "encode: write error\n");
    exit();
  }
}

void merge_argv(int count_strs,char merged_text[],char* argv[])
{
  int index=0;
  for (int i = 1; i < count_strs; i++)
  {
      for (int j = 0; argv[i][j]!='\0'; j++)
      {
        merged_text[index++]=argv[i][j];
      }
      if (i<count_strs-1)
      {
        merged_text[index++]=' ';
      }
  }
  merged_text[index++]='\n';
  merged_text[index]='\0';
  return;
}

int
main(int argc, char *argv[])
{
  char output_file[]="result.txt";
  int fd;
  char text[SIZE];
  merge_argv(argc,text,argv);
  if((fd = open(output_file, O_CREATE)) < 0){
    printf(1, "encode: cannot create %s\n", output_file);
    exit();
  }
  close(fd);
  if((fd = open(output_file, O_WRONLY)) < 0){
    printf(1, "encode: cannot open %s\n", output_file);
    exit();
  }
  encode(fd,text);
  close(fd);
  exit();
}
