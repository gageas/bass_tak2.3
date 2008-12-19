void InitTak_func();

int TAK_OpenFile (TAKSTREAM *tak,BASSFILE file);
void WINAPI TAK_Free(HSTREAM handle);

DWORD CALLBACK StreamProc(HSTREAM handle, BYTE* buffer, DWORD length, TAKSTREAM *stream);

extern ADDON_FUNCTIONS TAKfuncs;