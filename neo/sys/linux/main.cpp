/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.
Copyright (C) 2012 Krzysztof Klinikowski <kkszysiu@gmail.com>
Copyright (C) 2012 Havlena Petr <havlenapetr@gmail.com>

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/
#include "../../idlib/precompiled.h"
#include "../posix/posix_public.h"
#include "../sys_local.h"
#include "local.h"

#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#ifdef ID_MCHECK
#include <mcheck.h>
#endif

#if defined(__ANDROID__)
#include <android/sensor.h>
#include <android/window.h>
#include <android_native_app_glue.h>
#endif

static idStr	basepath;
static idStr	savepath;

#if defined(__ANDROID__)
extern void GLimp_AndroidInit(ANativeWindow *win);
extern void GLimp_AndroidQuit();

extern void Posix_EarlyInit();
extern void Posix_LateInit();
extern bool Posix_AddMousePollEvent(int action, int value);
extern void Posix_QueEvent(sysEventType_t type, int value, int value2,
                           int ptrLength, void *ptr);
#endif

#if defined(__ANDROID__)
class idAndroidEngine
{
	public:
		idAndroidEngine()
			: mRunning(false),
			  mExiting(false),
			  mLastMotionX(0),
			  mLastMotionY(0) {
		}

		void pause() {
			mRunning = false;
		}

		void resume() {
			mRunning = true;
		}

		void render() {
			if (isRunning()) {
				common->Frame();
			}
		}

		void start(ANativeWindow *win) {
			GLimp_AndroidInit(win);
			Posix_EarlyInit();
			common->Init(0, NULL, NULL);
			Posix_LateInit();
		}

		void stop() {
			GLimp_AndroidQuit();
			common->Quit();
			mExiting = true;
		}

		bool isRunning() {
			return mRunning && common->IsInitialized();
		}

		bool isExiting() {
			return mExiting;
		}

		void setLastMotionPos(int32_t x, int32_t y) {
			mLastMotionX = x;
			mLastMotionY = y;
		}

		void getLastMotionPos(int32_t *x, int32_t *y) {
			*x = mLastMotionX;
			*y = mLastMotionY;
		}

	private:
		bool mRunning;
		bool mExiting;
		int32_t mLastMotionX;
		int32_t mLastMotionY;
};
#endif

/*
===========
Sys_InitScanTable
===========
*/
void Sys_InitScanTable(void)
{
	common->DPrintf("TODO: Sys_InitScanTable\n");
}

/*
=================
Sys_AsyncThread
=================
*/
void *Sys_AsyncThread(void *p)
{
	int now;
	int start, end;
	int ticked, to_ticked;

#if defined(__ANDROID__)
	xthreadInfo *threadInfo = static_cast<xthreadInfo *>(p);
	assert(threadInfo);
#endif

	start = Sys_Milliseconds();
	ticked = start >> 4;

	while (1) {
		start = Sys_Milliseconds();
		to_ticked = start >> 4;

		while (ticked < to_ticked) {
			common->Async();
			ticked++;
			Sys_TriggerEvent(TRIGGER_EVENT_ONE);
		}

		// sleep
		end = Sys_Milliseconds() - start;
		if (end < 16) {
			usleep(16 - end);
		}

		// thread exit
#if defined(__ANDROID__)
		if (threadInfo->threadCancel) {
			break;
		}
#else
		pthread_testcancel();
#endif
	}

	return NULL;
}

/*
 ==============
 Sys_DefaultSavePath
 ==============
 */
const char *Sys_DefaultSavePath(void)
{
#if defined(__ANDROID__)
	sprintf(savepath, "/sdcard/data/com.game.dante");
#else
#if defined( ID_DEMO_BUILD )
	sprintf(savepath, "%s/.doom3-demo", getenv("HOME"));
#else
	sprintf(savepath, "%s/.doom3", getenv("HOME"));
#endif
#endif
	return savepath.c_str();
}
/*
==============
Sys_EXEPath
==============
*/
const char *Sys_EXEPath(void)
{
	static char	buf[ 1024 ];
	idStr		linkpath;
	int			len;

	buf[ 0 ] = '\0';
	sprintf(linkpath, "/proc/%d/exe", getpid());
	len = readlink(linkpath.c_str(), buf, sizeof(buf));

	if (len == -1) {
		Sys_Printf("couldn't stat exe path link %s\n", linkpath.c_str());
		buf[ len ] = '\0';
	}

	return buf;
}

/*
================
Sys_DefaultBasePath

Get the default base path
- binary image path
- current directory
- hardcoded
Try to be intelligent: if there is no BASE_GAMEDIR, try the next path
================
*/
const char *Sys_DefaultBasePath(void)
{
#if defined(__ANDROID__)
	return ANDROID_DEFAULT_PATH;
#else
	struct stat st;
	idStr testbase;
	basepath = Sys_EXEPath();

	if (basepath.Length()) {
		basepath.StripFilename();
		testbase = basepath;
		testbase += "/";
		testbase += BASE_GAMEDIR;

		if (stat(testbase.c_str(), &st) != -1 && S_ISDIR(st.st_mode)) {
			return basepath.c_str();
		} else {
			common->Printf("no '%s' directory in exe path %s, skipping\n", BASE_GAMEDIR, basepath.c_str());
		}
	}

	if (basepath != Posix_Cwd()) {
		basepath = Posix_Cwd();
		testbase = basepath;
		testbase += "/";
		testbase += BASE_GAMEDIR;

		if (stat(testbase.c_str(), &st) != -1 && S_ISDIR(st.st_mode)) {
			return basepath.c_str();
		} else {
			common->Printf("no '%s' directory in cwd path %s, skipping\n", BASE_GAMEDIR, basepath.c_str());
		}
	}

	common->Printf("WARNING: using hardcoded default base path\n");
	return LINUX_DEFAULT_PATH;
#endif
}

/*
===============
Sys_GetConsoleKey
===============
*/
unsigned char Sys_GetConsoleKey(bool shifted)
{
	return shifted ? '~' : '`';
}

/*
===============
Sys_Shutdown
===============
*/
void Sys_Shutdown(void)
{
	basepath.Clear();
	savepath.Clear();
	Posix_Shutdown();
}

/*
===============
Sys_GetProcessorId
===============
*/
cpuid_t Sys_GetProcessorId(void)
{
	return CPUID_GENERIC;
}

/*
===============
Sys_GetProcessorString
===============
*/
const char *Sys_GetProcessorString(void)
{
	return "generic";
}

/*
===============
Sys_FPU_EnableExceptions
===============
*/
void Sys_FPU_EnableExceptions(int exceptions)
{
}

/*
===============
Sys_FPE_handler
===============
*/
void Sys_FPE_handler(int signum, siginfo_t *info, void *context)
{
	assert(signum == SIGFPE);
	Sys_Printf("FPE\n");
}

/*
===============
Sys_GetClockticks
===============
*/
double Sys_GetClockTicks(void)
{
#if defined( __i386__ )
	unsigned long lo, hi;

	__asm__ __volatile__(
	        "push %%ebx\n"			\
	        "xor %%eax,%%eax\n"		\
	        "cpuid\n"					\
	        "rdtsc\n"					\
	        "mov %%eax,%0\n"			\
	        "mov %%edx,%1\n"			\
	        "pop %%ebx\n"
	        : "=r"(lo), "=r"(hi));
	return (double) lo + (double) 0xFFFFFFFF * hi;
#else
#warning unsupported CPU
	return 0;
#endif
}

/*
===============
MeasureClockTicks
===============
*/
double MeasureClockTicks(void)
{
	double t0, t1;

	t0 = Sys_GetClockTicks();
	Sys_Sleep(1000);
	t1 = Sys_GetClockTicks();
	return t1 - t0;
}

/*
===============
Sys_ClockTicksPerSecond
===============
*/
double Sys_ClockTicksPerSecond(void)
{
	static bool		init = false;
	static double	ret;

	int		fd, len, pos, end;
	char	buf[ 4096 ];

	if (init) {
		return ret;
	}

#if defined(__ANDROID__)
	fd = open("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", O_RDONLY);
#else
	fd = open("/proc/cpuinfo", O_RDONLY);
#endif

	if (fd == -1) {
		common->Printf("couldn't read /proc/cpuinfo\n");
		ret = MeasureClockTicks();
		init = true;
		common->Printf("measured CPU frequency: %g MHz\n", ret / 1000000.0);
		return ret;
	}

	len = read(fd, buf, 4096);
	close(fd);

#if defined(__ANDROID__)
	if (len > 0) {
		ret = atof(buf);
		common->Printf("/proc/cpuinfo CPU frequency: %g MHz", ret / 1000.0);
		ret *= 1000;
		init = true;
		return ret;
	}
#else
	pos = 0;

	while (pos < len) {
		if (!idStr::Cmpn(buf + pos, "cpu MHz", 7)) {
			pos = strchr(buf + pos, ':') - buf + 2;
			end = strchr(buf + pos, '\n') - buf;

			if (pos < len && end < len) {
				buf[end] = '\0';
				ret = atof(buf + pos);
			} else {
				common->Printf("failed parsing /proc/cpuinfo\n");
				ret = MeasureClockTicks();
				init = true;
				common->Printf("measured CPU frequency: %g MHz\n", ret / 1000000.0);
				return ret;
			}

			common->Printf("/proc/cpuinfo CPU frequency: %g MHz\n", ret);
			ret *= 1000000;
			init = true;
			return ret;
		}

		pos = strchr(buf + pos, '\n') - buf + 1;
	}
#endif

	common->Printf("failed parsing /proc/cpuinfo\n");
	ret = MeasureClockTicks();
	init = true;
	common->Printf("measured CPU frequency: %g MHz\n", ret / 1000000.0);
	return ret;
}

/*
================
Sys_GetSystemRam
returns in megabytes
================
*/
int Sys_GetSystemRam(void)
{
	long	count, page_size;
	int		mb;

	count = sysconf(_SC_PHYS_PAGES);

	if (count == -1) {
		common->Printf("GetSystemRam: sysconf _SC_PHYS_PAGES failed\n");
		return 512;
	}

	page_size = sysconf(_SC_PAGE_SIZE);

	if (page_size == -1) {
		common->Printf("GetSystemRam: sysconf _SC_PAGE_SIZE failed\n");
		return 512;
	}

	mb= (int)((double)count * (double)page_size / (1024 * 1024));
	// round to the nearest 16Mb
	mb = (mb + 8) & ~15;
	return mb;
}

/*
==================
Sys_DoStartProcess
if we don't fork, this function never returns
the no-fork lets you keep the terminal when you're about to spawn an installer

if the command contains spaces, system() is used. Otherwise the more straightforward execl ( system() blows though )
==================
*/
void Sys_DoStartProcess(const char *exeName, bool dofork)
{
	bool use_system = false;

	if (strchr(exeName, ' ')) {
		use_system = true;
	} else {
		// set exec rights when it's about a single file to execute
		struct stat buf;

		if (stat(exeName, &buf) == -1) {
			printf("stat %s failed: %s\n", exeName, strerror(errno));
		} else {
			if (chmod(exeName, buf.st_mode | S_IXUSR) == -1) {
				printf("cmod +x %s failed: %s\n", exeName, strerror(errno));
			}
		}
	}

	if (dofork) {
		switch (fork()) {
			case -1:
				// main thread
				break;
			case 0:

				if (use_system) {
					printf("system %s\n", exeName);
					system(exeName);
					_exit(0);
				} else {
					printf("execl %s\n", exeName);
					execl(exeName, exeName, NULL);
					printf("execl failed: %s\n", strerror(errno));
					_exit(-1);
				}

				break;
		}
	} else {
		if (use_system) {
			printf("system %s\n", exeName);
			system(exeName);
			sleep(1);	// on some systems I've seen that starting the new process and exiting this one should not be too close
		} else {
			printf("execl %s\n", exeName);
			execl(exeName, exeName, NULL);
			printf("execl failed: %s\n", strerror(errno));
		}

		// terminate
		_exit(0);
	}
}

/*
=================
Sys_OpenURL
=================
*/
void idSysLocal::OpenURL(const char *url, bool quit)
{
	const char	*script_path;
	idFile		*script_file;
	char		cmdline[ 1024 ];

	static bool	quit_spamguard = false;

	if (quit_spamguard) {
		common->DPrintf("Sys_OpenURL: already in a doexit sequence, ignoring %s\n", url);
		return;
	}

	common->Printf("Open URL: %s\n", url);
	// opening an URL on *nix can mean a lot of things ..
	// just spawn a script instead of deciding for the user :-)

	// look in the savepath first, then in the basepath
	script_path = fileSystem->BuildOSPath(cvarSystem->GetCVarString("fs_savepath"), "", "openurl.sh");
	script_file = fileSystem->OpenExplicitFileRead(script_path);

	if (!script_file) {
		script_path = fileSystem->BuildOSPath(cvarSystem->GetCVarString("fs_basepath"), "", "openurl.sh");
		script_file = fileSystem->OpenExplicitFileRead(script_path);
	}

	if (!script_file) {
		common->Printf("Can't find URL script 'openurl.sh' in either savepath or basepath\n");
		common->Printf("OpenURL '%s' failed\n", url);
		return;
	}

	fileSystem->CloseFile(script_file);

	// if we are going to quit, only accept a single URL before quitting and spawning the script
	if (quit) {
		quit_spamguard = true;
	}

	common->Printf("URL script: %s\n", script_path);

	// StartProcess is going to execute a system() call with that - hence the &
	idStr::snPrintf(cmdline, 1024, "%s '%s' &",  script_path, url);
	sys->StartProcess(cmdline, quit);
}

/*
 ==================
 Sys_DoPreferences
 ==================
 */
void Sys_DoPreferences(void) { }

/*
================
Sys_FPU_SetDAZ
================
*/
void Sys_FPU_SetDAZ(bool enable)
{
	/*
	DWORD dwData;

	_asm {
		movzx	ecx, byte ptr enable
		and		ecx, 1
		shl		ecx, 6
		STMXCSR	dword ptr dwData
		mov		eax, dwData
		and		eax, ~(1<<6)	// clear DAX bit
		or		eax, ecx		// set the DAZ bit
		mov		dwData, eax
		LDMXCSR	dword ptr dwData
	}
	*/
}

/*
================
Sys_FPU_SetFTZ
================
*/
void Sys_FPU_SetFTZ(bool enable)
{
	/*
	DWORD dwData;

	_asm {
		movzx	ecx, byte ptr enable
		and		ecx, 1
		shl		ecx, 15
		STMXCSR	dword ptr dwData
		mov		eax, dwData
		and		eax, ~(1<<15)	// clear FTZ bit
		or		eax, ecx		// set the FTZ bit
		mov		dwData, eax
		LDMXCSR	dword ptr dwData
	}
	*/
}

/*
===============
mem consistency stuff
===============
*/

#ifdef ID_MCHECK

const char *mcheckstrings[] = {
	"MCHECK_DISABLED",
	"MCHECK_OK",
	"MCHECK_FREE",	// block freed twice
	"MCHECK_HEAD",	// memory before the block was clobbered
	"MCHECK_TAIL"	// memory after the block was clobbered
};

void abrt_func(mcheck_status status)
{
	Sys_Printf("memory consistency failure: %s\n", mcheckstrings[ status + 1 ]);
	Posix_SetExit(EXIT_FAILURE);
	common->Quit();
}

#endif

#if defined(__ANDROID__)

static void handleCmd(struct android_app *app, int32_t cmd)
{
	Sys_DebugPrintf("handleCmd start cmd(%i)", cmd);
	idAndroidEngine *engine = static_cast<idAndroidEngine *>(app->userData);

	switch (cmd) {
		case APP_CMD_SAVE_STATE:
			break;
		case APP_CMD_INIT_WINDOW:
			engine->start(app->window);
			break;
		case APP_CMD_TERM_WINDOW:
			engine->stop();
			break;
		case APP_CMD_RESUME:
		case APP_CMD_GAINED_FOCUS:
			engine->resume();
			break;
		case APP_CMD_PAUSE:
		case APP_CMD_LOST_FOCUS:
			engine->pause();
			break;
	}

	Sys_DebugPrintf("handleCmd end cmd(%i)", cmd);
}

static int32_t handleInput(struct android_app *app, AInputEvent *event)
{
	Sys_DebugPrintf("handleInput start");
	idAndroidEngine *engine = static_cast<idAndroidEngine *>(app->userData);

	switch (AInputEvent_getType(event)) {
		case AINPUT_EVENT_TYPE_MOTION:
			size_t count = AMotionEvent_getPointerCount(event);

			for (int i = 0; i < count; i++) {
				int32_t x = AMotionEvent_getX(event, i);
				int32_t y = AMotionEvent_getY(event, i);

				switch (AMotionEvent_getAction(event)) {
					case AMOTION_EVENT_ACTION_DOWN:
						Posix_QueEvent(SE_KEY, K_MOUSE1, true, 0, NULL);

						if (!Posix_AddMousePollEvent(M_ACTION1, true)) {
							return 0;
						}

						return 1;
					case AMOTION_EVENT_ACTION_UP:
						Posix_QueEvent(SE_KEY, K_MOUSE1, false, 0, NULL);

						if (!Posix_AddMousePollEvent(M_ACTION1, false)) {
							return 0;
						}

						return 1;
					case AMOTION_EVENT_ACTION_MOVE:
						int32_t lastX, lastY;
						engine->getLastMotionPos(&lastX, &lastY);
						int32_t dx = x - lastX;
						int32_t dy = y - lastY;
						Posix_QueEvent(SE_MOUSE, dx, dy, 0, NULL);
						// if we overflow here, we'll get a warning,
						// but the delta will be completely processed
						Posix_AddMousePollEvent(M_DELTAX, dx);

						if (!Posix_AddMousePollEvent(M_DELTAY, dy)) {
							return 0;
						}

						engine->setLastMotionPos(x, y);
						return 1;
				}
			}

			break;
	}

	Sys_DebugPrintf("handleInput stop");
	return 0;
}

/**
 * This is the main entry point of a native application that is using
 * android_native_app_glue.  It runs in its own thread, with its own
 * event loop for receiving input events and doing other things.
 */
void android_main(struct android_app *state)
{
	// Make sure glue isn't stripped.
	app_dummy();

#ifdef ID_MCHECK
	// must have -lmcheck linkage
	mcheck(abrt_func);
	Sys_Printf("memory consistency checking enabled\n");
#endif

	idAndroidEngine engine;
	state->userData = &engine;
	state->onAppCmd = handleCmd;
	state->onInputEvent = handleInput;

	if (state->activity) {
		ANativeActivity_setWindowFlags(state->activity,
		                               AWINDOW_FLAG_FULLSCREEN |
		                               AWINDOW_FLAG_KEEP_SCREEN_ON, 0);
	}

	Sys_Printf("main loop started");

	while (!engine.isExiting()) {
		int ident;
		int events;
		struct android_poll_source *source;

		// If not animating, we will block forever waiting for events.
		// If animating, we loop until all events are read, then continue
		// to draw the next frame of animation.
		while ((ident = ALooper_pollAll(engine.isRunning() ? 0 : -1, NULL, &events, (void **)&source)) >= 0) {
			if (source != NULL) {
				source->process(state, source);
			}
		}

		engine.render();
	}

	Sys_Printf("main loop stopped");
}

#else

/*
===============
main
===============
*/
int main(int argc, const char **argv)
{
#ifdef ID_MCHECK
	// must have -lmcheck linkage
	mcheck(abrt_func);
	Sys_Printf("memory consistency checking enabled\n");
#endif

	Posix_EarlyInit();

	if (argc > 1) {
		common->Init(argc-1, &argv[1], NULL);
	} else {
		common->Init(0, NULL, NULL);
	}

	Posix_LateInit();

	while (1) {
		common->Frame();
	}
}

#endif
