#import <Foundation/Foundation.h>

#include "editor/EditorApp.h"

int main()
{
    @autoreleasepool
    {
        engine::editor::EditorApp app;
        if (!app.init())
            return 1;

        app.run();
        app.shutdown();
    }

    return 0;
}
