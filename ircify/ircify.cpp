/*
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
*/

#define _WIN32_WINNT _WIN32_WINNT_WINXP  //make everything XP compatible
#include "ircify.h"

HWND						MircHwnd;
HANDLE						hMircFileMap = NULL;
LPSTR						sMircData = NULL;
static CRITICAL_SECTION		CriticalSection;
unsigned int				crc_tab[256];
volatile BOOL				IsLoaded;
static char					SavedOutputStr[500] = { 0 };
static char webhelperport[10] = { 0 };
int GetAndSetPort();

int GetAndSetPort() {
	mEval("%ircify.port", webhelperport, 5);
	return SetConnectPort(atoi(webhelperport));
}

IRCIFY_API ChkStatus(HWND mwnd, HWND awnd, char data[300], char*, BOOL, BOOL)
{
	MircHwnd = mwnd;
	GetAndSetPort();
	switch (Status())
	{
	case 0:
		sprintf_s(data, 300, "0");
		break;
	case 1:
		sprintf_s(data, 300, "1");
		break;
	case 2:
		sprintf_s(data, 300, "2");
		break;
	}
	return 3;
}

int Status()
{
	TRACKINFO t = { 0 };
	memset(&t, 0, sizeof(TRACKINFO));
	int sngnfo = GetSongInfo(&t, 0);

	if (t.SpInfo.Running) {
		if (t.SpInfo.Playing) {
			return 1; //playing
		}
		else {
			return 2; //paused
		}
	}
	else {
		return 0; //spotify not running
	}
	return -1; //This should never happen
}

BOOLEAN WINAPI DllMain(HINSTANCE hDllHandle, DWORD nReason, LPVOID Reserved)
{
	switch (nReason)
	{
	case DLL_PROCESS_ATTACH:
		// mIrc does not handle RAPID (holding the np key down) execution of the dll..
		// it attempts to reload the dll several times.. which ends badly.. so instead
		// Check if the dll is already loaded, and if it is, force mirc to not load it again..
		if (IsLoaded) return -1;

		// 		AllocConsole();
		// 		freopen("CONOUT$", "w", stdout);

		IsLoaded = true;
		InitializeCriticalSectionAndSpinCount(&CriticalSection, 500);
		DisableThreadLibraryCalls(hDllHandle);

		if (hMircFileMap == NULL) {
			CreateDirectory(L"./cache", NULL);
			hMircFileMap = CreateFileMapping(INVALID_HANDLE_VALUE, 0, PAGE_READWRITE, 0, 1024, L"mIRC");
			sMircData = (LPSTR)MapViewOfFile(hMircFileMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
		}
		break;

	case DLL_PROCESS_DETACH:
		// Make sure we aren't still processing anything that
		// needs the mdata or filemap before unloading them..
		// Mirc doesn't always do things in the right order..
		EnterCriticalSection(&CriticalSection);
		if (hMircFileMap) {
			UnmapViewOfFile(sMircData);
			CloseHandle(hMircFileMap);
			hMircFileMap = NULL;
		}
		LeaveCriticalSection(&CriticalSection);
		DeleteCriticalSection(&CriticalSection);
		IsLoaded = false;
		break;
	}
	return TRUE;
}

IRCIFY_API dock(HWND mwnd, HWND awnd, char data[300], char*, BOOL, BOOL)
{
	int safteybreak = 0;
	char dockbuf[900];
	HWND settingsbox;
	HWND previewbox = 0;
	HWND spotthing = (HWND)atoi(data);
	settingsbox = FindWindowA(NULL, "ircify :: settings"); //find the ircify settings window in mirc
	do {
		previewbox = FindWindowExA(settingsbox, previewbox, "button", NULL);
		GetWindowTextA(previewbox, dockbuf, 900);
		safteybreak++;
	} while ((!strstr(dockbuf, "Preview Area")) && (safteybreak < 100)); //go through every button control in the window until we find the one named preview area.

	if (strstr(dockbuf, "Preview Area")) {
		SetParent(spotthing, previewbox);			//dock the preview window in the settings dialog.
		ShowWindow(spotthing, 3);
	}
	return 1;
}

IRCIFY_API ChkTrack(HWND mwnd, HWND awnd, char data[300], char*, BOOL, BOOL)
{
	MircHwnd = mwnd;
	GetAndSetPort();
	if (Status() < 1)
		return 1;
	char InputHash[900] = { 0 };
	mEval("%ircify.CurrTrack", InputHash, 900); //get the currently saved track hash from mirc

	TRACKINFO ti = { 0 };
	memset(&ti, 0, sizeof(TRACKINFO)); //make sure its nulled every time!

	int sngnfo = GetSongInfo(&ti, 0);
	if (sngnfo == -1) {
		return 1;
	}

	if (sngnfo == -5 || ti.tracktype == 2) {				// -5 = private session, which means no data to gather.. so get the titlebar instead..
		sprintf_s(data, 300, "0");
		return 3;
	}

	if (strcmp(InputHash, ti.URI)) { //compare hashes, if its not the same, save the current hash and return 1 to mirc.
		char test[500] = { 0 };
		sprintf_s(test, 300, "//set %%ircify.CurrTrack %s", ti.URI);
		mCmd(test);
		sprintf_s(data, 300, "1");
		return 3;
	}

	sprintf_s(data, 300, "0");
	return 3;
}

IRCIFY_API NowPlaying(HWND mwnd, HWND awnd, char data[300], char*, BOOL, BOOL)
{
	MircHwnd = mwnd;
	GetAndSetPort();
	BOOL DoMetadataLookup = true;
	if (Status() != 1) return 1; // Just return and halt if spotify is not playing or not running.

	if (strlen(data) > 1) {				//Check for the NoMeta parameter, used to make GetSongInfo not do a meta lookup.
		if (!strcmp(data, "NoMeta"))
			DoMetadataLookup = false;
	}

	TRACKINFO ti = { 0 };
	memset(&ti, 0, sizeof(TRACKINFO)); //make sure its nulled every time!

	int sngnfo = GetSongInfo(&ti, DoMetadataLookup);
	if (sngnfo == -1) {
		return 1;
	}
	if (ti.tracktype == 2) return 1;

	if ((ti.tracktype == 0) || (ti.tracktype == 1)) {
		CreateOutput(data, 0, &ti);
	}
	return 3;
}

IRCIFY_API Lookup(HWND mwnd, HWND awnd, char data[300], char*, BOOL, BOOL)
{
	MircHwnd = mwnd;
	GetAndSetPort();
	TRACKINFO ti = { 0 };
	memset(&ti, 0, sizeof(TRACKINFO));
	ti.tracktype = 1;
	MetadataLookup(data, &ti);
	CreateOutput(data, 1, &ti);
	return 3;
}

IRCIFY_API Version(HWND mwnd, HWND awnd, char data[300], char*, BOOL, BOOL)
{
	sprintf_s(data, 300, "Lib:%x-Dll:%s", LookupVersion(), GitStr);
	return 3;
}

IRCIFY_API Debug(HWND mwnd, HWND awnd, char data[300], char*, BOOL, BOOL)
{
	MircHwnd = mwnd;
	GetAndSetPort();
	TRACKINFO ti = { 0 };
	memset(&ti, 0, sizeof(TRACKINFO));
	int sngnfo = GetSongInfo(&ti, 0);

	char DebugStr[MAX_PATH] = { 0 };

	sprintf_s(DebugStr, 300, "\002Library:\002 %x", LookupVersion());
	mCmd(DebugStr);

	sprintf_s(DebugStr, 300, "\002Dll:\002 %s", GitStr);
	mMsg(DebugStr);

	mEval("$script(ircify.mrc)", data, MAX_PATH - 1);
	sprintf_s(DebugStr, 300, "\002Script Path:\002 %s", data);
	mMsg(DebugStr);

	mEval("$ircifvern", data, 14);
	sprintf_s(DebugStr, 300, "\002ScriptVer:\002 %s", data);
	mMsg(DebugStr);

	sprintf_s(DebugStr, 300, "\002Spotify:\002 %s", ti.SpInfo.SpVersion);
	mMsg(DebugStr);

	sprintf_s(DebugStr, 300, "\002SpotifyHelperApi:\002 %i", ti.SpInfo.LocApiVersion);
	mMsg(DebugStr);

	mEval("$version", data, 20);
	sprintf_s(DebugStr, 300, "\002Mirc Version:\002 %s", data);
	mMsg(DebugStr);

	mEval("$os", data, 20);
	sprintf_s(DebugStr, 300, "\002Os:\002 %s", data);
	mMsg(DebugStr);
	return 1;
}

IRCIFY_API GetCurrVol(HWND mwnd, HWND awnd, char data[300], char*, BOOL, BOOL)
{
	MircHwnd = mwnd;
	GetAndSetPort();
	TRACKINFO ti = { 0 };
	memset(&ti, 0, sizeof(TRACKINFO));
	int sngnfo = GetSongInfo(&ti, 0);
	sprintf_s(data, 300, "%.0f", ti.vol*100.0);
	return 3;
}

IRCIFY_API Cmd(HWND mwnd, HWND awnd, char data[300], char*, BOOL, BOOL)
{
	MircHwnd = mwnd;
	GetAndSetPort();
	// all the commands below are part of the LookupApi Library,
	// check the SpotifyLookupApi.h for avalable commands
	if (strlen(data) > 1) {
		//all the "Cmds" commented out no longer work...
/*		if (!strcmp(data, "PlayPause"))
			PlayPause();
		else if (!strcmp(data, "Next"))
			NextTrack();
		else if (!strcmp(data, "Prev"))
			PrevTrack();
		else if (!strcmp(data, "VolUp"))
			VolumeUp();
		else if (!strcmp(data, "VolDown"))
			VolumeDown();
		else if (!strcmp(data, "ShuffleToggle"))
			ShuffleToggle();
		else*/ if (strstr(data, "spotify:track:"))
			PlayURI(data, 0);
	}
	return 1;
}
// Get the Output format string from mirc using mEval, and replace the tags with the
// data from the getsonginfo/TRACKINFO struct.
int CreateOutput(char *out, int type, TRACKINFO *ti)
{
	char outputstr[500] = { 0 };
	// 	sprintf(outputstr,"Explicit: %i",ti->IsExplicit);
	// 	mEcho(outputstr);
	if (type == 0)
	{
		if (SavedOutputStr[0] == 0) {
			mEval("%ircify.output", SavedOutputStr, 500);
		}
		strcpy_s(outputstr, 500, SavedOutputStr);

		if (strlen(outputstr) < 4) {
			sprintf_s(outputstr, 500, "//set %%ircify.output \002np\002: .song. - .artist. (.album.) :: .uri.");
			mCmd(outputstr);
			sprintf_s(outputstr, 500, "\002np\002: .song. - .artist. (.album.) :: .uri.");
		}
	}
	else if (type == 1) {
		mEval("%ircify.luoutput", outputstr, 500);
		if (strlen(outputstr) < 4) {
			sprintf_s(outputstr, 500, "\002np\002: .song. - .artist. (.album.) :: .uri.");
		}
	}

	std::string OutputData;
	OutputData = outputstr;
	if (ti->IsPrivate) {
		if (OutputData.find(".uri.") != -1)
			OutputData = "\002np:\002 .artist. - .song. (.uri.)";
		else
			OutputData = "\002np:\002 .artist. - .song. (Private Mode)";
	}
	if (OutputData.find(".song.") != -1)
		OutputData.replace(OutputData.find(".song."), 6, ti->name);

	if (OutputData.find(".artist.") != -1)
		OutputData.replace(OutputData.find(".artist."), 8, ti->artist[0]);

	if (OutputData.find(".album.") != -1)
		OutputData.replace(OutputData.find(".album."), 7, ti->album);

	if (OutputData.find(".year.") != -1)
		OutputData.replace(OutputData.find(".year."), 6, ti->albumyear);

	if (OutputData.find(".explicit.") != -1) {
		if (ti->IsExplicit)
			OutputData.replace(OutputData.find(".explicit."), 10, "Explicit");
		else
			OutputData.replace(OutputData.find(".explicit."), 10, "");
	}
	if (OutputData.find(".uri.") != -1) {
		if (ti->tracktype == 1)
			OutputData.replace(OutputData.find(".uri."), 5, ti->URI);
		else if (ti->IsPrivate)
			OutputData.replace(OutputData.find(".uri."), 5, "Private Mode");
		else
			OutputData.replace(OutputData.find(".uri."), 5, "");
	}
	if (OutputData.find(".url.") != -1) {
		if (ti->tracktype == 1) {
			std::string UriPart = ti->URI;
			unsigned found = UriPart.find_last_of(":");
			std::string laststuff = UriPart.substr(found + 1).c_str();
			char UrlCombine[500] = { 0 };

			sprintf_s(UrlCombine, 500, "http://open.spotify.com/track/%s", laststuff.c_str());
			OutputData.replace(OutputData.find(".url."), 5, UrlCombine);
		}
		else if (ti->IsPrivate)
			OutputData.replace(OutputData.find(".url."), 5, "Private Mode");
		else
			OutputData.replace(OutputData.find(".url."), 5, "");
	}

	if (OutputData.find(".time.") != -1) {
		char PlayedTmp[30] = { 0 };
		convert_time(PlayedTmp, ti->length);
		OutputData.replace(OutputData.find(".time."), 6, PlayedTmp);
	}
	if (OutputData.find(".played.") != -1) {
		char PlayedTmp[30] = { 0 };
		convert_time(PlayedTmp, ti->currplay);
		OutputData.replace(OutputData.find(".played."), 8, PlayedTmp);
	}

	if (OutputData.find(".pbar.") != -1) {
		char BarTmp[30] = { 0 };
		int BarLen = (int)(((float)ti->currplay / (float)ti->length) *10.0f);
		if (BarLen <= 10 && BarLen >= 0) {
			memset(BarTmp, '|', BarLen + 1);
			BarTmp[BarLen + 1] = 3;				//yes i know this isn't the shortest code to do thi, but its fast!
			BarTmp[BarLen + 2] = '1';
			BarTmp[BarLen + 3] = '4';
			memset(BarTmp + BarLen + 4, ':', 10 - (BarLen + 1));
			BarTmp[13] = 3;
			OutputData.replace(OutputData.find(".pbar."), 6, BarTmp);
		}
	}
	if (OutputData.find(".popularity.") != -1) {
		char BarTmp[30] = { 0 };
		int BarLen = (int)((float)atof(ti->Popularity)*(float)10.0f);
		if (BarLen <= 10 && BarLen >= 0) {
			memset(BarTmp, '*', BarLen + 1);
			BarTmp[BarLen + 1] = 3;				//yes i know this isn't the shortest code to do thi, but its fast!
			BarTmp[BarLen + 2] = '1';
			BarTmp[BarLen + 3] = '4';
			memset(BarTmp + BarLen + 4, '*', 10 - (BarLen + 1));
			BarTmp[13] = 3;
			OutputData.replace(OutputData.find(".popularity."), 12, BarTmp);
		}
	}

	if (OutputData.find(".shuffle.") != -1) {
		if (ti->SpInfo.Shuffle)
			OutputData.replace(OutputData.find(".shuffle."), 9, "⇄");
		else
			OutputData.replace(OutputData.find(".shuffle."), 9, "");
	}

	if (OutputData.find(".coartists.") != -1) {
		if (ti->coartists > 1) {
			char CoArtistTmp[300] = { 0 };
			strcat_s(CoArtistTmp, " feat. ");
			strcat_s(CoArtistTmp, ti->artist[1]);
			int i = 1;

			do {
				i++;
				if (i == ti->coartists) { break; }
				strcat_s(CoArtistTmp, ", ");
				strcat_s(CoArtistTmp, ti->artist[i]);
			} while (i < 5);

			if (ti->coartists > 4)
				strcat_s(CoArtistTmp, "...");

			OutputData.replace(OutputData.find(".coartists."), 11, CoArtistTmp);
		}
		else {
			OutputData.replace(OutputData.find(".coartists."), 11, "");
		}
	}

	char TempOutputString[600] = { 0 };
	if (OutputData.find("|") != -1) {
		OutputData.replace(OutputData.find("|"), 1, "\002\002|");		// Replacing some chars that can cause issues
	}																// in mirc if they arent replaced.. Just adding 2x bold
	if (OutputData.find("%") != -1) {									// infront of them to make mirc not evaluate them, $eval(...,0) caused issues..
		OutputData.replace(OutputData.find("%"), 1, "\002\002%");
	}
	if (OutputData.find("$") != -1) {
		OutputData.replace(OutputData.find("$"), 1, "\002\002$");
	}

	if (type == 0) {
		sprintf_s(TempOutputString, 600, "/set %%ircify.data %s", OutputData.c_str());
	}
	else if (type == 1) {
		sprintf_s(TempOutputString, 600, "/set %%ircify.ludata %s", OutputData.c_str());
	}
	mCmd(TempOutputString); //execute the command set above
	lstrcpynA(out, OutputData.c_str(), 600); //copy the output so it can be returned in the main func using $dll
	return 1;
}

int convert_time(char *out, int sec) {
	if (sec < 1) { sprintf_s(out, 30, "00:00"); return 1; }

	int hours = (sec / 60 / 60) % 24;
	int minutes = (sec / 60) % 60;
	int seconds = sec % 60;

	if (sec > 3599)
		sprintf_s(out, 30, "%.2i:%.2i:%.2i", hours, minutes, seconds);
	else
		sprintf_s(out, 30, "%.2i:%.2i", minutes, seconds);

	return 1;
}

// mIrc communication.. Every command is inside criticalsections to make sure
// other threads is not able to modify the data while its being processed.
// $dllcall(..) in mirc is multithreaded and does not freeze mirc while processing!
void mCmd(const char * cmd)
{
	if (!TryEnterCriticalSection(&CriticalSection)) return;
	sprintf_s(sMircData, 1024, "%s", cmd);
	SendMessage(MircHwnd, WM_USER + 200, 1, 0);
	LeaveCriticalSection(&CriticalSection);
}
void mMsg(const char * cmd)
{
	if (!TryEnterCriticalSection(&CriticalSection)) return;
	sprintf_s(sMircData, 1024, "/say %s", cmd);
	SendMessage(MircHwnd, WM_USER + 200, 1, 0);
	LeaveCriticalSection(&CriticalSection);
}
void mEcho(const char * cmd)
{
	if (!TryEnterCriticalSection(&CriticalSection)) return;
	sprintf_s(sMircData, 1024, "/echo -s %s", cmd);
	SendMessage(MircHwnd, WM_USER + 200, 1, 0);
	LeaveCriticalSection(&CriticalSection);
}
void mEval(const char * data, char * res, int maxlen)
{
	if (!TryEnterCriticalSection(&CriticalSection)) return;
	lstrcpynA(sMircData, data, 1024);
	SendMessage(MircHwnd, WM_USER + 201, 0, 0);
	lstrcpynA(res, sMircData, maxlen);
	LeaveCriticalSection(&CriticalSection);
}
//////////////////////////////