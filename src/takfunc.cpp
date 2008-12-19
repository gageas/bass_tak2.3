#include <windows.h>
#include "bass_tak.h"
#include "takfunc.h"
#include "ioif.h"

// tag valueのバッファサイズの初期値。足りなかった場合は自動で拡張される
#define TAG_VALUE_SIZE 1024

extern TtakStreamIoInterface IOIF;

HANDLE streams_mutex = INVALID_HANDLE_VALUE;
static TAKSTREAM **streams=NULL; // steams, streamcについて、現実的な利用状況ではstreamsはTAKSTREAM[1]、streamc=1のまま動き続けるはずである。
static int streamc=0;
enum {STREAMS_OP_MODE_ADD,STREAMS_OP_MODE_REMOVE};
void operate_streams_slot(DWORD mode,TAKSTREAM* stream){
	WaitForSingleObject(streams_mutex,0);
	int i;
	switch(mode){
		case STREAMS_OP_MODE_ADD:
			for (i=0;i<streamc;i++){
				if(!streams[i]) break;
			}
			if (i==streamc) {
				streamc++;
				streams=(TAKSTREAM**)HeapReAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,streams,(streamc)*sizeof(TAKSTREAM*));
			}
			streams[i]=stream; // insert the new stream*/
			break;
		case STREAMS_OP_MODE_REMOVE:
			for (i=0;i<streamc;i++) {
				if (streams[i] && (streams[i]==stream)) {
					streams[i]=NULL; // clear the stream slot
					break;
				}
			}
			break;
	};
	ReleaseMutex(streams_mutex);
}
void InitTak_func(){
	if(streams_mutex == INVALID_HANDLE_VALUE){
		streams_mutex = CreateMutexA(0,FALSE,"BASS_TAK_STREAMS_SLOT_MUTEX");
		streams = (TAKSTREAM**)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(TAKSTREAM*));
		streamc = 1;
	}
}

static TAKSTREAM* GetTAKStream(HSTREAM handle){
	int i=0;
	for(i=0;i<streamc;i++){
		if(streams[i] == NULL)continue;
		if(streams[i]->handle == handle){
			return streams[i];
		}
	}
	return NULL;
}

static int setID3V24Frame(BYTE* buf,int charcode,const char* frameId,const BYTE* value,int valsize){
	memcpy(buf,frameId,4);
	if(charcode == -1){
		memcpy(buf+10,value,valsize);
	}else{
		buf[10] = charcode; // charcode
		memcpy(buf+11,value,valsize);
		valsize++;
	}
	buf[4] = (valsize>>21) & 0x7F;
	buf[5] = (valsize>>14) & 0x7F;
	buf[6] = (valsize>>7) & 0x7F;
	buf[7] = valsize & 0x7F;
	buf[8] = 0; // status flag;
	buf[9] = 0; // format flag;
	return 10 + valsize;
}

static void GetID3Tags(TAKSTREAM* stream){
	TtakAPEv2Tag tag;
	int i = 0; // loop counter 
	int len = 0; // tags count
	char key[64]; // tagName(key)
	BYTE* value; // value as UTF8
	char* valueAnsi; // value as Ansi
	int readed; // readed bytes of UTF8 value
	int readedAnsi; // readed bytes of Ansi value
	int tagsize = 0; // current V2 tag size

	tag = tak_SSD_GetAPEv2Tag(stream->streamDec);
	if( tak_APE_Valid(tag) != tak_True){return;}

	len = tak_APE_GetItemNum(tag);

	// init V1 Tag
	memcpy_s(stream->info.ID3V1,3,"TAG",3);

	tagsize = 10; // initial v2.4 tag size
	stream->info.ID3V2 = (BYTE*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,tagsize); //calloc(1,10); // init V2.4 Tag

	value = (BYTE*)HeapAlloc(GetProcessHeap(),0,TAG_VALUE_SIZE);
	valueAnsi = (char*)HeapAlloc(GetProcessHeap(),0,TAG_VALUE_SIZE);
	if(stream->info.ID3V2 == NULL){
		return; // could not alloc
	}
	memcpy_s(stream->info.ID3V2,3,"ID3",3);
	stream->info.ID3V2[3] = 0x04; //Version
	for(i=0;i<len;i++){
		tak_APE_GetItemKey(tag,i,key,64,&readed);

		tak_APE_GetItemValue(tag,i,NULL,0,&readed); // get Bytes
		if(readed > TAG_VALUE_SIZE){
			value = (BYTE*)HeapReAlloc(GetProcessHeap(),0,value,readed);
		}
		tak_APE_GetItemValue(tag,i,value,readed,&readed);

		tak_APE_GetTextItemValueAsAnsi(tag,i,-1,' ',NULL,0,&readedAnsi); // get bytes
		if(readedAnsi > TAG_VALUE_SIZE){
			valueAnsi = (char*)HeapReAlloc(GetProcessHeap(),0,valueAnsi,readedAnsi);
		}
		tak_APE_GetTextItemValueAsAnsi(tag,i,-1,' ',valueAnsi,readedAnsi,&readedAnsi);

		if( strcmp(key,APE_TAG_FIELD_TITLE) == 0 ){
			// for v2.4
			stream->info.ID3V2 = (BYTE*)HeapReAlloc(GetProcessHeap(),0,stream->info.ID3V2,tagsize+11+readed);
			tagsize += setID3V24Frame(stream->info.ID3V2+tagsize,3,"TIT2",value,readed);
			// for v1
			memcpy(stream->info.ID3V1 + 3,valueAnsi,min(readedAnsi,30));
		}else if( strcmp(key,APE_TAG_FIELD_ARTIST) == 0 ){
			stream->info.ID3V2 = (BYTE*)HeapReAlloc(GetProcessHeap(),0,stream->info.ID3V2,tagsize+11+readed);
			tagsize += setID3V24Frame(stream->info.ID3V2+tagsize,3,"TPE1",value,readed);
			// for v1
			memcpy(stream->info.ID3V1 + 33,valueAnsi,min(readedAnsi,30));
		}else if( strcmp(key,APE_TAG_FIELD_ALBUM) == 0 ){
			stream->info.ID3V2 = (BYTE*)HeapReAlloc(GetProcessHeap(),0,stream->info.ID3V2,tagsize+11+readed);
			tagsize += setID3V24Frame(stream->info.ID3V2+tagsize,3,"TALB",value,readed);
			// for v1
			memcpy(stream->info.ID3V1 + 63,valueAnsi,min(readedAnsi,30));
		}else if( strcmp(key,APE_TAG_FIELD_YEAR) == 0 ){
			stream->info.ID3V2 = (BYTE*)HeapReAlloc(GetProcessHeap(),0,stream->info.ID3V2,tagsize+11+readed);
			tagsize += setID3V24Frame(stream->info.ID3V2+tagsize,0,"TDRC",value,readed);
			// for v1
			memcpy(stream->info.ID3V1 + 93,valueAnsi,min(readedAnsi,4));
		}else if( strcmp(key,APE_TAG_FIELD_COMMENT) == 0 ){
			BYTE* t= (BYTE*)HeapAlloc(GetProcessHeap(),0,4+readed);
			strcpy_s((char*)t,4,"JPN");// J,P,N,(0)
			memcpy_s((char*)t+4,readed,value,readed);
			readed+=4;
			stream->info.ID3V2 = (BYTE*)HeapReAlloc(GetProcessHeap(),0,stream->info.ID3V2,tagsize+11+readed);

			tagsize += setID3V24Frame(stream->info.ID3V2+tagsize,3,"COMM",t,readed);
			HeapFree(GetProcessHeap(),0,t);
			memcpy(stream->info.ID3V1 + 97,valueAnsi,min(readedAnsi,28));
		}else if( strcmp(key,APE_TAG_FIELD_TRACK) == 0 ){
			stream->info.ID3V2 = (BYTE*)HeapReAlloc(GetProcessHeap(),0,stream->info.ID3V2,tagsize+11+readed);
			tagsize += setID3V24Frame(stream->info.ID3V2+tagsize,0,"TRCK",value,readed);
			// for v1
			stream->info.ID3V1[126] = atoi(valueAnsi);
		}
	}
	HeapFree(GetProcessHeap(),0,value);
	HeapFree(GetProcessHeap(),0,valueAnsi);
	tagsize -= 10;
	stream->info.ID3V2[9] = tagsize & 0x7F;
	stream->info.ID3V2[8] = (tagsize>>7) & 0x7F;
	stream->info.ID3V2[7] = (tagsize>>14) & 0x7F;
	stream->info.ID3V2[6] = (tagsize>>21) & 0x7F;
}

static void GetApeTag(TAKSTREAM* stream){
	BASSFILE file;
	file = stream->f;
	BYTE apeTagFooterBuf[APE_TAG_FOOTER_BYTES];
	QWORD fileSize; // tak file size
	DWORD tagSize; // tagSize = apeTagBody + apeTagFooter[32]

	fileSize = bassfunc->file.GetPos(file,BASS_FILEPOS_END);
	bassfunc->file.Seek(file,fileSize - APE_TAG_FOOTER_BYTES);
	bassfunc->file.Read(file,apeTagFooterBuf,APE_TAG_FOOTER_BYTES);
	if(memcmp(apeTagFooterBuf,"APETAGEX",8)==0){
		tagSize = (apeTagFooterBuf[15]<<24)+(apeTagFooterBuf[14]<<16)+(apeTagFooterBuf[13]<<8)+apeTagFooterBuf[12];
		if(tagSize >= (1024 * 1024 * 16)){ // too large
			return;
		}
		stream->info.APETAG = (BYTE*)HeapAlloc(GetProcessHeap(),0,tagSize+APE_TAG_FOOTER_BYTES); // header[32] + body + footer[32]
		if(stream->info.APETAG != NULL){
			memcpy(stream->info.APETAG,apeTagFooterBuf,APE_TAG_FOOTER_BYTES);
			bassfunc->file.Seek(file,fileSize - tagSize);
			bassfunc->file.Read(file,stream->info.APETAG + APE_TAG_FOOTER_BYTES,tagSize);
		}
		bassfunc->file.Seek(file,0);
	}
}


// called by BASS_StreamGetLength - get the stream playback length
static QWORD WINAPI TAK_GetLength(HSTREAM handle)
{
	TAKSTREAM *stream=GetTAKStream(handle);
	//	if (mode!=BASS_POS_BYTE) errorn(BASS_ERROR_NOTAVAIL); // only support byte positioning
	noerrorn(stream->info.DATALENGTH*((stream->flags & BASS_SAMPLE_FLOAT)?4:2)*stream->info.NCH); // return as 16bit
}

// get Tags
static const char *WINAPI TAK_GetTags(HSTREAM handle, DWORD tags)
{
	TAKSTREAM *stream=GetTAKStream(handle);

	switch(tags){
		case BASS_TAG_APE:
			return (char*)stream->info.APETAG;
			break;
		case BASS_TAG_ID3V2:
			return (char*)stream->info.ID3V2;
			break;
		case BASS_TAG_ID3:
			return (char*)stream->info.ID3V1;
			break;
	}
	return NULL;
}

static void WINAPI TAK_GetInfo(HSTREAM handle, BASS_CHANNELINFO* info)
{
	TAKSTREAM *stream=GetTAKStream(handle);
	info->flags|=bassfunc->file.GetFlags(stream->f)&BASS_STREAM_RESTRATE;
	info->origres=stream->info.BPS;
	info->origres=16;
	info->ctype=BASS_CTYPE_STREAM_TAK;
}

// called by BASS_ChannelSetFlags
// return accepted flags (BASS only uses BASS_SAMPLE_LOOP/BASS_STREAM_AUTOFREE/BASS_STREAM_RESTRATE)
static DWORD WINAPI TAK_SetFlags(HSTREAM handle, DWORD flags)
{
	return flags;
}

static QWORD WINAPI TAK_GetPosition(HSTREAM handle, QWORD pos)
{
	TAKSTREAM *stream=GetTAKStream(handle);
	int BytePerSample = (stream->flags & BASS_SAMPLE_FLOAT?4:2)*stream->info.NCH; //stream->info.BPS/8
	return tak_SSD_GetReadPos(stream->streamDec);
}

static BOOL WINAPI TAK_CanSetPosition(HSTREAM handle, QWORD pos)
{
	if (pos>=TAK_GetLength(handle)) error(BASS_ERROR_POSITION);
	return TRUE;
}

// called by BASS_ChannelSetPosition
// called after the above function - do the actual seeking
// return the actual resulting position
static QWORD WINAPI TAK_SetPosition(HSTREAM handle, QWORD pos)
{
	TAKSTREAM *stream=GetTAKStream(handle);

	int BytePerSample = (stream->flags & BASS_SAMPLE_FLOAT?4:2)*stream->info.NCH; //stream->info.BPS/8
	tak_SSD_Seek(stream->streamDec,pos/BytePerSample);
	return pos;
}

void WINAPI TAK_Free(HSTREAM handle)
{
	TAKSTREAM* tak;
	if((tak = GetTAKStream(handle)) == NULL)return;

	tak_SSD_Destroy(tak->streamDec);
	HeapFree(GetProcessHeap(),0,tak->prebufAlloc);
	HeapFree(GetProcessHeap(),0,tak->info.APETAG);
	HeapFree(GetProcessHeap(),0,tak->info.ID3V2);
	HeapFree(GetProcessHeap(),0,tak);

	operate_streams_slot(STREAMS_OP_MODE_REMOVE,tak);
}

static void Tak_Damaged(void * AUser,PtakSSDDamageItem item){ // `called on every occurrence of a data error.' Currently, nothing to do.
	return ;
}

int TAK_OpenFile (TAKSTREAM *tak,BASSFILE file) {
	Ttak_str_StreamInfo info;
	TtakSSDOptions Options;
	Options.Cpu = tak_Cpu_Any;
	Options.Flags = 0;

	tak->f = file;

	tak->streamDec = tak_SSD_Create_FromStream(&IOIF,tak,&Options,Tak_Damaged,tak);
	if(tak->streamDec == NULL){
		return -1;
	}

	if(tak_SSD_Valid(tak->streamDec) != tak_True){
		goto failed;
	}
	if(tak_SSD_GetStreamInfo(tak->streamDec,&info) != tak_res_Ok){
		goto failed;
	}

	operate_streams_slot(STREAMS_OP_MODE_ADD,tak);

	tak->info.NCH = info.Audio.ChannelNum;
	tak->info.BPS = info.Audio.SampleBits;
	tak->info.BSIZE = info.Sizes.FrameSize;
	tak->info.FORMAT = info.Encoder.Codec;
	tak->info.SAMPLERATE = info.Audio.SampleRate;
	tak->info.DATALENGTH = info.Sizes.SampleNum;
	GetApeTag(tak);
	GetID3Tags(tak);
	tak_SSD_Seek(tak->streamDec,0);
	return 0;

failed:
	tak_SSD_Destroy(tak->streamDec);
	return -1;
}

// return read bytes or BASS_STREAMPROC_END
static inline DWORD TAK_ReadStream(BYTE* buffer,DWORD length,TAKSTREAM *stream){
	int takBytePerSample = stream->info.BPS/8*stream->info.NCH; // eg 16bit stereo -> 4
	int outBytePerSample = ((stream->flags & BASS_SAMPLE_FLOAT)?4:2)*stream->info.NCH;

	int readSamples = length/outBytePerSample;
	int readedSampleCount;
	int readedBytes;

	BYTE* readBuf;

	readBuf = (BYTE*)HeapAlloc(GetProcessHeap(),0,readSamples*takBytePerSample);
	if(tak_SSD_ReadAudio(stream->streamDec , readBuf , readSamples , &readedSampleCount) != tak_res_Ok){
		HeapFree(GetProcessHeap(),0,readBuf);
		return BASS_STREAMPROC_END;
	}
	if(readedSampleCount <= 0){
		HeapFree(GetProcessHeap(),0,readBuf);
		return BASS_STREAMPROC_END;
	}
	readedBytes = readedSampleCount*outBytePerSample;

	long elementCnt = readedSampleCount*stream->info.NCH;
	BYTE* bufP = readBuf;
	if(stream->flags & BASS_SAMPLE_FLOAT){
		bassfunc->data.int2float(bufP,(float*)buffer,elementCnt,stream->info.BPS/8);
	}else{
		switch(stream->info.BPS){
			case 8: // 8bit -> 16bit
				while(elementCnt--){
					*((signed short*)buffer) = (((signed int)(*bufP++))-128) * 0x100;
					buffer+=2;
				}
				break;
			case 16: // 16bit -> 16bit
				memcpy(buffer,readBuf,readedBytes);
				break;
			case 24: // 24bit -> 16bit
				while(elementCnt--){ // tested with http://trendy.nikkeibp.co.jp/article/news/20060419/116354/
					bufP++;
					*buffer++=*bufP++;
					*buffer++=*bufP++;
				}
				break;
			case 32: // 32bit -> 16bit not tested.
				while(elementCnt--){
					bufP+=2;
					*buffer++=*bufP++;
					*buffer++=*bufP++;
				}
				break;
		}
	}
	HeapFree(GetProcessHeap(),0,readBuf);
	return readedBytes;
}

static void WINAPI TAK_PreBuf(HSTREAM handle, DWORD len)
{
	TAKSTREAM *stream=GetTAKStream(handle);
	if(stream == NULL){return;}
	stream->prebuflen=0;
	if(len > stream->prebufSize){
		stream->prebufAlloc = (BYTE*)HeapReAlloc(GetProcessHeap(),0,stream->prebufAlloc,len);
		if(stream->prebufAlloc == NULL){ // could not alloc
			stream->prebufSize = 0;
			return;
		}
		stream->prebufSize = len;
	}
	stream->prebuf = stream->prebufAlloc;
	stream->prebuflen = TAK_ReadStream(stream->prebuf,len,stream);
	if(stream->prebuflen & BASS_STREAMPROC_END){
		stream->prebuflen^BASS_STREAMPROC_END;
	}
}

DWORD CALLBACK StreamProc(HSTREAM handle, BYTE* buffer, DWORD length, TAKSTREAM *stream)
{
	DWORD count=0;
	DWORD offset=0;

	// use prebuffer
	if(stream->prebuflen>0){
		DWORD bytesToCopy = min(length,stream->prebuflen);
		memcpy(buffer,stream->prebuf,bytesToCopy);
		buffer += bytesToCopy;
		length -= bytesToCopy;
		stream->prebuf += bytesToCopy;
		stream->prebuflen -= bytesToCopy;
		offset += bytesToCopy;

		if(length == 0){
			return bytesToCopy;
		}
	}

	return offset+TAK_ReadStream(buffer,length,stream);
}


// add-on function table
ADDON_FUNCTIONS TAKfuncs={
	TAK_Free,
	TAK_GetLength,
	TAK_GetTags, //	TAK_GetTags,
	TAK_PreBuf,
	NULL,
	TAK_GetInfo,
	TAK_CanSetPosition,
	TAK_SetPosition,
	NULL, // let BASS handle the position/looping/syncing (POS & END)
	NULL,
	NULL,
	NULL, // let BASS decide when to resume a stalled stream
	TAK_SetFlags
};
