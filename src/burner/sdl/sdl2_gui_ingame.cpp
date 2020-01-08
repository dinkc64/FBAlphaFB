#include "burner.h"
#include "sdl2_gui_common.h"

#if SDL_BYTEORDER != SDL_BIG_ENDIAN
const UINT32 amask = 0xff000000;
const UINT32 rmask = 0x00ff0000;
const UINT32 gmask = 0x0000ff00;
const UINT32 bmask = 0x000000ff;
#else
const UINT32 amask = 0x000000ff;
const UINT32 rmask = 0x0000ff00;
const UINT32 gmask = 0x00ff0000;
const UINT32 bmask = 0xff000000;
#endif

static SDL_Renderer* sdlRenderer = NULL;
static SDL_Surface* screenshot = NULL;
static SDL_Texture* screenshotTexture = NULL;

static SDL_Rect title_texture_rect;
static SDL_Rect dest_title_texture_rect;

static int screenW = 0;
static int screenH = 0;


struct MenuItem
{
	const char* name;			// The filename of the zip file (without extension)
  void (*menuFunction)();
  char* (*menuText)();
};

#define MAINMENU 0
#define SOUNDMENU 1
#define GRAPHICSMENU 2
#define SAVESTATE 3
#define LOADSTATE 4
#define SCREENSHOT 5
#define RESET 6

// menu item tracking
static UINT16 current_menu = MAINMENU;
static UINT16 current_selected_item = 0;

void QuickSave()
{
  QuickState(1);
}

void QuickLoad()
{
  QuickState(0);
}

void GraphicsMenuSelected()
{
  current_selected_item = 0;
  current_menu = GRAPHICSMENU;
}

void SoundMenuSelected()
{
  current_selected_item = 0;
  current_menu = SOUNDMENU;
}

#define MAINMENU_COUNT 7

struct MenuItem mainMenu[MAINMENU_COUNT] =
{
 {"Sound Options\0", SoundMenuSelected, NULL},
 {"Graphics Options\0", GraphicsMenuSelected, NULL},
 {"Save State\0", QuickSave, NULL},
 {"Load State\0", QuickLoad, NULL},
 {"Save Screenshot\0", NULL, NULL},
 {"Reset!\0", NULL, NULL},
 {NULL,NULL,NULL}
};

// menu instance tracking
struct MenuItem *current_menu_items = mainMenu;
static UINT16 current_item_count = MAINMENU_COUNT;

void ingame_gui_init()
{
  AudSoundStop();
}

void ingame_gui_exit()
{
  AudSoundPlay();
  SDL_FreeSurface(screenshot);
  SDL_DestroyTexture(screenshotTexture);
}

void ingame_gui_render()
{
  SDL_SetRenderDrawColor(sdlRenderer, 0x1a, 0x1e, 0x1d, SDL_ALPHA_OPAQUE);
  SDL_RenderClear(sdlRenderer);
  SDL_RenderCopy(sdlRenderer, screenshotTexture, &title_texture_rect, &dest_title_texture_rect);
  incolor(fbn_color, /* unused */ 0);
  inprint(sdlRenderer, "FinalBurn Neo", 10, 10);
  inprint(sdlRenderer, "=============", 10, 20);

  switch (current_menu)
  {
      case MAINMENU:
        current_item_count = MAINMENU_COUNT;
        current_menu_items = mainMenu;
        break;
  }

  for(int i=0; i < current_item_count; i ++)
	{
		if (i ==current_selected_item)
		{
			calcSelectedItemColor();
		}
		else
		{
			incolor(normal_color, /* unused */ 0);
		}
    inprint(sdlRenderer,current_menu_items[i].name , 10, 30+(10*i));
  }

  SDL_RenderPresent(sdlRenderer);
}

int ingame_gui_process()
{
	SDL_Event e;

	while (SDL_PollEvent(&e))
	{
		if (e.type == SDL_QUIT)
		{
			return 1;
		}
    if (e.type == SDL_KEYDOWN)
    {
      switch (e.key.keysym.sym)
      {
      case SDLK_TAB:
        return 1;
        break;
      }
    }
  }
  return 0;
}

void ingame_gui_start(SDL_Renderer* renderer)
{
  int finished = 0;

  sdlRenderer = renderer;
  SDL_GetRendererOutputSize(sdlRenderer, &screenW, &screenH);

  screenshot =  SDL_CreateRGBSurface(0, screenW, screenH, 32, rmask, gmask, bmask, amask);
  SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, screenshot->pixels, screenshot->pitch);
  screenshotTexture = SDL_CreateTextureFromSurface(renderer, screenshot);
  SDL_FreeSurface(screenshot);
  screenshot = NULL;

	title_texture_rect.x = 0; //the x coordinate
	title_texture_rect.y = 0; // the y coordinate
	title_texture_rect.w = screenW; //the width of the texture
	title_texture_rect.h = screenH; //the height of the texture

	dest_title_texture_rect.x = 150; //the x coordinate
	dest_title_texture_rect.y = 0; // the y coordinate
	dest_title_texture_rect.w = 50; //the width of the texture
	dest_title_texture_rect.h = 50; //the height of the texture

  ingame_gui_init();

  while (!finished)
  {
    finished = ingame_gui_process();
    ingame_gui_render();
  }

  ingame_gui_exit();

}