#include <windows.h>
#include "bass_tak.h"
#include "takfunc.h"

// bass plugin information
static const BASS_PLUGINFORM pluginforms[]={
	{BASS_CTYPE_STREAM,"TAK file","*.tak"},
};
static const BASS_PLUGININFO plugininfo={0x02030000,1,pluginforms};

typedef struct
{
	HSYNC	handle;
	DWORD	type;
	QWORD	param;
} SYNC;

// Additional config options
#define BASS_CONFIG_TAK_FREQ	0x11000
#define BASS_CONFIG_TAK_CHANS	0x11001

static DWORD freq=44100, chans=2;

// NOTE: Invalid value of HSTREAM is 0, not INVALID_HANDLE_VALUE.
HSTREAM WINAPI StreamCreateProc(BASSFILE file, DWORD flags){

	if (bassfunc->file.GetFlags(file)&BASSFILE_BUFFERED) error(BASS_ERROR_FILEFORM);

	TAKSTREAM *stream=(TAKSTREAM*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(TAKSTREAM));
	
	if (TAK_OpenFile(stream,file) != 0 ) {
		HeapFree(GetProcessHeap(),0,stream);
		error(BASS_ERROR_FILEFORM);
	}

	// restrict flags to valid ones, and create the BASS stream
	flags&=BASS_SAMPLE_FLOAT|BASS_SAMPLE_SOFTWARE|BASS_SAMPLE_LOOP|BASS_SAMPLE_3D|BASS_SAMPLE_FX
		|BASS_STREAM_DECODE|BASS_STREAM_AUTOFREE
		|0x3f000000; // 0x3f000000 = all speaker flags
	if (!(stream->handle=bassfunc->CreateStream((DWORD)stream->info.SAMPLERATE,stream->info.NCH,flags,(STREAMPROC*)&StreamProc,(DWORD)stream,&TAKfuncs))) {
		TAK_Free(stream->handle);
		return 0;
	}
	stream->flags=flags;
	bassfunc->file.SetStream(file,stream->handle); // set the stream associated with the file
	noerrorn(stream->handle);
}

HSTREAM WINAPI BASS_TAK_StreamCreateFile(BOOL mem, const void *file, DWORD offset, DWORD length, DWORD flags)
{
#ifndef _WIN32
if (badbass) error(BASS_ERROR_VERSION);
#endif

	BASSFILE bfile=bassfunc->file.Open(mem,file,offset,length,flags,TRUE);
	if (!bfile){
		return 0;
	}
	HSTREAM s=StreamCreateProc(bfile,flags);
	if (!s){
		bassfunc->file.Close(bfile);
		return 0;
	}
	return s;
}

HSTREAM WINAPI BASS_TAK_StreamCreateURL(const char *url, DWORD offset, DWORD flags, DOWNLOADPROC *proc, void *user)
{
	HSTREAM s;
	BASSFILE bfile;
#ifndef _WIN32
if (badbass) error(BASS_ERROR_VERSION);
#endif
	bfile=bassfunc->file.OpenURL(url,offset,flags,proc,(DWORD)user,TRUE);
	if (!bfile) return 0; // OpenURL set the error code
	s=StreamCreateProc(bfile,flags);
	if (!s) bassfunc->file.Close(bfile);
	return s;
}

const void *WINAPI BASSplugin(DWORD face){
	switch (face) {
		case BASSPLUGIN_INFO:
			return &plugininfo;
		case BASSPLUGIN_CREATE:
			return (void*)&StreamCreateProc;
	}
	return NULL;
}

BOOL WINAPI DllMain( HINSTANCE hinstDLL,DWORD fdwReason,LPVOID lpvReserved){
	switch(fdwReason){
		case DLL_PROCESS_ATTACH: //initialize
			InitTak_func();
			break;
		case DLL_PROCESS_DETACH:
			break;
	}

	return TRUE;
}
