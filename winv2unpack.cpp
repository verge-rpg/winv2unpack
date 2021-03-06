#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>

typedef uint8_t byte;
typedef uint16_t word;

// ================================= Data ====================================

typedef struct
{
  FILE *fp;                           // real file pointer.
  byte s;                             // 0=real file 1=vfile;
  byte v;                             // if vfile, which vfile index
  byte i;                             // which file index in vfile is it?
  byte p;                             // data alignment pad. :)
} VFILE;

struct filestruct
{
  char fname[84];                     // pathname thingo
  int size;                           // size of the file
  int packofs;                        // where the file can be found in PACK
  int curofs;                         // current file offset.
  char extractable;                   // irrelevant to runtime, but...
  char override;                      // should we override?
};

struct mountstruct
{
  char mountname[80];                 // name of VRG packfile.
  FILE *vhandle;                      // Real file-handle of packfile.
  struct filestruct *files;           // File record array.
  int numfiles;                       // number of files in pack.
  int curofs;                         // Current filepointer.
};

struct mountstruct pack[3];            // packfile structs
byte filesmounted=0;                   // Number of VRG files to check.
char headertag[]={ 'V','R','G','P','A','C','K',0 };

// ================================= Code ====================================

int Exist(char *fname)
{ FILE *tempf;

  tempf=fopen(fname,"rb");
  if (tempf)
  {
     fclose(tempf);
     return 1;
  }
  else return 0;
}

void DecryptHeader()
{
	char lastvalue, precodebyte, *ptr;

	ptr=(char *) pack[filesmounted].files;
	lastvalue = *ptr;
	ptr++;

	while (ptr < (char *) (int) pack[filesmounted].files + (int) pack[filesmounted].numfiles*100)
	{
		precodebyte = *ptr;
		(*ptr) -= lastvalue;
		lastvalue = precodebyte;
		ptr++;
	}
}

void MountVFile(char *fname)
{ char buffer[10];

  if (!(pack[filesmounted].vhandle = fopen(fname,"rb")))
  {
    printf("*error* Unable to mount %s; file not found. \n",fname);
    exit(-1);
  }

  // Read pack header
  memset(&buffer, 0, 10);
  fread(&buffer, 1, 7, pack[filesmounted].vhandle);
  if (strcmp(buffer,headertag))
  {
    printf("*error* %s is not a valid packfile. \n",fname);
    exit(-1);
  }

  fread(&buffer, 1, 1, pack[filesmounted].vhandle);
  if (buffer[0]!=1)
  {
    printf("*error* %s is an incompatible packfile version. (ver reported: %d) \n",fname,buffer[0]);
    exit (-1);
  }

  fread(&pack[filesmounted].numfiles, 1, 4, pack[filesmounted].vhandle);
  memcpy(pack[filesmounted].mountname,fname,strlen(fname)+1);

  // Allocate memory for headers and read them in.

	int bufsize = pack[filesmounted].numfiles*100;
  pack[filesmounted].files = (struct filestruct *) malloc(bufsize);
  fread(pack[filesmounted].files, pack[filesmounted].numfiles, 100, pack[filesmounted].vhandle);

	DecryptHeader();

  filesmounted++;
}

VFILE *vopen(char *fname)
{ VFILE *tmp;
  char rf=0,vf=0;
  int i,j;

  // All files using V* are read-only. To write a file, use regular i/o.
  // First we'll see if a real file exists, then we'll check for one in VFiles,
  // if we don't find one in VFile or it's overridable then a real file will
  // be used. That's the general logic progression.

  if (Exist(fname)) rf=1;

  // Search the VFiles.
  for (i=filesmounted-1; i>=0; i--)
  {
    for (j=0; j<pack[i].numfiles; j++)
        if (!_stricmp(fname,pack[i].files[j].fname)) { vf=1; break; }
    if (vf) break;
  }

  if (!vf && !rf) return 0;

  tmp=(VFILE *) malloc(sizeof(VFILE));

  if (vf && rf)
  {
    if (pack[i].files[j].override) vf=0;
    else rf=0;
  }

  if (vf)
  {
    tmp->fp=pack[i].vhandle;
    tmp->s=1;
    tmp->v=i;
    tmp->i=j;
    pack[i].files[j].curofs=0;
    fseek(tmp->fp, pack[i].files[j].packofs, 0);
    pack[i].curofs=pack[i].files[j].packofs;
    return tmp;
  }

  tmp->fp=fopen(fname,"rb");
  tmp->s=0; tmp->v=0; tmp->i=0;
  return tmp;
}

void vread(char *dest, int len, VFILE *f)
{
  // This is fairly simple.. Just make sure our filepointer is at the right
  // place, then do a straight fread.

  if (f->s)
  {
    if (pack[f->v].curofs != (pack[f->v].files[f->i].packofs + pack[f->v].files[f->i].curofs))
       fseek(f->fp, pack[f->v].files[f->i].curofs+pack[f->v].files[f->i].packofs, 0);
    pack[f->v].files[f->i].curofs+=len;
    pack[f->v].curofs+=len;
  }
  fread(dest, 1, len, f->fp);
}

void vclose(VFILE *f)
{
  if (!f) return;
  if (!f->s) fclose(f->fp);
  f->fp=0;
  free(f);
}

int filesize(VFILE *f)
{ int oldpos,tmp;

  // Filesize for Vfiles is real simple.
  if (f->s) return pack[f->v].files[f->i].size;

  // It's a bit more complex for external files.
  oldpos=ftell(f->fp);
  fseek(f->fp, 0, 2);
  tmp=ftell(f->fp);
  fseek(f->fp, oldpos, 0);
  return tmp;
}

void vseek(VFILE *f, int offset, int origin)
{
  if (!f->s)
  {
    fseek(f->fp, offset, origin);
    return;
  }

  switch(origin)
  {
    case 0: pack[f->v].files[f->i].curofs=offset;
            fseek(f->fp, offset+pack[f->v].files[f->i].packofs, 0);
            return;
    case 1: pack[f->v].files[f->i].curofs+=offset;
            fseek(f->fp, offset, 1);
            return;
    case 2: pack[f->v].files[f->i].curofs=pack[f->v].files[f->i].size-offset;
            fseek(f->fp, pack[f->v].files[f->i].curofs+pack[f->v].files[f->i].packofs, 0);
            return;
  }
}

void _vscanf(VFILE *f, char *format, char *dest)
{
  fscanf(f->fp, format, dest);
  if (f->s)
    pack[f->v].files[f->i].curofs=ftell(f->fp)-pack[f->v].files[f->i].packofs;
}

char vgetc(VFILE *f)
{ char c;

  vread(&c, 1, f);
  return c;
}

word vgetw(VFILE *f)
{ word c;

  vread((char *) &c, 2, f);
  return c;
}

void vgets(char *str, int len, VFILE *f)
{
  if (f->s)
  {
    if (pack[f->v].curofs != (pack[f->v].files[f->i].packofs + pack[f->v].files[f->i].curofs))
       fseek(f->fp, pack[f->v].files[f->i].curofs+pack[f->v].files[f->i].packofs, 0);
    pack[f->v].files[f->i].curofs+=len;
    pack[f->v].curofs+=len;
  }
  fgets(str, len, f->fp);
}


int main(int argc, char** argv)
{
	if(argc != 2)
	{
		printf("USAGE: v2unpack path_to_v2_packfile.vrg\n");
		exit(-1);
	}

	MountVFile(argv[1]);
	for(int i=0;i<pack[0].numfiles;i++)
	{
		char* fname = pack[0].files[i].fname;
		int len = pack[0].files[i].size;

		VFILE* vf = vopen(fname);
		char* buf = (char*)malloc(len);
		vread(buf,len,vf);
		vclose(vf);

		//some filenames will have subdirectories. just chop all that, nobody wants to deal with this in portable C++
		//need to rewrite in c#
		fname = strrchr(fname,'\\') ? (1+strrchr(fname,'\\')) : fname;
		FILE* outf = fopen(fname,"wb");
		
		fwrite(buf,1,len,outf);
		fclose(outf);
	}
}