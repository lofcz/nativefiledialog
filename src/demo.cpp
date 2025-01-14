#include <nfd.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <comcat.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

int main(void)
{

    nfdchar_t *outPath = nullptr;
    nfdresult_t result = NFD_OpenDialogEx("[My group|pdf];[Super group|ico,txt];*.png,jpg;pdf", nullptr, "Hello world 2", "Open file dialog, custom name", "Select, YAY!", "Abort the mission", nullptr, &outPath);
    //nfdresult_t result2 = NFD_OpenDialogEx("png,jpg;pdf", nullptr, "Hello world 2", "Open file dialog, custom name", "Select, YAY!", "Abort the mission ABORT ABORT", &outPath);

    if (result == NFD_OKAY)
    {
        puts("Success!");
        puts(outPath);
        free(outPath);
    }
    else if (result == NFD_CANCEL)
    {
        puts("User pressed cancel.");
    }
    else
    {
        printf("Error: %s\n", NFD_GetError());
    }

#ifdef _WIN32
    CoUninitialize();
#endif

    return 0;
}
