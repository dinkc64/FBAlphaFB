#include "burner.h"
#include "main.h"

extern int RunIdle();
extern int RunInit();
extern int RunExit();

int nAppVirtualFps = 6000; // App fps * 100
bool bRunPause = 0;

static char NVRAMPath[MAX_PATH];

void SetNVRAMPath(const char *path)
{
    strncpy(NVRAMPath, path, MAX_PATH);
}

int MainInit(const char *path, const char *setname)
{
    if (path == NULL || setname == NULL) {
        fprintf(stderr, "Path or set name uninitialized\n");
        return 0;
    }

    fprintf(stderr, "Initializing '%s' in '%s'\n", setname, path);

    SDL_Init(SDL_INIT_AUDIO);
    BurnLibInit();

    int i;
    for (i = 0; i < nBurnDrvCount; i++) {
        nBurnDrvActive = i;
        if (strcmp(BurnDrvGetTextA(0), setname) == 0) {
            break;
        }
    }

    if (i == nBurnDrvCount) {
        fprintf(stderr, "%s is not supported by FB Neo.", setname);
        return 0;
    }

    bCheatsAllowed = false;
    sprintf(szAppRomPaths[0], path);

    if (DrvInit(i, 0))
        return 0;

    MediaInit();
    RunInit();

    // Load NVRAM
    char temp[MAX_PATH];
    snprintf(temp, MAX_PATH, "%s/%s.nvr", NVRAMPath, BurnDrvGetTextA(0));
    BurnStateLoad(temp, 0, NULL);

    return 1;
}

int MainFrame()
{
    RunIdle();

    return 1;
}

int MainEnd()
{
    // Save NVRAM
    char temp[MAX_PATH];
    snprintf(temp, MAX_PATH, "%s/%s.nvr", NVRAMPath, BurnDrvGetTextA(0));
    BurnStateSave(temp, 0);

    // Cleanup
    RunExit();
    DrvExit();
    MediaExit();
    SDL_Quit();

    return 1;
}
