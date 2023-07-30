#include <SDL2/SDL.h>
#include <dirent.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <Imlib2.h>
#include <stdlib.h>

typedef struct
{
    char** path;
    unsigned size;
}
Paths;

typedef struct
{
    SDL_Texture** texture;
    unsigned size;
}
Textures;

typedef struct View
{
    int speed;
    Textures textures;
    SDL_Rect* rect;
    struct View* next;
}
View;

typedef struct
{
    Display* x11d;
    SDL_Window* window;
    SDL_Renderer* renderer;
}
Video;

static void Quit(const char* const message, ...)
{
    va_list args;
    va_start(args, message);
    vfprintf(stdout, message, args);
    va_end(args);
    exit(1);
}

static int Compare(const void* a, const void* b)
{
    char* const pa = *(char**) a;
    char* const pb = *(char**) b;
    const unsigned la = strlen(pa);
    const unsigned lb = strlen(pb);
    return (la > lb) ? 1 : (la < lb) ? -1 : strcmp(pa, pb);
}

static void Sort(Paths* self)
{
    qsort(self->path, self->size, sizeof(*self->path), Compare);
}

static Paths Populate(const char* base)
{
    DIR* const dir = opendir(base);
    if(dir == NULL)
        Quit("Directory '%s' failed to open\n", base);
    unsigned max = 8;
    Paths self;
    self.size = 0;
    self.path = malloc(max * sizeof(*self.path));
    for(struct dirent* entry; (entry = readdir(dir));)
    {
        const char* const path = entry->d_name;
        if(strstr(path, ".bmp"))
        {
            char* const slash = "/";
            char* const buffer = malloc(strlen(base) + strlen(slash) + strlen(path) + 1);
            strcpy(buffer, base);
            strcat(buffer, slash);
            strcat(buffer, path);
            if(self.size == max)
            {
                max *= 2;
                self.path = realloc(self.path, max * sizeof(*self.path));
            }
            self.path[self.size] = buffer;
            self.size += 1;
        }
    }
    closedir(dir);
    Sort(&self);
    return self;
}

static void Depopulate(Paths* self)
{
    for(unsigned i = 0; i < self->size; i++)
        free(self->path[i]);
    free(self->path);
}

static Textures Cache(Paths* paths)
{
    Textures self;
    self.size = paths->size;
    self.texture = malloc(self.size * sizeof(*self.texture));
    for(unsigned i = 0; i < self.size; i++)
    {
        const char* const path = paths->path[i];
        self.texture[i] = imlib_load_image(path);
        if (self.texture[i] == NULL)
            Quit("File '%s' failed to open. %s\n", path, SDL_GetError());
    }
    return self;
}

static void Destroy(Textures* self)
{
    for(unsigned i = 0; i < self->size; i++)
        SDL_DestroyTexture(self->texture[i]);
    free(self->texture);
}

static Video Setup(void)
{
    Video self;
    self.x11d = XOpenDisplay(NULL);
    SDL_Init(SDL_INIT_VIDEO);

    // Create a new window that covers the whole screen
    int screen = DefaultScreen(self.x11d);
    Window x11w = XCreateSimpleWindow(self.x11d, RootWindow(self.x11d, screen), 0, 0,
                                      DisplayWidth(self.x11d, screen), DisplayHeight(self.x11d, screen),
                                      0, BlackPixel(self.x11d, screen), BlackPixel(self.x11d, screen));

    // Set the window's type to _NET_WM_WINDOW_TYPE_DESKTOP
    Atom type = XInternAtom(self.x11d, "_NET_WM_WINDOW_TYPE", False);
    Atom value = XInternAtom(self.x11d, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    XChangeProperty(self.x11d, x11w, type, XA_ATOM, 32, PropModeReplace, (unsigned char *)&value, 1);

    // Map the window (make it visible)
    XMapWindow(self.x11d, x11w);

    // Create an SDL window from this X11 window
    self.window = SDL_CreateWindowFrom((void *)x11w);
    self.renderer = SDL_CreateRenderer(self.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    return self;
}

static void Teardown(Video* self)
{
    XCloseDisplay(self->x11d);
    SDL_Quit();
    SDL_DestroyWindow(self->window);
    SDL_DestroyRenderer(self->renderer);
}

static View* Init(const char* const base, const int speed, SDL_Rect* rect, Video* video)
{
    View* self = malloc(sizeof(*self));
    self->speed = speed;
    Paths paths = Populate(base);
    self->textures = Cache(&paths, video->renderer);
    self->rect = rect;
    Depopulate(&paths);
    self->next = NULL;
    return self;
}

static View* Push(View* views, View* view)
{
    view->next = views;
    return view;
}

static void Cleanup(View* views)
{
    View* view = views;
    while(view)
    {
        View* next = view->next;
        Destroy(&view->textures);
        free(view->rect);
        free(view);
        view = next;
    }
}

static View* Parse(int argc, char** argv, Video* video)
{
    const int args = argc - 1;
    if(args < 2)
        Quit("Usage: paperview FOLDER SPEED\n"); // LEGACY PARAMETER SUPPORT.
    const int params = 6;
    if(args > 2 && args % params != 0)
        Quit("Usage: paperview FOLDER SPEED X Y W H FOLDER SPEED X Y W H # ... And so on\n"); // MULTI-MONITOR PARAMETER SUPPORT.
    View* views = NULL;
    for(int i = 1; i < argc; i += params)
    {
        const int a = i + 0;
        const int b = i + 1;
        const int c = i + 2;
        const int d = i + 3;
        const int e = i + 4;
        const int f = i + 5;
        const char* const base = argv[a];
        int speed = atoi(argv[b]);
        if(speed == 0)
            Quit("Invalid speed value\n");
        if(speed < 0)
            speed = INT32_MAX; // NEGATIVE SPEED VALUES CREATE STILL WALLPAPERS.
        SDL_Rect* rect = NULL;
        if(c != argc)
        {
            rect = malloc(sizeof(*rect));
            rect->x = atoi(argv[c]);
            rect->y = atoi(argv[d]);
            rect->w = atoi(argv[e]);
            rect->h = atoi(argv[f]);
        }
        views = Push(views, Init(base, speed, rect, video));
    }
    return views;
}

int main(int argc, char **argv)
{
    Video video = Setup();
    View *views = Parse(argc, argv, &video);

    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = 33000000;

    // Run the event loop until the window is closed
    SDL_Event event;
    while (SDL_WaitEvent(&event) && event.type != SDL_QUIT)
    {
        for (View *view = views; view; view = view->next)
        {
            const int index = SDL_GetTicks() / view->speed;
            const int frame = index % view->textures.size;
            Imlib_Image current = view->textures.texture[frame];
            
            imlib_context_push(video.render_context);
            imlib_context_set_image(current);
            imlib_render_image_on_drawable(0, 0);
            setRootAtoms(video.x11d, &video);
            XKillClient(video.x11d, AllTemporary);
            XSetCloseDownMode(video.x11d, RetainTemporary);
            XSetWindowBackgroundPixmap(video.x11d, video.root, video.pixmap);
            XClearWindow(video.x11d, video.root);
            XFlush(video.x11d);
            XSync(video.x11d, False);
            imlib_context_pop();
        }
        
        nanosleep(&timeout, NULL);
    }

    Cleanup(views);
    Teardown(&video);
    return 0;
}
