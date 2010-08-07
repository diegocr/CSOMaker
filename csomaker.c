/* ***** BEGIN LICENSE BLOCK *****
 * Version: MIT/X11 License
 * 
 * Copyright (c) 2010 Diego Casorran
 * Based on Compressed ISO9660 conveter by BOOSTER
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 * 
 * Contributor(s):
 *   Diego Casorran <dcasorran@gmail.com> (Original Author)
 * 
 * ***** END LICENSE BLOCK ***** */


#ifdef __amigaos4__
# define __USE_INLINE__
#endif

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <zlib.h>
#include "ciso.h"

struct Data
{
	Object *app;
};

//static struct Data D = {0};

//----------------------------------------------------------------------------

STATIC CONST UBYTE __version[] __attribute__((used)) = 
	"$VER:CSOMaker 0.5 (c)2009 Diego Casorran - http://Amiga.sf.net/";

/* this macro lets us long-align structures on the stack */
#define D_S(type,name)	\
	char a_##name[ sizeof( type ) + 3 ];	\
	type *name = ( type * ) ( ( LONG ) ( a_##name + 3 ) & ~3 )

//----------------------------------------------------------------------------

#undef CLOSE
//#define CLI_MODE	(D.app == NULL)
#define CLI_MODE	(1)

#if defined(AMIGA) || defined(AROS)
# define AMIGAOS 1
#endif

#ifdef AMIGAOS
# if !defined(__PPC__)
#  define FILE_T		BPTR
#  define SIZE_T		ULONG
#  define TRSF(f)		((f)-1)
#  define OPEN(a,b)		Open((a),(b))
#  define OPEN_OK(z,a,b)	((z = OPEN((a),(b))) != 0)
#  define OPENR(a)		OPEN((a),MODE_OLDFILE)
#  define OPENR_OK(z,a)		((z = OPENR((a))) != 0)
#  define OPENW(a)		OPEN((a),MODE_NEWFILE)
#  define OPENW_OK(z,a)		((z = OPENW((a))) != 0)
#  define CLOSE(a)		Close(a),a=0
#  define READ(a,b,c)		Read((a),(b),(c))
#  define READ_OK(a,b,c)	(READ((a),(b),(c))==(c))
#  define WRITE(a,b,c)		Write((a),(b),(c))
#  define WRITE_OK(a,b,c)	(WRITE((a),(b),(c))==(c))
#  define SEEK(a,b,c)		Seek((a),(b),TRSF(c))
#  define SEEK_OK(a,b,c)	(SEEK((a),(b),(c)) != -1L)
#  define DELETEFILE(a)		DeleteFile(a)
#  define MALLOC(a)		AllocVec((a)+1,MEMF_ANY)
#  define FREE(a)		FreeVec((a))
#  define MEMCPY(a,b,c)		CopyMem((b),(a),(c))
# else /* __PPC__ */
#  define UNIX
# endif /* __PPC__ */
# if !defined(AROS) && !defined(__amigaos4__)
#  define HAVE_ASYNCIO 1
# endif
#endif /* AMIGAOS */

#ifndef TRSF
# define TRSF(f) f
#endif

#ifdef UNIX
# define FILE_T			FILE *
# define SIZE_T			unsigned long
# define OPEN(a,b)		fopen((a),(b))
# define OPEN_OK(z,a,b)		((z = OPEN((a),(b))) != 0)
# define OPENR(a)		OPEN((a),"rb")
# define OPENR_OK(z,a)		((z = OPENR((a))) != 0)
# define OPENW(a)		OPEN((a),"wb")
# define OPENW_OK(z,a)		((z = OPENW((a))) != 0)
# define CLOSE(a)		fclose(a),a=0
# define READ(a,b,c)		fread((b),1,(c),(a))
# define READ_OK(a,b,c)		(READ((a),(b),(c))==(unsigned)(c))
# define WRITE(a,b,c)		fwrite((b),1,(c),(a))
# define WRITE_OK(a,b,c)	(WRITE((a),(b),(c))==(unsigned)(c))
# define SEEK(a,b,c)		fseek((a),(b),(c))
# define SEEK_OK(a,b,c)		(SEEK((a),(b),(c)) != -1L)
# define DELETEFILE(a)		unlink(a)
# define MALLOC(a)		malloc((a))
# define FREE(a)		free((a))
# define MEMCPY(a,b,c)		memcpy((a),(b),(c))
#endif /* UNIX */

#ifndef FILE_T
# error you have to add support for your platform
#endif /* FILE_T */

#ifdef HAVE_ASYNCIO
# include <proto/asyncio.h>
# undef Open
# undef Close
# undef Read
# undef Write
# undef Seek
# undef FILE_T
# define Open(a,b)	OpenAsync((a),(b)-MODE_OLDFILE,512*1024)
# define Close(a)	CloseAsync((a)),a=NULL
# define Read(a,b,c)	ReadAsync((a),(b),(c))
# define Write(a,b,c)	WriteAsync((a),(b),(c))
# define Seek(a,b,c)	SeekAsync((a),(b),(c))
typedef struct AsyncFile * FILE_T;
struct Library *AsyncIOBase = NULL;
# define _OPENASYNCIO	if((AsyncIOBase = OpenLibrary("asyncio.library",0))) {
# define _CLOSEASYNCIO	CloseLibrary(AsyncIOBase); }
#else /* HAVE_ASYNCIO */
# define _OPENASYNCIO	
# define _CLOSEASYNCIO	
#endif /* HAVE_ASYNCIO */


#ifdef __amigaos4__
# define FILESIZE _FILESIZE
STATIC SIZE_T FILESIZE(char *filename)
{
	SIZE_T result = 0;
	struct ExamineData *eData;
	if((eData = ExamineObjectTags(EX_StringNameInput,filename,TAG_DONE))) {
	  result = (SIZE_T) eData->FileSize;
	  FreeDosObject(DOS_EXAMINEDATA,eData);
	}
	return(result);
}
#endif /* __amigaos4__ */

#ifndef FILESIZE
# define FILESIZE _FILESIZE
STATIC SIZE_T FILESIZE(char *filename)
{
	SIZE_T result = 0;
	BPTR lock;
	
	if((lock = Lock((STRPTR)filename,SHARED_LOCK)))
	{
		D_S(struct FileInfoBlock,fib);
		
		if(Examine( lock, fib ))
			result = (SIZE_T) fib->fib_Size;
		
		UnLock(lock);
	}
	return(result);
}
#endif /* FILESIZE */
#ifndef MESSAGE
# define MESSAGE _MESSAGE
STATIC VOID MESSAGE(int type,const char *fmt, ...)
{
	va_list va;
	char buf[4096];
	unsigned bl = sizeof(buf);
	static const char *mt[] = 
	{
		"ERROR", "WARNING", "INFO", "DEBUG", NULL
	};
	
	if(type > -1 && 4 > type )
	{
		bl -= snprintf( buf, bl, "%s: ", mt[type]);
	}
	
	va_start(va,fmt);
	bl -= vsnprintf( &buf[sizeof(buf)-bl], bl-3, fmt, va );
	va_end(va);
	
	if( CLI_MODE )
	{
		strcat( &buf[sizeof(buf)-bl], "\n");
		
		PutStr(buf);
	}
	else
	{
		static struct IntuiText body = { 0,0,0, 15,5, NULL, NULL, NULL };
		static struct IntuiText   ok = { 0,0,0,  6,3, NULL, "Ok", NULL };
		
		body.IText = (UBYTE *)buf;
		AutoRequest(NULL,&body,NULL,&ok,0,0,640,72);
	}
}
#endif /* MESSAGE */
#ifndef ERROR
# define ERROR(m...) MESSAGE(0,m)
#endif /* ERROR */
#ifndef WARNING
# define WARNING(m...) MESSAGE(1,m)
#endif /* WARNING */
#ifndef INFO
# define INFO(m...) MESSAGE(2,m)
#endif /* INFO */
#ifndef DEBUG
# define DEBUG(m...) MESSAGE(3,m)
#endif /* ERROR */
#ifndef MSG
# define MSG(m...) MESSAGE(-1,m)
#endif /* MSG */

#ifdef AMIGA
# define ENDIANFIX(val)	val = \
 ((((val) &0xff)<<24)|(((val)>>8 &0xff)<<16)|(((val)>>16 &0xff)<<8)|((val)>>24 &0xff))
#else
# define ENDIANFIX(val)	((void)0)
#endif

#ifndef RATE
# define RATE(C,T) \
   ({typeof(C) _c=(C);unsigned d=(_c > 10240)?10240:1;(_c/d)*100/((T)/d);})
#endif /* RATE */

// write [de]compressed block
#define WRITE_BLOCK(fd,buf,len,bl) \
	if(!WRITE_OK(fd,buf,len)) { \
		ERROR("block %ld : Write error",bl); \
		error = 1; break; \
	}

//----------------------------------------------------------------------------
static FILE_T infd;
static FILE_T outfd;
static void *vbuf;

static void __fclose_all(void)
{
  if(infd)
    CLOSE(infd);
  if(outfd)
    CLOSE(outfd);
  if(vbuf)
    FREE(vbuf);
}

#ifdef UNIX
static void __setvbuf(void)
{
	char *vbs = getenv("CSOMAKER_VBUFSIZE");
	
	if( vbs != NULL )
	{
	  unsigned vbsl = atoi(vbs);
	  
	  if( vbsl && (vbuf = MALLOC(vbsl)))
	  {
	    setvbuf(outfd, vbuf, _IOFBF, vbsl );
	  }
	}
}
#else
# define __setvbuf() ((void)0)
#endif /* UNIX */
//----------------------------------------------------------------------------

static int CSOtoISO_Stub(FILE_T in,FILE_T out,CISO_H *ciso)
{
	int rc = 1;
	unsigned int *index_buf = NULL;
	unsigned char *block_buf1 = NULL;
	unsigned char *block_buf2 = NULL;
	int ciso_total_block,index_size;
	
	ciso_total_block = ciso->total_bytes / ciso->block_size;
	
	// allocate index block
	index_size = (ciso_total_block + 1 ) * sizeof(unsigned long);
	if(!(index_buf = MALLOC(index_size))
	|| !(block_buf1 = MALLOC(ciso->block_size))
	|| !(block_buf2 = MALLOC(ciso->block_size*2)))
	{
		if(index_buf) FREE(index_buf);
		if(block_buf1) FREE(block_buf1);
		ERROR("Can't allocate memory!");
	}
	else
	{
		memset(index_buf,0,index_size);
		
		// read index block
		if(!READ_OK(in,index_buf,index_size))
		{
			ERROR("File Read Error!");
		}
		else
		{
			z_stream z;
			int error = 0;
			register unsigned int index,index2;
			SIZE_T read_pos,read_size;
			register int block;
			int cmp_size;
			int status;
			int plain;
			int porcent = -1;
			
			// init zlib
			z.zalloc = Z_NULL;
			z.zfree  = Z_NULL;
			z.opaque = Z_NULL;
			
			for( block = 0 ; block < ciso_total_block ; block++ )
			{
				ENDIANFIX(index_buf[block]);
			}
			
			__setvbuf();
			
			for( block = 0 ; block < ciso_total_block ; block++ )
			{
				if( CLI_MODE )
				{
					if((block * 101 / ciso_total_block) > porcent)
					{
						fprintf(stderr," Decompressing...%3d%%\r",++porcent);
						fflush(stderr);
					}
				}
				
				if(inflateInit2(&z,-15) != Z_OK)
				{
					ERROR("deflateInit : %s\n", (z.msg) ? z.msg : "???");
					error = 1;
					break;
				}
				
				// check index
				index  = index_buf[block];
				plain  = index & 0x80000000;
				index &= 0x7fffffff;
				read_pos = index << (ciso->align);
				if(plain)
				{
					read_size = ciso->block_size;
				}
				else
				{
					index2 = index_buf[block+1] & 0x7fffffff;
					read_size = (index2-index) << (ciso->align);
				}
				if(!SEEK_OK(in,read_pos,SEEK_SET))
				{
					ERROR("block=%ld : seek error",block);
					error = 1;
					break;
				}
				
				z.avail_in = READ(in,block_buf2,read_size);
				if(z.avail_in != read_size)
				{
					if((block+1 == ciso_total_block) && z.avail_in > 0)
					{
						read_size = z.avail_in;
					}
					else
					{
						ERROR("block=%ld : read error",block);
						error = 1;
						break;
					}
				}
				
				if(plain)
				{
				//	MEMCPY(block_buf1,block_buf2,read_size);
				//	cmp_size = read_size;
					
					WRITE_BLOCK(out,block_buf2,read_size,block)
				}
				else
				{
					z.next_out  = block_buf1;
					z.avail_out = ciso->block_size;
					z.next_in   = block_buf2;
					status = inflate(&z, Z_FULL_FLUSH);
					if (status != Z_STREAM_END)
					{
						ERROR("block %ld: inflate : %s[%ld]", block,(z.msg) ? z.msg : "error",status);
						error = 1;
						break;
					}
					cmp_size = ciso->block_size - z.avail_out;
					if(cmp_size != (int)ciso->block_size)
					{
						ERROR("block %ld : block size error %ld != %ld",block,cmp_size,ciso->block_size);
						error = 1;
						break;
					}
					
					WRITE_BLOCK(out,block_buf1,cmp_size,block)
				}
				
				// term zlib
				if (inflateEnd(&z) != Z_OK)
				{
					ERROR("inflateEnd : %s", (z.msg) ? z.msg : "error");
					error = 1;
					break;
				}
			}
			
			rc = error;
		}
		
		FREE(index_buf);
		FREE(block_buf1);
		FREE(block_buf2);
	}
	
	return(rc);
}

static void CSOtoISO(char *in,char *out)
{
	if(!OPENR_OK(infd,in))
	{
		ERROR("Can't open \"%s\"", in );
	}
	else
	{
		if(!OPENW_OK(outfd,out))
		{
			ERROR("Can't write to \"%s\"", out );
		}
		else
		{
			CISO_H ciso;
			int error = 1;
			
			if(!READ_OK(infd,&ciso,sizeof(ciso)))
			{
				ERROR("Read Error %ld", __LINE__);
			}
			else if( ciso.total_bytes_hi != 0 )
			{ // XXX: TODO..
				ERROR("This CISO isn't supported...");
			}
			else if( ciso.magic[0] != 'C' ||
				 ciso.magic[1] != 'I' ||
				 ciso.magic[2] != 'S' ||
				 ciso.magic[3] != 'O' ||
				 ciso.block_size == 0 ||
				 ciso.total_bytes == 0 )
			{
				ERROR("CISO File Format Error");
			}
			else
			{
				/**
				 * ciso.magic is in BIG-ENDIAN, other LONGs
				 * aren't and we need to fix it...
				 */
				
				ENDIANFIX(ciso.header_size);
				ENDIANFIX(ciso.total_bytes);
				ENDIANFIX(ciso.block_size);
				
				if( CLI_MODE )
				{
					printf("Decompress '%s' to '%s'\n",in,out);
					printf("Total File Size %ld bytes\n",ciso.total_bytes);
					printf("block size      %ld  bytes\n",ciso.block_size);
					printf("total blocks    %ld  blocks\n",ciso.total_bytes / ciso.block_size);
					printf("index align     %ld\n",1L<<ciso.align);
				}
				
				error = CSOtoISO_Stub(infd,outfd,&ciso);
			}
			
			CLOSE(outfd);
			
			if( error )
			{
				DELETEFILE(out);
			}
		}
		
		CLOSE(infd);
	}
}

//----------------------------------------------------------------------------

static int ISOtoCSO_Stub(FILE_T in,FILE_T out,CISO_H *ciso,long level)
{
	int rc = 1;
	unsigned int *index_buf = NULL;
	unsigned char *block_buf1 = NULL;
	unsigned char *block_buf2 = NULL;
	unsigned int *crc_buf = NULL;
	int ciso_total_block,index_size;
	
	ciso_total_block = ciso->total_bytes / ciso->block_size;
	
	index_size = (ciso_total_block + 1 ) * sizeof(unsigned long);
	if(!(index_buf  = MALLOC(index_size))
	|| !(crc_buf    = MALLOC(index_size))
	|| !(block_buf1 = MALLOC(ciso->block_size))
	|| !(block_buf2 = MALLOC(ciso->block_size*2)))
	{
		if(index_buf) FREE(index_buf);
		if(crc_buf) FREE(crc_buf);
		if(block_buf1) FREE(block_buf1);
		ERROR("Can't allocate memory!");
	}
	else
	{
		z_stream z;
		int error = 0;
		SIZE_T write_pos;
		long block;
		int cmp_size;
		int status;
		int percent_period;
		register int percent_cnt;
		int align,align_b,align_m;
		unsigned char buf4[64];
		
		memset(index_buf,0,index_size);
		memset(crc_buf,0,index_size);
		memset(buf4,0,sizeof(buf4));
		
		// init zlib
		z.zalloc = Z_NULL;
		z.zfree  = Z_NULL;
		z.opaque = Z_NULL;
		
		if(!WRITE_OK(out,index_buf,index_size))
		{
			ERROR("dummy write index block");
			error = 1;
		}
		else
		{
			write_pos = sizeof(*ciso) + index_size;
			
			// compress data
			percent_period = ciso_total_block/100;
			percent_cnt    = 0;//ciso_total_block/100;
			
			align_b = 1<<(ciso->align);
			align_m = align_b -1;
			
			for( block = 0 ; block < ciso_total_block ; block++ )
			{
				if( CLI_MODE )
				{
					if(--percent_cnt < 1)
					{
						percent_cnt = percent_period;
						fprintf(stdout,"compress %3ld%% avarage rate %3ld%%\r"
							,block / percent_period
						//	,block==0 ? 0 : 100*write_pos/(block*0x800)
							,block==0 ? 0 : RATE(write_pos,ciso->total_bytes)
						);
						fflush(stdout);
					}
				}
				
				if(deflateInit2(&z, level , Z_DEFLATED, -15,8,Z_DEFAULT_STRATEGY) != Z_OK)
				{
					ERROR("deflateInit : %s", (z.msg) ? z.msg : "???");
					error = 1;
					break;
				}
				
				// write align
				align = (int)write_pos & align_m;
				if(align)
				{
					align = align_b - align;
					if(!WRITE_OK(out,buf4,align))
					{
						ERROR("block %d : Write error",block);
						error = 1;
						break;
					}
					write_pos += align;
				}
				
				// mark offset index
				index_buf[block] = write_pos>>(ciso->align);
				
				// read buffer
				z.next_out  = block_buf2;
				z.avail_out = ciso->block_size*2;
				z.next_in   = block_buf1;
				z.avail_in  = READ(in,block_buf1,ciso->block_size);
				if(z.avail_in != ciso->block_size)
				{
					ERROR("block=%ld : read error",block);
					error = 1;
					break;
				}
				
				// compress block
				status = deflate(&z, Z_FINISH);
				if(status != Z_STREAM_END)
				{
					ERROR("block %ld:deflate : %s[%ld]", block,(z.msg) ? z.msg : "error",status);
					error = 1;
					break;
				}
				
				cmp_size = ciso->block_size*2 - z.avail_out;
				
				// choise plain / compress
				if(cmp_size >= (signed)ciso->block_size)
				{
					// plain block mark
					index_buf[block] |= 0x80000000;
					
					cmp_size = ciso->block_size;
				//	MEMCPY(block_buf2,block_buf1,cmp_size);
					
					WRITE_BLOCK(out,block_buf1,cmp_size,block)
				}
				else
				{
					WRITE_BLOCK(out,block_buf2,cmp_size,block)
				}
				
				// mark next index
				write_pos += cmp_size;
				
				// term zlib
				if(deflateEnd(&z) != Z_OK)
				{
					ERROR("deflateEnd : %s", (z.msg) ? z.msg : "error");
					error = 1;
					break;
				}
			}
			
			if( ! error )
			{
				// last position (total size)
				index_buf[block] = write_pos>>(ciso->align);
				
				// endianess fix..
				for( block = 0 ; block <= ciso_total_block ; block++ )
				{
					ENDIANFIX(index_buf[block]);
				}
				
				// write header & index block
				if(!SEEK_OK(out,sizeof(*ciso),SEEK_SET)
				|| !WRITE_OK(out,index_buf,index_size)) {
					
					ERROR("lead out error");
					error = 1;
				}
				else if( CLI_MODE )
				{
					fprintf(stdout,"CISO Compression Completed :: Total Size = %8lu bytes :: Rate %ld%%\n"
						,write_pos,RATE(write_pos,ciso->total_bytes));
				}
			}
		}
		
		FREE(index_buf);
		FREE(crc_buf);
		FREE(block_buf1);
		FREE(block_buf2);
		
		rc = error;
	}
	
	return(rc);
}

static void ISOtoCSO(char *in,char *out,long level)
{
	if( level < 0 || level > 9 )
	{
		ERROR("Invalid compression level.");
	}
	else if(!OPENR_OK(infd,in))
	{
		ERROR("Can't open \"%s\"", in );
	}
	else
	{
		if(!OPENW_OK(outfd,out))
		{
			ERROR("Can't write to \"%s\"", out );
		}
		else
		{
			CISO_H ciso;
			int error = 1;
			
			memset(&ciso,0,sizeof(ciso));
			
			ciso.magic[0] = 'C';
			ciso.magic[1] = 'I';
			ciso.magic[2] = 'S';
			ciso.magic[3] = 'O';
			ciso.ver      = 0x01;
			
			ciso.block_size  = 0x800; /* ISO9660 one of sector */
			ciso.total_bytes = FILESIZE(in);
			
			if( CLI_MODE )
			{
				printf("Compressing '%s' to '%s'\n",in,out);
				printf("Total File Size %ld bytes\n",ciso.total_bytes);
				printf("block size      %ld  bytes\n",ciso.block_size);
				printf("total blocks    %ld  blocks\n",ciso.total_bytes / ciso.block_size);
				printf("index align     %ld\n",1L<<ciso.align);
				printf("compression level  %ld\n",level);
			}
			
			ENDIANFIX(ciso.total_bytes);
			ENDIANFIX(ciso.block_size);
			
			__setvbuf();
			
			if(!WRITE_OK(outfd,&ciso,sizeof(ciso)))
			{
				ERROR("Write Header Error");
			}
			else
			{
				ENDIANFIX(ciso.total_bytes);
				ENDIANFIX(ciso.block_size);
				
				error = ISOtoCSO_Stub(infd,outfd,&ciso,level);
			}
			
			CLOSE(outfd);
			
			if( error )
			{
				DELETEFILE(out);
			}
		}
		
		CLOSE(infd);
	}
}

//----------------------------------------------------------------------------

#ifdef AMIGAOS
# ifdef AROS
#  ifndef __libnix__
#   define __libnix__
#  endif /* __libnix__ */
# endif /* AROS */
# ifdef __libnix__
int __nocommandline=1;
# endif /* __libnix__ */
int main( void )
{
	LONG arg[3] = {0};
	struct RDArgs *args;
	
	_OPENASYNCIO
	if((args = ReadArgs( "SOURCE,DEST,LEVEL/N", (long*)arg, NULL)))
	{
		if(arg[0] && arg[1])
		{
			atexit(__fclose_all);
			
			if(arg[2])
			{
				ISOtoCSO((char *)arg[0],(char *)arg[1],*((int *)arg[2]));
			}
			else
			{
				CSOtoISO((char *)arg[0],(char *)arg[1]);
			}
		}
		
		FreeArgs(args);
	}
	_CLOSEASYNCIO
	
	return(0);
}
#else
int main(int c,char *v[])
{
	int l;
	
	if((l = atoi(v[3])) == 0)
	{
		CSOtoISO(v[1],v[2]);
	}
	else
	{
		
	}
}
#endif
