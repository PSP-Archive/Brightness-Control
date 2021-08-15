/**
	Brightness Control v1.0
	Copyright (C) 2012/05, Carlos DF (fLaSh)
	c4rl0s.pt@gmail.com
	
	main.c: Brightness Control main code

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <pspsysmem_kernel.h>
#include <pspdisplay_kernel.h>
#include <pspimpose_driver.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <psppower.h>
#include <pspctrl.h>
#include <pspsdk.h>
#include <string.h>
#include <psprtc.h>
#include <stdio.h>
#include <stdlib.h>

#include "sysconhk.h"
#include "blit.h"
#include "minIni.h"
#include "utils.h"

#define KERNEL_MODE 0x1000
#define HIGH_PRIORITY_THREAD 0x18
#define DEFAUT_PRIORITY_THREAD 0x12

#define ONE_SECOND 1000000

#define BRIGHTNESS_UP 1
#define BRIGHTNESS_DOWN 0
#define BRIGHTNESS_NONE 2

#define DISPLAY_MAX_TICK 200 	//Value for a timer used to display the brightness
#define DEFAUT_DISPLAY_FGCOLOR 0xFFFFFF  //Foregorund color is pure white
#define DEFAUT_DISPLAY_BGCOLOR 0x808080  //Background color is pure black

#define BRIGHTNESS_MESSAGE " Brightness %i "
#define BRIGHTNESS_CONFIG_FILE "brightness.bin"

#define INI_CONFIG_FILE "brightness.ini"

#define DEFAUT_KEY_COMBINATION (SYSCON_CTRL_LTRG + SYSCON_CTRL_LTRG)
#define DEFAUT_KEY_BUTTON_UP    SYSCON_CTRL_ALLOW_UP
#define DEFAUT_KEY_BUTTON_DN    SYSCON_CTRL_ALLOW_DN
#define DEFAUT_KEY_BUTTON_RT    SYSCON_CTRL_ALLOW_RT
#define DEFAUT_KEY_BUTTON_LT    SYSCON_CTRL_ALLOW_LT

#define SCREEN_ON  1
#define SCREEN_OFF 2

#define PATCH_BRIGHTNESS

#ifdef PATCH_BRIGHTNESS
	void (* _sceDisplaySetBrightness)(int brightness, int unk1) = NULL;
#endif

// #define DEBUG
#ifdef DEBUG
char debug_msg[128];
int debug_int = 0;
#endif

PSP_MODULE_INFO("Brightness", KERNEL_MODE, 1, 5);
PSP_MAIN_THREAD_ATTR(0);

//Prototypes
int sceDisplayEnable(void);
int sceDisplayDisable(void);

typedef struct _ConfigINI
{
		int ButtonKey;
		int ButtonUP;
		int ButtonDN;
		int ButtonRT;
		int ButtonLT;
		int LockControls;
		int StartupMSG;
		int DisplayMSG;
		int DisplayFGCOLOR;
		int DisplayBGCOLOR;
		char DisplayString[128];
		int Livel1;
		int Livel2;
		int Livel3;
		int Livel4;
		int Livel5;
} ConfigINI;

typedef struct _Config
{
		int brightness;
} Config;

enum {
   PSP_1000 = 0,
   PSP_2000 = 1,
   PSP_3000 = 2,
   PSP_4000 = 3,
   PSP_GO   = 4,
   PSP_7000 = 6,
   PSP_9000 = 8,
   PSP_11000 = 10,
};

static int running = 0;
static SceCtrlData sysconRawCtrl = {0};
static int displayTick = DISPLAY_MAX_TICK;
static u64 sleepTickLast = 0;
static u64 sysconScreenOnTick = 0;
static u32 sysconPrevButtons, sysconNewButtons;
static int screenState = SCREEN_ON;
static int sysconUnPressedScreen = 0;
static int lastSavedBrightness = -1;
static int imposeLivel[4];
static int brightnessLivel[5];
static ConfigINI configINI;
static Config config;
static char configPath[128];
static char iniPath[128];

void loadINI(void)
{
	configINI.ButtonKey = ini_getlhex("brightness", "ButtonKey", DEFAUT_KEY_COMBINATION, iniPath);
	configINI.ButtonUP = ini_getlhex("brightness", "ButtonUP", DEFAUT_KEY_BUTTON_UP, iniPath);
	configINI.ButtonDN = ini_getlhex("brightness", "ButtonDN", DEFAUT_KEY_BUTTON_DN, iniPath);
	configINI.ButtonRT = ini_getlhex("brightness", "ButtonRT", DEFAUT_KEY_BUTTON_RT, iniPath);
	configINI.ButtonLT = ini_getlhex("brightness", "ButtonLT", DEFAUT_KEY_BUTTON_LT, iniPath);
	configINI.LockControls = ini_getlhex("brightness", "LockControls", 0, iniPath);
	configINI.StartupMSG = ini_getlhex("brightness", "StartupMSG", 1, iniPath);
	configINI.DisplayMSG = ini_getlhex("brightness", "DisplayMSG", 1, iniPath);
	configINI.DisplayFGCOLOR = ini_getlhex("brightness", "DisplayFGCOLOR", DEFAUT_DISPLAY_FGCOLOR, iniPath);
	configINI.DisplayBGCOLOR = ini_getlhex("brightness", "DisplayBGCOLOR", DEFAUT_DISPLAY_BGCOLOR, iniPath);
	ini_gets("brightness", "DisplayString", BRIGHTNESS_MESSAGE, configINI.DisplayString, 128, iniPath);
	brightnessLivel[0] = ini_getl("brightness", "Livel1", 0, iniPath);
	brightnessLivel[1] = ini_getl("brightness", "Livel2", 0, iniPath);
	brightnessLivel[2] = ini_getl("brightness", "Livel3", 0, iniPath);
	brightnessLivel[3] = ini_getl("brightness", "Livel4", 0, iniPath);
	brightnessLivel[4] = ini_getl("brightness", "Livel5", 0, iniPath);
	// Fix bad values..
	int i;
	for (i = 0; i < 5; i++) {
		if ((brightnessLivel[i] < 11) || (brightnessLivel[i] > 100)) {
			brightnessLivel[i] = 0;
		}
	}
}

int getConfig(Config *cfg)
{
	SceUID fd;
	u32 k1;
   
	k1 = pspSdkSetK1(0);
	memset(cfg, 0, sizeof(*cfg));
	fd = sceIoOpen(configPath, PSP_O_RDONLY, 0644);

	if (fd < 0) {
		pspSdkSetK1(k1);
		return -1;
	}

	if (sceIoRead(fd, cfg, sizeof(*cfg)) != sizeof(*cfg)) {
		sceIoClose(fd);
		pspSdkSetK1(k1);
		return -2;
	}

	sceIoClose(fd);
	pspSdkSetK1(k1);

	return 0;
}

int setConfig(Config *cfg)
{
	u32 k1;
	SceUID fd;

	k1 = pspSdkSetK1(0);
	sceIoRemove(configPath);
	fd = sceIoOpen(configPath, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);

	if (fd < 0) {
		pspSdkSetK1(k1);
		return -1;
	}

	if (sceIoWrite(fd, cfg, sizeof(*cfg)) != sizeof(*cfg)) {
		sceIoClose(fd);
		pspSdkSetK1(k1);
		return -1;
	}

	sceIoClose(fd);
	pspSdkSetK1(k1);

	return 0;
}
  
void setConfigPath(const char *argp)
{
	#ifdef DEBUG
		strcpy(debug_msg, argp);
	#endif
	// Load config file from internal stored, special for a PSPgo
	int psp_model = sceKernelGetModel();
	if (psp_model == PSP_GO) {
		int dfd;
		const char *dir = "ef0:/SEPLUGINS/";
		dfd = sceIoDopen(dir);
		if (dfd >= 0) {
			strcpy(configPath, dir);
			strcpy(configPath + strlen(configPath), BRIGHTNESS_CONFIG_FILE);
			//
			strcpy(iniPath, dir);
			strcpy(iniPath + strlen(iniPath), INI_CONFIG_FILE);
			return;
		}
	}
	strcpy(configPath, argp);
	strrchr(configPath, '/')[1] = 0;
	strcpy(configPath + strlen(configPath), BRIGHTNESS_CONFIG_FILE);
	//
	strcpy(iniPath, argp);
	strrchr(iniPath, '/')[1] = 0;
	strcpy(iniPath + strlen(iniPath), INI_CONFIG_FILE);
}

void showDisplay(){
	displayTick = 0;
}

void setImposeLivels(void){	
	int psp_model = sceKernelGetModel();
	// PSP_1000 = 0,
	// PSP_2000 = 1,
	// PSP_3000 = 2,
	// PSP_4000 = 3,
	// PSP_GO   = 4,
	// PSP_7000 = 6,
	// PSP_9000 = 8,
	// PSP_11000 = 10,
	// per model set values:
	if ((psp_model == PSP_1000) || (psp_model == PSP_2000)) {
		imposeLivel[0] = 36;
		imposeLivel[1] = 44;
		imposeLivel[2] = 56;
		imposeLivel[3] = 68;
	} else {
		imposeLivel[0] = 44;
		imposeLivel[1] = 60;
		imposeLivel[2] = 72;
		imposeLivel[3] = 84;
	}
}

void setBrightnessImpose(int value){
	// This function corrects the difference between the brightness in diff states
	// when you start a game/homebrew or when back to xmb
	// Using a simple method selecting the more aprox. brightness value for the Impose object
	u32 k1;
	k1 = pspSdkSetK1(0);
	// -Set the Backlight level (0-4)
	// Eg: for a pspgo
	// 0 = 44
	// 1 = 60
	// 2 = 72
	// 3 = 84
	// 4 = 0 (screen off)
	// Select the more aprox. value
	if (value <= imposeLivel[0]) {
		sceImposeSetParam(PSP_IMPOSE_BACKLIGHT_BRIGHTNESS, 0);
	} else if (value <= imposeLivel[1]) {
		if ((value - imposeLivel[0]) < (imposeLivel[1]) -  value) {
			sceImposeSetParam(PSP_IMPOSE_BACKLIGHT_BRIGHTNESS, 0);
		} else {
			sceImposeSetParam(PSP_IMPOSE_BACKLIGHT_BRIGHTNESS, 1);
		}	
	} else if (value <= imposeLivel[2]) {
		if ((value - imposeLivel[1]) < (imposeLivel[2]) -  value) {
			sceImposeSetParam(PSP_IMPOSE_BACKLIGHT_BRIGHTNESS, 1);
		} else {
			sceImposeSetParam(PSP_IMPOSE_BACKLIGHT_BRIGHTNESS, 2);
		}		
	} else if (value <= imposeLivel[3]){
		if ((value - imposeLivel[2]) < (imposeLivel[3]) -  value) {
			sceImposeSetParam(PSP_IMPOSE_BACKLIGHT_BRIGHTNESS, 2);
		} else {
			sceImposeSetParam(PSP_IMPOSE_BACKLIGHT_BRIGHTNESS, 3);
		}	
	} else {
		sceImposeSetParam(PSP_IMPOSE_BACKLIGHT_BRIGHTNESS, 3);
	}
	
	pspSdkSetK1(k1);
}

int getBrightnessImpose(){
	int ret;
	u32 k1;
    k1 = pspSdkSetK1(0);
	ret = sceImposeGetParam(PSP_IMPOSE_BACKLIGHT_BRIGHTNESS);
    pspSdkSetK1(k1);
    return ret;
}

int getBrightness(){
	int ret;
	u32 k1;
    k1 = pspSdkSetK1(0);
	sceDisplayGetBrightness(&ret, 0);
    pspSdkSetK1(k1);
    return ret;
}

void setBrightness(int value){
	setBrightnessImpose(value);
	if (value >= 11) {
		config.brightness = value;
	}
	u32 k1;
    k1 = pspSdkSetK1(0);
	#ifdef PATCH_BRIGHTNESS
		if (_sceDisplaySetBrightness == NULL) {
			sceDisplaySetBrightness(value, 0);
		} else {
			_sceDisplaySetBrightness(value, 0);
		}
	#else
		sceDisplaySetBrightness(value, 0);
	#endif
    pspSdkSetK1(k1);
}

void saveBrightness(void) {
	// Save configs if need
	if (lastSavedBrightness != config.brightness) {
		setConfig(&config);
		lastSavedBrightness = config.brightness;
	}
}

void changeBrightness(int step, int direction) {
	
	u64 sleepNowTick;
	int sleepInterval;
	
	sceRtcGetCurrentTick(&sleepNowTick);

	if (step == 1) {
		sleepInterval = ONE_SECOND / 8;  // 0.16 secunds
	} else {
		sleepInterval = ONE_SECOND / 3;  // 0.33 secunds
	}
	// Make a simple function frezz..
	if (sleepTickLast > 0) {
		if ((sleepNowTick - sleepTickLast) < sleepInterval) {
			return;		
		}
	}

	sceRtcGetCurrentTick(&sleepTickLast);
	
	int brightness = getBrightness();
	
	if (direction == BRIGHTNESS_UP) {
		brightness = brightness + step;
	} else if (direction == BRIGHTNESS_DOWN) {
		brightness = brightness - step;
	} else if (direction == BRIGHTNESS_NONE) {
		// Egone
	}

	if (brightness < 11) brightness = 11;
	if (brightness > 100) brightness = 100;
	
	setBrightness(brightness);
		
	showDisplay();
}

void displayDisable() {
	u32 k1;
	k1 = pspSdkSetK1(0);
	sceDisplayDisable();
	pspSdkSetK1(k1);
	screenState = SCREEN_OFF;
}

void displayEnable() {
	u32 k1;
	k1 = pspSdkSetK1(0);
	sceDisplayEnable();
	pspSdkSetK1(k1);
	screenState = SCREEN_ON;
}

void setDisplay(int state) {
	if (state == SCREEN_ON) {
		setBrightness(config.brightness);
		displayEnable();
		if (configINI.StartupMSG) {
			showDisplay();
		}
	} else if (state == SCREEN_OFF) {
		setBrightness(0);
		displayDisable();
	}
}

#ifdef PATCH_BRIGHTNESS
void sceDisplaySetBrightness_Patched(int brightness, int unk1)
{
	// Dummy function, egnore..
	#ifdef DEBUG
		sprintf(debug_msg, "sceDisplaySetBrightness_Patched(%i, %i)", brightness, unk1); 
	#endif
}
typedef struct
{
   unsigned int major;
   unsigned int minor;
} fw_version;
void getFwVersion(fw_version *v)
{
   long int a = sceKernelDevkitVersion();
   v->major = (*((char *)&a+3));
   v->minor = (*((char *)&a+2)*10) + (*((char *)&a+1));
}
void PatchBrightness(SceSize args, void *argp)
{
	fw_version version;
	getFwVersion(&version);
	
	u32 nidSetBrightness = 0;

	// select appropriate NID
	if (version.major == 6) {
		if (version.minor == 61) {
			nidSetBrightness = 0x60112E07;
		} else if (version.minor == 60) {
			nidSetBrightness = 0x60112E07;
		} else if (version.minor == 39) {
			nidSetBrightness = 0x89FD2128;
		} else if (version.minor == 35) {
			nidSetBrightness = 0x89FD2128;
		} else if (version.minor == 20) {
			nidSetBrightness = 0xFF5A5D52;
		}
	}
	
	u32 text_addr = 0;
	if (nidSetBrightness > 0) {
		/* find the Brightness module */
		text_addr = FindFunc("sceDisplay_Service", "sceDisplay_driver", nidSetBrightness);// 0xFF5A5D52)
		if (text_addr != 0) {
			/* patch the Brightness set */
			PatchSyscall(text_addr, sceDisplaySetBrightness_Patched);
			/* ok, lets patch it */
			KERNEL_HIJACK_FUNCTION(text_addr, sceDisplaySetBrightness_Patched, _sceDisplaySetBrightness);
			// Clear caches
			ClearCaches();
		}
	}
	#ifdef DEBUG
		sprintf(debug_msg, "PatchBrightness %i, %i, %i.%i", text_addr, nidSetBrightness, version.major, version.minor);
	#endif
	sceKernelExitDeleteThread(0);
}
#endif

int main_thread(SceSize args, void *argp)
{
	// Load the INI file
	loadINI();
	// Load brightness settings
	if (getConfig(&config) != 0) {
		config.brightness = getBrightness();
	} else {
		 // Check for min and max brightness values
		if (config.brightness < 11) config.brightness = 11;
		if (config.brightness > 100) config.brightness = 100;
		// Set the brightness
		setBrightness(config.brightness);
	}
	lastSavedBrightness = config.brightness;
	// Install system hook
	install_syscon_hook();
	sceKernelDelayThread(ONE_SECOND);
	// Show brightness message on start up
	if (configINI.StartupMSG) {
		showDisplay();
	}
	// Infinite loop
	while (running)  
	{
		// Sleep loop
		sceKernelDelayThreadCB(ONE_SECOND / 5);
		
		// Show display?
	#ifdef DEBUG
		while (running) {
	#else
		while (displayTick < DISPLAY_MAX_TICK) {
	#endif
		
		#ifdef DEBUG
			blit_string(1, 1, debug_msg, DEFAUT_DISPLAY_FGCOLOR, DEFAUT_DISPLAY_BGCOLOR);
		#endif

			if (configINI.DisplayMSG) {
			
				char msg[128];
				sprintf(msg, configINI.DisplayString, getBrightness());
				blit_string(53, 4, msg, configINI.DisplayFGCOLOR, configINI.DisplayBGCOLOR);
				// Refresh the display..
				sceDisplayWaitVblankStart();
					
			} else {
				sceKernelDelayThreadCB(ONE_SECOND / 5);
			}
			// To prevent saving contantly the config file..
			// Only check it, when the display close..
			saveBrightness();
			
			// Incremment tick display..
			displayTick++;
		}

	}
	
	sceKernelExitDeleteThread(0);
	return 0;
}

/* Power Callback */
int power_callback(int unknown, int pwrflags, void *common)
{
	if (pwrflags & PSP_POWER_CB_RESUMING) {
		setDisplay(SCREEN_ON);
	}
	return 0;
}

/* Callback thread */
int CallbackThread(SceSize args, void *argp)
{
    int cbid;
    cbid = sceKernelCreateCallback("Power Callback", power_callback, NULL);
    scePowerRegisterCallback(0, cbid);
    // sceKernelSleepThreadCB();
	sceKernelExitDeleteThread(0);
	return 0;
}

/* Sets up the callback thread and returns its thread id */
void setupCallBacksAndPatch(void)
{
    int thid = sceKernelCreateThread("brightness_callback_thread", CallbackThread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) {
		sceKernelStartThread(thid, 0, 0);
	}
	#ifdef PATCH_BRIGHTNESS
		thid = sceKernelCreateThread("brightness_Patch_thread", PatchBrightness, 0x11, 0xFA0, 0, 0);
		if (thid >= 0) {
			sceKernelStartThread(thid, 0, 0);
		}
	#endif
}

int module_start(SceSize args, void *argp)
{
	running = 1;
	setConfigPath(argp);
	setImposeLivels();
	setupCallBacksAndPatch();
	/* Create a thread */
	int thid = sceKernelCreateThread("brightness_thread", main_thread, HIGH_PRIORITY_THREAD, 0x1000, 0, NULL); 
	if (thid > 0) {
		sceKernelStartThread(thid, args, argp);	
	}
	return 0;
}

int module_stop(SceSize args, void *argp)
{
	running = 0;
	uninstall_syscon_hook();
	return 0;
}

void syscon_ctrl(sceSysconPacket *packet)
{
	sysconNewButtons = 0; // Clear new buttons
	
	switch(packet->rx_response)
	{
	case 0x08: // Used for analogic stick

	case 0x07: // Used for digital buttons
		sysconNewButtons = syscon_get_dword(packet->rx_data);
		sysconNewButtons = ~sysconNewButtons;

		sysconPrevButtons = sysconRawCtrl.Buttons;
		sysconRawCtrl.Buttons = sysconNewButtons;
		
		if (((sysconNewButtons & configINI.ButtonKey) == configINI.ButtonKey) && (!(sysconNewButtons & SYSCON_CTRL_HOLD))) {

			if (getBrightness() > 10) { // Screen is ON?
			
				if ((sysconNewButtons & configINI.ButtonUP) == configINI.ButtonUP) {
				
					changeBrightness(1,  BRIGHTNESS_UP);
					if (configINI.LockControls) {
						sysconNewButtons &= ~configINI.ButtonKey;
						sysconNewButtons &= ~configINI.ButtonUP;
					}
					
				} else if ((sysconNewButtons & configINI.ButtonDN) == configINI.ButtonDN) {
				
					changeBrightness(1,  BRIGHTNESS_DOWN);
					if (configINI.LockControls) {
						sysconNewButtons &= ~configINI.ButtonKey;
						sysconNewButtons &= ~configINI.ButtonDN;
					}
					
				} else if ((sysconNewButtons & configINI.ButtonRT) == configINI.ButtonRT) {
				
					changeBrightness(10, BRIGHTNESS_UP);
					if (configINI.LockControls) {
						sysconNewButtons &= ~configINI.ButtonKey;
						sysconNewButtons &= ~configINI.ButtonRT;
					}
					
				} else if ((sysconNewButtons & configINI.ButtonLT) == configINI.ButtonLT) {
				
					changeBrightness(10, BRIGHTNESS_DOWN);
					if (configINI.LockControls) {
						sysconNewButtons &= ~configINI.ButtonKey;
						sysconNewButtons &= ~configINI.ButtonLT;
					}
					
				}
				
			}
			
		}
		
		// Usind this method to check the buttons works fine, but is a litle confused..
		// Here we ill control the SCREEN button, to change the brightness livels
		// and to turn the display ON/OFF manuality
		int sysconCheckBrightness = 0;
		if ((!(sysconNewButtons & SYSCON_CTRL_LCD)) || (!(sysconPrevButtons & SYSCON_CTRL_LCD))) {
			sysconScreenOnTick = 0;
		}
		
		// If the screen is turned off and back now to ON
		// set the right brightness	
		if (sysconUnPressedScreen == 0) {
			if (sysconNewButtons & SYSCON_CTRL_LCD) {
				if (!(sysconPrevButtons & SYSCON_CTRL_LCD)) {
					sysconUnPressedScreen = 1;
				}
			}
		} else {
			if ((sysconNewButtons & SYSCON_CTRL_LCD) && (screenState == SCREEN_ON)) {
				// Save the las tick for hold pressed SCREEN button
				if (sysconScreenOnTick == 0) {
					sceRtcGetCurrentTick(&sysconScreenOnTick);
				}
				u64 screenNowTick;
				sceRtcGetCurrentTick(&screenNowTick);
				// Check if a SCREEN button is pressed for some secunds to turn of the display
				if ((screenNowTick - sysconScreenOnTick) > (ONE_SECOND * 2)) {
					setDisplay(SCREEN_OFF);
					sysconCheckBrightness = 0;
					sysconUnPressedScreen = 0;
				}
			} else {
				sysconCheckBrightness = 1;
				sysconUnPressedScreen = 0;
			}
		}
		
		if (sysconCheckBrightness == 1) {
			if (screenState == SCREEN_OFF) {
				setDisplay(SCREEN_ON);
			} else { // Button SCREEN pressed?
			
				int brightness = getBrightness();
				if (brightness > 0) {
					int bChecked = 0;
					int i = 0;
					for (i = 0; i < 5; i++) {
						if (brightnessLivel[i] > 0) {
							if (brightness < brightnessLivel[i]) {
								setBrightness(brightnessLivel[i]);
								bChecked = 1;
								break;
							}
						}
					}
					i = 0;
					if (bChecked == 0) {
						for (i = 0; i < 5; i++) {
							if (brightnessLivel[i] > 0) {
								setBrightness(brightnessLivel[i]);
								bChecked = 1;
								break;
							}
						}
					}
					
					if (bChecked == 0) {
						// Set as standard system livels
						if (brightness < imposeLivel[0]) {
							setBrightness(imposeLivel[0]);
						} else if (brightness < imposeLivel[1]) {
							setBrightness(imposeLivel[1]);
						} else if (brightness < imposeLivel[2]) {
							setBrightness(imposeLivel[2]);
						} else if (brightness < imposeLivel[3]) {
							setBrightness(imposeLivel[3]);
						} else {
							setBrightness(imposeLivel[0]);
						}
					}
					
					showDisplay();
				
				} else if (config.brightness >= 11) {
					setDisplay(SCREEN_ON);
				}
				
			}
		}
		
		// Remove this button to the syscon..
		if (sysconNewButtons & SYSCON_CTRL_LCD) {
			sysconNewButtons &= ~SYSCON_CTRL_LCD;
			scePowerTick(PSP_POWER_TICK_DISPLAY);
		}
	}
	
	// Put the data to syscon..
	syscon_put_dword(packet->rx_data,~sysconNewButtons);
	syscon_make_checksum(&packet->rx_sts);
 
}

/* Exported function returns the address of module_info */
void* getModuleInfo(void)
{
	return (void *) &module_info;
}
